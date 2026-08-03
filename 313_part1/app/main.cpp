#include "VisionProcessor.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <opencv2/opencv.hpp>

#ifndef MODEL_PATH
#define MODEL_PATH "models/best.onnx"
#endif

int main() {
    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (wsa_result != 0) {
        return -1;
    }

    SOCKET udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); //创建一个udp发送工具

    if (udp_socket == INVALID_SOCKET) {
        WSACleanup();
        return -1;
    }

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &target_addr.sin_addr);

    VisionProcessor processor;

    processor.setUdpTarget(udp_socket, target_addr);

    // Day 7 之后，main 只负责启动系统和显示画面。
    // 模型加载、摄像头、后台推理线程都被封装进 VisionProcessor。
    if (!processor.init(MODEL_PATH, 0, 300)) {
        return -1;
    }

    while (true) {
        cv::Mat output_frame;
        if (processor.processFrame(output_frame)) {
            cv::imshow("Vision System Output", output_frame);
        }

        // 按 ESC 退出程序。
        if (cv::waitKey(1) == 27) {
            break;
        }
    }
    
    processor.stop();
    cv::destroyAllWindows();

    closesocket(udp_socket);
    WSACleanup();
    return 0;
}
