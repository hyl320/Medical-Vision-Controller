#include "VisionProcessor.h"

#include "Letterbox.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>

#include <cstring>
#include <sstream>
#include <string>



VisionProcessor::VisionProcessor() = default;

VisionProcessor::~VisionProcessor() {
    stop();
}

bool VisionProcessor::init(const std::string& model_path, int camera_id, int infer_interval_ms) {
    infer_interval_ms_ = infer_interval_ms;

    if (!model_.load(model_path)) {
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
    checkWatchdogTimeout();
    SystemState system_state = getSystemState();
    long long heartbeat_age_ms = getHeartbeatAgeMs();

    cv::Mat current_frame;
    if (!sensor_.getNextFrame(current_frame)) {
        return false;
    }

    {
        // 把最新原始画面交给后台线程，供下一次推理使用。
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_camera_frame_ = current_frame.clone();
    }

    // 显示画面也使用 640x640，保证坐标系和推理结果一致。
    output_frame = Letterbox(current_frame, cv::Size(640, 640));

    InferenceResult result_copy;
    RobotMessage robot_message_copy = getLatestRobotMessage();
    {
        // 复制最近一次推理结果；真正画图放在锁外，避免阻塞后台线程。
        std::lock_guard<std::mutex> lock(result_mutex_);
        result_copy = latest_result_;
    }

    // 每一帧都复用最近一次检测框和 mask，让画面持续有结果显示。
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

        if (system_state == SystemState::TRACKING && robot_message.state == RobotState::SAFE) {
            std::cout << std::fixed << std::setprecision(2)
                << "World 3D X: " << world.x_mm << " mm, "
                << "Y: " << world.y_mm << " mm, "
                << "Z: " << world.z_mm << " mm"
                << std::defaultfloat << std::setprecision(6) << std::endl;

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << "X:" << world.x_mm << ",Y:" << world.y_mm << ",Z:" << world.z_mm;

            std::string message = oss.str();
            sendto(
                udp_socket_,
                message.c_str(),
                static_cast<int>(message.size()),
                0,
                reinterpret_cast<sockaddr*>(&udp_target_addr_),
                sizeof(udp_target_addr_)
            );
            continue;
        }

        const char* stop_message = "CMD:STOP";
        while (running_) {
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

            sendto(
                udp_socket_,
                stop_message,
                static_cast<int>(std::strlen(stop_message)),
                0,
                reinterpret_cast<sockaddr*>(&udp_target_addr_),
                sizeof(udp_target_addr_)
            );

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void VisionProcessor::watchdogLoop() {
    watchdog_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (watchdog_socket_ == INVALID_SOCKET) {
        std::cerr << "[Watchdog] Failed to create UDP receive socket." << std::endl;
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

    if (bind(
        watchdog_socket_,
        reinterpret_cast<sockaddr*>(&local_addr),
        sizeof(local_addr)
    ) == SOCKET_ERROR) {
        std::cerr << "[Watchdog] Failed to bind UDP receive port "
                  << watchdog_port_ << ": " << WSAGetLastError() << std::endl;
        closesocket(watchdog_socket_);
        watchdog_socket_ = INVALID_SOCKET;
        return;
    }

    std::cout << "[Watchdog] Listening for heartbeat on 127.0.0.1:"
              << watchdog_port_ << std::endl;

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
                std::cerr << "[Watchdog] recvfrom failed: " << error << std::endl;
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
            }
        }
    }
}

void VisionProcessor::checkWatchdogTimeout() {
    bool timed_out = false;

    {
        std::lock_guard<std::mutex> lock(watchdog_mutex_);
        if (!has_heartbeat_) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_heartbeat_time_
        ).count();

        if (elapsed_ms > watchdog_timeout_ms_) {
            system_state_ = SystemState::E_STOP;
            estop_reason_ = EStopReason::WATCHDOG_TIMEOUT;
            timed_out = true;
        }
    }

    if (timed_out) {
        publishRobotMessage({ RobotState::TARGET_LOST, {} });
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
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_heartbeat_time_
    ).count();
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
    auto last_infer_time = std::chrono::steady_clock::now() -
        std::chrono::milliseconds(infer_interval_ms_);

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_infer_time
        ).count();

        if (elapsed_ms < infer_interval_ms_) {
            // 还没到下一次推理时间，短暂休眠，避免空转吃满 CPU。
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        cv::Mat frame_for_infer;
        {
            // 加锁只做一件事：把最新画面 clone 出来，然后马上释放锁。
            std::lock_guard<std::mutex> lock(frame_mutex_);
            if (!latest_camera_frame_.empty()) {
                frame_for_infer = latest_camera_frame_.clone();
            }
        }

        if (frame_for_infer.empty()) {
            // 程序刚启动时，主线程可能还没来得及提供第一帧。
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        last_infer_time = now;

        // 模型预处理：Letterbox 到 640x640，BGR 转 RGB，
        // 再转成 NCHW 格式的 float blob，并归一化到 0~1。
        cv::Mat letterbox_img = Letterbox(frame_for_infer, cv::Size(640, 640));
        cv::Mat model_input_rgb;
        cv::cvtColor(letterbox_img, model_input_rgb, cv::COLOR_BGR2RGB);

        cv::Mat blob;
        cv::dnn::blobFromImage(
            model_input_rgb,
            blob,
            1.0 / 255.0,
            cv::Size(640, 640),
            cv::Scalar(),
            false,
            false
        );

        InferenceResult result;
        if (model_.infer(blob, result)) {
            applyCenterFilter(result);

            if (result.contours.empty()) {
                enterEStop(EStopReason::TARGET_LOST);
                publishRobotMessage({ RobotState::TARGET_LOST, {} });
            }
            else {
                cv::Point2f camera_pixel = modelPointToCameraPixel(result.center, frame_for_infer.size());
                WorldPoint3D world = pixelToWorld3D(camera_pixel);

                if (!isInsideSafeZone(world)) {
                    enterEStop(EStopReason::OUT_OF_BOUNDS);
                    publishRobotMessage({ RobotState::OUT_OF_BOUNDS, world });
                }
                else {
                    publishRobotMessage({ RobotState::SAFE, world });
                }
            }
            

            // 推理成功后，把最新结果发布给主线程显示。
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

    if (!has_filtered_center_) {
        filtered_center_ = raw_center;
        has_filtered_center_ = true;
    } else {
        filtered_center_.x =
            center_filter_alpha_ * raw_center.x +
            (1.0f - center_filter_alpha_) * filtered_center_.x;

        filtered_center_.y =
            center_filter_alpha_ * raw_center.y +
            (1.0f - center_filter_alpha_) * filtered_center_.y;
    }

    result.center = filtered_center_;

    std::cout << "Raw Center: (" << raw_center.x << ", " << raw_center.y << ")"
              << "  Filtered Center: (" << filtered_center_.x << ", " << filtered_center_.y << ")"
              << std::endl;
}

cv::Point2f VisionProcessor::modelPointToCameraPixel(
    const cv::Point2f& model_point,
    const cv::Size& camera_size
) const {
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

    return cv::Point2f(
        (model_point.x - pad_x) / scale,
        (model_point.y - pad_y) / scale
    );
}

VisionProcessor::WorldPoint3D VisionProcessor::pixelToWorld3D(const cv::Point2f& pixel_point) const {
    WorldPoint3D world;
    world.z_mm = mock_depth_mm_;
    world.x_mm = (static_cast<double>(pixel_point.x) - camera_intrinsics_.cx) *
        mock_depth_mm_ / camera_intrinsics_.fx;
    world.y_mm = (static_cast<double>(pixel_point.y) - camera_intrinsics_.cy) *
        mock_depth_mm_ / camera_intrinsics_.fy;
    return world;
}

cv::Rect VisionProcessor::getSafeZoneDisplayRect(const cv::Size& frame_size) const {
    const int margin_x = std::max(20, frame_size.width / 8);
    const int margin_y = std::max(20, frame_size.height / 8);
    return cv::Rect(
        margin_x,
        margin_y,
        std::max(1, frame_size.width - margin_x * 2),
        std::max(1, frame_size.height - margin_y * 2)
    );
}

bool VisionProcessor::isInsideSafeZone(const WorldPoint3D& world) const {
    return world.x_mm >= safe_zone_.min_x_mm &&
        world.x_mm <= safe_zone_.max_x_mm &&
        world.y_mm >= safe_zone_.min_y_mm &&
        world.y_mm <= safe_zone_.max_y_mm;
}

void VisionProcessor::setUdpTarget(SOCKET socket, const sockaddr_in& target_addr) {
    udp_socket_ = socket;
    udp_target_addr_ = target_addr;
    udp_enabled_ = true;
}

//void VisionProcessor::printWorldCoordinate(const cv::Point2f& model_center, const cv::Size& camera_size)  {
//    cv::Point2f camera_pixel = modelPointToCameraPixel(model_center, camera_size);
//    if (camera_pixel.x < 0.0f || camera_pixel.y < 0.0f) {
//        return;
//    }
//
//    WorldPoint3D world = pixelToWorld3D(camera_pixel);
//
//    std::cout << std::fixed << std::setprecision(2)
//              << "World 3D X: " << world.x_mm << " mm, "
//              << "Y: " << world.y_mm << " mm, "
//              << "Z: " << world.z_mm << " mm"
//              << std::defaultfloat << std::setprecision(6) << std::endl;
//
//    std::ostringstream oss;
//    oss << std::fixed << std::setprecision(1)
//        << "X:" << world.x_mm << ",Y:" << world.y_mm << ",Z:" << world.z_mm;
//
//    std::string message = oss.str();
//
//    if (!udp_enabled_ || udp_socket_ == INVALID_SOCKET) {
//        return;
//    }
//
//    sendto(
//        udp_socket_,
//        message.c_str(),
//        static_cast<int>(message.size()),
//        0,
//        reinterpret_cast<sockaddr*>(&udp_target_addr_),
//        sizeof(udp_target_addr_)
//    );
//}
void VisionProcessor::printWorldCoordinate(
    const cv::Point2f& model_center,
    const cv::Size& camera_size
) {
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
}
