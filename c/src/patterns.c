#include "af_patterns.h"
#include <math.h>
#include <stddef.h>

#define AF_EPSILON 1e-12

static double dabs(double x) { return x < 0 ? -x : x; }

/* Derived candle geometry. */
static void geom(double o, double h, double l, double c,
                 double *body, double *range,
                 double *upper_shadow, double *lower_shadow,
                 int *is_bullish)
{
    *body  = dabs(c - o);
    *range = h - l;
    double top = c > o ? c : o;
    double bot = c < o ? c : o;
    *upper_shadow = h - top;
    *lower_shadow = bot - l;
    *is_bullish = c > o;
}

int af_is_doji(double o, double h, double l, double c, double doji_pct) {
    double body, range, us, ls; int bull;
    geom(o, h, l, c, &body, &range, &us, &ls, &bull);
    if (range < AF_EPSILON) return 0;
    return (body / range <= doji_pct) ? 1 : 0;
}

int af_is_hammer(double o, double h, double l, double c) {
    double body, range, us, ls; int bull;
    geom(o, h, l, c, &body, &range, &us, &ls, &bull);
    if (range < AF_EPSILON) return 0;
    double lo_ratio = ls / range;
    double hi_ratio = us / range;
    if (lo_ratio >= 0.60 && hi_ratio <= 0.20) return  1;   /* hammer */
    if (hi_ratio >= 0.60 && lo_ratio <= 0.20) return -1;   /* shooting star */
    return 0;
}

int af_is_engulfing(double p_o, double p_c, double c_o, double c_c) {
    int prev_bull = p_c > p_o;
    int curr_bull = c_c > c_o;
    if (curr_bull && !prev_bull && c_o < p_c && c_c > p_o) return  1;
    if (!curr_bull && prev_bull && c_o > p_c && c_c < p_o) return -1;
    return 0;
}

int af_is_marubozu(double o, double h, double l, double c, double body_min_pct) {
    double body, range, us, ls; int bull;
    geom(o, h, l, c, &body, &range, &us, &ls, &bull);
    if (range < AF_EPSILON) return 0;
    if (body / range < body_min_pct) return 0;
    double shadow_max = range * 0.05;
    if (bull  && us < shadow_max && ls < shadow_max) return  1;
    if (!bull && us < shadow_max && ls < shadow_max) return -1;
    return 0;
}

int af_is_pin_bar(double o, double h, double l, double c, double min_wick_mult) {
    double body, range, us, ls; int bull;
    geom(o, h, l, c, &body, &range, &us, &ls, &bull);
    if (range < AF_EPSILON) return 0;
    /* Tiny-body fix from cpp/ reference: treat very small bodies as 3% of range. */
    double effective_body = body > range * 0.03 ? body : range * 0.03;
    double lwm = ls / effective_body;
    double uwm = us / effective_body;
    double opp_max = range * 0.25;
    if (lwm >= min_wick_mult && us <= opp_max) return  1;
    if (uwm >= min_wick_mult && ls <= opp_max) return -1;
    return 0;
}

/* ── Morning Star ──────────────────────────────────────────────────────── */
int af_is_morning_star(double b0_o, double b0_h, double b0_l, double b0_c,
                        double b1_o, double b1_h, double b1_l, double b1_c,
                        double b2_o, double b2_h, double b2_l, double b2_c)
{
    (void)b1_h; (void)b1_l; (void)b2_h; (void)b2_l;
    double b0_body  = dabs(b0_c - b0_o);
    double b0_range = b0_h - b0_l;
    double b1_body  = dabs(b1_c - b1_o);
    /* b0: bearish with large body (body > range*0.60) */
    if (b0_range < AF_EPSILON) return 0;
    int b0_bear = b0_c < b0_o;
    if (!b0_bear || b0_body <= b0_range * 0.60) return 0;
    /* b1: small body (body < b0.body * 0.30) */
    if (b1_body >= b0_body * 0.30) return 0;
    /* b2: bullish, close above midpoint of b0 body */
    int b2_bull = b2_c > b2_o;
    double b0_mid = (b0_o + b0_c) / 2.0;
    if (!b2_bull || b2_c <= b0_mid) return 0;
    return 1;
}

/* ── Evening Star ──────────────────────────────────────────────────────── */
int af_is_evening_star(double b0_o, double b0_h, double b0_l, double b0_c,
                        double b1_o, double b1_h, double b1_l, double b1_c,
                        double b2_o, double b2_h, double b2_l, double b2_c)
{
    (void)b1_h; (void)b1_l; (void)b2_h; (void)b2_l;
    double b0_body  = dabs(b0_c - b0_o);
    double b0_range = b0_h - b0_l;
    double b1_body  = dabs(b1_c - b1_o);
    /* b0: bullish with large body (body > range*0.60) */
    if (b0_range < AF_EPSILON) return 0;
    int b0_bull = b0_c > b0_o;
    if (!b0_bull || b0_body <= b0_range * 0.60) return 0;
    /* b1: small body (body < b0.body * 0.30) */
    if (b1_body >= b0_body * 0.30) return 0;
    /* b2: bearish, close below midpoint of b0 body */
    int b2_bear = b2_c < b2_o;
    double b0_mid = (b0_o + b0_c) / 2.0;
    if (!b2_bear || b2_c >= b0_mid) return 0;
    return -1;
}

/* ── Three White Soldiers ──────────────────────────────────────────────── */
/* Matches cpp/ candlestick_patterns.cpp:
   - all three bullish
   - body_pct >= 0.60  (body / range)
   - upper_shadow < body * 0.30
   - each close strictly higher than previous close */
int af_is_three_white_soldiers(double b0_o, double b0_h, double b0_l, double b0_c,
                                double b1_o, double b1_h, double b1_l, double b1_c,
                                double b2_o, double b2_h, double b2_l, double b2_c)
{
    double o[3] = {b0_o, b1_o, b2_o};
    double h[3] = {b0_h, b1_h, b2_h};
    double l[3] = {b0_l, b1_l, b2_l};
    double c[3] = {b0_c, b1_c, b2_c};
    for (int i = 0; i < 3; ++i) {
        if (c[i] <= o[i]) return 0;                       /* must be bullish */
        double body  = c[i] - o[i];
        double range = h[i] - l[i];
        if (range < 1e-12) return 0;
        if (body / range < 0.60) return 0;                /* big body */
        double upper = h[i] - (c[i] > o[i] ? c[i] : o[i]);
        if (upper > body * 0.30) return 0;                /* tiny upper shadow */
    }
    if (b1_c <= b0_c || b2_c <= b1_c) return 0;           /* rising closes */
    return 1;
}

/* Matches cpp/ — mirror of three_white_soldiers. */
int af_is_three_black_crows(double b0_o, double b0_h, double b0_l, double b0_c,
                              double b1_o, double b1_h, double b1_l, double b1_c,
                              double b2_o, double b2_h, double b2_l, double b2_c)
{
    double o[3] = {b0_o, b1_o, b2_o};
    double h[3] = {b0_h, b1_h, b2_h};
    double l[3] = {b0_l, b1_l, b2_l};
    double c[3] = {b0_c, b1_c, b2_c};
    for (int i = 0; i < 3; ++i) {
        if (c[i] >= o[i]) return 0;                       /* must be bearish */
        double body  = o[i] - c[i];
        double range = h[i] - l[i];
        if (range < 1e-12) return 0;
        if (body / range < 0.60) return 0;
        double lower = (c[i] < o[i] ? c[i] : o[i]) - l[i];
        if (lower > body * 0.30) return 0;                /* tiny lower shadow */
    }
    if (b1_c >= b0_c || b2_c >= b1_c) return 0;           /* falling closes */
    return -1;
}

static int emit(af_pattern_match_t *out, size_t cap, size_t i, int idx,
                const char *name, int sig)
{
    if (i < cap) {
        out[i].bar_index = idx;
        out[i].name      = name;
        out[i].signal    = sig;
    }
    return sig != 0;
}

size_t af_scan_patterns(const af_bar_t *bars, size_t n,
                         af_pattern_match_t *out, size_t out_cap)
{
    if (!bars || n == 0) return 0;
    size_t count = 0;
    int s;
    for (size_t i = 0; i < n; ++i) {
        double o = bars[i].open, h = bars[i].high, l = bars[i].low, c = bars[i].close;

        if ((s = af_is_doji(o, h, l, c, 0.10)))               count += emit(out, out_cap, count, (int)i, "doji",     s);
        if ((s = af_is_hammer(o, h, l, c)))                   count += emit(out, out_cap, count, (int)i, "hammer",   s);
        if ((s = af_is_marubozu(o, h, l, c, 0.90)))           count += emit(out, out_cap, count, (int)i, "marubozu", s);
        if ((s = af_is_pin_bar(o, h, l, c, 2.0)))             count += emit(out, out_cap, count, (int)i, "pin_bar",  s);
        if (i > 0) {
            double po = bars[i - 1].open, pc = bars[i - 1].close;
            if ((s = af_is_engulfing(po, pc, o, c)))          count += emit(out, out_cap, count, (int)i, "engulfing",s);
        }
        if (i >= 2) {
            double b0o = bars[i-2].open, b0h = bars[i-2].high, b0l = bars[i-2].low, b0c = bars[i-2].close;
            double b1o = bars[i-1].open, b1h = bars[i-1].high, b1l = bars[i-1].low, b1c = bars[i-1].close;
            if ((s = af_is_morning_star(b0o,b0h,b0l,b0c, b1o,b1h,b1l,b1c, o,h,l,c)))
                count += emit(out, out_cap, count, (int)i, "morning_star", s);
            if ((s = af_is_evening_star(b0o,b0h,b0l,b0c, b1o,b1h,b1l,b1c, o,h,l,c)))
                count += emit(out, out_cap, count, (int)i, "evening_star", s);
            if ((s = af_is_three_white_soldiers(b0o,b0h,b0l,b0c, b1o,b1h,b1l,b1c, o,h,l,c)))
                count += emit(out, out_cap, count, (int)i, "three_white_soldiers", s);
            if ((s = af_is_three_black_crows(b0o,b0h,b0l,b0c, b1o,b1h,b1l,b1c, o,h,l,c)))
                count += emit(out, out_cap, count, (int)i, "three_black_crows", s);
        }
    }
    return count;
}
