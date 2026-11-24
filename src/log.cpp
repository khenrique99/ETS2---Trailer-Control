#include "log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace tc::log {

namespace {
std::atomic<scs_log_t> g_logger{nullptr};
std::mutex g_mutex;

void emit(scs_log_type_t type, const char *fmt, va_list args) {
    char body[1024];
    std::vsnprintf(body, sizeof(body), fmt, args);
    char line[1100];
    std::snprintf(line, sizeof(line), "[TrailerControl] %s", body);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (scs_log_t logger = g_logger.load()) {
        logger(type, line);
    }
}
} // namespace

void init(scs_log_t game_logger) { g_logger = game_logger; }
void shutdown() { g_logger = nullptr; }

void info(const char *fmt, ...) {
    va_list a; va_start(a, fmt); emit(SCS_LOG_TYPE_message, fmt, a); va_end(a);
}
void warn(const char *fmt, ...) {
    va_list a; va_start(a, fmt); emit(SCS_LOG_TYPE_warning, fmt, a); va_end(a);
}
void error(const char *fmt, ...) {
    va_list a; va_start(a, fmt); emit(SCS_LOG_TYPE_error, fmt, a); va_end(a);
}

} // namespace tc::log
