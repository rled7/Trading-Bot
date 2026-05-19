#ifndef AF_TYPES_H
#define AF_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define AF_MAX_SYMBOL_LEN 16

typedef enum {
    AF_OK = 0,
    AF_ERR_NOT_CONNECTED      = -1,
    AF_ERR_AUTH_FAILED        = -2,
    AF_ERR_INVALID_SYMBOL     = -3,
    AF_ERR_INVALID_LOTS       = -4,
    AF_ERR_ORDER_REJECTED     = -5,
    AF_ERR_POSITION_NOT_FOUND = -6,
    AF_ERR_INSUFFICIENT_DATA  = -7,
    AF_ERR_DRAWDOWN_LIMIT     = -8,
    AF_ERR_MAX_POSITIONS      = -9,
    AF_ERR_INVALID_PARAM      = -10,
    AF_ERR_OUT_OF_MEMORY      = -11,
    AF_ERR_IO                 = -12,
    AF_ERR_UNKNOWN            = -99
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

typedef enum {
    AF_ORDER_BUY = 0, AF_ORDER_SELL,
    AF_ORDER_BUY_LIMIT, AF_ORDER_SELL_LIMIT,
    AF_ORDER_BUY_STOP,  AF_ORDER_SELL_STOP
} af_order_type_t;

typedef struct {
    int64_t timestamp;
    double  open, high, low, close, volume, spread;
} af_bar_t;

typedef struct {
    int64_t  timestamp;
    double   bid, ask, volume;
    char     symbol[AF_MAX_SYMBOL_LEN];
} af_tick_t;

typedef struct {
    uint64_t       ticket;
    char           symbol[AF_MAX_SYMBOL_LEN];
    af_direction_t side;
    double         lots, open_price, current_price;
    double         sl, tp, profit, commission, swap;
    int64_t        open_time;
    uint32_t       magic;
    char           comment[64];
} af_position_t;

typedef struct {
    uint64_t       ticket;
    char           symbol[AF_MAX_SYMBOL_LEN];
    af_order_type_t type;
    double         lots, price, sl, tp, fill_price;
    int64_t        open_time;
    uint32_t       magic;
    char           comment[64];
} af_order_t;

typedef struct {
    double   balance, equity, margin, free_margin, profit;
    uint32_t leverage;
    char     currency[8];
    uint64_t login;
} af_account_info_t;

typedef struct {
    char   name[AF_MAX_SYMBOL_LEN];
    int    digits;
    double point, contract_size, volume_min, volume_max, volume_step;
} af_symbol_info_t;

#endif /* AF_TYPES_H */
