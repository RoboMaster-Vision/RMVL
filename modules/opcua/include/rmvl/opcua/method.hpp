/**
 * @file method.hpp
 * @author zhaoxi (535394140@qq.com)
 * @brief 方法节点
 * @version 2.2
 * @date 2023-10-23
 *
 * @copyright Copyright 2023 (c), zhaoxi
 *
 */

#pragma once

#include "variable.hpp"

namespace rm
{

class ServerView;

//! @addtogroup opcua
//! @{

/**
 * @brief OPC UA 方法参数信息
 * @note 不储备任何调用时数据，仅用于描述方法参数
 */
struct RMVL_EXPORTS_W_AG Argument final
{
    RMVL_W_RW std::string name;          //!< 参数名称
    RMVL_W_RW DataType type{};           //!< 参数数据类型 @see rm::DataType
    RMVL_W_RW uint32_t dims{1U};         //!< 参数维数，单数据则是 `1`，数组则是数组长度 @warning 不能为 `0`
    RMVL_W_RW std::string description{}; //!< 参数描述
};

/**
 * @brief OPC UA 方法回调函数
 *
 * @param[in] server_view 服务器视图，指代当前服务器
 * @param[in] obj_id 方法节点所在对象的 `NodeId`
 * @param[in] iargs 输入参数列表
 * @retval res, oargs
 * @return 是否成功完成当前操作，以及输出参数列表
 */
using MethodCallback = std::function<std::pair<bool, std::vector<Variable>>(ServerView, const NodeId &, const std::vector<Variable> &)>;

//! OPC UA 方法
class RMVL_EXPORTS_W Method final
{
public:
    RMVL_W Method() = default;

    template <typename Callable, typename = std::enable_if_t<std::is_convertible_v<Callable, MethodCallback>>>
    RMVL_W_SUBST("M")
    Method(Callable cb) : func(cb) {}

    //! 命名空间索引，默认为 `1`
    RMVL_W_RW uint16_t ns{1U};

    /**
     * @brief 浏览名称 BrowseName
     * @brief
     * - 属于非服务器层面的 ID 号，可用于完成路径搜索
     * @brief
     * - 同一个命名空间 `ns` 下该名称不能重复
     */
    RMVL_W_RW std::string browse_name{};

    /**
     * @brief 展示名称 DisplayName
     * @brief
     * - 在服务器上对外展示的名字 - `en-US`
     * @brief
     * - 同一个命名空间 `ns` 下该名称可以相同
     */
    RMVL_W_RW std::string display_name{};

    //! 方法的描述
    RMVL_W_RW std::string description{};

    //! 传入参数列表
    RMVL_W_RW std::vector<Argument> iargs{};

    //! 传出参数列表
    RMVL_W_RW std::vector<Argument> oargs{};

    //! 方法回调函数
    RMVL_W_RW MethodCallback func{};
};

//! @} opcua

} // namespace rm
