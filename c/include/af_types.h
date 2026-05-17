#ifndef AF_TYPES_H
#define AF_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define AF_MAX_SYMBOL_LEN 16

typedef enum {
    AF_OK = 0,
    AF_ERR_NOT_CONNECTED      = -1,
    AF_ERR_INVALID_SYMBOL     = -3,
    AF_ERR_INVALID_PARAM      = -10,
    AF_ERR_IO                 = -12
} af_error_t;

typedef enum {
    AF_TF_S15 = 15,
    AF_TF_M1  = 60,
    AF_TF_M5  = 300,
    AF_TF_M15 = 900,
    AF_TF_M30 = 1800,
    AF_TF_H1  = 3600,
    AF_TF_H4  = 14400,
    AF_TF_D1  = 86400,
    AF_TF_W1  = 604800
} af_timeframe_t;

typedef enum {
    AF_DIR_NONE  = 0,
    AF_DIR_LONG  = 1,
    AF_DIR_SHORT = -1
} af_direction_t;

typedef struct {
    int64_t timestamp;
    double  open, high, low, close, volume, spread;
} af_bar_t;

#endif /* AF_TYPES_H */
