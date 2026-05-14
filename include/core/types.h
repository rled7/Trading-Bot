/**
 * AlgoForge — include/core/types.h
 * Fundamental types. Pure C. Included by both C and C++ modules.
 */
#ifndef AF_CORE_TYPES_H
#define AF_CORE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ── */
#define AF_VERSION "1.0.0"

/* ── Limits ── */
#define AF_MAX_SYMBOL_LEN  12
#define AF_MAX_BARS        8192
#define AF_EPSILON         1e-10
#define AF_NaN             (0.0/0.0)
#define AF_IS_NAN(x)       ((x) != (x))
#define AF_VALID(x)        (!AF_IS_NAN(x) && (x) >= 0.0)

/* ── Trading mode ── */
typedef enum { AF_MODE_PAPER = 0, AF_MODE_LIVE = 1 } AF_TradingMode;

/* ── Direction ── */
typedef enum { AF_DIR_NONE = 0, AF_DIR_LONG = 1, AF_DIR_SHORT = -1 } AF_Direction;

/* ── Order type ── */
typedef enum {
    AF_ORDER_BUY = 0, AF_ORDER_SELL,
    AF_ORDER_BUY_LIMIT, AF_ORDER_SELL_LIMIT,
    AF_ORDER_BUY_STOP,  AF_ORDER_SELL_STOP
} AF_OrderType;

/* ── Timeframe (seconds per bar) ── */
typedef enum {
    AF_TF_S15 = 15,    AF_TF_M1  = 60,    AF_TF_M5  = 300,
    AF_TF_M15 = 900,   AF_TF_M30 = 1800,  AF_TF_H1  = 3600,
    AF_TF_H4  = 14400, AF_TF_D1  = 86400, AF_TF_W1  = 604800
} AF_Timeframe;

/* ── Error codes ── */
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
} AF_Error;

/* ── OHLCV Bar ── */
typedef struct {
    int64_t  timestamp;
    double   open, high, low, close, volume, spread;
    /* Derived (filled by af_bar_init) */
    double   body, range, body_pct, upper_shadow, lower_shadow;
    bool     is_bullish;
} AF_Bar;

static inline void af_bar_init(AF_Bar *b) {
    b->body         = fabs(b->close - b->open);
    b->range        = b->high - b->low;
    b->body_pct     = (b->range > AF_EPSILON) ? b->body / b->range : 0.0;
    b->upper_shadow = b->high - (b->close > b->open ? b->close : b->open);
    b->lower_shadow = (b->close < b->open ? b->close : b->open) - b->low;
    b->is_bullish   = b->close >= b->open;
}

/* ── Tick ── */
typedef struct {
    int64_t  timestamp;
    double   bid, ask, volume;
    char     symbol[AF_MAX_SYMBOL_LEN];
} AF_Tick;

#define AF_SPREAD(t)  ((t).ask - (t).bid)
#define AF_MID(t)     (((t).bid + (t).ask) * 0.5)

/* ── Position ── */
typedef struct {
    uint64_t     ticket;
    char         symbol[AF_MAX_SYMBOL_LEN];
    AF_Direction side;
    double       lots, open_price, current_price;
    double       sl, tp, profit, commission, swap;
    int64_t      open_time;
    uint32_t     magic;
    char         comment[64];
} AF_Position;

#define AF_POS_NET(p)  ((p).profit + (p).commission + (p).swap)

/* ── Order ── */
typedef struct {
    uint64_t     ticket;
    char         symbol[AF_MAX_SYMBOL_LEN];
    AF_OrderType type;
    double       lots, price, sl, tp, fill_price;
    int64_t      open_time, fill_time;
    uint32_t     magic;
    char         comment[64];
} AF_Order;

/* ── Account ── */
typedef struct {
    double   balance, equity, margin, free_margin, profit;
    uint32_t leverage;
    char     currency[8];
    uint64_t login;
} AF_AccountInfo;

/* ── Symbol spec ── */
typedef struct {
    char   name[AF_MAX_SYMBOL_LEN];
    int    digits;
    double point, contract_size, volume_min, volume_max, volume_step;
} AF_SymbolInfo;

/* ── Flat bar array (used by backtest + indicator engine) ── */
typedef struct {
    AF_Bar   *bars;      /* caller-allocated array */
    size_t    count;
    char      symbol[AF_MAX_SYMBOL_LEN];
    AF_Timeframe tf;
} AF_BarArray;

static inline const AF_Bar* af_bar_at(const AF_BarArray *a, size_t rev_idx) {
    if (!a || rev_idx >= a->count) return NULL;
    return &a->bars[a->count - 1 - rev_idx];
}

#ifdef __cplusplus
}
#endif
#endif /* AF_CORE_TYPES_H */
