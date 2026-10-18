#include "VisionWorker.h"

#include "ConfigManager.h"

#include <opencv2/opencv.hpp>

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTimer>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <string>

namespace {
QString systemStateText(SystemState state) {
    switch (state) {
    case SystemState::STANDBY:
        return QStringLiteral("STANDBY");
    case SystemState::TRACKING:
        return QStringLiteral("TRACKING");
    case SystemState::E_STOP:
        return QStringLiteral("E-STOP");
    default:
        return QStringLiteral("UNKNOWN");
    }
}
}

VisionWorker::VisionWorker(QObject* parent)
    : QObject(parent) {
}

QImage VisionWorker::matToQImage(const cv::Mat& frame) {
    if (frame.empty()) {
        return {};
    }

    if (frame.type() == CV_8UC3) {
        QImage image(frame.cols, frame.rows, QImage::Format_RGB888);
        cv::Mat rgb_view(
            frame.rows,
            frame.cols,
            CV_8UC3,
            image.bits(),
            static_cast<size_t>(image.bytesPerLine()));
        cv::cvtColor(frame, rgb_view, cv::COLOR_BGR2RGB);
        return image;
    }

    if (frame.type() == CV_8UC4) {
        QImage image(frame.cols, frame.rows, QImage::Format_RGBA8888);
        cv::Mat rgba_view(
            frame.rows,
            frame.cols,
            CV_8UC4,
            image.bits(),
            static_cast<size_t>(image.bytesPerLine()));
        cv::cvtColor(frame, rgba_view, cv::COLOR_BGRA2RGBA);
        return image;
    }

    if (frame.type() == CV_8UC1) {
        QImage image(frame.cols, frame.rows, QImage::Format_Grayscale8);
        for (int row = 0; row < frame.rows; ++row) {
            std::memcpy(
                image.scanLine(row),
                frame.ptr(row),
                static_cast<size_t>(frame.cols));
        }
        return image;
    }

    return {};
}

void VisionWorker::start() {
    if (running_) {
        return;
    }

    cleanup_done_ = false;
    if (!initializeProcessor()) {
        cleanupProcessor();
        emit finished();
        return;
    }

    running_ = true;
    telemetry_timer_.restart();

    if (!frame_timer_) {
        frame_timer_ = new QTimer(this);
        frame_timer_->setTimerType(Qt::PreciseTimer);
        frame_timer_->setInterval(0);
        connect(frame_timer_, &QTimer::timeout, this, &VisionWorker::processNextFrame);
    }
    frame_timer_->start();
}

bool VisionWorker::initializeProcessor() {
    const QString app_dir = QCoreApplication::applicationDirPath();
    const QString config_path = QDir(app_dir).filePath("config.json");
    const QString model_path = QDir(app_dir).filePath("models/best.onnx");

    ConfigManager::instance().load(config_path.toStdString());
    const AppConfig config = ConfigManager::instance().snapshot();

    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        return false;
    }
    winsock_started_ = true;

    udp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_ == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(static_cast<u_short>(config.network.udp_port));
    inet_pton(AF_INET, config.network.target_ip.c_str(), &target_addr.sin_addr);

    processor_.setUdpTarget(udp_socket_, target_addr);

    if (!processor_.init(model_path.toStdString(), 0, 300)) {
        return false;
    }

    return true;
}

void VisionWorker::processNextFrame() {
    if (!running_) {
        return;
    }

    cv::Mat output_frame;
    if (processor_.processFrame(output_frame)) {
        QImage image = matToQImage(output_frame);
        if (!image.isNull()) {
            emit frameReady(image);
        }
    }

    if (telemetry_timer_.elapsed() >= 100) {
        emitTelemetrySnapshot();
        telemetry_timer_.restart();
    }
}

void VisionWorker::emitTelemetrySnapshot() {
    const VisionTelemetry telemetry = processor_.getTelemetrySnapshot();
    emit telemetryReady(
        telemetry.x_mm,
        telemetry.y_mm,
        telemetry.z_mm,
        telemetry.coordinate_valid,
        telemetry.network_online,
        static_cast<qint64>(telemetry.heartbeat_age_ms),
        systemStateText(telemetry.system_state));
}

void VisionWorker::cleanupProcessor() {
    if (cleanup_done_) {
        return;
    }

    if (frame_timer_) {
        frame_timer_->stop();
    }

    processor_.stop();

    if (udp_socket_ != INVALID_SOCKET) {
        closesocket(udp_socket_);
        udp_socket_ = INVALID_SOCKET;
    }

    if (winsock_started_) {
        WSACleanup();
        winsock_started_ = false;
    }

    cleanup_done_ = true;
}

void VisionWorker::stop() {
    if (!running_ && cleanup_done_) {
        return;
    }

    running_ = false;
    cleanupProcessor();
    emit finished();
}

void VisionWorker::emergencyStop() {
    processor_.triggerEmergencyStop();
}

void VisionWorker::updateRuntimeTuning(
    double filter_alpha,
    double half_width_mm,
    double half_height_mm) {
    processor_.updateRuntimeTuning(filter_alpha, half_width_mm, half_height_mm);
}

void VisionWorker::saveConfig() {
    emit configSaved(ConfigManager::instance().save());
}
