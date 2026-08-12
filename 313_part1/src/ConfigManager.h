#pragma once

#include <string>

struct NetworkConfig {
    std::string target_ip = "127.0.0.1";
    int udp_port = 8888;
};

struct CameraConfig {
    double fx = 500.0;
    double fy = 500.0;
    double cx = 320.0;
    double cy = 240.0;
};

struct ControlConfig {
    double filter_alpha = 0.25;
    int heartbeat_timeout_ms = 500;
};

struct SafetyConfig {
    double min_x_mm = -200.0;
    double max_x_mm = 200.0;
    double min_y_mm = -150.0;
    double max_y_mm = 150.0;
};

struct AppConfig {
    NetworkConfig network;
    CameraConfig camera;
    ControlConfig control;
    SafetyConfig safety;
};

class ConfigManager {
public:
    static ConfigManager& instance();

    bool load(const std::string& config_path = "config.json");
    const AppConfig& config() const;
    const NetworkConfig& network() const;
    const CameraConfig& camera() const;
    const ControlConfig& control() const;
    const SafetyConfig& safety() const;

private:
    ConfigManager() = default;

    AppConfig config_{};
};
