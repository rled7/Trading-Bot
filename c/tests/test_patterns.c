#include <stdio.h>
#include "af_patterns.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHK(cond) do {                                                  \
    if (cond) ++g_passed;                                               \
    else { ++g_failed;                                                  \
           fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ── Doji ──────────────────────────────────────────────────────────── */
static void test_doji_match(void) {
    /* body = 0.005, range = 1.0 → 0.5% body, well under 10% threshold */
    CHK(af_is_doji(1.0, 1.5, 0.5, 1.005, 0.10) == 1);
}
static void test_doji_no_match_big_body(void) {
    /* body = 0.5, range = 1.0 → 50%, far above 10% threshold */
    CHK(af_is_doji(1.0, 1.5, 0.5, 1.5, 0.10) == 0);
}
static void test_doji_zero_range(void) {
    CHK(af_is_doji(1.0, 1.0, 1.0, 1.0, 0.10) == 0);
}
static void test_doji_threshold_boundary(void) {
    /* body = 0.09, range = 1.0 → 9%, clearly under 10% → match */
    CHK(af_is_doji(1.0, 1.5, 0.5, 1.09, 0.10) == 1);
    /* body = 0.11, range = 1.0 → 11%, clearly over → no match */
    CHK(af_is_doji(1.0, 1.5, 0.5, 1.11, 0.10) == 0);
}

/* ── Hammer / shooting star ────────────────────────────────────────── */
static void test_hammer_bullish(void) {
    /* Small body at top, long lower shadow */
    /* o=1.0, c=1.05, h=1.06, l=0.40 → range=0.66, lower=0.60 (91%), upper=0.01 */
    CHK(af_is_hammer(1.0, 1.06, 0.40, 1.05) == 1);
}
static void test_shooting_star(void) {
    /* Small body at bottom, long upper shadow */
    /* o=1.05, c=1.0, h=1.70, l=0.99 → range=0.71, upper=0.65 (91%), lower=0.01 */
    CHK(af_is_hammer(1.05, 1.70, 0.99, 1.0) == -1);
}
static void test_hammer_zero_range(void) {
    CHK(af_is_hammer(1.0, 1.0, 1.0, 1.0) == 0);
}
static void test_hammer_no_match_balanced(void) {
    /* Symmetric candle, no extreme shadow */
    CHK(af_is_hammer(1.0, 1.10, 0.90, 1.05) == 0);
}

/* ── Engulfing ─────────────────────────────────────────────────────── */
static void test_bullish_engulfing(void) {
    /* prev: bearish 100 → 95 ; curr: bullish 94 → 101  */
    CHK(af_is_engulfing(100, 95, 94, 101) == 1);
}
static void test_bearish_engulfing(void) {
    /* prev: bullish 95 → 100 ; curr: bearish 101 → 94  */
    CHK(af_is_engulfing(95, 100, 101, 94) == -1);
}
static void test_engulfing_no_match_same_direction(void) {
    CHK(af_is_engulfing(95, 100, 96, 101) == 0);
}
static void test_engulfing_no_match_inside_bar(void) {
    /* curr body fully INSIDE prev body — not engulfing */
    CHK(af_is_engulfing(95, 100, 99, 96) == 0);
}

/* ── Marubozu ──────────────────────────────────────────────────────── */
static void test_marubozu_bullish(void) {
    /* body = 0.95, range = 1.0 → 95% body, shadows < 5% */
    CHK(af_is_marubozu(1.00, 1.02, 1.00, 1.95, 0.90) == 1);
}
static void test_marubozu_bearish(void) {
    CHK(af_is_marubozu(1.95, 1.95, 0.97, 1.00, 0.90) == -1);
}
static void test_marubozu_no_match_long_shadow(void) {
    /* body 95% of range but lower shadow is huge → fail */
    CHK(af_is_marubozu(1.00, 1.95, 0.50, 1.95, 0.90) == 0);
}
static void test_marubozu_zero_range(void) {
    CHK(af_is_marubozu(1.0, 1.0, 1.0, 1.0, 0.90) == 0);
}

/* ── Pin bar ───────────────────────────────────────────────────────── */
static void test_pin_bar_bullish(void) {
    /* Small body up top, long lower wick */
    /* o=1.0, c=1.05, h=1.06, l=0.40 — same shape as the hammer */
    CHK(af_is_pin_bar(1.0, 1.06, 0.40, 1.05, 2.0) == 1);
}
static void test_pin_bar_bearish(void) {
    /* Small body down low, long upper wick */
    CHK(af_is_pin_bar(1.05, 1.70, 0.99, 1.0, 2.0) == -1);
}
static void test_pin_bar_no_match_tall_body(void) {
    /* Big body, no dominant wick */
    CHK(af_is_pin_bar(1.0, 1.95, 1.0, 1.95, 2.0) == 0);
}
static void test_pin_bar_zero_range(void) {
    CHK(af_is_pin_bar(1.0, 1.0, 1.0, 1.0, 2.0) == 0);
}

/* ── Scan ─────────────────────────────────────────────────────────── */
static void test_scan_collects_matches(void) {
    af_bar_t bars[3] = {0};
    /* bar 0: bearish big body 100 → 95 (no special pattern) */
    bars[0].open = 100; bars[0].high = 100.5; bars[0].low = 94.5; bars[0].close = 95;
    /* bar 1: bullish engulfing the prev: 94 → 101 (also a marubozu shape) */
    bars[1].open = 94;  bars[1].high = 101;    bars[1].low = 94;    bars[1].close = 101;
    /* bar 2: doji (open/close almost equal) */
    bars[2].open = 101; bars[2].high = 102;    bars[2].low = 100;   bars[2].close = 101.05;

    af_pattern_match_t out[16];
    size_t n = af_scan_patterns(bars, 3, out, 16);
    CHK(n > 0);

    /* Look for an engulfing match at bar 1 and a doji at bar 2. */
    int saw_engulfing_at_1 = 0;
    int saw_doji_at_2      = 0;
    for (size_t i = 0; i < n && i < 16; ++i) {
        if (out[i].bar_index == 1 && out[i].name[0] == 'e') saw_engulfing_at_1 = 1;
        if (out[i].bar_index == 2 && out[i].name[0] == 'd') saw_doji_at_2      = 1;
    }
    CHK(saw_engulfing_at_1);
    CHK(saw_doji_at_2);
}

static void test_scan_empty_input(void) {
    af_pattern_match_t out[4];
    CHK(af_scan_patterns(NULL, 0, out, 4) == 0);
}

static void test_scan_respects_out_cap(void) {
    /* Flat-doji wall: every bar matches doji → exceed cap, but count is honest */
    af_bar_t bars[10] = {0};
    for (int i = 0; i < 10; ++i) {
        bars[i].open = 1.0; bars[i].high = 1.5; bars[i].low = 0.5; bars[i].close = 1.001;
    }
    af_pattern_match_t out[4];
    size_t n = af_scan_patterns(bars, 10, out, 4);
    CHK(n >= 10);   /* total dojis found */
    /* Buffer only written for the first 4. */
}

/* ── Runner ───────────────────────────────────────────────────────── */
void test_patterns_run(int *total_passed, int *total_failed) {
    test_doji_match();
    test_doji_no_match_big_body();
    test_doji_zero_range();
    test_doji_threshold_boundary();

    test_hammer_bullish();
    test_shooting_star();
    test_hammer_zero_range();
    test_hammer_no_match_balanced();

    test_bullish_engulfing();
    test_bearish_engulfing();
    test_engulfing_no_match_same_direction();
    test_engulfing_no_match_inside_bar();

    test_marubozu_bullish();
    test_marubozu_bearish();
    test_marubozu_no_match_long_shadow();
    test_marubozu_zero_range();

    test_pin_bar_bullish();
    test_pin_bar_bearish();
    test_pin_bar_no_match_tall_body();
    test_pin_bar_zero_range();

    test_scan_collects_matches();
    test_scan_empty_input();
    test_scan_respects_out_cap();

    *total_passed += g_passed;
    *total_failed += g_failed;
}
