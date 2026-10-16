#pragma once

#include <opencv2/opencv.hpp>

class VisionSensor {
public:
    VisionSensor();
    ~VisionSensor();

    bool initialize(int camera_id = 0);
    bool getNextFrame(cv::Mat& frame);

private:
    cv::VideoCapture cap_;
};
