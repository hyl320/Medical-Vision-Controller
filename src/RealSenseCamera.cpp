#include "RealSenseCamera.h"

#include "Logger.h"

#ifdef MEDICAL_VISION_ENABLE_REALSENSE

#include <algorithm>

RealSenseCamera::RealSenseCamera() = default;

RealSenseCamera::~RealSenseCamera() {
    stop();
}

bool RealSenseCamera::initialize(int, const std::string&) {
    try {
        config_.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_BGR8, 30);
        config_.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
        profile_ = pipeline_.start(config_);
        initialized_ = true;
        Logger::LogInfo("RealSenseCamera initialized");
        return true;
    } catch (const rs2::error& error) {
        Logger::LogCritical(
            std::string("RealSense initialization failed: ") + error.what());
        return false;
    }
}

bool RealSenseCamera::getNextFrame(RgbdFrame& frame) {
    if (!initialized_) {
        return false;
    }

    try {
        rs2::frameset frames = pipeline_.wait_for_frames();
        frames = align_to_color_.process(frames);

        const rs2::video_frame color_frame = frames.get_color_frame();
        const rs2::depth_frame depth_frame = frames.get_depth_frame();
        if (!color_frame || !depth_frame) {
            return false;
        }

        frame.color = cv::Mat(
            color_frame.get_height(),
            color_frame.get_width(),
            CV_8UC3,
            const_cast<void*>(color_frame.get_data()),
            color_frame.get_stride_in_bytes()).clone();
        frame.depth = cv::Mat(
            depth_frame.get_height(),
            depth_frame.get_width(),
            CV_16UC1,
            const_cast<void*>(depth_frame.get_data()),
            depth_frame.get_stride_in_bytes()).clone();

        const rs2::video_stream_profile color_profile =
            color_frame.get_profile().as<rs2::video_stream_profile>();
        const rs2_intrinsics intrinsics = color_profile.get_intrinsics();
        frame.color_intrinsics = CameraIntrinsics{
            intrinsics.fx,
            intrinsics.fy,
            intrinsics.ppx,
            intrinsics.ppy
        };

        const rs2::depth_sensor depth_sensor =
            profile_.get_device().first<rs2::depth_sensor>();
        frame.depth_scale_mm = depth_sensor.get_depth_scale() * 1000.0;
        frame.timestamp_ms = static_cast<std::int64_t>(color_frame.get_timestamp());
        frame.aligned_depth_to_color = true;
        return frame.isValid();
    } catch (const rs2::error& error) {
        Logger::LogWarn(
            std::string("RealSense frame capture failed: ") + error.what());
        return false;
    }
}

void RealSenseCamera::stop() {
    if (initialized_) {
        pipeline_.stop();
        initialized_ = false;
    }
}

const char* RealSenseCamera::name() const {
    return "RealSenseCamera";
}

#else

RealSenseCamera::RealSenseCamera() = default;
RealSenseCamera::~RealSenseCamera() = default;

bool RealSenseCamera::initialize(int, const std::string&) {
    Logger::LogCritical(
        "RealSenseCamera is disabled. Reconfigure with MEDICAL_VISION_ENABLE_REALSENSE=ON.");
    return false;
}

bool RealSenseCamera::getNextFrame(RgbdFrame&) {
    return false;
}

void RealSenseCamera::stop() {
    initialized_ = false;
}

const char* RealSenseCamera::name() const {
    return "RealSenseCamera";
}

#endif
