#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // 0 代表系统默认的第一个摄像头
    cv::VideoCapture cap(0);

    // 工业级防御性编程：永远不要假设硬件是正常的
    if (!cap.isOpened()) {
        std::cerr << "[Error] 视觉底座启动失败：无法连接摄像头！请检查是否被其他程序（如微信/会议）占用。" << std::endl;
        return -1;
    }

    cv::Mat frame;
    std::cout << ">>> 视觉底座已成功启动！正在接入视频流... <<<" << std::endl;
    std::cout << ">>> 提示：将焦点放在视频窗口上，按 'q' 或 'ESC' 键退出。 <<<" << std::endl;

    // 视觉系统的主循环
    while (true) {
        cap >> frame; // 从相机硬件中抽出一帧图像

        if (frame.empty()) {
            std::cerr << "[Warning] 捕获到空帧，跳过..." << std::endl;
            continue;
        }

        // 把这帧图像画在屏幕上
        cv::imshow("Vision Output", frame);

        // 这里的 30 代表等待 30 毫秒，同时也控制了最高帧率大概在 30帧/秒
        char key = (char)cv::waitKey(30);
        if (key == 'q' || key == 27) { // 27 是 ESC 键的 ASCII 码
            std::cout << "接收到退出指令，系统安全下线。" << std::endl;
            break;
        }
    }

    // 释放硬件资源（极其重要，否则下次启动会报错）
    cap.release();
    cv::destroyAllWindows();
    return 0;
}