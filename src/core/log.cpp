#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace mn {

static std::mutex g_log_mutex;
static std::function<void(LogLevel, const std::string&)> g_sink;
static LogLevel g_level = LogLevel::Info;

void log_set_sink(std::function<void(LogLevel, const std::string&)> sink) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    g_sink = std::move(sink);
}

void log_set_level(LogLevel lvl) { g_level = lvl; }

void logf(LogLevel lvl, const char* fmt, ...) {
    if (lvl < g_level) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(g_log_mutex);
    if (g_sink) {
        g_sink(lvl, buf);
    } else {
        static const char* names[] = {"DBG", "INF", "WRN", "ERR"};
        fprintf(stderr, "[%s] %s\n", names[(int)lvl], buf);
    }
}

}  // namespace mn
