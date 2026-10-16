#pragma once

#include "CameraBase.h"

class MockCamera final : public ICameraBase {
public:
    MockCamera(double mock_depth_mm, bool synthetic_fallback);
    ~MockCamera() override;

    bool initialize(int camera_id = 0, const std::string& source = {}) override;
    bool getNextFrame(RgbdFrame& frame) override;
    void stop() override;
    const char* name() const override;

private:
    bool readFromSource(RgbdFrame& frame);
    bool makeSyntheticFrame(RgbdFrame& frame);
    void makeDepth(cv::Size size, cv::Mat& depth) const;

    cv::VideoCapture capture_;
    double mock_depth_mm_ = 500.0;
    bool synthetic_fallback_ = true;
    bool initialized_ = false;
    bool using_synthetic_ = false;
    std::int64_t frame_index_ = 0;
};
