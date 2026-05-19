#ifndef AF_LOGGER_H
#define AF_LOGGER_H

typedef enum {
    AF_LOG_DEBUG = 0,
    AF_LOG_INFO  = 1,
    AF_LOG_WARN  = 2,
    AF_LOG_ERROR = 3,
} af_log_level_t;

void af_logger_init(af_log_level_t min_level, const char *component);
void af_log(af_log_level_t level, const char *fmt, ...);
void af_log_info(const char *fmt, ...);
void af_log_warn(const char *fmt, ...);
void af_log_error(const char *fmt, ...);
void af_log_debug(const char *fmt, ...);

#define AF_LOG_INFO(...)  af_log_info(__VA_ARGS__)
#define AF_LOG_WARN(...)  af_log_warn(__VA_ARGS__)
#define AF_LOG_ERROR(...) af_log_error(__VA_ARGS__)
#define AF_LOG_DEBUG(...) af_log_debug(__VA_ARGS__)

#endif /* AF_LOGGER_H */
