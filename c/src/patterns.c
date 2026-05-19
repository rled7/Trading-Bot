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

/* ── Chart pattern helpers ─────────────────────────────────────────────── */
static double max_range(const af_bar_t *b, size_t from, size_t to) {
    double mx = b[from].high;
    for (size_t i = from + 1; i <= to; ++i)
        if (b[i].high > mx) mx = b[i].high;
    return mx;
}

static double min_range(const af_bar_t *b, size_t from, size_t to) {
    double mn = b[from].low;
    for (size_t i = from + 1; i <= to; ++i)
        if (b[i].low < mn) mn = b[i].low;
    return mn;
}

/* ── Double Bottom ─────────────────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp DoubleBottom logic exactly. */
int af_is_double_bottom(const af_bar_t *bars, size_t n) {
    if (n < 30) return 0;
    size_t mid = n / 2;
    double lo1 = min_range(bars, 0,   mid - 1);
    double hi  = max_range(bars, 0,   n - 1);
    double lo2 = min_range(bars, mid, n - 1);
    double tol = (lo1 + lo2) * 0.005;
    double diff = lo1 - lo2;
    if (diff < 0.0) diff = -diff;
    if (diff <= tol && hi > lo1 * 1.01 && bars[n - 1].close > (lo1 + hi) / 2.0)
        return 1;
    return 0;
}

/* ── Double Top ────────────────────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp DoubleTop logic exactly. */
int af_is_double_top(const af_bar_t *bars, size_t n) {
    if (n < 30) return 0;
    size_t mid = n / 2;
    double hi1 = max_range(bars, 0,   mid - 1);
    double lo  = min_range(bars, 0,   n - 1);
    double hi2 = max_range(bars, mid, n - 1);
    double tol = (hi1 + hi2) * 0.005;
    double diff = hi1 - hi2;
    if (diff < 0.0) diff = -diff;
    if (diff <= tol && lo < hi1 * 0.99 && bars[n - 1].close < (hi1 + lo) / 2.0)
        return -1;
    return 0;
}

/* ── Ascending Triangle ────────────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp AscendingTriangle logic exactly. */
int af_is_ascending_triangle(const af_bar_t *bars, size_t n) {
    if (n < 20) return 0;
    double resistance = max_range(bars, n - 20, n - 1);
    double lo_first   = min_range(bars, n - 20, n - 11);
    double lo_last    = min_range(bars, n - 10, n - 1);
    double hi_var_raw = max_range(bars, n - 20, n - 11) - max_range(bars, n - 10, n - 1);
    double hi_var = hi_var_raw < 0.0 ? -hi_var_raw : hi_var_raw;
    if (lo_last > lo_first * 1.002 && hi_var < resistance * 0.005)
        return 1;
    return 0;
}

/* ── Descending Triangle ───────────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp DescendingTriangle logic exactly. */
int af_is_descending_triangle(const af_bar_t *bars, size_t n) {
    if (n < 20) return 0;
    double support   = min_range(bars, n - 20, n - 1);
    double hi_first  = max_range(bars, n - 20, n - 11);
    double hi_last   = max_range(bars, n - 10, n - 1);
    double lo_var_raw = min_range(bars, n - 20, n - 11) - min_range(bars, n - 10, n - 1);
    double lo_var = lo_var_raw < 0.0 ? -lo_var_raw : lo_var_raw;
    if (hi_last < hi_first * 0.998 && lo_var < support * 0.005)
        return -1;
    return 0;
}

/* ── Head and Shoulders ────────────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp HeadAndShoulders logic exactly (bearish). */
int af_is_head_and_shoulders(const af_bar_t *bars, size_t n) {
    if (n < 40) return 0;
    size_t s = n - 40;
    double ls = max_range(bars, s,      s + 12);
    double h  = max_range(bars, s + 13, s + 26);
    double rs = max_range(bars, s + 27, n - 1);
    double nl_l = min_range(bars, s,      s + 12);
    double nl_r = min_range(bars, s + 27, n - 1);
    double neckline = nl_l < nl_r ? nl_l : nl_r;  /* std::min */
    double sym = (ls + rs > 1e-12) ? (ls - rs) / (ls + rs) * 2.0 : 0.0;
    if (sym < 0.0) sym = -sym;
    if (h > ls * 1.01 && h > rs * 1.01 && sym < 0.07 &&
        bars[n - 1].close < neckline)
        return -1;
    return 0;
}

/* ── Inverse Head and Shoulders ────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp InverseHeadAndShoulders logic exactly (bullish). */
int af_is_inverse_head_and_shoulders(const af_bar_t *bars, size_t n) {
    if (n < 40) return 0;
    size_t s = n - 40;
    double ls = min_range(bars, s,      s + 12);
    double h  = min_range(bars, s + 13, s + 26);
    double rs = min_range(bars, s + 27, n - 1);
    double nl_l = max_range(bars, s,      s + 12);
    double nl_r = max_range(bars, s + 27, n - 1);
    double neckline = nl_l > nl_r ? nl_l : nl_r;  /* std::max */
    double sym = (ls + rs > 1e-12) ? (ls - rs) / (ls + rs) * 2.0 : 0.0;
    if (sym < 0.0) sym = -sym;
    if (h < ls * 0.99 && h < rs * 0.99 && sym < 0.07 &&
        bars[n - 1].close > neckline)
        return 1;
    return 0;
}

/* ── Bullish Flag ───────────────────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp BullishFlag logic exactly. */
int af_is_bullish_flag(const af_bar_t *bars, size_t n) {
    if (n < 20) return 0;
    double pole_lo  = min_range(bars, n - 20, n - 11);
    double pole_hi  = max_range(bars, n - 20, n - 11);
    double pole_rng = pole_hi - pole_lo;
    double flag_hi  = max_range(bars, n - 10, n - 1);
    double flag_lo  = min_range(bars, n - 10, n - 1);
    double flag_rng = flag_hi - flag_lo;
    int pole_bull   = bars[n - 11].close > bars[n - 20].close;
    int tight_flag  = flag_rng < pole_rng * 0.45;
    if (pole_bull && tight_flag && pole_rng > pole_lo * 0.01)
        return 1;
    return 0;
}

/* ── Bearish Flag ───────────────────────────────────────────────────────── */
/* Matches cpp/ chart_patterns.cpp BearishFlag logic exactly. */
int af_is_bearish_flag(const af_bar_t *bars, size_t n) {
    if (n < 20) return 0;
    double pole_lo  = min_range(bars, n - 20, n - 11);
    double pole_hi  = max_range(bars, n - 20, n - 11);
    double pole_rng = pole_hi - pole_lo;
    double flag_rng = max_range(bars, n - 10, n - 1) - min_range(bars, n - 10, n - 1);
    int pole_bear   = bars[n - 11].close < bars[n - 20].close;
    int tight_flag  = flag_rng < pole_rng * 0.45;
    if (pole_bear && tight_flag && pole_rng > pole_lo * 0.01)
        return -1;
    return 0;
}

int af_fib_ratio(double a, double b, double target, double tol) {
    if (dabs(a) < 1e-12) return 0;
    return (dabs(b / a - target) <= tol) ? 1 : 0;
}

int af_is_gartley_bull(const af_bar_t *bars, size_t n) {
    if (n < 50) return 0;
    double x  = min_range(bars, n - 50, n - 40);
    double a  = max_range(bars, n - 40, n - 30);
    double bb = min_range(bars, n - 30, n - 20);
    double cc = max_range(bars, n - 20, n - 10);
    double d  = min_range(bars, n - 10, n - 1);
    double xa = a - x, ab = a - bb, bc = cc - bb, cd = cc - d;
    if (xa > 1e-6 && bc > 1e-6 &&
        af_fib_ratio(xa, ab, 0.618, 0.05) &&
        (af_fib_ratio(bc, cd, 1.272, 0.07) || af_fib_ratio(bc, cd, 1.618, 0.07)))
        return 1;
    return 0;
}

int af_is_gartley_bear(const af_bar_t *bars, size_t n) {
    if (n < 50) return 0;
    double x  = max_range(bars, n - 50, n - 40);
    double a  = min_range(bars, n - 40, n - 30);
    double bb = max_range(bars, n - 30, n - 20);
    double cc = min_range(bars, n - 20, n - 10);
    double d  = max_range(bars, n - 10, n - 1);
    double xa = x - a, ab = bb - a, bc = bb - cc, cd = d - cc;
    if (xa > 1e-6 && bc > 1e-6 &&
        af_fib_ratio(xa, ab, 0.618, 0.05) &&
        (af_fib_ratio(bc, cd, 1.272, 0.07) || af_fib_ratio(bc, cd, 1.618, 0.07)))
        return -1;
    return 0;
}

int af_is_bat_bull(const af_bar_t *bars, size_t n) {
    if (n < 50) return 0;
    double x  = min_range(bars, n - 50, n - 40);
    double a  = max_range(bars, n - 40, n - 30);
    double d  = min_range(bars, n - 10, n - 1);
    double xa = a - x, xd = a - d;
    if (xa > 1e-6 && af_fib_ratio(xa, xd, 0.886, 0.05)) return 1;
    return 0;
}

int af_is_butterfly_bull(const af_bar_t *bars, size_t n) {
    if (n < 50) return 0;
    double x  = min_range(bars, n - 50, n - 40);
    double a  = max_range(bars, n - 40, n - 30);
    double d  = min_range(bars, n - 10, n - 1);
    double xa = a - x, xd = a - d;
    if (xa > 1e-6 && af_fib_ratio(xa, xd, 1.272, 0.07)) return 1;
    return 0;
}

int af_is_crab_bull(const af_bar_t *bars, size_t n) {
    if (n < 50) return 0;
    double x  = min_range(bars, n - 50, n - 40);
    double a  = max_range(bars, n - 40, n - 30);
    double d  = min_range(bars, n - 10, n - 1);
    double xa = a - x, xd = a - d;
    if (xa > 1e-6 && af_fib_ratio(xa, xd, 1.618, 0.07)) return 1;
    return 0;
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
    /* Chart patterns — single-shot scan at the latest bar only. Calling
       them at every bar made scan O(n²); they're "what's the latest
       structural pattern" detectors, so once per scan is correct. */
    if (n >= 20) {
        int s;
        if ((s = af_is_ascending_triangle(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "ascending_triangle", s);
        if ((s = af_is_descending_triangle(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "descending_triangle", s);
        if ((s = af_is_bullish_flag(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "bullish_flag", s);
        if ((s = af_is_bearish_flag(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "bearish_flag", s);
    }
    if (n >= 30) {
        int s;
        if ((s = af_is_double_top(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "double_top", s);
        if ((s = af_is_double_bottom(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "double_bottom", s);
    }
    if (n >= 40) {
        int s;
        if ((s = af_is_head_and_shoulders(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "head_and_shoulders", s);
        if ((s = af_is_inverse_head_and_shoulders(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "inverse_head_and_shoulders", s);
    }
    if (n >= 50) {
        int s;
        if ((s = af_is_gartley_bull(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "gartley_bull", s);
        if ((s = af_is_gartley_bear(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "gartley_bear", s);
        if ((s = af_is_bat_bull(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "bat_bull", s);
        if ((s = af_is_butterfly_bull(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "butterfly_bull", s);
        if ((s = af_is_crab_bull(bars, n)))
            count += emit(out, out_cap, count, (int)(n - 1), "crab_bull", s);
    }
    return count;
}
