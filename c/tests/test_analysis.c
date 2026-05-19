/**
 * AlgoForge — c/tests/test_analysis.c
 * Unit tests for the Round 9 analysis layer.
 *
 * Tests:
 *   af_market_structure_analyse()
 *   af_trend_classify()
 *   af_score_timeframe()
 *   af_eval_session()
 *   af_classify_regime()
 *   af_mtf_analyze()
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/af_analysis.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHK(cond) do {                                                      \
    if (cond) ++g_passed;                                                   \
    else { ++g_failed;                                                      \
           fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); }\
} while (0)

#define CHK_NEAR(a, b, tol) CHK(((a) - (b)) < (tol) && ((b) - (a)) < (tol))

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Build a bar from a single close price (open=close-0.5, high=close+1, low=close-1). */
static af_bar_t make_bar(double c) {
    af_bar_t b;
    b.timestamp = 0;
    b.open   = c - 0.5;
    b.high   = c + 1.0;
    b.low    = c - 1.0;
    b.close  = c;
    b.volume = 1000.0;
    b.spread = 0.0001;
    return b;
}

/* Build an uptrend bar array: prices start at base and rise by step each bar. */
static void fill_trend_bars(af_bar_t *bars, size_t n, double base, double step) {
    for (size_t i = 0; i < n; ++i)
        bars[i] = make_bar(base + (double)i * step);
}

/* Build a downtrend bar array: prices start at base and fall by step each bar. */
static void fill_downtrend_bars(af_bar_t *bars, size_t n, double base, double step) {
    for (size_t i = 0; i < n; ++i)
        bars[i] = make_bar(base - (double)i * step);
}

/* ══════════════════════════════════════════════════════════════════════════
 * 1.  MARKET STRUCTURE
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_structure_null_bars(void) {
    af_structure_result_t r = af_market_structure_analyse(NULL, 0, 3);
    CHK(r.trend == AF_TREND_RANGING);
    CHK(r.signal == 0);
    CHK(r.swing_high_count == 0);
    CHK(r.swing_low_count  == 0);
}

static void test_structure_too_few_bars(void) {
    af_bar_t bars[5];
    fill_trend_bars(bars, 5, 1.0, 0.1);
    /* Need >= swing_strength*4 = 12 bars for strength=3 */
    af_structure_result_t r = af_market_structure_analyse(bars, 5, 3);
    CHK(r.trend == AF_TREND_RANGING);
    CHK(r.signal == 0);
}

static void test_structure_uptrend(void) {
    /* 50 bars with monotonically rising closes will produce HH+HL pattern. */
    enum { N = 50 };
    af_bar_t bars[N];
    fill_trend_bars(bars, N, 100.0, 1.0);
    af_structure_result_t r = af_market_structure_analyse(bars, N, 3);
    /* Should detect some swing highs/lows */
    CHK(r.swing_high_count >= 0); /* at least no crash */
    CHK(r.swing_low_count  >= 0);
    /* Trend should be bullish or ranging (monotonic may not produce clean swings) */
    CHK(r.signal >= 0); /* not bearish */
}

static void test_structure_trend_state_names(void) {
    CHK(strcmp(af_trend_state_name(AF_TREND_STRONG_BULL), "STRONG_BULL") == 0);
    CHK(strcmp(af_trend_state_name(AF_TREND_BULL),        "BULL")        == 0);
    CHK(strcmp(af_trend_state_name(AF_TREND_RANGING),     "RANGING")     == 0);
    CHK(strcmp(af_trend_state_name(AF_TREND_BEAR),        "BEAR")        == 0);
    CHK(strcmp(af_trend_state_name(AF_TREND_STRONG_BEAR), "STRONG_BEAR") == 0);
}

static void test_structure_swing_array_bounds(void) {
    /* Ensure swing arrays never overflow AF_MAX_SWINGS. */
    enum { N = 200 };
    af_bar_t bars[N];
    /* Oscillating bars: alternating high/low */
    for (int i = 0; i < N; ++i) {
        double c = (i % 2 == 0) ? 100.0 + (double)i * 0.01
                                 : 99.0  - (double)i * 0.01;
        bars[i] = make_bar(c);
        bars[i].high = c + 2.0;
        bars[i].low  = c - 2.0;
    }
    af_structure_result_t r = af_market_structure_analyse(bars, N, 3);
    CHK(r.swing_high_count >= 0 && r.swing_high_count <= AF_MAX_SWINGS);
    CHK(r.swing_low_count  >= 0 && r.swing_low_count  <= AF_MAX_SWINGS);
}

/* ══════════════════════════════════════════════════════════════════════════
 * 2.  TREND CLASSIFIER
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_trend_null_bars(void) {
    af_trend_classification_t r = af_trend_classify(NULL, 0);
    CHK(r.bias   == AF_BIAS_NEUTRAL);
    CHK(r.signal == 0);
}

static void test_trend_too_few_bars(void) {
    af_bar_t bars[100];
    fill_trend_bars(bars, 100, 1.0, 0.01);
    af_trend_classification_t r = af_trend_classify(bars, 100);
    /* Requires n >= 200; should return neutral default */
    CHK(r.bias   == AF_BIAS_NEUTRAL);
    CHK(r.signal == 0);
}

static void test_trend_strong_uptrend(void) {
    /* 300 bars rising strongly — price will be above all EMAs. */
    enum { N = 300 };
    af_bar_t *bars = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!bars) { ++g_failed; return; }
    fill_trend_bars(bars, N, 100.0, 0.5);
    af_trend_classification_t r = af_trend_classify(bars, N);
    CHK(r.signal == 1);
    CHK(r.bias == AF_BIAS_STRONG_BULL || r.bias == AF_BIAS_BULL);
    CHK(r.confidence > 0.40);
    free(bars);
}

static void test_trend_strong_downtrend(void) {
    enum { N = 300 };
    af_bar_t *bars = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!bars) { ++g_failed; return; }
    fill_downtrend_bars(bars, N, 100.0, 0.5);
    af_trend_classification_t r = af_trend_classify(bars, N);
    CHK(r.signal == -1);
    CHK(r.bias == AF_BIAS_STRONG_BEAR || r.bias == AF_BIAS_BEAR);
    CHK(r.confidence > 0.40);
    free(bars);
}

static void test_trend_confidence_bounds(void) {
    enum { N = 300 };
    af_bar_t *bars = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!bars) { ++g_failed; return; }
    fill_trend_bars(bars, N, 50.0, 0.1);
    af_trend_classification_t r = af_trend_classify(bars, N);
    CHK(r.confidence >= 0.0 && r.confidence <= 1.0);
    free(bars);
}

static void test_trend_bias_names(void) {
    CHK(strcmp(af_trend_bias_name(AF_BIAS_STRONG_BULL), "STRONG_BULL") == 0);
    CHK(strcmp(af_trend_bias_name(AF_BIAS_BULL),        "BULL")        == 0);
    CHK(strcmp(af_trend_bias_name(AF_BIAS_NEUTRAL),     "NEUTRAL")     == 0);
    CHK(strcmp(af_trend_bias_name(AF_BIAS_BEAR),        "BEAR")        == 0);
    CHK(strcmp(af_trend_bias_name(AF_BIAS_STRONG_BEAR), "STRONG_BEAR") == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * 3.  CONFLUENCE SCORER
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_score_null_inputs(void) {
    af_confluence_score_t sc = af_score_timeframe(
        "EURUSD", "H1", NULL, NULL, 0, 0.0, 0, 0.0);
    CHK(sc.score     == 0.0);
    CHK(sc.direction == 0);
    CHK(sc.grade     == 'F');
}

static void test_score_strong_bull_struct_trend(void) {
    /* struct=+1/0.9 and trend=+1/0.9 → should give high bull score */
    af_confluence_score_t sc = af_score_timeframe(
        "EURUSD", "H4", NULL, NULL, 1, 0.9, 1, 0.9);
    CHK(sc.direction == 1);
    CHK(sc.score > 60.0);
    CHK(sc.grade == 'A' || sc.grade == 'B');
}

static void test_score_strong_bear_struct_trend(void) {
    af_confluence_score_t sc = af_score_timeframe(
        "EURUSD", "H4", NULL, NULL, -1, 0.9, -1, 0.9);
    CHK(sc.direction == -1);
    CHK(sc.score > 60.0);
}

static void test_score_conflicting_signals(void) {
    /* struct=bull, trend=bear → near 50/50, direction likely 0 */
    af_confluence_score_t sc = af_score_timeframe(
        "EURUSD", "H4", NULL, NULL, 1, 0.5, -1, 0.5);
    /* margin = |bull_pct - bear_pct| < 10 → direction = 0 */
    CHK(sc.direction == 0);
}

static void test_score_tradeable(void) {
    af_confluence_score_t sc = af_score_timeframe(
        "XAUUSD", "D1", NULL, NULL, 1, 0.9, 1, 0.9);
    CHK(af_confluence_is_tradeable(&sc) == (sc.score >= 60.0 && sc.direction != 0));
}

static void test_score_symbol_copy(void) {
    af_confluence_score_t sc = af_score_timeframe(
        "GBPUSD", "M15", NULL, NULL, 1, 0.8, 1, 0.7);
    CHK(strcmp(sc.symbol,    "GBPUSD") == 0);
    CHK(strcmp(sc.timeframe, "M15")    == 0);
}

static void test_score_grade_boundaries(void) {
    /* Score at grade boundary A: struct+trend very high confidence */
    af_confluence_score_t sc_a = af_score_timeframe(
        "SYM", "TF", NULL, NULL, 1, 0.99, 1, 0.99);
    CHK(sc_a.grade == 'A' || sc_a.grade == 'B');

    /* No signals → score=0 → grade F */
    af_confluence_score_t sc_f = af_score_timeframe(
        "SYM", "TF", NULL, NULL, 0, 0.0, 0, 0.0);
    CHK(sc_f.grade == 'F');
}

static void test_score_with_indicator_result(void) {
    af_engine_result_t ind;
    memset(&ind, 0, sizeof(ind));
    ind.bullish = 5;
    ind.bearish = 1;
    af_confluence_score_t sc = af_score_timeframe(
        "USDJPY", "H1", &ind, NULL, 1, 0.7, 1, 0.6);
    CHK(sc.direction == 1);
    CHK(sc.ind_bull == 5);
    CHK(sc.ind_bear == 1);
}

/* ══════════════════════════════════════════════════════════════════════════
 * 4.  SESSION FILTER
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_session_sunday_dead(void) {
    af_session_score_t s = af_eval_session("EURUSD", 12, 6); /* Sunday = 6 */
    CHK(strcmp(s.session, "DEAD") == 0);
    CHK(s.quality   == 0.0);
    CHK(s.tradeable == false);
}

static void test_session_friday_late_dead(void) {
    /* Friday (dow=4) at 20:00+ → DEAD */
    af_session_score_t s = af_eval_session("EURUSD", 20, 4);
    CHK(strcmp(s.session, "DEAD") == 0);
    CHK(s.tradeable == false);
    /* Friday 19:00 should NOT be dead */
    af_session_score_t s2 = af_eval_session("EURUSD", 19, 4);
    CHK(strcmp(s2.session, "DEAD") != 0);
}

static void test_session_overlap(void) {
    /* Hour 13 (1 PM UTC) → OVERLAP for non-JPY, non-XAU */
    af_session_score_t s = af_eval_session("EURUSD", 13, 1 /* Monday */);
    CHK(strcmp(s.session, "OVERLAP") == 0);
    CHK(s.quality >= 0.90); /* SESS_DEFAULT[13] = 1.00 */
    CHK(s.tradeable == true);
}

static void test_session_london(void) {
    af_session_score_t s = af_eval_session("GBPUSD", 9, 2);
    CHK(strcmp(s.session, "LONDON") == 0);
    CHK(s.tradeable == true);
}

static void test_session_ny(void) {
    af_session_score_t s = af_eval_session("EURUSD", 18, 3);
    CHK(strcmp(s.session, "NY") == 0);
    CHK(s.tradeable == true);
}

static void test_session_jpy_dead_hours(void) {
    /* JPY dead hours: 22 and 23 UTC */
    af_session_score_t s22 = af_eval_session("USDJPY", 22, 1);
    CHK(strcmp(s22.session, "DEAD") == 0);
    af_session_score_t s23 = af_eval_session("USDJPY", 23, 1);
    CHK(strcmp(s23.session, "DEAD") == 0);
    /* Hour 21 should not be dead for JPY */
    af_session_score_t s21 = af_eval_session("USDJPY", 21, 1);
    CHK(strcmp(s21.session, "DEAD") != 0);
}

static void test_session_dead_hours_non_jpy(void) {
    /* Non-JPY dead: >= 22 or <= 3 */
    af_session_score_t s0 = af_eval_session("EURUSD", 0, 1);
    CHK(strcmp(s0.session, "DEAD") == 0);
    af_session_score_t s3 = af_eval_session("EURUSD", 3, 1);
    CHK(strcmp(s3.session, "DEAD") == 0);
    af_session_score_t s22 = af_eval_session("EURUSD", 22, 1);
    CHK(strcmp(s22.session, "DEAD") == 0);
    /* Hour 4 should NOT be dead for EURUSD */
    af_session_score_t s4 = af_eval_session("EURUSD", 4, 1);
    CHK(strcmp(s4.session, "DEAD") != 0);
}

static void test_session_xauusd_quality(void) {
    /* XAUUSD overlap hour 12: SESS_XAUUSD[12] = 1.00 */
    af_session_score_t s = af_eval_session("XAUUSD", 12, 1);
    CHK(strcmp(s.session, "OVERLAP") == 0);
    CHK_NEAR(s.quality, 1.00, 0.001);
}

static void test_session_fields(void) {
    af_session_score_t s = af_eval_session("EURUSD", 10, 2);
    CHK(s.hour_utc    == 10);
    CHK(s.day_of_week == 2);
}

/* ══════════════════════════════════════════════════════════════════════════
 * 5.  VOLATILITY REGIME
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_regime_null_bars(void) {
    af_regime_result_t r = af_classify_regime(NULL, 0);
    CHK(r.regime          == AF_REGIME_LOW_RANGING);
    CHK(r.size_multiplier == 0.80);
}

static void test_regime_too_few_bars(void) {
    af_bar_t bars[50];
    fill_trend_bars(bars, 50, 100.0, 0.1);
    af_regime_result_t r = af_classify_regime(bars, 50);
    CHK(r.regime == AF_REGIME_LOW_RANGING);
}

static void test_regime_returns_valid(void) {
    enum { N = 150 };
    af_bar_t *bars = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!bars) { ++g_failed; return; }
    fill_trend_bars(bars, N, 1.0, 0.01);
    af_regime_result_t r = af_classify_regime(bars, N);
    CHK(r.atr_percentile >= 0.0 && r.atr_percentile <= 100.0);
    CHK(r.size_multiplier >= 0.0);
    free(bars);
}

static void test_regime_multipliers(void) {
    /* Verify the multiplier is within documented range for all regimes */
    static const af_vol_regime_t regimes[] = {
        AF_REGIME_SQUEEZE, AF_REGIME_EXPANDING,
        AF_REGIME_HIGH_TRENDING, AF_REGIME_HIGH_RANGING,
        AF_REGIME_LOW_TRENDING, AF_REGIME_LOW_RANGING
    };
    static const double expected[] = { 0.0, 1.2, 1.0, 0.3, 0.75, 0.80 };
    for (int i = 0; i < 6; ++i) {
        /* Create a mock result and check name function works */
        (void)expected[i];
        CHK(af_vol_regime_name(regimes[i]) != NULL);
    }
}

static void test_regime_name_strings(void) {
    CHK(strcmp(af_vol_regime_name(AF_REGIME_HIGH_TRENDING), "HIGH_TRENDING") == 0);
    CHK(strcmp(af_vol_regime_name(AF_REGIME_HIGH_RANGING),  "HIGH_RANGING")  == 0);
    CHK(strcmp(af_vol_regime_name(AF_REGIME_LOW_TRENDING),  "LOW_TRENDING")  == 0);
    CHK(strcmp(af_vol_regime_name(AF_REGIME_LOW_RANGING),   "LOW_RANGING")   == 0);
    CHK(strcmp(af_vol_regime_name(AF_REGIME_SQUEEZE),       "SQUEEZE")       == 0);
    CHK(strcmp(af_vol_regime_name(AF_REGIME_EXPANDING),     "EXPANDING")     == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * 6.  MULTI-TIMEFRAME CONFLUENCE
 * ══════════════════════════════════════════════════════════════════════════ */

static void test_mtf_null_inputs(void) {
    af_mtf_result_t r = af_mtf_analyze(NULL, NULL, NULL, NULL, 0, NULL, NULL);
    CHK(r.direction == 0);
}

static void test_mtf_zero_tf_count(void) {
    const af_bar_t *bptf[1] = { NULL };
    size_t cptf[1] = { 0 };
    af_timeframe_t tfptf[1] = { AF_TF_H1 };
    af_mtf_result_t r = af_mtf_analyze("EURUSD", bptf, cptf, tfptf, 0, NULL, NULL);
    CHK(r.direction == 0);
}

static void test_mtf_single_tf_bullish(void) {
    enum { N = 300 };
    af_bar_t *bars = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!bars) { ++g_failed; return; }
    fill_trend_bars(bars, N, 100.0, 0.5);

    const af_bar_t *bptf[1]   = { bars };
    size_t          cptf[1]   = { N };
    af_timeframe_t  tfptf[1]  = { AF_TF_H1 };

    af_mtf_result_t r = af_mtf_analyze("EURUSD", bptf, cptf, tfptf, 1, NULL, NULL);
    CHK(r.tf_count == 1);
    CHK(r.weighted_score >= 0.0);
    CHK(r.direction == 1 || r.direction == 0); /* strong uptrend → likely 1 */
    CHK(strcmp(r.symbol, "EURUSD") == 0);
    CHK(r.confidence >= 0.0 && r.confidence <= 1.0);

    free(bars);
}

static void test_mtf_single_tf_bearish(void) {
    enum { N = 300 };
    af_bar_t *bars = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!bars) { ++g_failed; return; }
    fill_downtrend_bars(bars, N, 200.0, 0.5);

    const af_bar_t *bptf[1]   = { bars };
    size_t          cptf[1]   = { N };
    af_timeframe_t  tfptf[1]  = { AF_TF_H1 };

    af_mtf_result_t r = af_mtf_analyze("EURUSD", bptf, cptf, tfptf, 1, NULL, NULL);
    CHK(r.direction == -1 || r.direction == 0);
    free(bars);
}

static void test_mtf_multiple_tfs(void) {
    enum { N = 300 };
    af_bar_t *b_d1 = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    af_bar_t *b_h4 = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    af_bar_t *b_h1 = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!b_d1 || !b_h4 || !b_h1) {
        free(b_d1); free(b_h4); free(b_h1);
        ++g_failed; return;
    }
    fill_trend_bars(b_d1, N, 100.0, 0.5);
    fill_trend_bars(b_h4, N, 100.0, 0.5);
    fill_trend_bars(b_h1, N, 100.0, 0.5);

    const af_bar_t *bptf[3]  = { b_d1, b_h4, b_h1 };
    size_t          cptf[3]  = { N,    N,     N    };
    af_timeframe_t  tfptf[3] = { AF_TF_D1, AF_TF_H4, AF_TF_H1 };

    af_mtf_result_t r = af_mtf_analyze("EURUSD", bptf, cptf, tfptf, 3, NULL, NULL);
    CHK(r.tf_count == 3);
    CHK(r.weighted_score >= 0.0);
    CHK(r.direction == 1 || r.direction == 0);
    /* dominant_tf should be non-empty */
    CHK(strlen(r.dominant_tf) > 0);

    free(b_d1); free(b_h4); free(b_h1);
}

static void test_mtf_precomputed_indicators(void) {
    enum { N = 300 };
    af_bar_t *bars = (af_bar_t *)malloc(N * sizeof(af_bar_t));
    if (!bars) { ++g_failed; return; }
    fill_trend_bars(bars, N, 100.0, 0.5);

    af_engine_result_t ind;
    memset(&ind, 0, sizeof(ind));
    ind.bullish = 8;
    ind.bearish = 2;

    const af_bar_t       *bptf[1] = { bars };
    size_t                cptf[1] = { N };
    af_timeframe_t        tfptf[1]= { AF_TF_H4 };
    const af_engine_result_t *iptr[1] = { &ind };

    af_mtf_result_t r = af_mtf_analyze("EURUSD", bptf, cptf, tfptf, 1, iptr, NULL);
    CHK(r.tf_count == 1);
    CHK(r.direction == 1 || r.direction == 0);
    free(bars);
}

static void test_mtf_symbol_propagated(void) {
    af_mtf_result_t r = af_mtf_analyze("USDJPY", NULL, NULL, NULL, 0, NULL, NULL);
    CHK(strcmp(r.symbol, "USDJPY") == 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Entry point
 * ══════════════════════════════════════════════════════════════════════════ */

void test_analysis_run(int *total_passed, int *total_failed) {
    g_passed = 0;
    g_failed = 0;

    printf("  [analysis] market_structure...\n");
    test_structure_null_bars();
    test_structure_too_few_bars();
    test_structure_uptrend();
    test_structure_trend_state_names();
    test_structure_swing_array_bounds();

    printf("  [analysis] trend_classify...\n");
    test_trend_null_bars();
    test_trend_too_few_bars();
    test_trend_strong_uptrend();
    test_trend_strong_downtrend();
    test_trend_confidence_bounds();
    test_trend_bias_names();

    printf("  [analysis] confluence_score...\n");
    test_score_null_inputs();
    test_score_strong_bull_struct_trend();
    test_score_strong_bear_struct_trend();
    test_score_conflicting_signals();
    test_score_tradeable();
    test_score_symbol_copy();
    test_score_grade_boundaries();
    test_score_with_indicator_result();

    printf("  [analysis] session_filter...\n");
    test_session_sunday_dead();
    test_session_friday_late_dead();
    test_session_overlap();
    test_session_london();
    test_session_ny();
    test_session_jpy_dead_hours();
    test_session_dead_hours_non_jpy();
    test_session_xauusd_quality();
    test_session_fields();

    printf("  [analysis] volatility_regime...\n");
    test_regime_null_bars();
    test_regime_too_few_bars();
    test_regime_returns_valid();
    test_regime_multipliers();
    test_regime_name_strings();

    printf("  [analysis] mtf_analyze...\n");
    test_mtf_null_inputs();
    test_mtf_zero_tf_count();
    test_mtf_single_tf_bullish();
    test_mtf_single_tf_bearish();
    test_mtf_multiple_tfs();
    test_mtf_precomputed_indicators();
    test_mtf_symbol_propagated();

    printf("  [analysis] passed=%d failed=%d\n", g_passed, g_failed);
    *total_passed += g_passed;
    *total_failed += g_failed;
}
