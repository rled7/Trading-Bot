/**
 * AlgoForge — include/patterns/pattern_types.h
 * Pattern detection result types. Pure C.
 */
#ifndef AF_PATTERN_TYPES_H
#define AF_PATTERN_TYPES_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AF_PAT_CANDLESTICK = 0,
    AF_PAT_CHART,
    AF_PAT_HARMONIC,
    AF_PAT_ELLIOTT,
    AF_PAT_WYCKOFF,
    AF_PAT_VOLUME,
} AF_PatternCategory;

typedef enum {
    AF_PAT_DIR_BULLISH  =  1,
    AF_PAT_DIR_BEARISH  = -1,
    AF_PAT_DIR_NEUTRAL  =  0,
} AF_PatternDirection;

typedef struct {
    char                  name[64];
    AF_PatternCategory    category;
    AF_PatternDirection   direction;
    double                confidence;     /* 0.0 – 1.0 */
    int                   bar_index;      /* bar where pattern completes */
} AF_PatternMatch;

#define AF_MAX_PATTERN_MATCHES 64

typedef struct {
    AF_PatternMatch matches[AF_MAX_PATTERN_MATCHES];
    int             count;
    int             bullish_count;
    int             bearish_count;
} AF_PatternResult;

static inline int af_pattern_net(const AF_PatternResult *r) {
    return r->bullish_count - r->bearish_count;
}

/* ── C math helpers used by pattern detection ── */

/** Returns +1 if engulfing bullish, -1 if bearish, 0 otherwise */
int af_is_engulfing(const AF_Bar *prev, const AF_Bar *curr);

/** Returns +1 if hammer, -1 if shooting star, 0 otherwise */
int af_is_hammer(const AF_Bar *bar);

/** Returns +1 if doji, 0 otherwise */
int af_is_doji(const AF_Bar *bar, double doji_pct);

/** Returns +1 if marubozu bull, -1 bear, 0 otherwise */
int af_is_marubozu(const AF_Bar *bar, double body_min_pct);

/** Pin bar: long wick ≥ min_wick_mult × body, body at extreme */
int af_is_pin_bar(const AF_Bar *bar, double min_wick_mult);

/** Fibonacci ratio check within tolerance */
int af_fib_ratio(double a, double b, double target, double tol);

#ifdef __cplusplus
}
#endif
#endif /* AF_PATTERN_TYPES_H */
