#include "VisionWorker.h"

#include "ConfigManager.h"

#include <opencv2/opencv.hpp>

#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>

#include <winsock2.h>
#include <ws2tcpip.h>

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
        cv::Mat rgb_frame;
        cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
        return QImage(
            rgb_frame.data,
            rgb_frame.cols,
            rgb_frame.rows,
            static_cast<int>(rgb_frame.step),
            QImage::Format_RGB888
        ).copy();
    }

    if (frame.type() == CV_8UC4) {
        cv::Mat rgba_frame;
        cv::cvtColor(frame, rgba_frame, cv::COLOR_BGRA2RGBA);
        return QImage(
            rgba_frame.data,
            rgba_frame.cols,
            rgba_frame.rows,
            static_cast<int>(rgba_frame.step),
            QImage::Format_RGBA8888
        ).copy();
    }

    if (frame.type() == CV_8UC1) {
        return QImage(
            frame.data,
            frame.cols,
            frame.rows,
            static_cast<int>(frame.step),
            QImage::Format_Grayscale8
        ).copy();
    }

    return {};
}

void VisionWorker::start() {
    if (running_) {
        return;
    }

    const QString app_dir = QCoreApplication::applicationDirPath();
    const QString config_path = QDir(app_dir).filePath("config.json");
    const QString model_path = QDir(app_dir).filePath("models/best.onnx");

    ConfigManager::instance().load(config_path.toStdString());
    const AppConfig config = ConfigManager::instance().snapshot();

    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        emit finished();
        return;
    }

    SOCKET udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket == INVALID_SOCKET) {
        WSACleanup();
        emit finished();
        return;
    }

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(static_cast<u_short>(config.network.udp_port));
    inet_pton(AF_INET, config.network.target_ip.c_str(), &target_addr.sin_addr);

    processor_.setUdpTarget(udp_socket, target_addr);

    if (!processor_.init(model_path.toStdString(), 0, 300)) {
        closesocket(udp_socket);
        WSACleanup();
        emit finished();
        return;
    }

    running_ = true;
    QElapsedTimer telemetry_timer;
    telemetry_timer.start();

    while (running_) {
        cv::Mat output_frame;
        if (processor_.processFrame(output_frame)) {
            QImage image = matToQImage(output_frame);
            if (!image.isNull()) {
                emit frameReady(image);
            }
        }

        if (telemetry_timer.elapsed() >= 100) {
            const VisionTelemetry telemetry = processor_.getTelemetrySnapshot();
            emit telemetryReady(
                telemetry.x_mm,
                telemetry.y_mm,
                telemetry.z_mm,
                telemetry.coordinate_valid,
                telemetry.network_online,
                static_cast<qint64>(telemetry.heartbeat_age_ms),
                systemStateText(telemetry.system_state));
            telemetry_timer.restart();
        }
    }

    processor_.stop();
    closesocket(udp_socket);
    WSACleanup();
    emit finished();
}

void VisionWorker::stop() {
    running_ = false;
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
