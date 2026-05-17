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

/* ── MACD ──────────────────────────────────────────────────────────────── */
static void test_macd_invalid_args(void) {
    double ml[10], sl[10], hl[10];
    double in[10];
    for (int i = 0; i < 10; ++i) in[i] = (double)(i + 1);
    CHK(af_macd(NULL, 10, 3, 6, 2, ml, sl, hl) == AF_ERR_INVALID_PARAM);
    CHK(af_macd(in, 10, 3, 6, 2, NULL, sl, hl) == AF_ERR_INVALID_PARAM);
    CHK(af_macd(in, 10, 0, 6, 2, ml, sl, hl)   == AF_ERR_INVALID_PARAM);
    CHK(af_macd(in, 10, 3, 0, 2, ml, sl, hl)   == AF_ERR_INVALID_PARAM);
    CHK(af_macd(in, 10, 3, 6, 0, ml, sl, hl)   == AF_ERR_INVALID_PARAM);
}

static void test_macd_nan_prefix(void) {
    /* With slow=6, signal=3: macd_line is NaN until index 5 (period-1),
       signal_line is NaN for further indices. */
    double ml[20], sl[20], hl[20];
    double in[20];
    for (int i = 0; i < 20; ++i) in[i] = 100.0 + i;
    CHK(af_macd(in, 20, 3, 6, 3, ml, sl, hl) == AF_OK);
    /* macd_line at index 4 (slow-1=5) first valid: indices 0..4 NaN */
    for (int i = 0; i < 5; ++i) CHK(is_nan(ml[i]));
    CHK(!is_nan(ml[5]));
}

static void test_macd_histogram_identity(void) {
    /* histogram must exactly equal macd_line - signal_line at every valid index */
    double ml[30], sl[30], hl[30];
    double in[30];
    for (int i = 0; i < 30; ++i) in[i] = 100.0 + i * 0.5;
    CHK(af_macd(in, 30, 5, 10, 4, ml, sl, hl) == AF_OK);
    for (int i = 0; i < 30; ++i) {
        if (!is_nan(ml[i]) && !is_nan(sl[i])) {
            CHK_NEAR(hl[i], ml[i] - sl[i], 1e-12);
        }
    }
}

static void test_macd_constant_input(void) {
    /* For constant input, EMA(fast)==EMA(slow) once both are defined,
       so macd_line -> 0, signal_line -> 0, histogram -> 0. */
    double ml[50], sl[50], hl[50];
    double in[50];
    for (int i = 0; i < 50; ++i) in[i] = 42.0;
    CHK(af_macd(in, 50, 5, 12, 9, ml, sl, hl) == AF_OK);
    /* At i=49 all should be 0 (or very close, due to EMA seeding). */
    CHK_NEAR(ml[49], 0.0, 1e-10);
    CHK_NEAR(hl[49], 0.0, 1e-10);
}

/* ── Bollinger Bands ───────────────────────────────────────────────────── */
static void test_bollinger_invalid_args(void) {
    double u[5], m[5], l[5];
    double in[] = {1,2,3,4,5};
    CHK(af_bollinger(NULL, 5, 3, 2.0, u, m, l) == AF_ERR_INVALID_PARAM);
    CHK(af_bollinger(in, 5, 0, 2.0, u, m, l)   == AF_ERR_INVALID_PARAM);
    CHK(af_bollinger(in, 5, 3, 2.0, NULL, m, l) == AF_ERR_INVALID_PARAM);
}

static void test_bollinger_insufficient_data(void) {
    double u[3], m[3], l[3];
    double in[] = {1,2,3};
    CHK(af_bollinger(in, 3, 5, 2.0, u, m, l) == AF_ERR_IO);
}

static void test_bollinger_ordering(void) {
    /* upper >= middle >= lower at every valid index */
    double u[30], m[30], l[30];
    double in[30];
    for (int i = 0; i < 30; ++i) in[i] = 100.0 + (i % 7) * 1.3;
    CHK(af_bollinger(in, 30, 5, 2.0, u, m, l) == AF_OK);
    for (int i = 4; i < 30; ++i) {
        CHK(u[i] >= m[i]);
        CHK(m[i] >= l[i]);
    }
}

static void test_bollinger_constant_input(void) {
    /* Constant series -> stddev=0 -> upper==middle==lower */
    double u[10], m[10], l[10];
    double in[10];
    for (int i = 0; i < 10; ++i) in[i] = 5.0;
    CHK(af_bollinger(in, 10, 4, 2.0, u, m, l) == AF_OK);
    for (int i = 3; i < 10; ++i) {
        CHK_NEAR(u[i], 5.0, 1e-12);
        CHK_NEAR(m[i], 5.0, 1e-12);
        CHK_NEAR(l[i], 5.0, 1e-12);
    }
}

static void test_bollinger_known_values(void) {
    /* period=3, mult=2, input={2,4,4,4,5,5,7,9}
       Window [2,4,4]: mean=10/3≈3.333, variance=((2-3.333)^2+(4-3.333)^2+(4-3.333)^2)/3
       = (1.778+0.444+0.444)/3 = 2.667/3 = 0.889, sd≈0.9428
       upper≈3.333+2*0.9428=5.219, lower≈3.333-1.886=1.448
    */
    double u[8], m[8], l[8];
    double in[] = {2,4,4,4,5,5,7,9};
    CHK(af_bollinger(in, 8, 3, 2.0, u, m, l) == AF_OK);
    CHK(is_nan(u[0]) && is_nan(u[1]));
    CHK_NEAR(m[2], 10.0/3.0, 1e-10);
    double var = ((2.0-10.0/3.0)*(2.0-10.0/3.0)
                + (4.0-10.0/3.0)*(4.0-10.0/3.0)
                + (4.0-10.0/3.0)*(4.0-10.0/3.0)) / 3.0;
    double sd_exp = 0.0;
    /* manual Newton-Raphson for test */
    { double g = var > 1.0 ? var*0.5 : 1.0;
      for (int k=0;k<64;k++){double ng=0.5*(g+var/g);if(ng==g)break;g=ng;}
      sd_exp = g; }
    CHK_NEAR(u[2], 10.0/3.0 + 2.0*sd_exp, 1e-10);
    CHK_NEAR(l[2], 10.0/3.0 - 2.0*sd_exp, 1e-10);
}

/* ── Stochastic ────────────────────────────────────────────────────────── */
static void test_stochastic_invalid_args(void) {
    double k[5], d[5];
    double h[]={1,2,3,4,5}, l[]={0,1,2,3,4}, c[]={1,1,2,3,4};
    CHK(af_stochastic(NULL, l, c, 5, 3, 2, k, d) == AF_ERR_INVALID_PARAM);
    CHK(af_stochastic(h, l, c, 5, 0, 2, k, d)    == AF_ERR_INVALID_PARAM);
    CHK(af_stochastic(h, l, c, 5, 3, 0, k, d)    == AF_ERR_INVALID_PARAM);
    CHK(af_stochastic(h, l, c, 5, 3, 2, NULL, d) == AF_ERR_INVALID_PARAM);
}

static void test_stochastic_insufficient_data(void) {
    double k[2], d[2];
    double h[]={1,2}, l[]={0,1}, c[]={1,2};
    CHK(af_stochastic(h, l, c, 2, 5, 3, k, d) == AF_ERR_IO);
}

static void test_stochastic_bounds(void) {
    /* %K must be in [0, 100] for every valid index */
    double k[50], d[50];
    double h[50], l[50], c[50];
    for (int i = 0; i < 50; ++i) {
        h[i] = 100.0 + (i % 11) * 2.0;
        l[i] = 80.0  + (i % 7)  * 1.5;
        c[i] = 90.0  + (i % 9)  * 1.0;
        if (c[i] > h[i]) c[i] = h[i];
        if (c[i] < l[i]) c[i] = l[i];
    }
    CHK(af_stochastic(h, l, c, 50, 5, 3, k, d) == AF_OK);
    for (int i = 0; i < 50; ++i) {
        if (!is_nan(k[i])) CHK(k[i] >= 0.0 && k[i] <= 100.0);
        if (!is_nan(d[i])) CHK(d[i] >= 0.0 && d[i] <= 100.0);
    }
}

static void test_stochastic_pure_uptrend(void) {
    /* In a pure uptrend, close == high each bar → raw %K = 100 throughout.
       SMA of 100 is 100, so k_out and d_out should both be 100. */
    double k[30], d[30];
    double h[30], l[30], c[30];
    for (int i = 0; i < 30; ++i) {
        l[i] = 90.0 + i;
        h[i] = 100.0 + i;
        c[i] = h[i]; /* close == high → raw K = 100 */
    }
    CHK(af_stochastic(h, l, c, 30, 5, 3, k, d) == AF_OK);
    /* k_out first valid at k_period + d_period - 2 = 5+3-2=6 */
    for (int i = 6; i < 30; ++i) CHK_NEAR(k[i], 100.0, 1e-9);
}

/* ── OBV ───────────────────────────────────────────────────────────────── */
static void test_obv_invalid_args(void) {
    double out[4];
    double c[]={1,2,3,4}, v[]={10,20,30,40};
    CHK(af_obv(NULL, v, 4, out) == AF_ERR_INVALID_PARAM);
    CHK(af_obv(c, NULL, 4, out) == AF_ERR_INVALID_PARAM);
    CHK(af_obv(c, v, 0, out)    == AF_ERR_INVALID_PARAM);
    CHK(af_obv(c, v, 4, NULL)   == AF_ERR_INVALID_PARAM);
}

static void test_obv_uptrend_monotone(void) {
    /* Pure uptrend: every close > prev close → OBV strictly increases */
    double c[] = {1,2,3,4,5};
    double v[] = {10,10,10,10,10};
    double out[5];
    CHK(af_obv(c, v, 5, out) == AF_OK);
    CHK_NEAR(out[0], 0.0, 1e-12);
    CHK(out[1] > out[0]);
    CHK(out[2] > out[1]);
    CHK(out[3] > out[2]);
    CHK(out[4] > out[3]);
    CHK_NEAR(out[4], 40.0, 1e-12); /* 0+10+10+10+10 */
}

static void test_obv_downtrend(void) {
    /* Pure downtrend: OBV strictly decreases */
    double c[] = {5,4,3,2,1};
    double v[] = {10,10,10,10,10};
    double out[5];
    CHK(af_obv(c, v, 5, out) == AF_OK);
    CHK_NEAR(out[0], 0.0, 1e-12);
    CHK_NEAR(out[4], -40.0, 1e-12);
}

static void test_obv_flat(void) {
    /* Flat prices: OBV stays at 0 */
    double c[] = {5,5,5,5,5};
    double v[] = {10,10,10,10,10};
    double out[5];
    CHK(af_obv(c, v, 5, out) == AF_OK);
    for (int i = 0; i < 5; ++i) CHK_NEAR(out[i], 0.0, 1e-12);
}

static void test_obv_known_values(void) {
    /* c: 10, 11, 10, 12, 12 ; v: 100, 200, 150, 300, 50
       OBV: 0, 200, 50, 350, 350 */
    double c[] = {10, 11, 10, 12, 12};
    double v[] = {100, 200, 150, 300, 50};
    double out[5];
    CHK(af_obv(c, v, 5, out) == AF_OK);
    CHK_NEAR(out[0], 0.0, 1e-12);
    CHK_NEAR(out[1], 200.0, 1e-12);
    CHK_NEAR(out[2], 50.0,  1e-12);  /* 200 - 150 */
    CHK_NEAR(out[3], 350.0, 1e-12);  /* 50 + 300 */
    CHK_NEAR(out[4], 350.0, 1e-12);  /* flat */
}

/* ── ADX ───────────────────────────────────────────────────────────────── */
static void test_adx_invalid_args(void) {
    double a[10], p[10], m[10];
    double h[]={2,3,4,5,6,7,8,9,10,11};
    double l[]={1,2,3,4,5,6,7,8,9,10};
    double c[]={1,2,3,4,5,6,7,8,9,10};
    CHK(af_adx(NULL, l, c, 10, 3, a, p, m) == AF_ERR_INVALID_PARAM);
    CHK(af_adx(h, l, c, 10, 0, a, p, m)    == AF_ERR_INVALID_PARAM);
    CHK(af_adx(h, l, c, 10, 3, NULL, p, m) == AF_ERR_INVALID_PARAM);
}

static void test_adx_insufficient_data(void) {
    double a[5], p[5], m[5];
    double h[]={2,3,4,5,6}, l[]={1,2,3,4,5}, c[]={1,2,3,4,5};
    /* period=3 needs n >= 2*3+1=7 */
    CHK(af_adx(h, l, c, 5, 3, a, p, m) == AF_ERR_IO);
}

static void test_adx_bounds(void) {
    /* ADX and DI values should all be in [0, 100] once defined */
    double a[100], p[100], m[100];
    double h[100], l[100], c[100];
    double price = 100.0;
    for (int i = 0; i < 100; ++i) {
        double noise = ((i * 6271 + 3141) % 97) * 0.1;
        h[i] = price + 1.0 + noise;
        l[i] = price - 1.0;
        c[i] = price + noise * 0.3;
        price += ((i % 5 == 0) ? 0.5 : -0.2);
    }
    CHK(af_adx(h, l, c, 100, 14, a, p, m) == AF_OK);
    for (int i = 14; i < 100; ++i) {
        CHK(a[i] >= 0.0 && a[i] <= 100.0);
        CHK(p[i] >= 0.0 && p[i] <= 100.0);
        CHK(m[i] >= 0.0 && m[i] <= 100.0);
    }
}

static void test_adx_nan_prefix(void) {
    /* Indices before `period` must be NaN */
    double a[50], p[50], m[50];
    double h[50], l[50], c[50];
    for (int i = 0; i < 50; ++i) { h[i]=101; l[i]=99; c[i]=100; }
    CHK(af_adx(h, l, c, 50, 7, a, p, m) == AF_OK);
    for (int i = 0; i < 7; ++i) {
        CHK(is_nan(a[i]));
        CHK(is_nan(p[i]));
        CHK(is_nan(m[i]));
    }
    CHK(!is_nan(a[7]));
}

/* ── WMA ───────────────────────────────────────────────────────────────── */
static void test_wma_invalid_args(void) {
    double out[4]; double in[] = {1,2,3,4};
    CHK(af_wma(NULL, 4, 2, out) == AF_ERR_INVALID_PARAM);
    CHK(af_wma(in,   4, 2, NULL) == AF_ERR_INVALID_PARAM);
    CHK(af_wma(in,   4, 0, out) == AF_ERR_INVALID_PARAM);
}

static void test_wma_insufficient_data(void) {
    double out[2]; double in[] = {1,2};
    CHK(af_wma(in, 2, 5, out) == AF_ERR_IO);
    CHK(is_nan(out[0]) && is_nan(out[1]));
}

static void test_wma_period_one_is_identity(void) {
    double out[5]; double in[] = {1,2,3,4,5};
    CHK(af_wma(in, 5, 1, out) == AF_OK);
    for (int i = 0; i < 5; ++i) CHK_NEAR(out[i], in[i], 1e-12);
}

static void test_wma_known_values(void) {
    /* period=3, input={1,2,3,4,5}
       weights: 1,2,3; wsum=6
       out[2] = (1*1 + 2*2 + 3*3)/6 = 14/6 ≈ 2.3333
       out[3] = (1*2 + 2*3 + 3*4)/6 = 20/6 ≈ 3.3333
       out[4] = (1*3 + 2*4 + 3*5)/6 = 26/6 ≈ 4.3333 */
    double out[5]; double in[] = {1,2,3,4,5};
    CHK(af_wma(in, 5, 3, out) == AF_OK);
    CHK(is_nan(out[0]) && is_nan(out[1]));
    CHK_NEAR(out[2], 14.0/6.0, 1e-12);
    CHK_NEAR(out[3], 20.0/6.0, 1e-12);
    CHK_NEAR(out[4], 26.0/6.0, 1e-12);
}

static void test_wma_constant_input(void) {
    /* WMA of constant is that constant */
    double out[10]; double in[10];
    for (int i = 0; i < 10; ++i) in[i] = 7.0;
    CHK(af_wma(in, 10, 4, out) == AF_OK);
    for (int i = 3; i < 10; ++i) CHK_NEAR(out[i], 7.0, 1e-12);
}

/* ── CCI ───────────────────────────────────────────────────────────────── */
static void test_cci_invalid_args(void) {
    double out[5];
    double h[]={1,2,3,4,5}, l[]={0,1,2,3,4}, c[]={1,1,2,3,4};
    CHK(af_cci(NULL, l, c, 5, 3, 0.015, out) == AF_ERR_INVALID_PARAM);
    CHK(af_cci(h, NULL, c, 5, 3, 0.015, out) == AF_ERR_INVALID_PARAM);
    CHK(af_cci(h, l, NULL, 5, 3, 0.015, out) == AF_ERR_INVALID_PARAM);
    CHK(af_cci(h, l, c, 5, 0, 0.015, out)    == AF_ERR_INVALID_PARAM);
    CHK(af_cci(h, l, c, 5, 3, 0.015, NULL)   == AF_ERR_INVALID_PARAM);
}

static void test_cci_insufficient_data(void) {
    double out[2]; double h[]={1,2}, l[]={0,1}, c[]={1,2};
    CHK(af_cci(h, l, c, 2, 5, 0.015, out) == AF_ERR_IO);
    CHK(is_nan(out[0]) && is_nan(out[1]));
}

static void test_cci_known_values(void) {
    /* period=3, constant=0.015, all bars: h=11, l=9, c=10 → TP=10 every bar
       SMA(TP)=10, MAD=0 → CCI=0 */
    double out[5];
    double h[] = {11,11,11,11,11};
    double l[] = {9, 9, 9, 9, 9};
    double c[] = {10,10,10,10,10};
    CHK(af_cci(h, l, c, 5, 3, 0.015, out) == AF_OK);
    CHK(is_nan(out[0]) && is_nan(out[1]));
    for (int i = 2; i < 5; ++i) CHK_NEAR(out[i], 0.0, 1e-10);
}

static void test_cci_direction(void) {
    /* Rising series: TP increases, CCI should be positive */
    double h[] = {10,11,12,13,14,15,16,17,18,19,20};
    double l[] = {8, 9,10,11,12,13,14,15,16,17,18};
    double c[] = {9,10,11,12,13,14,15,16,17,18,19};
    double out[11];
    CHK(af_cci(h, l, c, 11, 5, 0.015, out) == AF_OK);
    /* After warmup, CCI should be positive (current TP > mean over window) */
    for (int i = 4; i < 11; ++i) CHK(out[i] > 0.0);
}

/* ── Williams %R ───────────────────────────────────────────────────────── */
static void test_williams_r_invalid_args(void) {
    double out[5];
    double h[]={1,2,3,4,5}, l[]={0,1,2,3,4}, c[]={1,1,2,3,4};
    CHK(af_williams_r(NULL, l, c, 5, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_williams_r(h, NULL, c, 5, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_williams_r(h, l, NULL, 5, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_williams_r(h, l, c, 5, 0, out)    == AF_ERR_INVALID_PARAM);
    CHK(af_williams_r(h, l, c, 5, 3, NULL)   == AF_ERR_INVALID_PARAM);
}

static void test_williams_r_insufficient_data(void) {
    double out[2]; double h[]={1,2}, l[]={0,1}, c[]={1,2};
    CHK(af_williams_r(h, l, c, 2, 5, out) == AF_ERR_IO);
    CHK(is_nan(out[0]) && is_nan(out[1]));
}

static void test_williams_r_bounds(void) {
    /* %R must be in [-100, 0] at all valid indices */
    double out[50]; double h[50], l[50], c[50];
    for (int i = 0; i < 50; ++i) {
        h[i] = 100.0 + (i % 7) * 2.0;
        l[i] = 90.0  + (i % 5) * 1.0;
        c[i] = 95.0  + (i % 3) * 1.0;
        if (c[i] > h[i]) c[i] = h[i];
        if (c[i] < l[i]) c[i] = l[i];
    }
    CHK(af_williams_r(h, l, c, 50, 14, out) == AF_OK);
    for (int i = 13; i < 50; ++i) CHK(out[i] >= -100.0 && out[i] <= 0.0);
}

static void test_williams_r_known_values(void) {
    /* period=3, 3 identical bars: h=12, l=8, c=12
       highest_high=12, lowest_low=8, range=4
       %R = -100*(12-12)/4 = 0 */
    double h[] = {12,12,12};
    double l[] = {8, 8, 8};
    double c[] = {12,12,12};
    double out[3];
    CHK(af_williams_r(h, l, c, 3, 3, out) == AF_OK);
    CHK(is_nan(out[0]) && is_nan(out[1]));
    CHK_NEAR(out[2], 0.0, 1e-12);
}

static void test_williams_r_at_low(void) {
    /* close == low → %R = -100 */
    double h[] = {12,12,12};
    double l[] = {8, 8, 8};
    double c[] = {8, 8, 8};
    double out[3];
    CHK(af_williams_r(h, l, c, 3, 3, out) == AF_OK);
    CHK_NEAR(out[2], -100.0, 1e-12);
}

/* ── ROC ───────────────────────────────────────────────────────────────── */
static void test_roc_invalid_args(void) {
    double out[4]; double in[] = {1,2,3,4};
    CHK(af_roc(NULL, 4, 2, out) == AF_ERR_INVALID_PARAM);
    CHK(af_roc(in,   4, 2, NULL) == AF_ERR_INVALID_PARAM);
    CHK(af_roc(in,   4, 0, out) == AF_ERR_INVALID_PARAM);
}

static void test_roc_insufficient_data(void) {
    double out[3]; double in[] = {1,2,3};
    CHK(af_roc(in, 3, 3, out) == AF_ERR_IO);
    CHK(is_nan(out[0]) && is_nan(out[1]) && is_nan(out[2]));
}

static void test_roc_known_values(void) {
    /* period=1: ROC[i] = (src[i]-src[i-1])/src[i-1]*100
       input: 10,11,12 → out[1]=(11-10)/10*100=10, out[2]=(12-11)/11*100≈9.0909 */
    double out[3]; double in[] = {10,11,12};
    CHK(af_roc(in, 3, 1, out) == AF_OK);
    CHK(is_nan(out[0]));
    CHK_NEAR(out[1], 10.0, 1e-10);
    CHK_NEAR(out[2], (12.0-11.0)/11.0*100.0, 1e-10);
}

static void test_roc_uptrend(void) {
    /* Strict uptrend: all ROC should be positive */
    double in[20]; double out[20];
    for (int i = 0; i < 20; ++i) in[i] = 100.0 + i * 0.5;
    CHK(af_roc(in, 20, 5, out) == AF_OK);
    for (int i = 5; i < 20; ++i) CHK(out[i] > 0.0);
}

static void test_roc_constant_input(void) {
    /* Constant series → ROC = 0 at all valid indices */
    double in[10]; double out[10];
    for (int i = 0; i < 10; ++i) in[i] = 50.0;
    CHK(af_roc(in, 10, 3, out) == AF_OK);
    for (int i = 3; i < 10; ++i) CHK_NEAR(out[i], 0.0, 1e-12);
}

/* ── MFI ───────────────────────────────────────────────────────────────── */
static void test_mfi_invalid_args(void) {
    double out[5];
    double h[]={1,2,3,4,5}, l[]={0,1,2,3,4}, c[]={1,1,2,3,4}, v[]={1,1,1,1,1};
    CHK(af_mfi(NULL, l, c, v, 5, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_mfi(h, NULL, c, v, 5, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_mfi(h, l, NULL, v, 5, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_mfi(h, l, c, NULL, 5, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_mfi(h, l, c, v, 5, 0, out)    == AF_ERR_INVALID_PARAM);
    CHK(af_mfi(h, l, c, v, 5, 3, NULL)   == AF_ERR_INVALID_PARAM);
}

static void test_mfi_insufficient_data(void) {
    double out[3];
    double h[]={1,2,3}, l[]={0,1,2}, c[]={1,2,3}, v[]={1,1,1};
    CHK(af_mfi(h, l, c, v, 3, 3, out) == AF_ERR_IO);
    CHK(is_nan(out[0]) && is_nan(out[1]) && is_nan(out[2]));
}

static void test_mfi_bounds(void) {
    /* MFI must be in [0, 100] at all valid indices */
    double out[50];
    double h[50], l[50], c[50], v[50];
    for (int i = 0; i < 50; ++i) {
        h[i] = 100.0 + (i % 7) * 2.0;
        l[i] = 90.0  - (i % 5) * 1.0;
        c[i] = 95.0  + (i % 3) * 1.0;
        if (c[i] > h[i]) c[i] = h[i];
        if (c[i] < l[i]) c[i] = l[i];
        v[i] = 1000.0 + i * 10.0;
    }
    CHK(af_mfi(h, l, c, v, 50, 14, out) == AF_OK);
    for (int i = 14; i < 50; ++i) CHK(out[i] >= 0.0 && out[i] <= 100.0);
}

static void test_mfi_all_rising(void) {
    /* Strictly rising TP → all money flow positive → MFI near 100 */
    double h[20], l[20], c[20], v[20];
    double out[20];
    for (int i = 0; i < 20; ++i) {
        h[i] = 100.0 + i * 1.5;
        l[i] = 98.0  + i * 1.5;
        c[i] = 99.0  + i * 1.5;
        v[i] = 1000.0;
    }
    CHK(af_mfi(h, l, c, v, 20, 5, out) == AF_OK);
    /* After the first window (index 5+), MFI should be high */
    for (int i = 6; i < 20; ++i) CHK(out[i] > 90.0);
}

/* ── VWAP ──────────────────────────────────────────────────────────────── */
static void test_vwap_invalid_args(void) {
    double out[3];
    double h[]={1,2,3}, l[]={0,1,2}, c[]={1,2,3}, v[]={1,1,1};
    CHK(af_vwap(NULL, l, c, v, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_vwap(h, NULL, c, v, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_vwap(h, l, NULL, v, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_vwap(h, l, c, NULL, 3, out) == AF_ERR_INVALID_PARAM);
    CHK(af_vwap(h, l, c, v, 3, NULL)   == AF_ERR_INVALID_PARAM);
}

static void test_vwap_known_values(void) {
    /* 3 bars with uniform volume=1:
       bar0: h=12,l=8,c=10 → TP=10; bar1: h=14,l=10,c=12 → TP=12
       bar2: h=16,l=12,c=14 → TP=14
       VWAP[0]=10, VWAP[1]=(10+12)/2=11, VWAP[2]=(10+12+14)/3=12 */
    double h[]={12,14,16}, l[]={8,10,12}, c[]={10,12,14}, v[]={1,1,1};
    double out[3];
    CHK(af_vwap(h, l, c, v, 3, out) == AF_OK);
    CHK_NEAR(out[0], 10.0, 1e-10);
    CHK_NEAR(out[1], 11.0, 1e-10);
    CHK_NEAR(out[2], 12.0, 1e-10);
}

static void test_vwap_between_high_low(void) {
    /* VWAP should be between overall low and overall high */
    double h[30], l[30], c[30], v[30];
    double out[30];
    for (int i = 0; i < 30; ++i) {
        h[i] = 100.0 + (i % 7) * 2.0;
        l[i] = 90.0  + (i % 5) * 1.0;
        c[i] = 95.0  + (i % 3) * 1.0;
        if (c[i] > h[i]) c[i] = h[i];
        if (c[i] < l[i]) c[i] = l[i];
        v[i] = 1000.0 + i * 50.0;
    }
    CHK(af_vwap(h, l, c, v, 30, out) == AF_OK);
    /* Find overall high/low */
    double maxh = h[0], minl = l[0];
    for (int i = 1; i < 30; ++i) {
        if (h[i] > maxh) maxh = h[i];
        if (l[i] < minl) minl = l[i];
    }
    for (int i = 0; i < 30; ++i)
        CHK(out[i] >= minl && out[i] <= maxh);
}

static void test_vwap_constant_input(void) {
    /* Constant prices → VWAP equals that constant */
    double h[10], l[10], c[10], v[10];
    double out[10];
    for (int i = 0; i < 10; ++i) { h[i]=11; l[i]=9; c[i]=10; v[i]=1000; }
    CHK(af_vwap(h, l, c, v, 10, out) == AF_OK);
    for (int i = 0; i < 10; ++i) CHK_NEAR(out[i], 10.0, 1e-10);
}

/* ── Keltner Channels ──────────────────────────────────────────────────── */
static void test_keltner_invalid_args(void) {
    double u[10], m[10], l[10];
    double h[10], lo[10], c[10];
    for (int i = 0; i < 10; ++i) { h[i]=11; lo[i]=9; c[i]=10; }
    CHK(af_keltner(NULL, lo, c, 10, 5, 5, 2.0, u, m, l) == AF_ERR_INVALID_PARAM);
    CHK(af_keltner(h, NULL, c, 10, 5, 5, 2.0, u, m, l) == AF_ERR_INVALID_PARAM);
    CHK(af_keltner(h, lo, NULL, 10, 5, 5, 2.0, u, m, l) == AF_ERR_INVALID_PARAM);
    CHK(af_keltner(h, lo, c, 10, 0, 5, 2.0, u, m, l)    == AF_ERR_INVALID_PARAM);
    CHK(af_keltner(h, lo, c, 10, 5, 0, 2.0, u, m, l)    == AF_ERR_INVALID_PARAM);
    CHK(af_keltner(h, lo, c, 10, 5, 5, 2.0, NULL, m, l) == AF_ERR_INVALID_PARAM);
}

static void test_keltner_ordering(void) {
    /* upper >= middle >= lower at every valid index */
    double u[60], m[60], lo[60];
    double h[60], lp[60], c[60];
    for (int i = 0; i < 60; ++i) {
        double mid = 100.0 + (i % 11) * 0.5;
        h[i] = mid + 1.0; lp[i] = mid - 1.0; c[i] = mid;
    }
    CHK(af_keltner(h, lp, c, 60, 10, 14, 2.0, u, m, lo) == AF_OK);
    for (int i = 0; i < 60; ++i) {
        if (!is_nan(u[i]) && !is_nan(m[i]) && !is_nan(lo[i])) {
            CHK(u[i] >= m[i]);
            CHK(m[i] >= lo[i]);
        }
    }
}

static void test_keltner_constant_input(void) {
    /* Constant prices: ATR stabilises at constant range; channels are symmetric */
    double u[50], m[50], lo[50];
    double h[50], lp[50], c[50];
    for (int i = 0; i < 50; ++i) { h[i]=11; lp[i]=9; c[i]=10; }
    CHK(af_keltner(h, lp, c, 50, 10, 14, 2.0, u, m, lo) == AF_OK);
    /* At index 49, middle should equal 10, and upper/lower should be symmetric */
    CHK_NEAR(m[49], 10.0, 1e-9);
    CHK_NEAR(u[49] - m[49], m[49] - lo[49], 1e-9);
}

static void test_keltner_nan_prefix(void) {
    /* Indices before warmup should be NaN */
    double u[30], m[30], lo[30];
    double h[30], lp[30], c[30];
    for (int i = 0; i < 30; ++i) { h[i]=11; lp[i]=9; c[i]=10; }
    CHK(af_keltner(h, lp, c, 30, 10, 14, 2.0, u, m, lo) == AF_OK);
    /* At least the first ema_period-1=9 indices of middle should be NaN */
    for (int i = 0; i < 9; ++i) CHK(is_nan(m[i]));
    /* And at index 29 should be defined */
    CHK(!is_nan(m[29]));
    CHK(!is_nan(u[29]));
    CHK(!is_nan(lo[29]));
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

    test_macd_invalid_args();
    test_macd_nan_prefix();
    test_macd_histogram_identity();
    test_macd_constant_input();

    test_bollinger_invalid_args();
    test_bollinger_insufficient_data();
    test_bollinger_ordering();
    test_bollinger_constant_input();
    test_bollinger_known_values();

    test_stochastic_invalid_args();
    test_stochastic_insufficient_data();
    test_stochastic_bounds();
    test_stochastic_pure_uptrend();

    test_obv_invalid_args();
    test_obv_uptrend_monotone();
    test_obv_downtrend();
    test_obv_flat();
    test_obv_known_values();

    test_adx_invalid_args();
    test_adx_insufficient_data();
    test_adx_bounds();
    test_adx_nan_prefix();

    test_wma_invalid_args();
    test_wma_insufficient_data();
    test_wma_period_one_is_identity();
    test_wma_known_values();
    test_wma_constant_input();

    test_cci_invalid_args();
    test_cci_insufficient_data();
    test_cci_known_values();
    test_cci_direction();

    test_williams_r_invalid_args();
    test_williams_r_insufficient_data();
    test_williams_r_bounds();
    test_williams_r_known_values();
    test_williams_r_at_low();

    test_roc_invalid_args();
    test_roc_insufficient_data();
    test_roc_known_values();
    test_roc_uptrend();
    test_roc_constant_input();

    test_mfi_invalid_args();
    test_mfi_insufficient_data();
    test_mfi_bounds();
    test_mfi_all_rising();

    test_vwap_invalid_args();
    test_vwap_known_values();
    test_vwap_between_high_low();
    test_vwap_constant_input();

    test_keltner_invalid_args();
    test_keltner_ordering();
    test_keltner_constant_input();
    test_keltner_nan_prefix();

    *total_passed += g_passed;
    *total_failed += g_failed;
}
