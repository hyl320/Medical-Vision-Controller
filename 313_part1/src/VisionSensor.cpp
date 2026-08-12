#include "VisionSensor.h"

#include "Logger.h"

VisionSensor::VisionSensor() = default;

VisionSensor::~VisionSensor() {
    if (cap_.isOpened()) {
        cap_.release();
        Logger::LogInfo("Camera resource released");
    }
}

bool VisionSensor::initialize(int camera_id) {
    cap_.open(camera_id);
    if (!cap_.isOpened()) {
        Logger::LogCritical("Failed to connect camera");
        return false;
    }

    Logger::LogInfo("Camera initialized successfully");
    return true;
}

bool VisionSensor::getNextFrame(cv::Mat& frame) {
    if (!cap_.isOpened()) {
        return false;
    }

    cap_ >> frame;
    if (frame.empty()) {
        Logger::LogWarn("Captured empty frame");
        return false;
    }

    return true;
}
