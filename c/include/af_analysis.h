/**
 * AlgoForge — c/include/af_analysis.h
 * C port of the C++ analysis layer: market structure, trend classification,
 * confluence scoring, session filtering, volatility regime, and multi-TF
 * confluence aggregation (Round 9).
 *
 * All output structs are value types — no heap allocation inside.
 */
#ifndef AF_ANALYSIS_H
#define AF_ANALYSIS_H

#include <stddef.h>
#include <stdbool.h>
#include "af_types.h"
#include "af_indicators.h"
#include "af_patterns.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */
#define AF_MAX_SWINGS 64

/* ── Market Structure ───────────────────────────────────────────────────── */

typedef enum {
    AF_TREND_STRONG_BULL = 0,
    AF_TREND_BULL,
    AF_TREND_RANGING,
    AF_TREND_BEAR,
    AF_TREND_STRONG_BEAR
} af_trend_state_t;

typedef struct {
    size_t index;
    double price;
    bool   is_high;
} af_swing_point_t;

typedef struct {
    af_trend_state_t  trend;
    int               signal;          /* +1 / 0 / -1                   */
    double            confidence;
    bool              bos;             /* break of structure             */
    bool              choch;           /* change of character            */
    double            last_swing_high;
    double            last_swing_low;
    af_swing_point_t  swing_highs[AF_MAX_SWINGS];
    int               swing_high_count;
    af_swing_point_t  swing_lows[AF_MAX_SWINGS];
    int               swing_low_count;
} af_structure_result_t;

/**
 * af_market_structure_analyse — detect HH/HL/LH/LL, BOS, ChoCH.
 * swing_strength: number of bars to each side used for swing detection (default 3).
 * bars[0] = oldest, bars[n-1] = newest.
 */
af_structure_result_t af_market_structure_analyse(
    const af_bar_t *bars, size_t n, int swing_strength);

const char *af_trend_state_name(af_trend_state_t s);

/* ── Trend Classifier ───────────────────────────────────────────────────── */

typedef enum {
    AF_BIAS_STRONG_BULL = 0,
    AF_BIAS_BULL,
    AF_BIAS_NEUTRAL,
    AF_BIAS_BEAR,
    AF_BIAS_STRONG_BEAR
} af_trend_bias_t;

typedef struct {
    af_trend_bias_t bias;
    int             signal;      /* +1 / 0 / -1 */
    double          confidence;
    double          adx;
    bool            trending;
} af_trend_classification_t;

/**
 * af_trend_classify — EMA9/21/50/200 + ADX(14) based trend scoring.
 * Requires n >= 200.
 */
af_trend_classification_t af_trend_classify(const af_bar_t *bars, size_t n);

const char *af_trend_bias_name(af_trend_bias_t b);

/* ── Confluence Score ────────────────────────────────────────────────────── */

typedef struct {
    char   symbol[AF_MAX_SYMBOL_LEN];
    char   timeframe[16];
    double score;          /* 0 – 100                              */
    int    direction;      /* +1=bull / 0=neutral / -1=bear        */
    char   grade;          /* 'A' 'B' 'C' 'D' 'F'                 */
    double confidence;
    int    ind_bull;
    int    ind_bear;
    int    pat_bull;
    int    pat_bear;
    int    structure_signal;
    int    trend_signal;
} af_confluence_score_t;

/** is_tradeable: score >= 60 && direction != 0 */
static inline bool af_confluence_is_tradeable(const af_confluence_score_t *s) {
    return s && s->score >= 60.0 && s->direction != 0;
}

/**
 * af_score_timeframe — compute confluence score for one symbol/timeframe pair.
 * ind_sum and pat may be NULL (their contributions are skipped).
 */
af_confluence_score_t af_score_timeframe(
    const char               *symbol,
    const char               *timeframe,
    const af_engine_result_t *ind_sum,    /* nullable */
    const AF_PatternResult   *pat,        /* nullable */
    int    struct_sig,  double struct_conf,
    int    trend_sig,   double trend_conf);

/* ── Session Filter ──────────────────────────────────────────────────────── */

typedef struct {
    char   session[16];    /* "OVERLAP" "LONDON" "NY" "ASIA" "DEAD" */
    double quality;        /* 0 – 1                                  */
    bool   tradeable;      /* quality >= 0.45                        */
    char   reason[128];
    int    hour_utc;
    int    day_of_week;
} af_session_score_t;

/**
 * af_eval_session — evaluate trading session quality.
 * dow: 0=Monday … 4=Friday, 5=Saturday, 6=Sunday.
 * hour_utc: 0–23.
 */
af_session_score_t af_eval_session(const char *symbol,
                                   int hour_utc, int dow);

/* ── Volatility Regime ───────────────────────────────────────────────────── */

typedef enum {
    AF_REGIME_HIGH_TRENDING = 0,
    AF_REGIME_HIGH_RANGING,
    AF_REGIME_LOW_TRENDING,
    AF_REGIME_LOW_RANGING,
    AF_REGIME_SQUEEZE,
    AF_REGIME_EXPANDING
} af_vol_regime_t;

typedef struct {
    af_vol_regime_t regime;
    double          atr_percentile;
    bool            bb_squeeze;
    double          adx;
    bool            trending;
    double          size_multiplier;
} af_regime_result_t;

/**
 * af_classify_regime — ATR percentile + BB squeeze + ADX based regime.
 * Requires n >= 100.
 */
af_regime_result_t af_classify_regime(const af_bar_t *bars, size_t n);

const char *af_vol_regime_name(af_vol_regime_t r);

/* ── Multi-Timeframe Confluence ──────────────────────────────────────────── */

typedef struct {
    char   symbol[AF_MAX_SYMBOL_LEN];
    int    direction;      /* +1=LONG / -1=SHORT / 0=NEUTRAL */
    double confidence;
    double weighted_score;
    int    tf_count;
    char   dominant_tf[8];
} af_mtf_result_t;

/**
 * af_mtf_analyze — run confluence scoring across multiple timeframes and
 * return a weighted aggregate signal.
 *
 * bars_per_tf[i]   : bar array for timeframe tfs[i], oldest → newest.
 * counts_per_tf[i] : number of bars in bars_per_tf[i].
 * tfs[i]           : AF_TF_* value for timeframe i.
 * ind_per_tf[i]    : pre-computed indicator result (may be NULL — will be run).
 * pat_per_tf[i]    : pre-computed pattern result   (may be NULL — will be run).
 * tf_count         : number of timeframes (1–4 recommended).
 *
 * TF weights: AF_TF_D1=4.0, AF_TF_H4=3.0, AF_TF_H1=2.0, AF_TF_M15=1.0,
 *             all others=1.0.
 */
af_mtf_result_t af_mtf_analyze(
    const char             *symbol,
    const af_bar_t        **bars_per_tf,
    const size_t           *counts_per_tf,
    const af_timeframe_t   *tfs,
    int                     tf_count,
    const af_engine_result_t **ind_per_tf,  /* may be NULL or contain NULLs */
    const AF_PatternResult   **pat_per_tf   /* may be NULL or contain NULLs */
);

#ifdef __cplusplus
}
#endif

#endif /* AF_ANALYSIS_H */
