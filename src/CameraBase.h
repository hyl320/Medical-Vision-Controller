#pragma once

#include <opencv2/opencv.hpp>

#include <cstdint>
#include <memory>
#include <string>

struct CameraIntrinsics {
    double fx = 500.0;
    double fy = 500.0;
    double cx = 320.0;
    double cy = 240.0;
};

struct RgbdFrame {
    cv::Mat color;
    cv::Mat depth;
    CameraIntrinsics color_intrinsics{};
    double depth_scale_mm = 1.0;
    std::int64_t timestamp_ms = 0;
    bool aligned_depth_to_color = false;

    bool isValid() const {
        return !color.empty() &&
            color.type() == CV_8UC3 &&
            !depth.empty() &&
            depth.type() == CV_16UC1 &&
            color.size() == depth.size();
    }
};

class ICameraBase {
public:
    virtual ~ICameraBase() = default;

    virtual bool initialize(int camera_id = 0, const std::string& source = {}) = 0;
    virtual bool getNextFrame(RgbdFrame& frame) = 0;
    virtual void stop() = 0;
    virtual const char* name() const = 0;
};

using CameraPtr = std::unique_ptr<ICameraBase>;

