#include "../include/af_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

static af_log_level_t g_min_level  = AF_LOG_INFO;
static char           g_component[64] = "main";

void af_logger_init(af_log_level_t min_level, const char *component)
{
    g_min_level = min_level;
    if (component && component[0]) {
        strncpy(g_component, component, sizeof(g_component) - 1);
        g_component[sizeof(g_component) - 1] = '\0';
    }
}

void af_log(af_log_level_t level, const char *fmt, ...)
{
    if (level < g_min_level) return;

    /* Timestamp */
    time_t now = time(NULL);
    struct tm utc;
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &utc);

    /* Level string */
    const char *level_str;
    switch (level) {
        case AF_LOG_DEBUG: level_str = "DEBUG"; break;
        case AF_LOG_INFO:  level_str = "INFO";  break;
        case AF_LOG_WARN:  level_str = "WARN";  break;
        case AF_LOG_ERROR: level_str = "ERROR"; break;
        default:           level_str = "INFO";  break;
    }

    /* Format message */
    va_list ap;
    va_start(ap, fmt);
    char raw[1024];
    vsnprintf(raw, sizeof(raw), fmt, ap);
    va_end(ap);

    /* Escape backslashes and double-quotes in msg */
    char msg[2048];
    size_t ri = 0, wi = 0;
    while (raw[ri] && wi < sizeof(msg) - 2) {
        if (raw[ri] == '\\' || raw[ri] == '"') {
            msg[wi++] = '\\';
        }
        msg[wi++] = raw[ri++];
    }
    msg[wi] = '\0';

    /* Output — errors go to stderr, everything else to stdout */
    FILE *out = (level == AF_LOG_ERROR) ? stderr : stdout;
    fprintf(out,
        "{\"ts\":\"%s\",\"level\":\"%s\",\"component\":\"%s\",\"msg\":\"%s\"}\n",
        ts, level_str, g_component, msg);
    fflush(out);
}

void af_log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    af_log(AF_LOG_INFO, "%s", buf);
}

void af_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    af_log(AF_LOG_WARN, "%s", buf);
}

void af_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    af_log(AF_LOG_ERROR, "%s", buf);
}

void af_log_debug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    af_log(AF_LOG_DEBUG, "%s", buf);
}
