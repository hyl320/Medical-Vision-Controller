#include "VisionWorker.h"

#include "ConfigManager.h"

#include <opencv2/opencv.hpp>

#include <QDebug>
#include <QCoreApplication>
#include <QDir>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

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
    const AppConfig& config = ConfigManager::instance().config();

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

    while (running_) {
        cv::Mat output_frame;
        if (!processor_.processFrame(output_frame)) {
            continue;
        }

        QImage image = matToQImage(output_frame);
        if (!image.isNull()) {
            emit frameReady(image);
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
