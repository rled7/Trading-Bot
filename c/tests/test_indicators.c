#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "af_indicators.h"

static int g_passed = 0;
static int g_failed = 0;

#define CHK(cond) do {                                                  \
    if (cond) ++g_passed;                                               \
    else { ++g_failed;                                                  \
           fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHK_NEAR(a, b, tol) CHK(fabs((a) - (b)) < (tol))

static int is_nan(double x) { return x != x; }

/* ── SMA ───────────────────────────────────────────────────────────────── */
static void test_sma_invalid_args(void) {
    double out[4];
    double in[]  = {1, 2, 3, 4};
    CHK(af_sma(NULL, 4, 2, out) == AF_ERR_INVALID_PARAM);
    CHK(af_sma(in,   4, 2, NULL) == AF_ERR_INVALID_PARAM);
    CHK(af_sma(in,   4, 0, out) == AF_ERR_INVALID_PARAM);
}

static void test_sma_period_greater_than_n(void) {
    double out[2];
    double in[] = {1, 2};
    CHK(af_sma(in, 2, 5, out) == AF_ERR_IO);
    CHK(is_nan(out[0]) && is_nan(out[1]));
}

static void test_sma_period_equals_n(void) {
    double out[3] = {0};
    double in[]  = {2, 4, 6};
    CHK(af_sma(in, 3, 3, out) == AF_OK);
    CHK(is_nan(out[0]) && is_nan(out[1]));
    CHK_NEAR(out[2], 4.0, 1e-12);
}

static void test_sma_known_values(void) {
    double out[5];
    double in[]  = {1, 2, 3, 4, 5};
    CHK(af_sma(in, 5, 3, out) == AF_OK);
    CHK(is_nan(out[0]) && is_nan(out[1]));
    CHK_NEAR(out[2], 2.0, 1e-12);  /* (1+2+3)/3 */
    CHK_NEAR(out[3], 3.0, 1e-12);  /* (2+3+4)/3 */
    CHK_NEAR(out[4], 4.0, 1e-12);  /* (3+4+5)/3 */
}

static void test_sma_constant_input(void) {
    double out[10];
    double in[10];
    for (int i = 0; i < 10; ++i) in[i] = 7.5;
    CHK(af_sma(in, 10, 4, out) == AF_OK);
    for (int i = 3; i < 10; ++i) CHK_NEAR(out[i], 7.5, 1e-12);
}

static void test_sma_period_one_is_identity(void) {
    double out[5];
    double in[]  = {1, 2, 3, 4, 5};
    CHK(af_sma(in, 5, 1, out) == AF_OK);
    for (int i = 0; i < 5; ++i) CHK_NEAR(out[i], in[i], 1e-12);
}

/* ── EMA ───────────────────────────────────────────────────────────────── */
static void test_ema_invalid_args(void) {
    double out[4]; double in[] = {1,2,3,4};
    CHK(af_ema(in, 4, 0, out) == AF_ERR_INVALID_PARAM);
    CHK(af_ema(NULL, 4, 2, out) == AF_ERR_INVALID_PARAM);
}

static void test_ema_insufficient_data(void) {
    double out[2]; double in[] = {1,2};
    CHK(af_ema(in, 2, 5, out) == AF_ERR_IO);
}

static void test_ema_constant_input(void) {
    double out[20], in[20];
    for (int i = 0; i < 20; ++i) in[i] = 3.14;
    CHK(af_ema(in, 20, 5, out) == AF_OK);
    /* EMA of a constant series is the constant. */
    for (int i = 4; i < 20; ++i) CHK_NEAR(out[i], 3.14, 1e-12);
}

static void test_ema_responds_to_change(void) {
    /* After a step up, EMA should rise toward the new level. */
    double out[20], in[20];
    for (int i = 0; i < 10; ++i) in[i] = 100.0;
    for (int i = 10; i < 20; ++i) in[i] = 110.0;
    CHK(af_ema(in, 20, 5, out) == AF_OK);
    CHK(out[10] > 100.0 && out[10] < 110.0);
    CHK(out[19] > out[10]);
    CHK(out[19] < 110.0);
}

/* ── RSI ───────────────────────────────────────────────────────────────── */
static void test_rsi_bounds(void) {
    double out[100], in[100];
    /* Noisy walk. */
    double p = 100.0;
    for (int i = 0; i < 100; ++i) { p += ((i * 9301 + 49297) % 233 - 116) * 0.01; in[i] = p; }
    CHK(af_rsi(in, 100, 14, out) == AF_OK);
    for (int i = 14; i < 100; ++i) CHK(out[i] >= 0.0 && out[i] <= 100.0);
}

static void test_rsi_strict_uptrend(void) {
    double out[50], in[50];
    for (int i = 0; i < 50; ++i) in[i] = 100.0 + i * 0.5;
    CHK(af_rsi(in, 50, 14, out) == AF_OK);
    /* Pure uptrend: avg-loss collapses to 0, rs is capped at 100 → RSI ≈ 99.01.
       (Matches the cpp/ reference; an infinite rs would give exactly 100.) */
    CHK(out[49] > 99.0 && out[49] <= 100.0);
}

static void test_rsi_strict_downtrend(void) {
    double out[50], in[50];
    for (int i = 0; i < 50; ++i) in[i] = 100.0 - i * 0.5;
    CHK(af_rsi(in, 50, 14, out) == AF_OK);
    /* All losses, no gains → RSI = 0. */
    CHK(out[49] < 5.0);
}

static void test_rsi_insufficient_data(void) {
    double out[5]; double in[] = {1,2,3,4,5};
    /* n must be > period; 5 <= 5 fails. */
    CHK(af_rsi(in, 5, 5, out) == AF_ERR_IO);
}

/* ── ATR ───────────────────────────────────────────────────────────────── */
static void test_atr_invalid_args(void) {
    double out[4]; double h[]={1,2,3,4}, l[]={0,1,2,3}, c[]={1,1,2,3};
    CHK(af_atr(NULL, l, c, 4, 2, out) == AF_ERR_INVALID_PARAM);
    CHK(af_atr(h, l, c, 4, 0, out)    == AF_ERR_INVALID_PARAM);
}

static void test_atr_non_negative(void) {
    double out[100], h[100], l[100], c[100];
    for (int i = 0; i < 100; ++i) {
        double mid = 100.0 + i * 0.1;
        h[i] = mid + 0.5; l[i] = mid - 0.5; c[i] = mid;
    }
    CHK(af_atr(h, l, c, 100, 14, out) == AF_OK);
    for (int i = 13; i < 100; ++i) CHK(out[i] >= 0.0);
}

static void test_atr_constant_range(void) {
    /* Range of 2 each bar, no gaps -> ATR should be 2 (after seed). */
    double out[50], h[50], l[50], c[50];
    for (int i = 0; i < 50; ++i) { h[i] = 11; l[i] = 9; c[i] = 10; }
    CHK(af_atr(h, l, c, 50, 14, out) == AF_OK);
    CHK_NEAR(out[49], 2.0, 1e-9);
}

static void test_atr_insufficient_data(void) {
    double out[5]; double h[]={1,1,1,1,1}, l[]={0,0,0,0,0}, c[]={1,1,1,1,1};
    CHK(af_atr(h, l, c, 5, 14, out) == AF_ERR_IO);
}

/* ── runner ────────────────────────────────────────────────────────────── */
void test_indicators_run(int *total_passed, int *total_failed) {
    /* legacy smoke checks from test_smoke.c — keep them. */
    CHK(AF_TF_H1 == 3600);
    CHK(AF_DIR_LONG == 1);
    CHK(AF_DIR_SHORT == -1);
    CHK(sizeof(af_bar_t) >= 6 * sizeof(double) + sizeof(int64_t));

    test_sma_invalid_args();
    test_sma_period_greater_than_n();
    test_sma_period_equals_n();
    test_sma_known_values();
    test_sma_constant_input();
    test_sma_period_one_is_identity();

    test_ema_invalid_args();
    test_ema_insufficient_data();
    test_ema_constant_input();
    test_ema_responds_to_change();

    test_rsi_bounds();
    test_rsi_strict_uptrend();
    test_rsi_strict_downtrend();
    test_rsi_insufficient_data();

    test_atr_invalid_args();
    test_atr_non_negative();
    test_atr_constant_range();
    test_atr_insufficient_data();

    *total_passed += g_passed;
    *total_failed += g_failed;
}
