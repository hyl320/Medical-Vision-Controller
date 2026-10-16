#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

struct Detection {
    cv::Rect box;
    float score = 0.0f;
    int class_id = -1;
    std::vector<float> mask_coeffs;
};

struct InferenceResult {
    std::vector<Detection> detections;
    cv::Mat mask;
    std::vector<std::vector<cv::Point>> contours;
    cv::Point2f raw_center{-1.0f, -1.0f};
    cv::Point2f center{-1.0f, -1.0f};
    long long inference_ms = 0;
    bool valid = false;
};

class OnnxModel {
public:
    OnnxModel();

    bool load(const std::string& model_path);
    bool isLoaded() const;
    void printModelInfo() const;
    bool infer(const cv::Mat& blob, InferenceResult& result);
    void drawResult(cv::Mat& image, const InferenceResult& result) const;
    bool runOnce(const cv::Mat& blob, cv::Mat& image);

private:
    bool prepareNames();

    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_name_strings_;
    std::vector<std::string> output_name_strings_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
};
