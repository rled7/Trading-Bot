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
    }
    return count;
}
