/**
 * @file find.cpp
 * @author RoboMaster Vision Community
 * @brief find features and combos
 * @version 0.1
 * @date 2022-12-10
 *
 * @copyright Copyright 2022 (c), RoboMaster Vision Community
 *
 */

#include <iostream>

#include <opencv2/imgproc.hpp>

#include "rmvl/detector/gyro_detector.h"

#include "rmvlpara/camera.hpp"
#include "rmvlpara/detector/gyro_detector.h"

using namespace rm;
using namespace para;
using namespace std;
using namespace cv;

void GyroDetector::find(Mat &src, vector<feature_ptr> &features, vector<combo_ptr> &combos, vector<Mat> &rois)
{
    // ----------------------- light_blob -----------------------
    // 找到所有灯条
    vector<light_blob_ptr> blobs = findLightBlobs(src);
    // 删除过亮灯条
    eraseBrightBlobs(src, blobs);
    // ------------------------- armor --------------------------
    if (blobs.size() >= 2)
    {
        // 找到所有装甲板
        vector<armor_ptr> armors = findArmors(blobs);
#ifdef HAVE_RMVL_ORT
        if (_ort)
        {
            rois.clear();
            rois.reserve(armors.size());
            for (const auto &armor : armors)
            {
                Mat roi = Armor::getNumberROI(src, armor);
                auto type = _ort->inference({roi})[0];
                armor->setType(_robot_t[type]);
                rois.emplace_back(roi);
            }
            // eraseFakeArmors(armors);
        }
#else
        for (auto armor : armors)
            armor->setType(RobotType::UNKNOWN);
#endif // HAVE_RMVL_ORT

        // 根据匹配误差筛选
        eraseErrorArmors(armors);
        // 更新至特征容器
        for (const auto &blob : blobs)
            features.emplace_back(blob);
        // 更新至组合体容器
        for (const auto &armor : armors)
            combos.emplace_back(armor);
    }
}

vector<light_blob_ptr> GyroDetector::findLightBlobs(Mat &bin)
{
    // 储存找到的灯条
    vector<light_blob_ptr> light_blobs;
    // 储存查找出的轮廓
    vector<vector<Point>> contours;
    // 查找最外围轮廓
    findContours(bin, contours, RETR_EXTERNAL, CHAIN_APPROX_NONE);
    for (auto &contour : contours)
    {
        // 排除面积过小的误识别
        if (contourArea(contour) < gyro_detector_param.MIN_CONTOUR_AREA)
            continue;
        // 构造灯条对象
        light_blob_ptr p_light = LightBlob::make_feature(contour);
        // 将识别出的灯条 push 到 light_blobs 中
        if (p_light != nullptr)
            light_blobs.push_back(p_light);
    }
    return light_blobs;
}

vector<armor_ptr> GyroDetector::findArmors(vector<light_blob_ptr> &light_blobs)
{
    // 灯条从左到右排序
    sort(light_blobs.begin(), light_blobs.end(),
         [&](const light_blob_ptr &lhs, const light_blob_ptr &rhs)
         {
             return lhs->getCenter().x < rhs->getCenter().x;
         });
    // 储存所有匹配到的装甲板
    vector<armor_ptr> current_armors;
    if (light_blobs.size() < 2)
        return current_armors;
    // -------------------------------------【匹配】-------------------------------------
    for (size_t i = 0; i + 1 < light_blobs.size(); ++i)
    {
        for (size_t j = i + 1; j < light_blobs.size(); j++)
        {
            // 构造装甲板
            armor_ptr armor = Armor::make_combo(light_blobs[i], light_blobs[j], _gyro_data, _tick);
            // 是否找到目标（未找到则单次跳出）
            if (armor == nullptr)
                continue;
            /**
             * @brief 装甲板区域内是否包含灯条中心点
             * @note 分支限界，复杂度不会过高
             */
            bool contain = false;
            for (size_t k = i + 1; k < j; ++k)
            {
                if (Armor::isContainBlob(light_blobs[k], armor))
                {
                    contain = true;
                    break;
                }
            }
            // 是否包含灯条中心点（包含则单次跳出）
            if (contain)
                continue;
            current_armors.emplace_back(armor);
        }
    }
    return current_armors;
}

void GyroDetector::eraseErrorArmors(std::vector<armor_ptr> &armors)
{
    // 判断大小是否允许被删除
    if (armors.size() < 2)
        return;
    unordered_map<armor_ptr, bool> armor_map; // [装甲板 : 能否删除]
    for (const auto &armor : armors)
        armor_map[armor] = false;
    // 设置是否删除的标志位
    for (size_t i = 0; i + 1 < armors.size(); i++)
    {
        for (size_t j = i + 1; j < armors.size(); j++)
        {
            // 共享左灯条 or 右灯条，优先匹配宽度小的
            if (armors[i]->at(0) == armors[j]->at(0) || armors[i]->at(1) == armors[j]->at(1))
                armor_map[armors[i]->getWidth() > armors[j]->getWidth() ? armors[i] : armors[j]] = true;
            else if (armors[i]->at(0) == armors[j]->at(1) || armors[i]->at(1) == armors[j]->at(0))
                armor_map[armors[i]->getError() > armors[j]->getError() ? armors[i] : armors[j]] = true;
        }
    }
    // 删除
    armors.erase(remove_if(armors.begin(), armors.end(),
                           [&armor_map](const armor_ptr &val)
                           {
                               return armor_map[val];
                           }),
                 armors.end());
}

void GyroDetector::eraseFakeArmors(vector<armor_ptr> &armors)
{
    armors.erase(remove_if(armors.begin(), armors.end(),
                           [&](armor_ptr &it)
                           {
                               return it->getType().RobotTypeID == RobotType::UNKNOWN;
                           }),
                 armors.end());
}

void GyroDetector::eraseBrightBlobs(Mat src, vector<light_blob_ptr> &blobs)
{
    blobs.erase(remove_if(blobs.begin(), blobs.end(),
                          [&](light_blob_ptr &blob) -> bool
                          {
                              int total_brightness = 0;
                              for (int i = -5; i <= 5; i++)
                              {
                                  if (i == 0)
                                      continue;
                                  int x = blob->getCenter().x - blob->getHeight() * i / 5;
                                  x = (x > src.cols) ? src.cols - 1 : x;
                                  x = (x < 0) ? 1 : x;
                                  int y = blob->getCenter().y;
                                  y = y < 0 ? 1 : y;
                                  y = (y > src.rows) ? src.rows - 1 : y;
                                  auto colors = src.at<Vec3b>(y, x);
                                  int brightness = 0.1 * colors[0] + 0.6 * colors[1] + 0.3 * colors[2];
                                  total_brightness += brightness;
                              }
                              return (total_brightness > 1100);
                          }),
                blobs.end());
}