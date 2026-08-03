#pragma once

#include <opencv2/opencv.hpp>

// Letterbox 预处理：
// YOLO 系列模型通常要求固定输入尺寸，例如 640x640。
// 直接 resize 会拉伸图像，导致人体比例变形；Letterbox 会保持原图比例，
// 再用灰色边框补齐到目标尺寸，这样更接近模型训练时的输入方式。
//
// src: 摄像头原始图像。
// target_size: 模型输入尺寸，默认 640x640。
// 返回值: 保持比例缩放并补边后的图像。
cv::Mat Letterbox(const cv::Mat& src, const cv::Size& target_size = cv::Size(640, 640));
