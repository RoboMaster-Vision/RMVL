/**
 * @file server.cpp
 * @author zhaoxi (535394140@qq.com)
 * @brief OPC UA 服务器
 * @version 2.2
 * @date 2024-03-29
 *
 * @copyright Copyright 2023 (c), zhaoxi
 *
 */

#include <stack>

#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>

#include "rmvl/opcua/server.hpp"

#include "cvt.hpp"

namespace rm
{

///////////////////////// 基本配置 /////////////////////////

Server::Server(uint16_t port, std::string_view name, const std::vector<UserConfig> &users)
{
    UA_ServerConfig init_config{};
    // 修改日志
#if OPCUA_VERSION < 10400
    init_config.logger = UA_Log_Stdout_withLevel(UA_LOGLEVEL_ERROR);
#else
    init_config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_ERROR);
#endif
    // 设置服务器配置
    UA_ServerConfig_setMinimal(&init_config, port, nullptr);
    // 初始化服务器
    _server = UA_Server_newWithConfig(&init_config);
    UA_ServerConfig *config = UA_Server_getConfig(_server);
    // 修改名字
    if (!name.empty())
    {
        UA_LocalizedText_clear(&config->applicationDescription.applicationName);
        config->applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC("en-US", name.data());
        for (size_t i = 0; i < config->endpointsSize; ++i)
        {
            UA_LocalizedText *ptr = &config->endpoints[i].server.applicationName;
            UA_LocalizedText_clear(ptr);
            *ptr = UA_LOCALIZEDTEXT_ALLOC("en-US", name.data());
        }
    }
    // 修改采样间隔和发布间隔
    config->samplingIntervalLimits.min = 2.0;
    config->publishingIntervalLimits.min = 2.0;

    if (!users.empty())
    {
        std::vector<UA_UsernamePasswordLogin> usr_passwd;
        usr_passwd.reserve(users.size());
        for (const auto &[id, passwd] : users)
        {
            UA_UsernamePasswordLogin each;
            each.username = UA_STRING(helper::to_char(id));
            each.password = UA_STRING(helper::to_char(passwd));
            usr_passwd.emplace_back(each);
        }
        // 修改访问控制
        config->accessControl.clear(&config->accessControl);
#if OPCUA_VERSION >= 10400
        UA_AccessControl_default(config, false, nullptr, usr_passwd.size(), usr_passwd.data());
#elif OPCUA_VERSION >= 10300 && OPCUA_VERSION < 10400
        UA_AccessControl_default(config, false, nullptr,
                                 &config->securityPolicies[config->securityPoliciesSize - 1].policyUri,
                                 usr_passwd.size(), usr_passwd.data());
#else
        UA_AccessControl_default(config, false, &config->securityPolicies[config->securityPoliciesSize - 1].policyUri,
                                 usr_passwd.size(), usr_passwd.data());
#endif
    }
}

Server::Server(ServerUserConfig on_config, uint16_t port, std::string_view name, const std::vector<UserConfig> &users) : Server(port, name, users)
{
    if (on_config != nullptr)
        on_config(_server);
}

void Server::start()
{
    _running = true;
    _run = std::thread([this]() {
        UA_StatusCode retval = UA_Server_run(_server, &_running);
        if (retval != UA_STATUSCODE_GOOD)
            ERROR_("Failed to initialize server: %s", UA_StatusCode_name(retval));
    });
}

Server::~Server()
{
    _running = false;
    if (_run.joinable())
        _run.join();
    UA_Server_delete(_server);
}

static Variable serverRead(UA_Server *p_server, const NodeId &node)
{
    RMVL_DbgAssert(p_server != nullptr);

    UA_Variant p_val;
    UA_Variant_init(&p_val);
    auto status = UA_Server_readValue(p_server, node, &p_val);
    if (status != UA_STATUSCODE_GOOD)
        return {};
    Variable retval = helper::cvtVariable(p_val);
    UA_Variant_clear(&p_val);
    return retval;
}

static bool serverWrite(UA_Server *p_server, const NodeId &node, const Variable &val)
{
    RMVL_DbgAssert(p_server != nullptr);

    auto variant = helper::cvtVariable(val);
    auto status = UA_Server_writeValue(p_server, node, variant);
    UA_Variant_clear(&variant);
    if (status != UA_STATUSCODE_GOOD)
        ERROR_("Failed to write variable, error code: %s", UA_StatusCode_name(status));
    return status == UA_STATUSCODE_GOOD;
}

///////////////////////// 节点配置 /////////////////////////

NodeId Server::addVariableTypeNode(const VariableType &vtype) const
{
    RMVL_DbgAssert(_server != nullptr);

    UA_VariableTypeAttributes attr = UA_VariableTypeAttributes_default;
    UA_Variant variant = helper::cvtVariable(vtype);
    // 设置属性
    attr.value = variant;
    attr.dataType = variant.type->typeId;
    attr.valueRank = vtype.size() == 1 ? UA_VALUERANK_SCALAR : 1;
    if (attr.valueRank != UA_VALUERANK_SCALAR)
    {
        attr.arrayDimensionsSize = variant.arrayDimensionsSize;
        attr.arrayDimensions = variant.arrayDimensions;
    }
    attr.description = UA_LOCALIZEDTEXT(helper::zh_CN(), helper::to_char(vtype.description));
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(vtype.display_name));
    NodeId retval;
    auto status = UA_Server_addVariableTypeNode(
        _server, UA_NODEID_NULL, nodeBaseDataVariableType, nodeHasSubtype,
        UA_QUALIFIEDNAME(vtype.ns, helper::to_char(vtype.browse_name)),
        UA_NODEID_NULL, attr, nullptr, &retval);
    UA_Variant_clear(&variant);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add variable type node: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    return retval;
}

NodeId Server::addVariableNode(const Variable &val, const NodeId &parent_id) const
{
    RMVL_DbgAssert(_server != nullptr);

    // 变量节点属性 `UA_VariableAttributes`
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant variant = helper::cvtVariable(val);
    // 设置属性
    attr.value = variant;
    attr.dataType = variant.type->typeId;
    attr.accessLevel = val.access_level;
    attr.valueRank = val.size() == 1 ? UA_VALUERANK_SCALAR : 1;
    if (attr.valueRank != UA_VALUERANK_SCALAR)
    {
        attr.arrayDimensionsSize = variant.arrayDimensionsSize;
        attr.arrayDimensions = variant.arrayDimensions;
    }
    attr.description = UA_LOCALIZEDTEXT(helper::zh_CN(), helper::to_char(val.description));
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(val.display_name));
    // 获取变量节点的变量类型节点
    NodeId type_id{nodeBaseDataVariableType};
    const auto p_type = val.type();
    if (p_type != nullptr)
    {
        type_id = type_id | find(p_type->browse_name);
        if (type_id.empty())
        {
            ERROR_("Failed to find the variable type ID during adding variable node");
            type_id = nodeBaseDataVariableType;
        }
    }
    NodeId retval;
    // 添加节点至服务器
    NodeId object_folder_id{nodeObjectsFolder};
    NodeId ref_id = (parent_id == object_folder_id) ? nodeOrganizes : nodeHasComponent;
    auto status = UA_Server_addVariableNode(
        _server, UA_NODEID_NULL, parent_id, ref_id, UA_QUALIFIEDNAME(val.ns, helper::to_char(val.browse_name)),
        type_id, attr, nullptr, &retval);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add variable node: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    UA_Variant_clear(&variant);
    return retval;
}

Variable Server::read(const NodeId &node) const { return serverRead(_server, node); }
bool Server::write(const NodeId &node, const Variable &val) const { return serverWrite(_server, node, val); }

bool Server::addVariableNodeValueCallBack(NodeId id, ValueCallBackBeforeRead before_read, ValueCallBackAfterWrite after_write) const
{
    RMVL_DbgAssert(_server != nullptr);

    UA_ValueCallback callback{before_read, after_write};
    auto status = UA_Server_setVariableNode_valueCallback(_server, id, callback);
    if (status != UA_STATUSCODE_GOOD)
        ERROR_("Function addVariableNodeValueCallBack: %s", UA_StatusCode_name(status));
    return status == UA_STATUSCODE_GOOD;
}

NodeId Server::addDataSourceVariableNode(const Variable &val, DataSourceRead on_read, DataSourceWrite on_write, NodeId parent_id) const
{
    RMVL_DbgAssert(_server != nullptr);

    // 变量节点属性 `UA_VariableAttributes`
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    // 设置属性
    attr.accessLevel = val.access_level;
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(val.display_name));
    attr.description = UA_LOCALIZEDTEXT(helper::zh_CN(), helper::to_char(val.description));
    // 获取变量节点的变量类型节点
    NodeId type_id{nodeBaseDataVariableType};
    const auto p_type = val.type();
    if (p_type != nullptr)
    {
        type_id = type_id | find(p_type->browse_name);
        if (type_id.empty())
        {
            ERROR_("Failed to find the variable type ID during adding variable node");
            type_id = nodeBaseDataVariableType;
        }
    }
    NodeId retval;
    // 获取数据源重定向信息
    UA_DataSource data_source;
    data_source.read = on_read;
    data_source.write = on_write;
    // 添加节点至服务器
    auto status = UA_Server_addDataSourceVariableNode(
        _server, UA_NODEID_NULL, parent_id, nodeOrganizes, UA_QUALIFIEDNAME(val.ns, helper::to_char(val.browse_name)),
        type_id, attr, data_source, nullptr, &retval);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add data source variable node: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    return retval;
}

NodeId Server::addMethodNode(const Method &method, const NodeId &parent_id) const
{
    RMVL_DbgAssert(_server != nullptr);

    UA_MethodAttributes attr = UA_MethodAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(method.display_name));
    attr.description = UA_LOCALIZEDTEXT(helper::zh_CN(), helper::to_char(method.description));
    attr.executable = true;
    attr.userExecutable = true;
    // 提取数据至 `UA_Argument`
    std::vector<UA_Argument> inputs;
    inputs.reserve(method.iargs.size());
    for (const auto &arg : method.iargs)
        inputs.push_back(helper::cvtArgument(arg));
    std::vector<UA_Argument> outputs;
    outputs.reserve(method.oargs.size());
    for (const auto &arg : method.oargs)
        outputs.push_back(helper::cvtArgument(arg));
    // 添加节点
    NodeId retval;
    auto status = UA_Server_addMethodNode(
        _server, UA_NODEID_NULL, parent_id, nodeHasComponent, UA_QUALIFIEDNAME(method.ns, helper::to_char(method.browse_name)),
        attr, method.func, inputs.size(), inputs.data(), outputs.size(), outputs.data(), nullptr, &retval);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add method node: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    // 添加 Mandatory 属性
    status = UA_Server_addReference(_server, retval, nodeHasModellingRule,
                                    UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY), true);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add the \"Mandatory\" reference node: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    return retval;
}

void Server::setMethodNodeCallBack(const NodeId &id, UA_MethodCallback on_method) const
{
    RMVL_DbgAssert(_server != nullptr);
    UA_Server_setMethodNodeCallback(_server, id, on_method);
}

NodeId Server::addObjectTypeNode(const ObjectType &otype) const
{
    RMVL_DbgAssert(_server != nullptr);

    // 定义对象类型节点
    UA_ObjectTypeAttributes attr = UA_ObjectTypeAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(otype.display_name));
    attr.description = UA_LOCALIZEDTEXT(helper::zh_CN(), helper::to_char(otype.description));
    NodeId retval;
    // 获取父节点的 NodeID
    NodeId parent_id{nodeBaseObjectType};
    const ObjectType *current = otype.getBase();
    std::stack<std::string> base_stack;
    while (current != nullptr)
    {
        base_stack.push(current->browse_name);
        current = current->getBase();
    }
    while (!base_stack.empty())
    {
        parent_id = parent_id | find(base_stack.top());
        base_stack.pop();
    }
    if (parent_id.empty())
    {
        ERROR_("Failed to find the base object type ID during adding object type node");
        parent_id = nodeBaseObjectType;
    }
    auto status = UA_Server_addObjectTypeNode(
        _server, UA_NODEID_NULL, parent_id,
        nodeHasSubtype,
        UA_QUALIFIEDNAME(otype.ns, helper::to_char(otype.browse_name)),
        attr, nullptr, &retval);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add object type: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    // 添加变量节点作为对象类型节点的子节点
    for (const auto &[browse_name, val] : otype.getVariables())
    {
        // 添加至服务器
        NodeId sub_retval = addVariableNode(val, retval);
        // 设置子变量节点为强制生成
        status = UA_Server_addReference(
            _server, sub_retval, nodeHasModellingRule,
            UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY), true);
        if (status != UA_STATUSCODE_GOOD)
        {
            ERROR_("Failed to add reference during adding object type node, browse name: %s, error code: %s", browse_name.c_str(), UA_StatusCode_name(status));
            return UA_NODEID_NULL;
        }
    }
    // 添加方法节点作为对象类型节点的子节点
    for (const auto &val : otype.getMethods())
        addMethodNode(val.second, retval);
    return retval;
}

NodeId Server::addObjectNode(const Object &obj, NodeId parent_id) const
{
    RMVL_DbgAssert(_server != nullptr);

    UA_ObjectAttributes attr{UA_ObjectAttributes_default};
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(obj.display_name));
    attr.description = UA_LOCALIZEDTEXT(helper::zh_CN(), helper::to_char(obj.description));
    // 获取对象类型节点
    const ObjectType *current = obj.type();
    NodeId type_id{nodeBaseObjectType};
    std::stack<std::string> base_stack;
    while (current != nullptr)
    {
        base_stack.push(current->browse_name);
        current = current->getBase();
    }
    while (!base_stack.empty())
    {
        type_id = type_id | find(base_stack.top());
        base_stack.pop();
    }
    if (type_id.empty())
    {
        WARNING_("The object node \"%s\" does not belong to any object type node", obj.browse_name.c_str());
        type_id = nodeBaseObjectType;
    }
    // 添加至服务器
    NodeId retval;
    auto status = UA_Server_addObjectNode(
        _server, UA_NODEID_NULL, parent_id, nodeOrganizes, UA_QUALIFIEDNAME(obj.ns, helper::to_char(obj.browse_name)),
        type_id, attr, nullptr, &retval);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add object node to server, error code: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    // 添加额外变量节点
    for (const auto &[browse_name, variable] : obj.getVariables())
    {
        auto sub_node_id = retval | find(browse_name);
        if (!sub_node_id.empty())
            write(sub_node_id, variable);
        else
            addVariableNode(variable, retval);
    }
    // 添加额外方法节点
    for (const auto &[browse_name, method] : obj.getMethods())
    {
        auto sub_node_id = retval | find(browse_name);
        if (!sub_node_id.empty())
            setMethodNodeCallBack(sub_node_id, method.func);
        else
            addMethodNode(method, retval);
    }
    return retval;
}

NodeId Server::addViewNode(const View &view) const
{
    RMVL_DbgAssert(_server != nullptr);

    // 准备数据
    NodeId retval;
    UA_ViewAttributes attr = UA_ViewAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(view.display_name));
    attr.description = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(view.description));
    // 创建并添加 View 节点
    auto status = UA_Server_addViewNode(
        _server, UA_NODEID_NULL, nodeViewsFolder, nodeOrganizes,
        UA_QUALIFIEDNAME(view.ns, helper::to_char(view.browse_name)), attr, nullptr, &retval);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add view node, error: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    // 添加引用
    for (const auto &node : view.data())
    {
        UA_ExpandedNodeId exp = UA_EXPANDEDNODEID_NULL;
        exp.nodeId = node;
        status = UA_Server_addReference(_server, retval, nodeOrganizes, exp, true);
        if (status != UA_STATUSCODE_GOOD)
        {
            ERROR_("Failed to add reference, error: %s", UA_StatusCode_name(status));
            return UA_NODEID_NULL;
        }
    }

    return retval;
}

NodeId Server::addEventTypeNode(const EventType &etype) const
{
    RMVL_DbgAssert(_server != nullptr);

    NodeId retval;
    UA_ObjectTypeAttributes attr = UA_ObjectTypeAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(etype.display_name));
    attr.description = UA_LOCALIZEDTEXT(helper::zh_CN(), helper::to_char(etype.description));

    auto status = UA_Server_addObjectTypeNode(
        _server, UA_NODEID_NULL, nodeBaseEventType,
        nodeHasSubtype,
        UA_QUALIFIEDNAME(etype.ns, helper::to_char(etype.browse_name)),
        attr, nullptr, &retval);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to add event type: %s", UA_StatusCode_name(status));
        return UA_NODEID_NULL;
    }
    // 添加自定义数据（非默认属性）
    for (const auto &[browse_name, val] : etype.data())
    {
        UA_VariableAttributes val_attr = UA_VariableAttributes_default;
        val_attr.displayName = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(browse_name));
        val_attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
        UA_Variant_setScalarCopy(&val_attr.value, &val, &UA_TYPES[UA_TYPES_INT32]);
        NodeId sub_id;
        status = UA_Server_addVariableNode(
            _server, UA_NODEID_NULL, retval, nodeHasProperty,
            UA_QUALIFIEDNAME(etype.ns, helper::to_char(browse_name)), nodePropertyType,
            val_attr, nullptr, &sub_id);
        if (status != UA_STATUSCODE_GOOD)
        {
            ERROR_("Failed to add event type property: %s", UA_StatusCode_name(status));
            return UA_NODEID_NULL;
        }
        // 设置子变量节点为强制生成
        status = UA_Server_addReference(
            _server, sub_id, nodeHasModellingRule,
            UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY), true);
        if (status != UA_STATUSCODE_GOOD)
        {
            ERROR_("Failed to add reference during adding event type node, browse name: %s, error code: %s",
                   browse_name.c_str(), UA_StatusCode_name(status));
            return UA_NODEID_NULL;
        }
    }
    return retval;
}

bool Server::triggerEvent(const NodeId &node_id, const Event &event) const
{
    RMVL_DbgAssert(_server != nullptr);

    NodeId type_id = nodeBaseEventType | find(event.type()->browse_name);
    if (type_id.empty())
    {
        ERROR_("Failed to find the event type ID during triggering event");
        return false;
    }
    // 创建事件
    NodeId event_id;
    auto status = UA_Server_createEvent(_server, type_id, &event_id);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to create event: %s", UA_StatusCode_name(status));
        return false;
    }

    // 设置事件默认属性
    UA_DateTime time = UA_DateTime_now();
    UA_String source_name = UA_STRING(helper::to_char(event.source_name));
    UA_LocalizedText evt_msg = UA_LOCALIZEDTEXT(helper::en_US(), helper::to_char(event.message));
    UA_Server_writeObjectProperty_scalar(_server, event_id, UA_QUALIFIEDNAME(0, const_cast<char *>("Time")), &time, &UA_TYPES[UA_TYPES_DATETIME]);
    UA_Server_writeObjectProperty_scalar(_server, event_id, UA_QUALIFIEDNAME(0, const_cast<char *>("SourceName")), &source_name, &UA_TYPES[UA_TYPES_STRING]);
    UA_Server_writeObjectProperty_scalar(_server, event_id, UA_QUALIFIEDNAME(0, const_cast<char *>("Severity")), &event.severity, &UA_TYPES[UA_TYPES_UINT16]);
    UA_Server_writeObjectProperty_scalar(_server, event_id, UA_QUALIFIEDNAME(0, const_cast<char *>("Message")), &evt_msg, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    // 设置事件自定义属性
    for (const auto &[browse_name, prop] : event.data())
    {
        NodeId sub_node_id = event_id | find(browse_name);
        if (!sub_node_id.empty())
            UA_Server_writeObjectProperty_scalar(_server, event_id, UA_QUALIFIEDNAME(event.ns, helper::to_char(browse_name)),
                                                 &prop, &UA_TYPES[UA_TYPES_INT32]);
    }

    // 触发事件
    status = UA_Server_triggerEvent(_server, event_id, node_id, nullptr, true);
    if (status != UA_STATUSCODE_GOOD)
    {
        ERROR_("Failed to trigger event: %s", UA_StatusCode_name(status));
        return false;
    }
    return true;
}

//////////////////////// 服务端视图 ////////////////////////

Variable ServerView::read(const NodeId &node) const { return serverRead(_server, node); }
bool ServerView::write(const NodeId &node, const Variable &val) const { return serverWrite(_server, node, val); }

} // namespace rm
