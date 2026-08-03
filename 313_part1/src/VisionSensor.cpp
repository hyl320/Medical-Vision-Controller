#include "VisionSensor.h"

#include <iostream>

VisionSensor::VisionSensor() = default;

VisionSensor::~VisionSensor() {
    if (cap_.isOpened()) {
        cap_.release();
        std::cout << "[Info] 摄像头资源已释放。" << std::endl;
    }
}

bool VisionSensor::initialize(int camera_id) {
    cap_.open(camera_id);
    if (!cap_.isOpened()) {
        std::cerr << "[Error] 无法连接摄像头。" << std::endl;
        return false;
    }

    std::cout << "[Info] 摄像头初始化成功。" << std::endl;
    return true;
}

bool VisionSensor::getNextFrame(cv::Mat& frame) {
    if (!cap_.isOpened()) {
        return false;
    }

    cap_ >> frame;
    if (frame.empty()) {
        std::cerr << "[Warning] 捕获到空帧。" << std::endl;
        return false;
    }

    return true;
}
