#pragma once

#include "CameraBase.h"

#ifdef MEDICAL_VISION_ENABLE_REALSENSE
#include <librealsense2/rs.hpp>
#endif

class RealSenseCamera final : public ICameraBase {
public:
    RealSenseCamera();
    ~RealSenseCamera() override;

    bool initialize(int camera_id = 0, const std::string& source = {}) override;
    bool getNextFrame(RgbdFrame& frame) override;
    void stop() override;
    const char* name() const override;

private:
#ifdef MEDICAL_VISION_ENABLE_REALSENSE
    rs2::pipeline pipeline_;
    rs2::config config_;
    rs2::align align_to_color_{ RS2_STREAM_COLOR };
    rs2::pipeline_profile profile_;
#endif
    bool initialized_ = false;
};
