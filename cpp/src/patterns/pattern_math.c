/**
 * AlgoForge — src/patterns/pattern_math.c
 * Pure-C candlestick and harmonic ratio math.
 */
#include "patterns/pattern_types.h"
#include <math.h>

int af_is_engulfing(const AF_Bar *p, const AF_Bar *curr) {
    if (!p||!curr) return 0;
    if (curr->is_bullish && !p->is_bullish &&
        curr->open < p->close && curr->close > p->open) return 1;
    if (!curr->is_bullish && p->is_bullish &&
        curr->open > p->close && curr->close < p->open) return -1;
    return 0;
}

int af_is_hammer(const AF_Bar *b) {
    if (!b||b->range<AF_EPSILON) return 0;
    double lo_ratio = b->lower_shadow / b->range;
    double hi_ratio = b->upper_shadow  / b->range;
    /* Hammer: long lower shadow, small upper shadow */
    if (lo_ratio >= 0.60 && hi_ratio <= 0.20) return 1;
    /* Shooting star: long upper shadow, small lower shadow */
    if (hi_ratio >= 0.60 && lo_ratio <= 0.20) return -1;
    return 0;
}

int af_is_doji(const AF_Bar *b, double doji_pct) {
    if (!b||b->range<AF_EPSILON) return 0;
    return (b->body_pct <= doji_pct) ? 1 : 0;
}

int af_is_marubozu(const AF_Bar *b, double body_min_pct) {
    if (!b||b->range<AF_EPSILON) return 0;
    if (b->body_pct < body_min_pct) return 0;
    double shadow_max = b->range * 0.05;
    if (b->is_bullish && b->upper_shadow<shadow_max && b->lower_shadow<shadow_max) return 1;
    if (!b->is_bullish && b->upper_shadow<shadow_max && b->lower_shadow<shadow_max) return -1;
    return 0;
}

int af_is_pin_bar(const AF_Bar *b, double min_wick_mult) {
    /* Pin bar: body is small relative to range, one wick is very long */
    if (!b||b->range<AF_EPSILON) return 0;
    /* Use range-based check to handle tiny-body pin bars */
    double effective_body = b->body > b->range*0.03 ? b->body : b->range*0.03;
    double lwm = b->lower_shadow / effective_body;
    double uwm = b->upper_shadow / effective_body;
    double opp_max = b->range * 0.25; /* opposite wick <= 25% of range */
    if (lwm >= min_wick_mult && b->upper_shadow <= opp_max) return 1;
    if (uwm >= min_wick_mult && b->lower_shadow <= opp_max) return -1;
    return 0;
}

int af_fib_ratio(double a, double b, double target, double tol) {
    if (fabs(a) < 1e-12) return 0;
    double ratio = b / a;
    return (fabs(ratio - target) <= tol) ? 1 : 0;
}
