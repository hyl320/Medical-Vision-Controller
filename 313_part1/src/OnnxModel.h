#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

// 单个检测目标的信息。
//
// YOLO-Seg 的 output0 里不仅包含检测框和类别分数，
// 还包含用于生成分割 mask 的 mask coefficients。
struct Detection {
    // 检测框，坐标系是当前 640x640 的模型/显示坐标系。
    cv::Rect box;

    // 置信度，表示这个目标预测有多可信。
    float score = 0.0f;

    // 类别编号。你的模型有多个类别时，可以用它区分部位或目标类型。
    int class_id = -1;

    // YOLO-Seg 用来和 proto 原型矩阵相乘的 mask 系数。
    // 常见数量是 32，对应 output1 的 32 个 proto 通道。
    std::vector<float> mask_coeffs;
};

// 一次 ONNX 推理的完整后处理结果。
//
// 这个结构体会在线程之间传递：
// 后台推理线程生成它，主线程拿它来画画面。
struct InferenceResult {
    // NMS 后保留下来的检测目标。当前 mask 只基于第一个主目标生成。
    std::vector<Detection> detections;

    // 单目标二值 mask，CV_32F，尺寸通常是 640x640。
    // 1 表示目标区域，0 表示背景。
    cv::Mat mask;

    // mask 的外部轮廓。当前只保留最大核心轮廓，便于画边界和求中心。
    std::vector<std::vector<cv::Point>> contours;

    // 未滤波的中心点，来自 moments 计算，会比较抖。
    cv::Point2f raw_center{-1.0f, -1.0f};

    // 滤波后的中心点，适合后续控制逻辑使用。
    cv::Point2f center{-1.0f, -1.0f};

    // 单次 ONNX Runtime 推理耗时，单位 ms。
    long long inference_ms = 0;

    // true 表示本次结果结构有效。即使没有检测到目标，也可以是有效的一次推理。
    bool valid = false;
};

// OnnxModel 是 ONNX Runtime 推理层。
//
// 它负责：
// 1. 加载 .onnx 模型。
// 2. 执行一次模型推理。
// 3. 解析 YOLO-Seg 的 output0 / output1。
// 4. 生成检测框、mask、轮廓和原始中心点。
// 5. 把结果画回图像。
//
// 它不负责摄像头，也不负责多线程调度；这些由 VisionProcessor 管理。
class OnnxModel {
public:
    OnnxModel();

    // 加载 ONNX 模型文件，并缓存输入/输出节点名称。
    bool load(const std::string& model_path);

    // 判断模型是否已经成功加载。
    bool isLoaded() const;

    // 打印模型输入/输出节点名称，方便确认 output0 / output1。
    void printModelInfo() const;

    // 执行一次推理。
    // blob 是已经预处理好的 NCHW float tensor，通常形状为 1x3x640x640。
    // result 会被写入完整后处理结果。
    bool infer(const cv::Mat& blob, InferenceResult& result);

    // 把推理结果画到 image 上：
    // mask、轮廓、bbox、raw center、filtered center 都在这里显示。
    void drawResult(cv::Mat& image, const InferenceResult& result) const;

    // 调试用接口：推理一次并立刻画到 image 上。
    // 当前主流程更多使用 infer() + VisionProcessor 滤波 + drawResult()。
    bool runOnce(const cv::Mat& blob, cv::Mat& image);

private:
    // 读取并缓存 ONNX Runtime 的输入/输出节点名称。
    // 这样每次推理时不用重复向 session 查询节点名。
    bool prepareNames();

    // ONNX Runtime 全局环境。一个模型类持有一个环境即可。
    Ort::Env env_;

    // Session 配置，例如线程数、图优化级别等。
    Ort::SessionOptions session_options_;

    // 真正的 ONNX Runtime 会话对象，负责执行模型。
    std::unique_ptr<Ort::Session> session_;

    // 节点名称必须保存成 string，保证 const char* 指针在推理时仍然有效。
    std::vector<std::string> input_name_strings_;
    std::vector<std::string> output_name_strings_;

    // ONNX Runtime Run() 需要 const char* 数组，所以这里缓存指针版本。
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
};
