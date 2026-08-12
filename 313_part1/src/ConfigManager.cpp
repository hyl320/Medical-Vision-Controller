#include "ConfigManager.h"

#include "Logger.h"

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {
template <typename T>
T valueOrDefault(const nlohmann::json& node, const char* key, const T& default_value) {
    if (!node.is_object() || !node.contains(key) || node.at(key).is_null()) {
        return default_value;
    }

    return node.at(key).get<T>();
}
}

ConfigManager& ConfigManager::instance() {
    static ConfigManager manager;
    return manager;
}

bool ConfigManager::load(const std::string& config_path) {
    config_ = AppConfig{};

    std::ifstream file(config_path);
    if (!file.is_open()) {
        Logger::LogCritical("[CRITICAL] Config file missing or invalid!");
        Logger::LogWarn("配置文件加载失败，使用默认参数");
        return false;
    }

    try {
        nlohmann::json root;
        file >> root;
        if (!root.is_object()) {
            throw std::runtime_error("config root must be a JSON object");
        }

        const auto& network = root.value("Network", nlohmann::json::object());
        config_.network.target_ip = valueOrDefault(network, "target_ip", config_.network.target_ip);
        config_.network.udp_port = valueOrDefault(network, "udp_port", config_.network.udp_port);

        const auto& camera = root.value("Camera", nlohmann::json::object());
        config_.camera.fx = valueOrDefault(camera, "fx", config_.camera.fx);
        config_.camera.fy = valueOrDefault(camera, "fy", config_.camera.fy);
        config_.camera.cx = valueOrDefault(camera, "cx", config_.camera.cx);
        config_.camera.cy = valueOrDefault(camera, "cy", config_.camera.cy);

        const auto& control = root.value("Control", nlohmann::json::object());
        config_.control.filter_alpha = valueOrDefault(control, "filter_alpha", config_.control.filter_alpha);
        config_.control.heartbeat_timeout_ms = valueOrDefault(control, "heartbeat_timeout_ms", config_.control.heartbeat_timeout_ms);

        const auto& safety = root.value("Safety", nlohmann::json::object());
        config_.safety.min_x_mm = valueOrDefault(safety, "min_x_mm", config_.safety.min_x_mm);
        config_.safety.max_x_mm = valueOrDefault(safety, "max_x_mm", config_.safety.max_x_mm);
        config_.safety.min_y_mm = valueOrDefault(safety, "min_y_mm", config_.safety.min_y_mm);
        config_.safety.max_y_mm = valueOrDefault(safety, "max_y_mm", config_.safety.max_y_mm);

        Logger::LogInfo("Config loaded: " + config_path);
        return true;
    } catch (const std::exception& e) {
        config_ = AppConfig{};
        Logger::LogCritical("[CRITICAL] Config file missing or invalid!");
        Logger::LogWarn(std::string("配置文件加载失败，使用默认参数: ") + e.what());
        return false;
    }
}

const AppConfig& ConfigManager::config() const {
    return config_;
}

const NetworkConfig& ConfigManager::network() const {
    return config_.network;
}

const CameraConfig& ConfigManager::camera() const {
    return config_.camera;
}

const ControlConfig& ConfigManager::control() const {
    return config_.control;
}

const SafetyConfig& ConfigManager::safety() const {
    return config_.safety;
}
