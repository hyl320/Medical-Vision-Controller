#include "ConfigManager.h"
#include "VisionProcessor.h"

#include "Letterbox.h"
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

VisionProcessor::VisionProcessor() = default;

VisionProcessor::~VisionProcessor() {
    stop();
}

bool VisionProcessor::init(const std::string& model_path, int camera_id, int infer_interval_ms) {
    infer_interval_ms_ = infer_interval_ms;

    const AppConfig config = ConfigManager::instance().snapshot();
    camera_intrinsics_ = CameraIntrinsics{
        config.camera.fx,
        config.camera.fy,
        config.camera.cx,
        config.camera.cy
    };
    watchdog_timeout_ms_ = config.control.heartbeat_timeout_ms;
    {
        std::lock_guard<std::mutex> lock(runtime_config_mutex_);
        center_filter_alpha_ = static_cast<float>(config.control.filter_alpha);
        safe_zone_ = SafeZone{
            config.safety.min_x_mm,
            config.safety.max_x_mm,
            config.safety.min_y_mm,
            config.safety.max_y_mm
        };
    }

    Logger::Init();
    Logger::LogInfo("System startup");

    if (!model_.load(model_path)) {
        Logger::LogCritical("Model load failed");
        return false;
    }

    model_.printModelInfo();

    if (!sensor_.initialize(camera_id)) {
        return false;
    }

    running_ = true;
    message_thread_ = std::thread(&VisionProcessor::messageLoop, this);
    watchdog_thread_ = std::thread(&VisionProcessor::watchdogLoop, this);
    inference_thread_ = std::thread(&VisionProcessor::inferenceLoop, this);
    return true;
}

void VisionProcessor::setCoordinateDebugEnabled(bool enabled) {
    coordinate_debug_enabled_ = enabled;
    Logger::SetDebugEnabled(enabled);
    Logger::LogInfo(enabled ? "3D coordinate debug enabled" : "3D coordinate debug disabled");
}

bool VisionProcessor::isCoordinateDebugEnabled() const {
    return coordinate_debug_enabled_.load();
}

void VisionProcessor::updateRuntimeTuning(
    double filter_alpha,
    double half_width_mm,
    double half_height_mm) {
    const double clamped_alpha = std::clamp(filter_alpha, 0.01, 1.0);
    const double clamped_half_width = std::max(10.0, half_width_mm);
    const double clamped_half_height = std::max(10.0, half_height_mm);

    {
        std::lock_guard<std::mutex> lock(runtime_config_mutex_);
        center_filter_alpha_ = static_cast<float>(clamped_alpha);
        safe_zone_ = SafeZone{
            -clamped_half_width,
            clamped_half_width,
            -clamped_half_height,
            clamped_half_height
        };
    }

    ConfigManager::instance().updateRuntimeTuning(
        clamped_alpha,
        clamped_half_width,
        clamped_half_height);

    Logger::LogInfo(
        "Runtime tuning updated: alpha=" + std::to_string(clamped_alpha) +
        ", fence_half_width=" + std::to_string(clamped_half_width) +
        " mm, fence_half_height=" + std::to_string(clamped_half_height) + " mm");
}

VisionTelemetry VisionProcessor::getTelemetrySnapshot() {
    const RobotMessage message = getLatestRobotMessage();
    const SystemState state = getSystemState();
    const long long heartbeat_age_ms = getHeartbeatAgeMs();

    VisionTelemetry telemetry;
    telemetry.x_mm = message.world.x_mm;
    telemetry.y_mm = message.world.y_mm;
    telemetry.z_mm = message.world.z_mm;
    telemetry.coordinate_valid =
        !manual_estop_latched_ &&
        (message.state == RobotState::SAFE || message.state == RobotState::OUT_OF_BOUNDS);
    telemetry.heartbeat_age_ms = heartbeat_age_ms;
    telemetry.network_online =
        heartbeat_age_ms >= 0 && heartbeat_age_ms <= watchdog_timeout_ms_;
    telemetry.system_state = state;
    return telemetry;
}

void VisionProcessor::triggerEmergencyStop() {
    std::lock_guard<std::mutex> send_lock(udp_send_mutex_);
    if (manual_estop_latched_.exchange(true)) {
        return;
    }

    enterEStop(EStopReason::MANUAL_TRIGGER);
    publishRobotMessage({ RobotState::TARGET_LOST, {} });
    sendStopCommandLocked();
    Logger::LogCritical("Manual emergency stop triggered");
}

void VisionProcessor::publishRobotMessage(const RobotMessage& message) {
    {
        std::lock_guard<std::mutex> lock(world_mutex_);
        latest_robot_message_ = message;
        has_new_robot_message_ = true;
    }

    world_cv_.notify_one();
}

VisionProcessor::RobotMessage VisionProcessor::getLatestRobotMessage() {
    std::lock_guard<std::mutex> lock(world_mutex_);
    return latest_robot_message_;
}

void VisionProcessor::publishWorldCoordinate(const WorldPoint3D& world) {
    publishRobotMessage(RobotMessage{ RobotState::SAFE, world });
}

bool VisionProcessor::processFrame(cv::Mat& output_frame) {
    const auto frame_time = std::chrono::high_resolution_clock::now();
    checkWatchdogTimeout();
    SystemState system_state = getSystemState();
    long long heartbeat_age_ms = getHeartbeatAgeMs();

    const auto camera_read_start = std::chrono::high_resolution_clock::now();
    cv::Mat current_frame;
    if (!sensor_.getNextFrame(current_frame)) {
        return false;
    }
    const auto camera_read_end = std::chrono::high_resolution_clock::now();

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_camera_frame_ = current_frame.clone();
    }

    output_frame = Letterbox(current_frame, cv::Size(640, 640));
    updateFrameStats(frame_time, camera_read_start, camera_read_end);

    InferenceResult result_copy;
    RobotMessage robot_message_copy = getLatestRobotMessage();
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        result_copy = latest_result_;
    }

    model_.drawResult(output_frame, result_copy);
    drawUiOverlay(output_frame, robot_message_copy, system_state);
    drawDashboard(output_frame, system_state, heartbeat_age_ms);
    return true;
}

void VisionProcessor::stop() {
    running_ = false;
    world_cv_.notify_all();
    if (inference_thread_.joinable()) {
        inference_thread_.join();
    }
    if (message_thread_.joinable()) {
        message_thread_.join();
    }
    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }
    if (watchdog_socket_ != INVALID_SOCKET) {
        closesocket(watchdog_socket_);
        watchdog_socket_ = INVALID_SOCKET;
    }
}

void VisionProcessor::messageLoop() {
    while (running_) {
        RobotMessage robot_message;

        {
            std::unique_lock<std::mutex> lock(world_mutex_);
            world_cv_.wait(lock, [this]() {
                return has_new_robot_message_ || !running_;
            });

            if (!running_) {
                break;
            }

            robot_message = latest_robot_message_;
            has_new_robot_message_ = false;
        }

        const WorldPoint3D& world = robot_message.world;
        SystemState system_state = getSystemState();

        if (!udp_enabled_ || udp_socket_ == INVALID_SOCKET) {
            continue;
        }

        if (system_state == SystemState::TRACKING &&
            robot_message.state == RobotState::SAFE &&
            !manual_estop_latched_) {
            if (coordinate_debug_enabled_) {
                Logger::LogDebug(
                    "World 3D X: " + std::to_string(world.x_mm) +
                    " mm, Y: " + std::to_string(world.y_mm) +
                    " mm, Z: " + std::to_string(world.z_mm) + " mm"
                );
            }

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << "X:" << world.x_mm << ",Y:" << world.y_mm << ",Z:" << world.z_mm;

            std::string message = oss.str();
            const auto udp_send_start = std::chrono::high_resolution_clock::now();
            {
                std::lock_guard<std::mutex> send_lock(udp_send_mutex_);
                if (manual_estop_latched_) {
                    sendStopCommandLocked();
                    continue;
                }
                sendto(
                    udp_socket_,
                    message.c_str(),
                    static_cast<int>(message.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&udp_target_addr_),
                    sizeof(udp_target_addr_)
                );
            }
            const auto udp_send_end = std::chrono::high_resolution_clock::now();
            updateUdpSendStats(udp_send_start, udp_send_end);
            continue;
        }

        const char* stop_message = "CMD:STOP";
        while (running_) {
            if (getSystemState() == SystemState::TRACKING) {
                break;
            }

            {
                std::lock_guard<std::mutex> lock(world_mutex_);
                if (has_new_robot_message_) {
                    robot_message = latest_robot_message_;
                    has_new_robot_message_ = false;
                    if (robot_message.state == RobotState::SAFE) {
                        break;
                    }
                }
            }

            const auto udp_send_start = std::chrono::high_resolution_clock::now();
            {
                std::lock_guard<std::mutex> send_lock(udp_send_mutex_);
                sendto(
                    udp_socket_,
                    stop_message,
                    static_cast<int>(std::strlen(stop_message)),
                    0,
                    reinterpret_cast<sockaddr*>(&udp_target_addr_),
                    sizeof(udp_target_addr_)
                );
            }
            const auto udp_send_end = std::chrono::high_resolution_clock::now();
            updateUdpSendStats(udp_send_start, udp_send_end);

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void VisionProcessor::sendStopCommandLocked() {
    if (!udp_enabled_ || udp_socket_ == INVALID_SOCKET) {
        return;
    }

    const char* stop_message = "CMD:STOP";
    sendto(
        udp_socket_,
        stop_message,
        static_cast<int>(std::strlen(stop_message)),
        0,
        reinterpret_cast<sockaddr*>(&udp_target_addr_),
        sizeof(udp_target_addr_)
    );
}

void VisionProcessor::watchdogLoop() {
    watchdog_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (watchdog_socket_ == INVALID_SOCKET) {
        Logger::LogCritical("Failed to create UDP receive socket");
        return;
    }

    DWORD timeout_ms = static_cast<DWORD>(watchdog_recv_timeout_ms_);
    setsockopt(
        watchdog_socket_,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout_ms),
        sizeof(timeout_ms)
    );

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(static_cast<u_short>(watchdog_port_));
    inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr);

    if (bind(watchdog_socket_, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) == SOCKET_ERROR) {
        Logger::LogCritical("Failed to bind UDP receive port " + std::to_string(watchdog_port_));
        closesocket(watchdog_socket_);
        watchdog_socket_ = INVALID_SOCKET;
        return;
    }

    Logger::LogInfo("Listening for heartbeat on 127.0.0.1:" + std::to_string(watchdog_port_));

    char buffer[256];
    while (running_) {
        sockaddr_in sender_addr{};
        int sender_len = sizeof(sender_addr);
        int received = recvfrom(
            watchdog_socket_,
            buffer,
            static_cast<int>(sizeof(buffer) - 1),
            0,
            reinterpret_cast<sockaddr*>(&sender_addr),
            &sender_len
        );

        if (received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                continue;
            }

            if (running_) {
                Logger::LogWarn("recvfrom failed: " + std::to_string(error));
            }
            continue;
        }

        buffer[received] = '\0';
        std::string feedback(buffer);
        if (feedback != "ACK:OK") {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(watchdog_mutex_);
            last_heartbeat_time_ = std::chrono::steady_clock::now();
            has_heartbeat_ = true;
            if (system_state_ == SystemState::STANDBY ||
                (system_state_ == SystemState::E_STOP && estop_reason_ == EStopReason::WATCHDOG_TIMEOUT)) {
                system_state_ = SystemState::TRACKING;
                estop_reason_ = EStopReason::NONE;
                world_cv_.notify_one();
            }
        }
    }
}

void VisionProcessor::checkWatchdogTimeout() {
    bool timed_out = false;
    long long age_ms = -1;

    {
        std::lock_guard<std::mutex> lock(watchdog_mutex_);
        if (!has_heartbeat_) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_).count();

        if (age_ms > watchdog_timeout_ms_) {
            system_state_ = SystemState::E_STOP;
            estop_reason_ = EStopReason::WATCHDOG_TIMEOUT;
            timed_out = true;
        }
    }

    if (timed_out) {
        Logger::LogCritical("Heartbeat timeout");
        publishRobotMessage({ RobotState::TARGET_LOST, {} });
    } else if (age_ms > watchdog_timeout_ms_ * 0.7) {
        Logger::LogWarn("Heartbeat delay is rising: " + std::to_string(age_ms) + " ms");
    }
}

SystemState VisionProcessor::getSystemState() {
    std::lock_guard<std::mutex> lock(watchdog_mutex_);
    return system_state_;
}

void VisionProcessor::setSystemState(SystemState state) {
    std::lock_guard<std::mutex> lock(watchdog_mutex_);
    system_state_ = state;
    if (state != SystemState::E_STOP) {
        estop_reason_ = EStopReason::NONE;
    }
}

void VisionProcessor::enterEStop(EStopReason reason) {
    std::lock_guard<std::mutex> lock(watchdog_mutex_);
    system_state_ = SystemState::E_STOP;
    estop_reason_ = reason;
}

long long VisionProcessor::getHeartbeatAgeMs() {
    std::lock_guard<std::mutex> lock(watchdog_mutex_);
    if (!has_heartbeat_) {
        return -1;
    }

    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_).count();
}

const char* VisionProcessor::systemStateName(SystemState state) const {
    switch (state) {
    case SystemState::STANDBY:
        return "STANDBY";
    case SystemState::TRACKING:
        return "TRACKING";
    case SystemState::E_STOP:
        return "E_STOP";
    default:
        return "UNKNOWN";
    }
}

void VisionProcessor::inferenceLoop() {
    auto last_infer_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(infer_interval_ms_);

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_infer_time).count();

        if (elapsed_ms < infer_interval_ms_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        cv::Mat frame_for_infer;
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            if (!latest_camera_frame_.empty()) {
                frame_for_infer = latest_camera_frame_.clone();
            }
        }

        if (frame_for_infer.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        last_infer_time = now;

        const auto inference_start = std::chrono::high_resolution_clock::now();
        cv::Mat letterbox_img = Letterbox(frame_for_infer, cv::Size(640, 640));
        cv::Mat model_input_rgb;
        cv::cvtColor(letterbox_img, model_input_rgb, cv::COLOR_BGR2RGB);

        cv::Mat blob;
        cv::dnn::blobFromImage(model_input_rgb, blob, 1.0 / 255.0, cv::Size(640, 640), cv::Scalar(), false, false);

        InferenceResult result;
        if (model_.infer(blob, result)) {
            const auto inference_end = std::chrono::high_resolution_clock::now();
            updateInferenceStats(inference_start, inference_end);
            applyCenterFilter(result);

            if (result.contours.empty()) {
                Logger::LogCritical("Target lost");
                enterEStop(EStopReason::TARGET_LOST);
                publishRobotMessage({ RobotState::TARGET_LOST, {} });
            } else {
                cv::Point2f camera_pixel = modelPointToCameraPixel(result.center, frame_for_infer.size());
                WorldPoint3D world = pixelToWorld3D(camera_pixel);

                if (!isInsideSafeZone(world)) {
                    Logger::LogCritical("Electronic fence breach");
                    enterEStop(EStopReason::OUT_OF_BOUNDS);
                    publishRobotMessage({ RobotState::OUT_OF_BOUNDS, world });
                } else {
                    publishRobotMessage({ RobotState::SAFE, world });
                }
            }

            std::lock_guard<std::mutex> lock(result_mutex_);
            latest_result_ = std::move(result);
        }
    }
}

void VisionProcessor::applyCenterFilter(InferenceResult& result) {
    bool has_current_center = result.raw_center.x >= 0.0f && result.raw_center.y >= 0.0f;

    if (!has_current_center) {
        has_filtered_center_ = false;
        filtered_center_ = cv::Point2f(-1.0f, -1.0f);
        result.center = filtered_center_;
        return;
    }

    cv::Point2f raw_center = result.raw_center;
    float filter_alpha = 0.25f;
    {
        std::lock_guard<std::mutex> lock(runtime_config_mutex_);
        filter_alpha = center_filter_alpha_;
    }

    if (!has_filtered_center_) {
        filtered_center_ = raw_center;
        has_filtered_center_ = true;
    } else {
        filtered_center_.x = filter_alpha * raw_center.x + (1.0f - filter_alpha) * filtered_center_.x;
        filtered_center_.y = filter_alpha * raw_center.y + (1.0f - filter_alpha) * filtered_center_.y;
    }

    result.center = filtered_center_;

    if (coordinate_debug_enabled_) {
        Logger::LogDebug(
            "Raw Center: (" + std::to_string(raw_center.x) + ", " + std::to_string(raw_center.y) + ")" +
            "  Filtered Center: (" + std::to_string(filtered_center_.x) + ", " + std::to_string(filtered_center_.y) + ")"
        );
    }
}

cv::Point2f VisionProcessor::modelPointToCameraPixel(const cv::Point2f& model_point, const cv::Size& camera_size) const {
    if (model_point.x < 0.0f || model_point.y < 0.0f || camera_size.width <= 0 || camera_size.height <= 0) {
        return cv::Point2f(-1.0f, -1.0f);
    }

    const float scale = std::min(
        640.0f / static_cast<float>(camera_size.width),
        640.0f / static_cast<float>(camera_size.height)
    );
    const float resized_width = static_cast<float>(camera_size.width) * scale;
    const float resized_height = static_cast<float>(camera_size.height) * scale;
    const float pad_x = (640.0f - resized_width) * 0.5f;
    const float pad_y = (640.0f - resized_height) * 0.5f;

    return cv::Point2f((model_point.x - pad_x) / scale, (model_point.y - pad_y) / scale);
}

VisionProcessor::WorldPoint3D VisionProcessor::pixelToWorld3D(const cv::Point2f& pixel_point) const {
    WorldPoint3D world;
    world.z_mm = mock_depth_mm_;
    world.x_mm = (static_cast<double>(pixel_point.x) - camera_intrinsics_.cx) * mock_depth_mm_ / camera_intrinsics_.fx;
    world.y_mm = (static_cast<double>(pixel_point.y) - camera_intrinsics_.cy) * mock_depth_mm_ / camera_intrinsics_.fy;
    return world;
}

cv::Rect VisionProcessor::getSafeZoneDisplayRect(const cv::Size& frame_size) const {
    SafeZone safe_zone;
    CameraIntrinsics intrinsics;
    {
        std::lock_guard<std::mutex> lock(runtime_config_mutex_);
        safe_zone = safe_zone_;
        intrinsics = camera_intrinsics_;
    }

    const double half_width_mm = std::max(std::abs(safe_zone.min_x_mm), std::abs(safe_zone.max_x_mm));
    const double half_height_mm = std::max(std::abs(safe_zone.min_y_mm), std::abs(safe_zone.max_y_mm));
    const int rect_width = std::clamp(
        static_cast<int>((half_width_mm * 2.0 * intrinsics.fx) / mock_depth_mm_),
        40,
        std::max(40, frame_size.width - 20));
    const int rect_height = std::clamp(
        static_cast<int>((half_height_mm * 2.0 * intrinsics.fy) / mock_depth_mm_),
        40,
        std::max(40, frame_size.height - 20));
    const int margin_x = std::max(10, (frame_size.width - rect_width) / 2);
    const int margin_y = std::max(10, (frame_size.height - rect_height) / 2);
    return cv::Rect(
        margin_x,
        margin_y,
        std::max(1, rect_width),
        std::max(1, rect_height)
    );
}

bool VisionProcessor::isInsideSafeZone(const WorldPoint3D& world) const {
    SafeZone safe_zone;
    {
        std::lock_guard<std::mutex> lock(runtime_config_mutex_);
        safe_zone = safe_zone_;
    }

    return world.x_mm >= safe_zone.min_x_mm &&
        world.x_mm <= safe_zone.max_x_mm &&
        world.y_mm >= safe_zone.min_y_mm &&
        world.y_mm <= safe_zone.max_y_mm;
}

void VisionProcessor::setUdpTarget(SOCKET socket, const sockaddr_in& target_addr) {
    udp_socket_ = socket;
    udp_target_addr_ = target_addr;
    udp_enabled_ = true;
}

void VisionProcessor::printWorldCoordinate(const cv::Point2f& model_center, const cv::Size& camera_size) {
    cv::Point2f camera_pixel = modelPointToCameraPixel(model_center, camera_size);
    if (camera_pixel.x < 0.0f || camera_pixel.y < 0.0f) {
        return;
    }

    WorldPoint3D world = pixelToWorld3D(camera_pixel);
    publishWorldCoordinate(world);
}

void VisionProcessor::drawUiOverlay(cv::Mat& frame, const RobotMessage& message, SystemState state) const {
    if (frame.empty()) {
        return;
    }

    cv::Rect safe_rect = getSafeZoneDisplayRect(frame.size());
    if (state == SystemState::TRACKING && message.state == RobotState::SAFE) {
        cv::rectangle(frame, safe_rect, cv::Scalar(0, 255, 0), 2);
        return;
    }

    cv::rectangle(frame, cv::Rect(0, 0, frame.cols, frame.rows), cv::Scalar(0, 0, 255), 8);

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    bool blink_on = ((now_ms / 300) % 2) == 0;
    if (!blink_on) {
        return;
    }

    const std::string warning = "E-STOP!";
    int baseline = 0;
    double font_scale = 2.0;
    int thickness = 4;
    cv::Size text_size = cv::getTextSize(warning, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
    cv::Point text_org(
        (frame.cols - text_size.width) / 2,
        (frame.rows + text_size.height) / 2
    );

    cv::putText(
        frame,
        warning,
        text_org,
        cv::FONT_HERSHEY_SIMPLEX,
        font_scale,
        cv::Scalar(0, 0, 255),
        thickness,
        cv::LINE_AA
    );
}

void VisionProcessor::drawDashboard(cv::Mat& frame, SystemState state, long long heartbeat_age_ms) const {
    if (frame.empty()) {
        return;
    }

    PerformanceStats stats = getPerformanceStats();
    cv::Scalar state_color(0, 255, 255);
    if (state == SystemState::TRACKING) {
        state_color = cv::Scalar(0, 255, 0);
    } else if (state == SystemState::E_STOP) {
        state_color = cv::Scalar(0, 0, 255);
    }

    std::string state_text = std::string("State: ") + systemStateName(state);
    std::string ping_text = "Ping: LOST!";
    if (heartbeat_age_ms >= 0 && heartbeat_age_ms <= watchdog_timeout_ms_) {
        ping_text = "Ping: " + std::to_string(heartbeat_age_ms) + "ms";
    }

    std::string fps_text = "FPS: --";
    if (stats.fps > 0.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << "FPS: " << stats.fps;
        fps_text = oss.str();
    }

    std::ostringstream ai_oss;
    ai_oss << std::fixed << std::setprecision(1) << "AI: " << stats.inference_ms << "ms";

    std::ostringstream net_oss;
    net_oss << std::fixed << std::setprecision(1) << "Net: " << stats.udp_send_ms << "ms";

    cv::putText(
        frame,
        state_text,
        cv::Point(16, 32),
        cv::FONT_HERSHEY_SIMPLEX,
        0.8,
        state_color,
        2,
        cv::LINE_AA
    );

    cv::putText(
        frame,
        ping_text,
        cv::Point(16, 64),
        cv::FONT_HERSHEY_SIMPLEX,
        0.7,
        heartbeat_age_ms >= 0 && heartbeat_age_ms <= watchdog_timeout_ms_
            ? cv::Scalar(255, 255, 255)
            : cv::Scalar(0, 0, 255),
        2,
        cv::LINE_AA
    );

    cv::putText(
        frame,
        fps_text,
        cv::Point(16, 96),
        cv::FONT_HERSHEY_SIMPLEX,
        0.7,
        cv::Scalar(0, 255, 0),
        2,
        cv::LINE_AA
    );

    cv::putText(
        frame,
        ai_oss.str(),
        cv::Point(16, 128),
        cv::FONT_HERSHEY_SIMPLEX,
        0.7,
        cv::Scalar(0, 255, 255),
        2,
        cv::LINE_AA
    );

    cv::putText(
        frame,
        net_oss.str(),
        cv::Point(16, 160),
        cv::FONT_HERSHEY_SIMPLEX,
        0.7,
        cv::Scalar(0, 255, 255),
        2,
        cv::LINE_AA
    );
}

void VisionProcessor::updateFrameStats(
    const std::chrono::high_resolution_clock::time_point& frame_time,
    const std::chrono::high_resolution_clock::time_point& camera_read_start,
    const std::chrono::high_resolution_clock::time_point& camera_read_end) {
    std::lock_guard<std::mutex> lock(performance_mutex_);
    performance_stats_.camera_read_start = camera_read_start;
    performance_stats_.camera_read_end = camera_read_end;
    performance_stats_.camera_read_ms = std::chrono::duration<double, std::milli>(camera_read_end - camera_read_start).count();

    if (performance_stats_.fps_window_start.time_since_epoch().count() == 0) {
        performance_stats_.fps_window_start = frame_time;
        performance_stats_.last_frame_time = frame_time;
        performance_stats_.frames_in_window = 0;
        performance_stats_.fps_sample_sum = 0.0;
    }

    ++performance_stats_.frames_in_window;
    if (performance_stats_.last_frame_time.time_since_epoch().count() != 0) {
        const double frame_interval_ms = std::chrono::duration<double, std::milli>(frame_time - performance_stats_.last_frame_time).count();
        if (frame_interval_ms > 0.0) {
            performance_stats_.fps_sample_sum += 1000.0 / frame_interval_ms;
        }
    }

    const double elapsed_ms = std::chrono::duration<double, std::milli>(frame_time - performance_stats_.fps_window_start).count();
    if (performance_stats_.frames_in_window >= 10 || elapsed_ms >= 1000.0) {
        const int sample_count = performance_stats_.frames_in_window;
        if (sample_count > 0) {
            performance_stats_.fps = performance_stats_.fps_sample_sum / static_cast<double>(sample_count);
        }
        performance_stats_.frames_in_window = 0;
        performance_stats_.fps_sample_sum = 0.0;
        performance_stats_.fps_window_start = frame_time;
    }

    performance_stats_.last_frame_time = frame_time;
}

void VisionProcessor::updateInferenceStats(
    const std::chrono::high_resolution_clock::time_point& inference_start,
    const std::chrono::high_resolution_clock::time_point& inference_end) {
    std::lock_guard<std::mutex> lock(performance_mutex_);
    performance_stats_.inference_start = inference_start;
    performance_stats_.inference_end = inference_end;
    performance_stats_.inference_ms = std::chrono::duration<double, std::milli>(inference_end - inference_start).count();
}

void VisionProcessor::updateUdpSendStats(
    const std::chrono::high_resolution_clock::time_point& udp_send_start,
    const std::chrono::high_resolution_clock::time_point& udp_send_end) {
    std::lock_guard<std::mutex> lock(performance_mutex_);
    performance_stats_.udp_send_start = udp_send_start;
    performance_stats_.udp_send_end = udp_send_end;
    performance_stats_.udp_send_ms = std::chrono::duration<double, std::milli>(udp_send_end - udp_send_start).count();
}

VisionProcessor::PerformanceStats VisionProcessor::getPerformanceStats() const {
    std::lock_guard<std::mutex> lock(performance_mutex_);
    return performance_stats_;
}
