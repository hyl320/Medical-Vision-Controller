#pragma once

#include <opencv2/opencv.hpp>

// VisionSensor 是摄像头访问层。
//
// 这个类只负责两件事：
// 1. 打开摄像头。
// 2. 从摄像头读取下一帧 cv::Mat。
//
// 它不做模型推理、不做图像预处理、不画 UI。
// 这样可以让摄像头逻辑和视觉算法逻辑解耦。
class VisionSensor {
public:
    // 构造函数不直接打开摄像头，真正打开动作放在 initialize() 里。
    VisionSensor();

    // 析构时释放 cv::VideoCapture，避免摄像头资源被占用。
    ~VisionSensor();

    // 打开指定编号的摄像头。
    // camera_id = 0 通常表示默认摄像头。
    // 如果电脑有多个摄像头，可以尝试 1、2 等编号。
    bool initialize(int camera_id = 0);

    // 从摄像头读取一帧图像。
    // 成功时 frame 会被写入当前画面；失败时返回 false。
    bool getNextFrame(cv::Mat& frame);

private:
    // OpenCV 的摄像头句柄，封装在类内部，外部不直接操作硬件资源。
    cv::VideoCapture cap_;
};
