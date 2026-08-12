#pragma once

#include <string>

namespace Logger {

void Init();
void SetDebugEnabled(bool enabled);
bool IsDebugEnabled();

void LogInfo(const std::string& message);
void LogDebug(const std::string& message);
void LogWarn(const std::string& message);
void LogCritical(const std::string& message);

}
