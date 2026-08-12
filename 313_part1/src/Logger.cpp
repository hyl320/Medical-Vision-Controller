#include "Logger.h"

#include <atomic>
#include <filesystem>
#include <ctime>
#include <mutex>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {
std::once_flag g_init_flag;
std::atomic<bool> g_debug_enabled{ true };

std::string MakeLogFilePath() {
    namespace fs = std::filesystem;
    const fs::path log_dir("logs");
    std::error_code ec;
    fs::create_directories(log_dir, ec);

    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char date_buf[32];
    std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", &tm);
    return (log_dir / (std::string("sys_") + date_buf + ".log")).string();
}
}

namespace Logger {

void Init() {
    std::call_once(g_init_flag, []() {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            MakeLogFilePath(),
            5 * 1024 * 1024,
            5
        );
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

        auto logger = std::make_shared<spdlog::logger>(
            "medical_vision",
            spdlog::sinks_init_list{ console_sink, file_sink }
        );
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    });
}

void SetDebugEnabled(bool enabled) {
    g_debug_enabled = enabled;
}

bool IsDebugEnabled() {
    return g_debug_enabled.load();
}

void LogInfo(const std::string& message) {
    Init();
    spdlog::info(message);
}

void LogDebug(const std::string& message) {
    Init();
    if (g_debug_enabled.load()) {
        spdlog::debug(message);
    }
}

void LogWarn(const std::string& message) {
    Init();
    spdlog::warn(message);
}

void LogCritical(const std::string& message) {
    Init();
    spdlog::critical(message);
}

}
