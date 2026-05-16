/**
 * AlgoForge — src/monitoring/logger.cpp
 * Logging setup. Uses printf fallback if spdlog unavailable.
 */
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cstring>
#include <string>

namespace af {

static bool g_debug = false;

void logger_init(const char *log_dir, const char *level) {
    g_debug = (level && std::string(level) == "debug");
    printf("[Logger] Initialised | dir=%s level=%s\n",
           log_dir ? log_dir : ".", level ? level : "info");
}

void log_info(const char *fmt, ...) {
    time_t t = time(nullptr);
    char ts[20]; struct tm *tm = gmtime(&t);
    if (tm) strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    else    strcpy(ts, "??:??:??");
    printf("[%s][INFO] ", ts);
    va_list args; va_start(args, fmt);
    vprintf(fmt, args); va_end(args);
    printf("\n");
}

void log_warn(const char *fmt, ...) {
    time_t t = time(nullptr);
    char ts[20]; struct tm *tm = gmtime(&t);
    if (tm) strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    else    strcpy(ts, "??:??:??");
    printf("[%s][WARN] ", ts);
    va_list args; va_start(args, fmt);
    vprintf(fmt, args); va_end(args);
    printf("\n");
}

void log_error(const char *fmt, ...) {
    time_t t = time(nullptr);
    char ts[20]; struct tm *tm = gmtime(&t);
    if (tm) strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    else    strcpy(ts, "??:??:??");
    fprintf(stderr, "[%s][ERROR] ", ts);
    va_list args; va_start(args, fmt);
    vfprintf(stderr, fmt, args); va_end(args);
    fprintf(stderr, "\n");
}

void log_debug(const char *fmt, ...) {
    if (!g_debug) return;
    printf("[DEBUG] ");
    va_list args; va_start(args, fmt);
    vprintf(fmt, args); va_end(args);
    printf("\n");
}

} /* namespace af */
