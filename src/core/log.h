// Mirrorly - tiny thread-safe logger
#pragma once
#include <functional>
#include <string>

namespace mn {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void log_set_sink(std::function<void(LogLevel, const std::string&)> sink);
void log_set_level(LogLevel lvl);
void logf(LogLevel lvl, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#define LOGD(...) ::mn::logf(::mn::LogLevel::Debug, __VA_ARGS__)
#define LOGI(...) ::mn::logf(::mn::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::mn::logf(::mn::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::mn::logf(::mn::LogLevel::Error, __VA_ARGS__)

}  // namespace mn
