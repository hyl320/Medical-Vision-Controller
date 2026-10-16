#include "MockCamera.h"

#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace {
bool isCameraIndex(const std::string& source) {
    return !source.empty() && std::all_of(source.begin(), source.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}
}

MockCamera::MockCamera(double mock_depth_mm, bool synthetic_fallback)
    : mock_depth_mm_(std::max(1.0, mock_depth_mm)),
      synthetic_fallback_(synthetic_fallback) {
}

MockCamera::~MockCamera() {
    stop();
}

bool MockCamera::initialize(int camera_id, const std::string& source) {
    frame_index_ = 0;
    using_synthetic_ = false;

    if (source.empty()) {
        capture_.open(camera_id);
    } else if (isCameraIndex(source)) {
        capture_.open(std::atoi(source.c_str()));
    } else {
        capture_.open(source);
    }

    if (!capture_.isOpened()) {
        if (!synthetic_fallback_) {
            Logger::LogCritical("MockCamera failed to open source");
            return false;
        }

        using_synthetic_ = true;
        initialized_ = true;
        Logger::LogWarn("MockCamera source unavailable; using synthetic RGB-D frames");
        return true;
    }

    initialized_ = true;
    Logger::LogInfo("MockCamera initialized");
    return true;
}

bool MockCamera::getNextFrame(RgbdFrame& frame) {
    if (!initialized_) {
        return false;
    }

    if (using_synthetic_) {
        return makeSyntheticFrame(frame);
    }

    return readFromSource(frame);
}

void MockCamera::stop() {
    if (capture_.isOpened()) {
        capture_.release();
    }
    initialized_ = false;
    using_synthetic_ = false;
}

const char* MockCamera::name() const {
    return "MockCamera";
}

bool MockCamera::readFromSource(RgbdFrame& frame) {
    cv::Mat color;
    capture_ >> color;
    if (color.empty()) {
        Logger::LogWarn("MockCamera source returned an empty frame");
        return false;
    }

    frame.color = color;
    frame.depth.create(color.size(), CV_16UC1);
    makeDepth(color.size(), frame.depth);
    frame.color_intrinsics = CameraIntrinsics{
        500.0,
        500.0,
        color.cols * 0.5,
        color.rows * 0.5
    };
    frame.depth_scale_mm = 1.0;
    frame.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.aligned_depth_to_color = true;
    return frame.isValid();
}

bool MockCamera::makeSyntheticFrame(RgbdFrame& frame) {
    constexpr int width = 640;
    constexpr int height = 480;

    frame.color = cv::Mat(height, width, CV_8UC3, cv::Scalar(28, 32, 38));
    const double phase = static_cast<double>(frame_index_) * 0.045;
    const int center_x = width / 2 + static_cast<int>(std::sin(phase) * 120.0);
    const int center_y = height / 2 + static_cast<int>(std::cos(phase * 0.7) * 70.0);

    cv::rectangle(frame.color, cv::Rect(0, 0, width, height), cv::Scalar(80, 80, 80), 2);
    cv::circle(frame.color, cv::Point(center_x, center_y), 55, cv::Scalar(70, 180, 245), -1);
    cv::circle(frame.color, cv::Point(center_x, center_y), 55, cv::Scalar(240, 240, 240), 2);
    cv::putText(
        frame.color,
        "MOCK RGB-D",
        cv::Point(18, 34),
        cv::FONT_HERSHEY_SIMPLEX,
        0.8,
        cv::Scalar(220, 220, 220),
        2,
        cv::LINE_AA);

    frame.depth.create(frame.color.size(), CV_16UC1);
    makeDepth(frame.color.size(), frame.depth);
    frame.depth.at<std::uint16_t>(center_y, center_x) =
        static_cast<std::uint16_t>(std::clamp(mock_depth_mm_ - 80.0, 1.0, 65535.0));
    frame.color_intrinsics = CameraIntrinsics{
        500.0,
        500.0,
        width * 0.5,
        height * 0.5
    };
    frame.depth_scale_mm = 1.0;
    frame.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.aligned_depth_to_color = true;
    ++frame_index_;
    return frame.isValid();
}

void MockCamera::makeDepth(cv::Size size, cv::Mat& depth) const {
    depth.setTo(cv::Scalar(static_cast<std::uint16_t>(
        std::clamp(mock_depth_mm_, 1.0, 65535.0))));
}
