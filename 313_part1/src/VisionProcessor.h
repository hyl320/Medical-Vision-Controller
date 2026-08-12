#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "OnnxModel.h"
#include "VisionSensor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/opencv.hpp>

enum class SystemState {
    STANDBY,
    TRACKING,
    E_STOP
};

enum class EStopReason {
    NONE,
    WATCHDOG_TIMEOUT,
    TARGET_LOST,
    OUT_OF_BOUNDS
};

class VisionProcessor {
public:
    VisionProcessor();
    ~VisionProcessor();

    bool init(const std::string& model_path, int camera_id = 0, int infer_interval_ms = 300);
    bool processFrame(cv::Mat& output_frame);
    void setUdpTarget(SOCKET socket, const sockaddr_in& target_addr);
    void setCoordinateDebugEnabled(bool enabled);
    bool isCoordinateDebugEnabled() const;
    void stop();

private:
    struct CameraIntrinsics {
        double fx = 500.0;
        double fy = 500.0;
        double cx = 320.0;
        double cy = 240.0;
    };

    struct WorldPoint3D {
        double x_mm = 0.0;
        double y_mm = 0.0;
        double z_mm = 0.0;
    };

    enum class RobotState {
        SAFE,
        OUT_OF_BOUNDS,
        TARGET_LOST
    };

    struct RobotMessage {
        RobotState state = RobotState::TARGET_LOST;
        WorldPoint3D world{};
    };

    struct SafeZone {
        double min_x_mm = -200.0;
        double max_x_mm = 200.0;
        double min_y_mm = -150.0;
        double max_y_mm = 150.0;
    };

    struct PerformanceStats {
        double fps = 0.0;
        double camera_read_ms = 0.0;
        double inference_ms = 0.0;
        double udp_send_ms = 0.0;
        int frames_in_window = 0;
        double fps_sample_sum = 0.0;
        std::chrono::high_resolution_clock::time_point fps_window_start{};
        std::chrono::high_resolution_clock::time_point last_frame_time{};
        std::chrono::high_resolution_clock::time_point camera_read_start{};
        std::chrono::high_resolution_clock::time_point camera_read_end{};
        std::chrono::high_resolution_clock::time_point inference_start{};
        std::chrono::high_resolution_clock::time_point inference_end{};
        std::chrono::high_resolution_clock::time_point udp_send_start{};
        std::chrono::high_resolution_clock::time_point udp_send_end{};
    };

    void publishRobotMessage(const RobotMessage& message);
    RobotMessage getLatestRobotMessage();
    void publishWorldCoordinate(const WorldPoint3D& world);
    void messageLoop();
    void watchdogLoop();
    void checkWatchdogTimeout();
    SystemState getSystemState();
    void setSystemState(SystemState state);
    void enterEStop(EStopReason reason);
    long long getHeartbeatAgeMs();
    const char* systemStateName(SystemState state) const;
    void inferenceLoop();
    void applyCenterFilter(InferenceResult& result);

    cv::Point2f modelPointToCameraPixel(const cv::Point2f& model_point, const cv::Size& camera_size) const;
    WorldPoint3D pixelToWorld3D(const cv::Point2f& pixel_point) const;
    bool isInsideSafeZone(const WorldPoint3D& world) const;
    cv::Rect getSafeZoneDisplayRect(const cv::Size& frame_size) const;
    void drawUiOverlay(cv::Mat& frame, const RobotMessage& message, SystemState state) const;
    void drawDashboard(cv::Mat& frame, SystemState state, long long heartbeat_age_ms) const;
    void printWorldCoordinate(const cv::Point2f& model_center, const cv::Size& camera_size);
    void updateFrameStats(
        const std::chrono::high_resolution_clock::time_point& frame_time,
        const std::chrono::high_resolution_clock::time_point& camera_read_start,
        const std::chrono::high_resolution_clock::time_point& camera_read_end);
    void updateInferenceStats(
        const std::chrono::high_resolution_clock::time_point& inference_start,
        const std::chrono::high_resolution_clock::time_point& inference_end);
    void updateUdpSendStats(
        const std::chrono::high_resolution_clock::time_point& udp_send_start,
        const std::chrono::high_resolution_clock::time_point& udp_send_end);
    PerformanceStats getPerformanceStats() const;

    VisionSensor sensor_;
    OnnxModel model_;

    int infer_interval_ms_ = 300;

    SOCKET udp_socket_ = INVALID_SOCKET;
    sockaddr_in udp_target_addr_{};
    bool udp_enabled_ = false;

    std::atomic<bool> running_{ false };
    std::thread inference_thread_;
    std::thread message_thread_;
    std::thread watchdog_thread_;

    std::mutex frame_mutex_;
    std::mutex result_mutex_;
    std::mutex world_mutex_;
    std::mutex watchdog_mutex_;
    mutable std::mutex performance_mutex_;
    std::condition_variable world_cv_;

    cv::Mat latest_camera_frame_;
    InferenceResult latest_result_;

    RobotMessage latest_robot_message_;
    bool has_new_robot_message_ = false;

    float center_filter_alpha_ = 0.25f;
    bool has_filtered_center_ = false;
    cv::Point2f filtered_center_{ -1.0f, -1.0f };

    CameraIntrinsics camera_intrinsics_{};
    const double mock_depth_mm_ = 500.0;
    SafeZone safe_zone_{};
    SystemState system_state_ = SystemState::STANDBY;
    EStopReason estop_reason_ = EStopReason::NONE;
    SOCKET watchdog_socket_ = INVALID_SOCKET;
    std::chrono::steady_clock::time_point last_heartbeat_time_{};
    bool has_heartbeat_ = false;
    PerformanceStats performance_stats_{};
    const int watchdog_port_ = 9999;
    const int watchdog_recv_timeout_ms_ = 200;
    int watchdog_timeout_ms_ = 500;
    std::atomic<bool> coordinate_debug_enabled_{ true };
    std::chrono::steady_clock::time_point last_heartbeat_warn_log_{};
};
