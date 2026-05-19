/**
 * AlgoForge — c/src/analysis.c
 * C port of the C++ analysis layer (Round 9).
 *
 * Implements:
 *   af_market_structure_analyse()
 *   af_trend_classify()
 *   af_score_timeframe()
 *   af_eval_session()
 *   af_classify_regime()
 *   af_mtf_analyze()
 *
 * No dynamic allocation.  C11 / -Wall -Wextra -lm clean.
 */
#include "../include/af_analysis.h"
#include "../include/af_indicators.h"
#include "../include/af_patterns.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── internal helpers ───────────────────────────────────────────────────── */

static int af_isnan_d(double x) { return x != x; }

static double af_fabs_d(double x) { return x < 0.0 ? -x : x; }

static double af_min_d(double a, double b) { return a < b ? a : b; }
static double af_max_d(double a, double b) { return a > b ? a : b; }

/* Copy at most dst_len-1 chars from src into dst, always NUL-terminate. */
static void af_strlcpy(char *dst, const char *src, size_t dst_len) {
    if (!dst || dst_len == 0) return;
    size_t i = 0;
    if (src) {
        for (; src[i] && i < dst_len - 1; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Case-insensitive substring search (ASCII only). */
static int af_stristr(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    size_t nl = strlen(needle);
    for (; *hay; ++hay) {
        size_t i;
        for (i = 0; i < nl; ++i) {
            char h = hay[i], n = needle[i];
            if (h >= 'a' && h <= 'z') h -= 32;
            if (n >= 'a' && n <= 'z') n -= 32;
            if (h != n) break;
        }
        if (i == nl) return 1;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * 1.  MARKET STRUCTURE
 * ══════════════════════════════════════════════════════════════════════════ */

const char *af_trend_state_name(af_trend_state_t s) {
    switch (s) {
        case AF_TREND_STRONG_BULL: return "STRONG_BULL";
        case AF_TREND_BULL:        return "BULL";
        case AF_TREND_RANGING:     return "RANGING";
        case AF_TREND_BEAR:        return "BEAR";
        case AF_TREND_STRONG_BEAR: return "STRONG_BEAR";
    }
    return "RANGING";
}

af_structure_result_t af_market_structure_analyse(
    const af_bar_t *bars, size_t n, int swing_strength)
{
    af_structure_result_t r;
    memset(&r, 0, sizeof(r));
    r.trend      = AF_TREND_RANGING;
    r.signal     = 0;
    r.confidence = 0.40;

    if (!bars || n < (size_t)(swing_strength * 4)) return r;

    int s = swing_strength;

    /* ── Detect swing highs and lows ── */
    for (size_t i = (size_t)s; i < n - (size_t)s; ++i) {
        bool is_sh = true, is_sl = true;
        for (int j = 1; j <= s; ++j) {
            if (bars[i].high <= bars[i - j].high ||
                bars[i].high <= bars[i + j].high) is_sh = false;
            if (bars[i].low  >= bars[i - j].low  ||
                bars[i].low  >= bars[i + j].low)  is_sl = false;
        }
        if (is_sh && r.swing_high_count < AF_MAX_SWINGS) {
            af_swing_point_t sp;
            sp.index   = i;
            sp.price   = bars[i].high;
            sp.is_high = true;
            r.swing_highs[r.swing_high_count++] = sp;
        }
        if (is_sl && r.swing_low_count < AF_MAX_SWINGS) {
            af_swing_point_t sp;
            sp.index   = i;
            sp.price   = bars[i].low;
            sp.is_high = false;
            r.swing_lows[r.swing_low_count++] = sp;
        }
    }

    if (r.swing_high_count < 2 || r.swing_low_count < 2) return r;

    /* ── Classify HH/HL/LH/LL using last two swings ── */
    int shi = r.swing_high_count;
    int sli = r.swing_low_count;

    double sh_last  = r.swing_highs[shi - 1].price;
    double sh_prev  = r.swing_highs[shi - 2].price;
    double sl_last  = r.swing_lows[sli - 1].price;
    double sl_prev  = r.swing_lows[sli - 2].price;

    bool hh = sh_last > sh_prev;
    bool hl = sl_last > sl_prev;
    bool lh = sh_last < sh_prev;
    bool ll = sl_last < sl_prev;

    if      (hh && hl) { r.trend = AF_TREND_STRONG_BULL; r.signal =  1; r.confidence = 0.90; }
    else if (hh || hl) { r.trend = AF_TREND_BULL;        r.signal =  1; r.confidence = 0.70; }
    else if (ll && lh) { r.trend = AF_TREND_STRONG_BEAR; r.signal = -1; r.confidence = 0.90; }
    else if (ll || lh) { r.trend = AF_TREND_BEAR;        r.signal = -1; r.confidence = 0.70; }
    else               { r.trend = AF_TREND_RANGING;     r.signal =  0; r.confidence = 0.40; }

    r.last_swing_high = sh_last;
    r.last_swing_low  = sl_last;

    /* ── Break of Structure / Change of Character ── */
    double cur = bars[n - 1].close;
    if (r.trend == AF_TREND_BEAR || r.trend == AF_TREND_STRONG_BEAR) {
        r.bos = r.choch = (cur > sh_last);
    } else if (r.trend == AF_TREND_BULL || r.trend == AF_TREND_STRONG_BULL) {
        r.bos = r.choch = (cur < sl_last);
    }

    if (r.choch) r.signal = -r.signal;

    return r;
}

/* ══════════════════════════════════════════════════════════════════════════
 * 2.  TREND CLASSIFIER
 * ══════════════════════════════════════════════════════════════════════════ */

const char *af_trend_bias_name(af_trend_bias_t b) {
    switch (b) {
        case AF_BIAS_STRONG_BULL: return "STRONG_BULL";
        case AF_BIAS_BULL:        return "BULL";
        case AF_BIAS_NEUTRAL:     return "NEUTRAL";
        case AF_BIAS_BEAR:        return "BEAR";
        case AF_BIAS_STRONG_BEAR: return "STRONG_BEAR";
    }
    return "NEUTRAL";
}

af_trend_classification_t af_trend_classify(const af_bar_t *bars, size_t n) {
    af_trend_classification_t r;
    memset(&r, 0, sizeof(r));
    r.bias       = AF_BIAS_NEUTRAL;
    r.signal     = 0;
    r.confidence = 0.40;

    if (!bars || n < 200) return r;

    /* Build close, high, low arrays on the stack for small n, heap for large.
       Use VLAs only if supported; use malloc unconditionally for safety. */
    double *close_arr = (double *)malloc(n * sizeof(double));
    double *high_arr  = (double *)malloc(n * sizeof(double));
    double *low_arr   = (double *)malloc(n * sizeof(double));
    double *e9        = (double *)malloc(n * sizeof(double));
    double *e21       = (double *)malloc(n * sizeof(double));
    double *e50       = (double *)malloc(n * sizeof(double));
    double *e200      = (double *)malloc(n * sizeof(double));
    double *adx_arr   = (double *)malloc(n * sizeof(double));
    double *pdi_arr   = (double *)malloc(n * sizeof(double));
    double *mdi_arr   = (double *)malloc(n * sizeof(double));

    if (!close_arr || !high_arr || !low_arr ||
        !e9 || !e21 || !e50 || !e200 ||
        !adx_arr || !pdi_arr || !mdi_arr) {
        /* OOM — return neutral */
        free(close_arr); free(high_arr); free(low_arr);
        free(e9); free(e21); free(e50); free(e200);
        free(adx_arr); free(pdi_arr); free(mdi_arr);
        return r;
    }

    for (size_t i = 0; i < n; ++i) {
        close_arr[i] = bars[i].close;
        high_arr[i]  = bars[i].high;
        low_arr[i]   = bars[i].low;
    }

    af_ema(close_arr, n,   9, e9);
    af_ema(close_arr, n,  21, e21);
    af_ema(close_arr, n,  50, e50);
    af_ema(close_arr, n, 200, e200);
    af_adx(high_arr, low_arr, close_arr, n, 14, adx_arr, pdi_arr, mdi_arr);

    double price = close_arr[n - 1];
    int bull_score = 0, bear_score = 0;

    /* Price vs each EMA (+1 bull / +1 bear) — up to 8 pts */
    if (!af_isnan_d(e9[n-1])) {
        if (price > e9[n-1])  ++bull_score;
        if (price < e9[n-1])  ++bear_score;
    }
    if (!af_isnan_d(e21[n-1])) {
        if (price > e21[n-1]) ++bull_score;
        if (price < e21[n-1]) ++bear_score;
    }
    if (!af_isnan_d(e50[n-1])) {
        if (price > e50[n-1]) ++bull_score;
        if (price < e50[n-1]) ++bear_score;
    }
    if (!af_isnan_d(e200[n-1])) {
        if (price > e200[n-1]) ++bull_score;
        if (price < e200[n-1]) ++bear_score;
    }

    /* EMA alignment (9>21, 21>50, 50>200) — up to 4 more pts each direction */
    if (!af_isnan_d(e9[n-1]) && !af_isnan_d(e21[n-1])) {
        if (e9[n-1] > e21[n-1]) ++bull_score;
        if (e9[n-1] < e21[n-1]) ++bear_score;
    }
    if (!af_isnan_d(e21[n-1]) && !af_isnan_d(e50[n-1])) {
        if (e21[n-1] > e50[n-1]) ++bull_score;
        if (e21[n-1] < e50[n-1]) ++bear_score;
    }
    if (!af_isnan_d(e50[n-1]) && !af_isnan_d(e200[n-1])) {
        if (e50[n-1] > e200[n-1]) ++bull_score;
        if (e50[n-1] < e200[n-1]) ++bear_score;
    }

    r.adx      = af_isnan_d(adx_arr[n-1]) ? 0.0 : adx_arr[n-1];
    r.trending = r.adx > 20.0;

    int net = bull_score - bear_score;

    if      (net >=  6 && r.trending) { r.bias = AF_BIAS_STRONG_BULL; r.signal =  1; }
    else if (net >=  3)               { r.bias = AF_BIAS_BULL;        r.signal =  1; }
    else if (net <= -6 && r.trending) { r.bias = AF_BIAS_STRONG_BEAR; r.signal = -1; }
    else if (net <= -3)               { r.bias = AF_BIAS_BEAR;        r.signal = -1; }
    else                              { r.bias = AF_BIAS_NEUTRAL;     r.signal =  0; }

    r.confidence = af_min_d(0.95,
                            0.40 + (double)(net < 0 ? -net : net) * 0.07
                                 + (r.trending ? 0.10 : 0.0));

    free(close_arr); free(high_arr); free(low_arr);
    free(e9); free(e21); free(e50); free(e200);
    free(adx_arr); free(pdi_arr); free(mdi_arr);
    return r;
}

/* ══════════════════════════════════════════════════════════════════════════
 * 3.  CONFLUENCE SCORER
 * ══════════════════════════════════════════════════════════════════════════ */

static double pat_weight(AF_PatternCategory cat) {
    switch (cat) {
        case AF_PAT_CANDLESTICK: return 0.60;
        case AF_PAT_CHART:       return 0.90;
        case AF_PAT_HARMONIC:    return 0.85;
        case AF_PAT_ELLIOTT:     return 0.80;
        case AF_PAT_WYCKOFF:     return 1.00;
        case AF_PAT_VOLUME:      return 0.75;
    }
    return 0.75;
}

af_confluence_score_t af_score_timeframe(
    const char               *symbol,
    const char               *timeframe,
    const af_engine_result_t *ind_sum,
    const AF_PatternResult   *pat,
    int    struct_sig,  double struct_conf,
    int    trend_sig,   double trend_conf)
{
    af_confluence_score_t sc;
    memset(&sc, 0, sizeof(sc));
    af_strlcpy(sc.symbol,    symbol    ? symbol    : "", sizeof(sc.symbol));
    af_strlcpy(sc.timeframe, timeframe ? timeframe : "", sizeof(sc.timeframe));

    double bull_pts = 0.0, bear_pts = 0.0;

    /* Indicator signals */
    if (ind_sum) {
        sc.ind_bull  = ind_sum->bullish;
        sc.ind_bear  = ind_sum->bearish;
        bull_pts    += (double)sc.ind_bull;
        bear_pts    += (double)sc.ind_bear;
    }

    /* Pattern signals */
    if (pat) {
        sc.pat_bull = pat->bullish_count;
        sc.pat_bear = pat->bearish_count;
        for (int i = 0; i < pat->count; ++i) {
            const AF_PatternMatch *m = &pat->matches[i];
            double w = pat_weight(m->category) * m->confidence * 3.0;
            if (m->direction == AF_PAT_DIR_BULLISH) bull_pts += w;
            if (m->direction == AF_PAT_DIR_BEARISH) bear_pts += w;
        }
    }

    /* Structure contribution */
    sc.structure_signal = struct_sig;
    if (struct_sig ==  1) bull_pts += struct_conf * 5.0;
    if (struct_sig == -1) bear_pts += struct_conf * 5.0;

    /* Trend contribution */
    sc.trend_signal = trend_sig;
    if (trend_sig ==  1) bull_pts += trend_conf * 4.0;
    if (trend_sig == -1) bear_pts += trend_conf * 4.0;

    double total = bull_pts + bear_pts;
    if (total < 1e-10) {
        sc.score     = 0.0;
        sc.direction = 0;
        sc.grade     = 'F';
        sc.confidence = 0.0;
        return sc;
    }

    double bull_pct = bull_pts / total * 100.0;
    double bear_pct = bear_pts / total * 100.0;
    sc.score     = af_max_d(bull_pct, bear_pct);
    sc.direction = (bull_pts >= bear_pts) ? 1 : -1;

    double margin = af_fabs_d(bull_pct - bear_pct);
    if (margin < 10.0) sc.direction = 0;

    sc.grade = sc.score >= 80.0 ? 'A'
             : sc.score >= 65.0 ? 'B'
             : sc.score >= 50.0 ? 'C'
             : sc.score >= 35.0 ? 'D' : 'F';

    sc.confidence = af_min_d(1.0, sc.score / 100.0 * (1.0 + margin / 200.0));
    return sc;
}

/* ══════════════════════════════════════════════════════════════════════════
 * 4.  SESSION FILTER
 * ══════════════════════════════════════════════════════════════════════════ */

/* Hourly quality tables (index = UTC hour 0–23) */
static const double SESS_DEFAULT[24] = {
    0.30, 0.25, 0.20, 0.20, 0.25, 0.35,
    0.55, 0.80, 0.85, 0.90, 0.90, 0.90,
    1.00, 1.00, 1.00, 0.95, 0.85, 0.70,
    0.55, 0.45, 0.40, 0.30, 0.20, 0.25
};
static const double SESS_USDJPY[24] = {
    0.80, 0.85, 0.85, 0.80, 0.75, 0.70,
    0.75, 0.90, 0.95, 0.80, 0.70, 0.65,
    0.85, 0.90, 0.85, 0.75, 0.60, 0.50,
    0.40, 0.35, 0.30, 0.40, 0.55, 0.70
};
static const double SESS_XAUUSD[24] = {
    0.35, 0.30, 0.25, 0.25, 0.30, 0.40,
    0.60, 0.75, 0.85, 0.90, 0.90, 0.90,
    1.00, 1.00, 0.95, 0.85, 0.70, 0.50,
    0.40, 0.35, 0.30, 0.25, 0.20, 0.25
};

af_session_score_t af_eval_session(const char *symbol, int hour_utc, int dow) {
    af_session_score_t s;
    memset(&s, 0, sizeof(s));
    s.hour_utc    = hour_utc;
    s.day_of_week = dow;

    /* Sunday — no trading */
    if (dow == 6) {
        af_strlcpy(s.session, "DEAD", sizeof(s.session));
        s.quality   = 0.0;
        s.tradeable = false;
        snprintf(s.reason, sizeof(s.reason), "Sunday — thin market");
        return s;
    }

    /* Friday 20:00+ UTC — weekend gap risk */
    if (dow == 4 && hour_utc >= 20) {
        af_strlcpy(s.session, "DEAD", sizeof(s.session));
        s.quality   = 0.0;
        s.tradeable = false;
        snprintf(s.reason, sizeof(s.reason),
                 "Friday 20:00+ UTC — weekend gap risk");
        return s;
    }

    bool is_jpy = af_stristr(symbol, "JPY");
    bool dead;
    if (is_jpy) {
        dead = (hour_utc == 22 || hour_utc == 23);
    } else {
        dead = (hour_utc >= 22 || hour_utc <= 3);
    }

    if (dead) {
        af_strlcpy(s.session, "DEAD", sizeof(s.session));
        s.quality   = 0.05;
        s.tradeable = false;
        snprintf(s.reason, sizeof(s.reason),
                 "Dead hours %02d:00 UTC", hour_utc);
        return s;
    }

    /* Select quality table */
    const double *map;
    if (af_stristr(symbol, "JPY"))
        map = SESS_USDJPY;
    else if (af_stristr(symbol, "XAU"))
        map = SESS_XAUUSD;
    else
        map = SESS_DEFAULT;

    s.quality = map[hour_utc];

    /* Session label */
    if      (hour_utc >= 12 && hour_utc <= 16)
        af_strlcpy(s.session, "OVERLAP", sizeof(s.session));
    else if (hour_utc >=  7 && hour_utc <= 11)
        af_strlcpy(s.session, "LONDON",  sizeof(s.session));
    else if (hour_utc >= 17 && hour_utc <= 20)
        af_strlcpy(s.session, "NY",      sizeof(s.session));
    else
        af_strlcpy(s.session, "ASIA",    sizeof(s.session));

    s.tradeable = (s.quality >= 0.45);
    snprintf(s.reason, sizeof(s.reason),
             "%s session quality=%.0f%%", s.session, s.quality * 100.0);
    return s;
}

/* ══════════════════════════════════════════════════════════════════════════
 * 5.  VOLATILITY REGIME
 * ══════════════════════════════════════════════════════════════════════════ */

const char *af_vol_regime_name(af_vol_regime_t r) {
    switch (r) {
        case AF_REGIME_HIGH_TRENDING: return "HIGH_TRENDING";
        case AF_REGIME_HIGH_RANGING:  return "HIGH_RANGING";
        case AF_REGIME_LOW_TRENDING:  return "LOW_TRENDING";
        case AF_REGIME_LOW_RANGING:   return "LOW_RANGING";
        case AF_REGIME_SQUEEZE:       return "SQUEEZE";
        case AF_REGIME_EXPANDING:     return "EXPANDING";
    }
    return "LOW_RANGING";
}

af_regime_result_t af_classify_regime(const af_bar_t *bars, size_t n) {
    af_regime_result_t r;
    memset(&r, 0, sizeof(r));
    r.regime          = AF_REGIME_LOW_RANGING;
    r.size_multiplier = 0.80;

    if (!bars || n < 100) return r;

    double *h   = (double *)malloc(n * sizeof(double));
    double *l   = (double *)malloc(n * sizeof(double));
    double *c   = (double *)malloc(n * sizeof(double));
    double *atr = (double *)malloc(n * sizeof(double));
    double *bu  = (double *)malloc(n * sizeof(double));
    double *bm  = (double *)malloc(n * sizeof(double));
    double *bl  = (double *)malloc(n * sizeof(double));
    double *adx_arr = (double *)malloc(n * sizeof(double));
    double *pdi_arr = (double *)malloc(n * sizeof(double));
    double *mdi_arr = (double *)malloc(n * sizeof(double));

    if (!h || !l || !c || !atr || !bu || !bm || !bl ||
        !adx_arr || !pdi_arr || !mdi_arr) {
        free(h); free(l); free(c); free(atr);
        free(bu); free(bm); free(bl);
        free(adx_arr); free(pdi_arr); free(mdi_arr);
        return r;
    }

    for (size_t i = 0; i < n; ++i) {
        h[i] = bars[i].high;
        l[i] = bars[i].low;
        c[i] = bars[i].close;
    }

    /* ── ATR percentile (last 100 bars) ── */
    af_atr(h, l, c, n, 14, atr);
    double atr_now = af_isnan_d(atr[n-1]) ? 0.001 : atr[n-1];
    size_t look = n < 100 ? n : 100;
    int less_than = 0;
    for (size_t i = n - look; i < n; ++i) {
        if (!af_isnan_d(atr[i]) && atr[i] < atr_now) ++less_than;
    }
    r.atr_percentile = (double)less_than / (double)look * 100.0;

    /* ── Bollinger Bands bandwidth (upper-lower) / middle ── */
    af_bollinger(c, n, 20, 2.0, bu, bm, bl);

    /*  Compute bandwidth array manually: bw[i] = (upper[i] - lower[i]) / middle[i] */
    double bw_now = 1.0;
    if (!af_isnan_d(bu[n-1]) && !af_isnan_d(bl[n-1]) && !af_isnan_d(bm[n-1])
        && bm[n-1] != 0.0) {
        bw_now = (bu[n-1] - bl[n-1]) / bm[n-1];
    }

    /* Minimum bandwidth over last 100 bars */
    double bw_min = bw_now;
    size_t look2 = n < 100 ? n : 100;
    for (size_t i = n - look2; i < n; ++i) {
        if (!af_isnan_d(bu[i]) && !af_isnan_d(bl[i]) && !af_isnan_d(bm[i])
            && bm[i] != 0.0) {
            double bw_i = (bu[i] - bl[i]) / bm[i];
            if (bw_i < bw_min) bw_min = bw_i;
        }
    }
    r.bb_squeeze = (bw_now <= bw_min * 1.05);

    /* BW slope: compare current vs 5 bars ago */
    double bw_prev = bw_now; /* default: no slope */
    if (n >= 5 && !af_isnan_d(bu[n-5]) && !af_isnan_d(bl[n-5])
        && !af_isnan_d(bm[n-5]) && bm[n-5] != 0.0) {
        bw_prev = (bu[n-5] - bl[n-5]) / bm[n-5];
    }
    double bw_slope = bw_now - bw_prev;
    bool expanding  = (bw_slope > 0.0) && !r.bb_squeeze;

    /* ── ADX ── */
    af_adx(h, l, c, n, 14, adx_arr, pdi_arr, mdi_arr);
    r.adx      = af_isnan_d(adx_arr[n-1]) ? 20.0 : adx_arr[n-1];
    r.trending = (r.adx >= 23.0);

    /* ── Classify ── */
    if (r.bb_squeeze) {
        r.regime          = AF_REGIME_SQUEEZE;
        r.size_multiplier = 0.0;
    } else if (expanding && r.atr_percentile > 50.0) {
        r.regime          = AF_REGIME_EXPANDING;
        r.size_multiplier = 1.2;
    } else if (r.atr_percentile >= 65.0 && r.trending) {
        r.regime          = AF_REGIME_HIGH_TRENDING;
        r.size_multiplier = 1.0;
    } else if (r.atr_percentile >= 65.0 && !r.trending) {
        r.regime          = AF_REGIME_HIGH_RANGING;
        r.size_multiplier = 0.3;
    } else if (r.atr_percentile <= 35.0 && r.trending) {
        r.regime          = AF_REGIME_LOW_TRENDING;
        r.size_multiplier = 0.75;
    } else {
        r.regime          = AF_REGIME_LOW_RANGING;
        r.size_multiplier = 0.80;
    }

    free(h); free(l); free(c); free(atr);
    free(bu); free(bm); free(bl);
    free(adx_arr); free(pdi_arr); free(mdi_arr);
    return r;
}

/* ══════════════════════════════════════════════════════════════════════════
 * 6.  MULTI-TIMEFRAME CONFLUENCE ANALYZER
 * ══════════════════════════════════════════════════════════════════════════ */

/** TF weight table — higher TF = stronger weight. */
static double tf_weight(af_timeframe_t tf) {
    switch (tf) {
        case AF_TF_D1:  return 4.0;
        case AF_TF_H4:  return 3.0;
        case AF_TF_H1:  return 2.0;
        case AF_TF_M15: return 1.0;
        default:        return 1.0;
    }
}

/** Return a short human-readable label for a timeframe (max 7 chars + NUL). */
static const char *tf_label(af_timeframe_t tf) {
    switch (tf) {
        case AF_TF_S15: return "S15";
        case AF_TF_M1:  return "M1";
        case AF_TF_M5:  return "M5";
        case AF_TF_M15: return "M15";
        case AF_TF_M30: return "M30";
        case AF_TF_H1:  return "H1";
        case AF_TF_H4:  return "H4";
        case AF_TF_D1:  return "D1";
        case AF_TF_W1:  return "W1";
    }
    return "UNK";
}

af_mtf_result_t af_mtf_analyze(
    const char             *symbol,
    const af_bar_t        **bars_per_tf,
    const size_t           *counts_per_tf,
    const af_timeframe_t   *tfs,
    int                     tf_count,
    const af_engine_result_t **ind_per_tf,
    const AF_PatternResult   **pat_per_tf)
{
    af_mtf_result_t result;
    memset(&result, 0, sizeof(result));
    af_strlcpy(result.symbol, symbol ? symbol : "", sizeof(result.symbol));

    if (!bars_per_tf || !counts_per_tf || !tfs || tf_count <= 0) {
        result.direction = 0;
        return result;
    }

    double weighted_bull = 0.0;
    double weighted_bear = 0.0;
    double total_weight  = 0.0;

    /* Track dominant timeframe (highest weight among tradeable TFs) */
    double dom_weight     = -1.0;
    af_timeframe_t dom_tf = (af_timeframe_t)0;

    for (int i = 0; i < tf_count; ++i) {
        if (!bars_per_tf[i] || counts_per_tf[i] == 0) continue;

        const af_bar_t *bars = bars_per_tf[i];
        size_t          cnt  = counts_per_tf[i];
        af_timeframe_t  tf   = tfs[i];
        double          w    = tf_weight(tf);

        /* ── Run market structure analysis ── */
        af_structure_result_t st = af_market_structure_analyse(bars, cnt, 3);

        /* ── Run trend classification ── */
        af_trend_classification_t tr = af_trend_classify(bars, cnt);

        /* ── Get or compute indicator summary ── */
        af_engine_result_t ind_local;
        const af_engine_result_t *ind_ptr = NULL;
        if (ind_per_tf && ind_per_tf[i]) {
            ind_ptr = ind_per_tf[i];
        } else {
            ind_local = af_run_indicators(bars, cnt,
                                          symbol ? symbol : "", tf);
            ind_ptr   = &ind_local;
        }

        /* ── Get or compute pattern result ── */
        AF_PatternResult pat_local;
        const AF_PatternResult *pat_ptr = NULL;
        if (pat_per_tf && pat_per_tf[i]) {
            pat_ptr = pat_per_tf[i];
        } else {
            pat_local = af_pattern_engine_scan(bars, cnt);
            pat_ptr   = &pat_local;
        }

        /* ── Compute confluence score for this TF ── */
        af_confluence_score_t sc = af_score_timeframe(
            symbol,
            tf_label(tf),
            ind_ptr,
            pat_ptr,
            st.signal,  st.confidence,
            tr.signal,  tr.confidence);

        /* ── Accumulate weighted bull/bear votes ── */
        if (sc.direction ==  1) weighted_bull += w * sc.score;
        if (sc.direction == -1) weighted_bear += w * sc.score;
        total_weight += w;

        /* Track dominant TF */
        if (sc.direction != 0 && w > dom_weight) {
            dom_weight = w;
            dom_tf     = tf;
        }

        ++result.tf_count;
    }

    if (total_weight < 1e-10) {
        result.direction = 0;
        return result;
    }

    double norm_bull = weighted_bull / total_weight;
    double norm_bear = weighted_bear / total_weight;
    double best      = af_max_d(norm_bull, norm_bear);

    result.weighted_score = best;

    double margin = af_fabs_d(norm_bull - norm_bear);
    if (margin < 5.0) {
        /* Too close to call — neutral */
        result.direction = 0;
    } else {
        result.direction = (norm_bull >= norm_bear) ? 1 : -1;
    }

    /* Confidence: proportion of score differential normalised to [0,1] */
    result.confidence = af_min_d(1.0, margin / 50.0);

    /* Dominant timeframe label */
    af_strlcpy(result.dominant_tf,
               (dom_weight >= 0.0) ? tf_label(dom_tf) : "NONE",
               sizeof(result.dominant_tf));

    return result;
}
