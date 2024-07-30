/**
 * @file combo.cpp
 * @author zhaoxi (535394140@qq.com)
 * @brief
 * @version 1.0
 * @date 2023-09-18
 *
 * @copyright Copyright 2023 (c), zhaoxi
 *
 */

#include "rmvl/combo/combo.h"

namespace rm
{

DefaultCombo::DefaultCombo(feature::ptr p_feature, double tick) : combo()
{
    _features = {p_feature};
    _height = p_feature->getHeight();
    _width = p_feature->getWidth();
    _center = p_feature->getCenter();
    _angle = p_feature->getAngle();
    _corners = p_feature->getCorners();
    _type = p_feature->getType();
    _tick = tick;
}

combo::ptr DefaultCombo::clone(double tick)
{
    auto retval = std::make_shared<DefaultCombo>(*this);
    // 更新内部所有特征
    for (std::size_t i = 0; i < _features.size(); ++i)
        retval->_features[i] = _features[i]->clone();
    // 更新时间戳
    retval->_tick = tick;
    return retval;
}

} // namespace rm
