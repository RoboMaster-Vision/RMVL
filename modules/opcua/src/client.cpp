/**
 * @file client.cpp
 * @author zhaoxi (535394140@qq.com)
 * @brief OPC UA 客户端
 * @version 1.0
 * @date 2023-10-29
 *
 * @copyright Copyright 2023 (c), zhaoxi
 *
 */

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>

#include "rmvl/opcua/client.hpp"
#include "rmvlpara/opcua.hpp"

namespace rm
{

Client::Client(std::string_view address, UserConfig usr)
{
    _client = UA_Client_new();
    UA_ClientConfig *config = UA_Client_getConfig(_client);
    auto status = UA_ClientConfig_setDefault(config);
    if (status == UA_STATUSCODE_GOOD)
    {
        if (usr.id.empty() || usr.passwd.empty())
            status = UA_Client_connect(_client, address.data());
        else
            status = UA_Client_connectUsername(_client, address.data(), usr.id.c_str(), usr.passwd.c_str());
    }
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Failed to create client");
        UA_Client_delete(_client);
        _client = nullptr;
    }
}

Client::~Client()
{
    auto status = UA_Client_disconnect(_client);
    if (status != UA_STATUSCODE_GOOD)
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Failed to disconnect the client");
    UA_Client_delete(_client);
    _client = nullptr;
}

void Client::spin()
{
    bool warning{};
    while (true)
    {
        auto status = UA_Client_run_iterate(_client, para::opcua_param.SPIN_TIMEOUT);
        if (!warning && status != UA_STATUSCODE_GOOD)
        {
            UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT,
                           "No events and message received, spinning indefinitely, error status: %s", UA_StatusCode_name(status));
            warning = true;
        }
        warning = (status == UA_STATUSCODE_GOOD) ? false : warning;
    }
}

void Client::spinOnce() { UA_Client_run_iterate(_client, para::opcua_param.SPIN_TIMEOUT); }

bool Client::read(const UA_NodeId &node, Variable &val)
{
    UA_Variant variant;

    UA_StatusCode status = UA_Client_readValueAttribute(_client, node, &variant);
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Failed to read value from the specific node, error: %s", UA_StatusCode_name(status));
        return false;
    }
    // 变量节点信息
    val = helper::cvtVariable(&variant);
    return true;
}

bool Client::write(const UA_NodeId &node, const Variable &val)
{
    auto status = UA_Client_writeValueAttribute(_client, node, helper::cvtVariable(val));
    if (status != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Failed to write value to the specific node, error: %s", UA_StatusCode_name(status));
        return false;
    }
    return true;
}

} // namespace rm
