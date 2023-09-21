/**
 * @file gyro_tracker.h
 * @author RoboMaster Vision Community
 * @brief 装甲板追踪器头文件
 * @version 0.1
 * @date 2022-12-10
 *
 * @copyright Copyright 2022 (c), RoboMaster Vision Community
 *
 */

#pragma once

#include "rmvl/core/kalman.hpp"
#include "rmvl/combo/armor.h"

#include "tracker.h"

namespace rm
{

//! @addtogroup gyro_tracker
//! @{

//! 整车状态追踪器
class GyroTracker final : public tracker
{
public:
    //! 消失状态
    enum VanishState : uint8_t
    {
        VANISH = 0U, //!< 丢失
        APPEAR = 1U  //!< 出现
    };

private:
    float _sample_time = 0.f; //!< 采样帧差时间
    cv::Vec2f _pose;          //!< 修正后的装甲板姿态法向量
    float _rotspeed = 0.f;    //!< 绕 y 轴自转角速度（俯视顺时针为正，滤波数据，弧度）

    KF44f _motion_filter;   //!< 目标转角滤波器
    KF66f _center3d_filter; //!< 位置滤波器
    KF44f _pose_filter;     //!< 姿态滤波器

    std::deque<RobotType> _type_deque; //!< 装甲板状态队列（数字）

public:
    GyroTracker() = delete;

    //! 初始化追踪器
    explicit GyroTracker(const combo_ptr &p_armor);

    /**
     * @brief 构建 GyroTracker
     *
     * @param[in] p_armor 第一帧装甲（不允许为空）
     */
    static inline std::shared_ptr<GyroTracker> make_tracker(const combo_ptr &p_armor) { return std::make_shared<GyroTracker>(p_armor); }

    /**
     * @brief 动态类型转换
     *
     * @param[in] p_tracker tracker_ptr 抽象指针
     * @return 派生对象指针
     */
    static inline std::shared_ptr<GyroTracker> cast(tracker_ptr p_tracker)
    {
        return std::dynamic_pointer_cast<GyroTracker>(p_tracker);
    }

    /**
     * @brief 更新时间序列
     *
     * @param[in] p_armor 传入 tracker 的组合体
     * @param[in] time 时间戳
     * @param[in] gyro_data 云台数据
     */
    void update(combo_ptr p_armor, int64 time, const GyroData &gyro_data) override;

    /**
     * @brief 更新消失状态
     *
     * @param[in] state 消失状态
     */
    inline void updateVanishState(VanishState state) { state == VANISH ? _vanish_num++ : _vanish_num = 0; }

    //! 获取帧差时间
    inline float getSampleTime() const { return _sample_time; }
    //! 获取修正后的装甲板姿态法向量
    inline const cv::Vec2f &getPose() const { return _pose; }
    //! 获取绕 y 轴的自转角速度（俯视顺时针为正，滤波数据，弧度）
    inline float getRotatedSpeed() const { return _rotspeed; }

private:
    /**
     * @brief 从 combo 中更新数据
     *
     * @param[in] p_combo armor_ptr 共享指针
     */
    void updateFromCombo(combo_ptr p_combo);

    //! 初始化 tracker 的距离和运动滤波器
    void initFilter();

    /**
     * @brief 更新装甲板类型
     *
     * @param[in] stat 类型
     */
    void updateType(RMStatus stat);

    /**
     * @brief 更新运动滤波器
     * @note 将图像相对速度和陀螺仪速度融合后再做滤波的好处是，
     *       可以一定程度上减少时序不精准的问题
     */
    void updateMotionFilter();

    //! 更新位置滤波器
    void updatePositionFilter();

    //! 更新姿态滤波器
    void updatePoseFilter();

    /**
     * @brief 解算单个追踪器的角速度
     *
     * @return 角速度（俯视图逆时针为正）
     */
    float calcRotationSpeed();
};

//! 包含旋转的装甲板追踪器共享指针
using gyro_tracker_ptr = std::shared_ptr<GyroTracker>;

//! @} gyro_tracker

} // namespace rm