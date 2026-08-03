#include "OnnxModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <utility>

namespace {
constexpr int kInputSize = 640;
constexpr int kNumCandidates = 8400;
constexpr int kNumClasses = 7;
constexpr int kMaskChannels = 32;
constexpr int kProtoH = 160;
constexpr int kProtoW = 160;
constexpr float kConfThreshold = 0.25f;
constexpr float kNmsThreshold = 0.45f;
constexpr float kMaskThreshold = 0.5f;

float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

bool shouldLogNow() {
    static auto last_log_time = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_log_time).count() < 2000) {
        return false;
    }
    last_log_time = now;
    return true;
}

void printShape(const std::string& name, const std::vector<int64_t>& shape) {
    std::cout << name << " shape: ";
    for (size_t i = 0; i < shape.size(); ++i) {
        std::cout << shape[i];
        if (i + 1 < shape.size()) {
            std::cout << "x";
        }
    }
    std::cout << std::endl;
}
}

OnnxModel::OnnxModel()
    : env_(ORT_LOGGING_LEVEL_WARNING, "MedicalVision"),
      session_options_(),
      session_(nullptr) {
    session_options_.SetIntraOpNumThreads(1);
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
}

bool OnnxModel::load(const std::string& model_path) {
    try {
#ifdef _WIN32
        std::wstring wide_model_path(model_path.begin(), model_path.end());
        session_ = std::make_unique<Ort::Session>(
            env_,
            wide_model_path.c_str(),
            session_options_
        );
#else
        session_ = std::make_unique<Ort::Session>(
            env_,
            model_path.c_str(),
            session_options_
        );
#endif
        if (!prepareNames()) {
            return false;
        }

        std::cout << "[Info] ONNX model loaded: " << model_path << std::endl;
        return true;
    }
    catch (const Ort::Exception& e) {
        std::cerr << "[Error] Failed to load ONNX model: " << e.what() << std::endl;
        return false;
    }
}

bool OnnxModel::prepareNames() {
    if (!session_) {
        return false;
    }

    Ort::AllocatorWithDefaultOptions allocator;
    input_name_strings_.clear();
    output_name_strings_.clear();
    input_names_.clear();
    output_names_.clear();

    for (size_t i = 0; i < session_->GetInputCount(); ++i) {
        auto input_name = session_->GetInputNameAllocated(i, allocator);
        input_name_strings_.emplace_back(input_name.get());
    }

    for (size_t i = 0; i < session_->GetOutputCount(); ++i) {
        auto output_name = session_->GetOutputNameAllocated(i, allocator);
        output_name_strings_.emplace_back(output_name.get());
    }

    for (const auto& name : input_name_strings_) {
        input_names_.push_back(name.c_str());
    }

    for (const auto& name : output_name_strings_) {
        output_names_.push_back(name.c_str());
    }

    return !input_names_.empty() && !output_names_.empty();
}

bool OnnxModel::isLoaded() const {
    return session_ != nullptr;
}

void OnnxModel::printModelInfo() const {
    if (!session_) {
        std::cerr << "[Error] Model is not loaded." << std::endl;
        return;
    }

    std::cout << "Input count: " << input_name_strings_.size() << std::endl;
    for (const auto& name : input_name_strings_) {
        std::cout << "Input Name: " << name << std::endl;
    }

    std::cout << "Output count: " << output_name_strings_.size() << std::endl;
    for (const auto& name : output_name_strings_) {
        std::cout << "Output Name: " << name << std::endl;
    }
}

bool OnnxModel::infer(const cv::Mat& blob, InferenceResult& result) {
    result = InferenceResult{};

    if (!session_) {
        std::cerr << "[Error] Model is not loaded." << std::endl;
        return false;
    }

    if (blob.empty() || blob.dims != 4 || blob.type() != CV_32F) {
        std::cerr << "[Error] Invalid input blob. Expected CV_32F blob with shape 1x3x640x640." << std::endl;
        return false;
    }

    std::vector<int64_t> input_shape;
    for (int i = 0; i < blob.dims; ++i) {
        input_shape.push_back(static_cast<int64_t>(blob.size[i]));
    }

    cv::Mat continuous_blob = blob.isContinuous() ? blob : blob.clone();
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator,
        OrtMemTypeDefault
    );

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info,
        reinterpret_cast<float*>(continuous_blob.data),
        continuous_blob.total(),
        input_shape.data(),
        input_shape.size()
    );

    try {
        bool log_this_run = shouldLogNow();
        auto start = std::chrono::high_resolution_clock::now();

        std::vector<Ort::Value> outputs = session_->Run(
            Ort::RunOptions{ nullptr },
            input_names_.data(),
            &input_tensor,
            input_names_.size(),
            output_names_.data(),
            output_names_.size()
        );

        auto end = std::chrono::high_resolution_clock::now();
        result.inference_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (outputs.size() < 2) {
            std::cerr << "[Error] Expected YOLO-seg outputs: output0 and output1." << std::endl;
            return false;
        }

        if (log_this_run) {
            for (size_t i = 0; i < outputs.size(); ++i) {
                auto shape = outputs[i].GetTensorTypeAndShapeInfo().GetShape();
                std::string name = i < output_name_strings_.size()
                    ? "Output " + std::to_string(i) + " (" + output_name_strings_[i] + ")"
                    : "Output " + std::to_string(i);
                printShape(name, shape);
            }
        }

        float* output0_data = outputs[0].GetTensorMutableData<float>();
        float* proto_data = outputs[1].GetTensorMutableData<float>();

        std::vector<cv::Rect> boxes;
        std::vector<float> scores;
        std::vector<Detection> candidates;
        boxes.reserve(64);
        scores.reserve(64);
        candidates.reserve(64);

        for (int i = 0; i < kNumCandidates; ++i) {
            int best_class_id = -1;
            float best_score = 0.0f;

            for (int c = 0; c < kNumClasses; ++c) {
                float score = output0_data[(4 + c) * kNumCandidates + i];
                if (score > best_score) {
                    best_score = score;
                    best_class_id = c;
                }
            }

            if (best_score < kConfThreshold) {
                continue;
            }

            float cx = output0_data[0 * kNumCandidates + i];
            float cy = output0_data[1 * kNumCandidates + i];
            float w = output0_data[2 * kNumCandidates + i];
            float h = output0_data[3 * kNumCandidates + i];

            int left = static_cast<int>(cx - w / 2.0f);
            int top = static_cast<int>(cy - h / 2.0f);
            int width = static_cast<int>(w);
            int height = static_cast<int>(h);

            cv::Rect box(left, top, width, height);
            box &= cv::Rect(0, 0, kInputSize, kInputSize);

            if (box.area() <= 0) {
                continue;
            }

            Detection detection;
            detection.box = box;
            detection.score = best_score;
            detection.class_id = best_class_id;
            detection.mask_coeffs.reserve(kMaskChannels);

            int mask_offset = 4 + kNumClasses;
            for (int m = 0; m < kMaskChannels; ++m) {
                detection.mask_coeffs.push_back(output0_data[(mask_offset + m) * kNumCandidates + i]);
            }

            boxes.push_back(detection.box);
            scores.push_back(detection.score);
            candidates.push_back(std::move(detection));
        }

        std::vector<int> indices;
        if (!boxes.empty()) {
            cv::dnn::NMSBoxes(boxes, scores, kConfThreshold, kNmsThreshold, indices);
        }

        result.detections.reserve(indices.size());
        for (int idx : indices) {
            result.detections.push_back(candidates[idx]);
        }

        if (!result.detections.empty()) {
            const Detection& detection = result.detections.front();
            cv::Mat low_res_mask(kProtoH, kProtoW, CV_32F, cv::Scalar(0.0f));
            const int proto_plane_size = kProtoH * kProtoW;

            for (int y = 0; y < kProtoH; ++y) {
                float* mask_row = low_res_mask.ptr<float>(y);
                for (int x = 0; x < kProtoW; ++x) {
                    int pixel_index = y * kProtoW + x;
                    float value = 0.0f;

                    for (int c = 0; c < kMaskChannels; ++c) {
                        value += detection.mask_coeffs[c] * proto_data[c * proto_plane_size + pixel_index];
                    }

                    mask_row[x] = sigmoid(value);
                }
            }

            cv::Mat mask_640;
            cv::resize(low_res_mask, mask_640, cv::Size(kInputSize, kInputSize), 0, 0, cv::INTER_LINEAR);

            cv::Mat binary_mask;
            cv::threshold(mask_640, binary_mask, kMaskThreshold, 1.0, cv::THRESH_BINARY);

            result.mask = cv::Mat::zeros(binary_mask.size(), CV_32F);
            binary_mask(detection.box).copyTo(result.mask(detection.box));

            cv::Mat contour_mask;
            result.mask.convertTo(contour_mask, CV_8U, 255.0);

            // 1) 闭运算：先把主区域内部的小洞尽量补上
            cv::Mat closed_mask;
            cv::Mat kernel = cv::getStructuringElement(
                cv::MORPH_ELLIPSE,
                cv::Size(5, 5)
            );
            cv::morphologyEx(contour_mask, closed_mask, cv::MORPH_CLOSE, kernel);

            // 2) 提取外轮廓
            result.contours.clear();
            cv::findContours(
                closed_mask,
                result.contours,
                cv::RETR_EXTERNAL,
                cv::CHAIN_APPROX_SIMPLE
            );

            // 3) 只保留面积最大的核心轮廓
            int best_index = -1;
            double best_area = 0.0;
            for (int i = 0; i < static_cast<int>(result.contours.size()); ++i) {
                double area = cv::contourArea(result.contours[i]);
                if (area > best_area) {
                    best_area = area;
                    best_index = i;
                }
            }

            cv::Mat solid_mask = cv::Mat::zeros(closed_mask.size(), CV_8U);
            if (best_index >= 0 && best_area > 100.0) {
                cv::drawContours(
                    solid_mask,
                    result.contours,
                    best_index,
                    cv::Scalar(255),
                    cv::FILLED
                );

                result.contours = { result.contours[best_index] };

                // 4) 用最大轮廓的实心区域计算 moments，得到中心点
                cv::Moments m = cv::moments(solid_mask, true);
                if (m.m00 > 0.0) {
                    result.raw_center = cv::Point2f(
                        static_cast<float>(m.m10 / m.m00),
                        static_cast<float>(m.m01 / m.m00)
                    );
                    result.center = result.raw_center;
                }
            }
        }

        result.valid = true;

        if (log_this_run) {
            std::cout << "Mask coefficients count: " << kMaskChannels << std::endl;
            std::cout << "Proto shape: 1x" << kMaskChannels << "x" << kProtoH << "x" << kProtoW << std::endl;
            std::cout << "Detections before NMS: " << boxes.size() << std::endl;
            std::cout << "Detections after NMS: " << result.detections.size() << std::endl;
            std::cout << "Contours: " << result.contours.size() << std::endl;
            if (result.raw_center.x >= 0.0f && result.raw_center.y >= 0.0f) {
                std::cout << "Raw Center: (" << result.raw_center.x << ", " << result.raw_center.y << ")" << std::endl;
            } else {
                std::cout << "Center: target not found" << std::endl;
            }
            std::cout << "Inference Time: " << result.inference_ms << " ms" << std::endl;
        }

        return true;
    }
    catch (const Ort::Exception& e) {
        std::cerr << "[Error] ONNX inference failed: " << e.what() << std::endl;
        return false;
    }
}

void OnnxModel::drawResult(cv::Mat& image, const InferenceResult& result) const {
    if (!result.valid || image.empty()) {
        return;
    }

    if (!result.mask.empty()) {
        cv::Mat mask = result.mask;
        if (mask.size() != image.size()) {
            cv::resize(mask, mask, image.size(), 0, 0, cv::INTER_NEAREST);
        }

        cv::Mat mask_u8;
        mask.convertTo(mask_u8, CV_8U, 255.0);

        cv::Mat color_layer(image.size(), image.type(), cv::Scalar(0, 255, 0));
        cv::Mat blended;
        cv::addWeighted(image, 0.6, color_layer, 0.4, 0.0, blended);
        blended.copyTo(image, mask_u8);
    }

    if (!result.contours.empty()) {
        cv::drawContours(image, result.contours, -1, cv::Scalar(0, 255, 255), 2);
    }

    if (result.raw_center.x >= 0.0f && result.raw_center.y >= 0.0f) {
        cv::Point raw_center_point(
            static_cast<int>(std::round(result.raw_center.x)),
            static_cast<int>(std::round(result.raw_center.y))
        );

        cv::circle(image, raw_center_point, 4, cv::Scalar(0, 0, 255), cv::FILLED);
        cv::line(
            image,
            cv::Point(raw_center_point.x - 10, raw_center_point.y),
            cv::Point(raw_center_point.x + 10, raw_center_point.y),
            cv::Scalar(0, 0, 255),
            2
        );
        cv::line(
            image,
            cv::Point(raw_center_point.x, raw_center_point.y - 10),
            cv::Point(raw_center_point.x, raw_center_point.y + 10),
            cv::Scalar(0, 0, 255),
            2
        );
    }

    if (result.center.x >= 0.0f && result.center.y >= 0.0f) {
        cv::Point center_point(
            static_cast<int>(std::round(result.center.x)),
            static_cast<int>(std::round(result.center.y))
        );

        cv::circle(image, center_point, 5, cv::Scalar(0, 255, 0), cv::FILLED);
        cv::line(
            image,
            cv::Point(center_point.x - 12, center_point.y),
            cv::Point(center_point.x + 12, center_point.y),
            cv::Scalar(0, 255, 0),
            2
        );
        cv::line(
            image,
            cv::Point(center_point.x, center_point.y - 12),
            cv::Point(center_point.x, center_point.y + 12),
            cv::Scalar(0, 255, 0),
            2
        );
    }

    for (const Detection& detection : result.detections) {
        cv::rectangle(image, detection.box, cv::Scalar(0, 255, 0), 2);

        std::string label = "class " + std::to_string(detection.class_id) + " " +
            std::to_string(detection.score).substr(0, 4);
        int label_top = std::max(detection.box.y - 5, 15);
        cv::putText(
            image,
            label,
            cv::Point(detection.box.x, label_top),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cv::Scalar(0, 255, 0),
            2
        );
    }
}

bool OnnxModel::runOnce(const cv::Mat& blob, cv::Mat& image) {
    InferenceResult result;
    if (!infer(blob, result)) {
        return false;
    }

    drawResult(image, result);
    return true;
}
