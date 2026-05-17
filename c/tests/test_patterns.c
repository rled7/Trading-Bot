#include <stdio.h>
#include <string.h>
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

/* ── Morning Star ──────────────────────────────────────────────────── */
static void test_morning_star_match(void) {
    /* b0: bearish, big body: o=10, h=10.2, l=6, c=6.5 → body=3.5, range=4.2, body/range=0.83 > 0.60 ✓
       b1: small body relative to b0: o=6.3, h=6.4, l=6.0, c=6.1 → body=0.2, b0.body=3.5, 0.2<3.5*0.30=1.05 ✓
       b2: bullish, close > b0 body midpoint = (10+6.5)/2=8.25: o=6.5, h=9.0, l=6.4, c=9.0 → close=9.0 > 8.25 ✓ */
    CHK(af_is_morning_star(10.0,10.2,6.0,6.5,
                            6.3,6.4,6.0,6.1,
                            6.5,9.0,6.4,9.0) == 1);
}

static void test_morning_star_b0_small_body(void) {
    /* b0 body too small: o=10, h=10.5, l=6, c=8 → body=2, range=4.5, ratio=0.44 < 0.60 → no match */
    CHK(af_is_morning_star(10.0,10.5,6.0,8.0,
                            6.3,6.4,6.0,6.1,
                            6.5,9.0,6.4,9.0) == 0);
}

static void test_morning_star_b1_large_body(void) {
    /* b1 body too large: b1.body=2.0, b0.body=3.5, 2.0 >= 3.5*0.30=1.05 → no match */
    CHK(af_is_morning_star(10.0,10.2,6.0,6.5,
                            6.0,7.5,5.5,8.0,
                            8.5,9.5,8.4,9.5) == 0);
}

static void test_morning_star_b2_not_bullish(void) {
    /* b2 bearish → no match */
    CHK(af_is_morning_star(10.0,10.2,6.0,6.5,
                            6.3,6.4,6.0,6.1,
                            9.0,9.2,6.4,6.0) == 0);
}

static void test_morning_star_b2_close_low(void) {
    /* b2 bullish but closes below b0 midpoint → no match */
    CHK(af_is_morning_star(10.0,10.2,6.0,6.5,
                            6.3,6.4,6.0,6.1,
                            6.5,7.0,6.4,7.0) == 0);
}

static void test_morning_star_zero_range(void) {
    /* b0 has zero range → no match */
    CHK(af_is_morning_star(5.0,5.0,5.0,5.0,
                            6.3,6.4,6.0,6.1,
                            6.5,9.0,6.4,9.0) == 0);
}

/* ── Evening Star ──────────────────────────────────────────────────── */
static void test_evening_star_match(void) {
    /* mirror of morning star: b0 bullish big body, b1 small, b2 bearish close below b0 mid */
    /* b0: o=6, h=10.2, l=5.8, c=9.5 → body=3.5, range=4.4, body/range=0.80 > 0.60 ✓
       b1: o=9.7, h=9.9, l=9.4, c=9.6 → body=0.1, b0.body=3.5, 0.1<1.05 ✓
       b0 mid=(6+9.5)/2=7.75; b2: o=9.5, h=9.6, l=6.0, c=7.0, bearish, c<7.75 ✓ */
    CHK(af_is_evening_star(6.0,10.2,5.8,9.5,
                            9.7,9.9,9.4,9.6,
                            9.5,9.6,6.0,7.0) == -1);
}

static void test_evening_star_b0_bearish(void) {
    /* b0 bearish → no match for evening star */
    CHK(af_is_evening_star(9.5,10.2,5.8,6.0,
                            9.7,9.9,9.4,9.6,
                            9.5,9.6,6.0,7.0) == 0);
}

static void test_evening_star_b2_bullish(void) {
    /* b2 bullish → no match */
    CHK(af_is_evening_star(6.0,10.2,5.8,9.5,
                            9.7,9.9,9.4,9.6,
                            7.0,9.0,6.8,8.5) == 0);
}

static void test_evening_star_zero_range(void) {
    CHK(af_is_evening_star(5.0,5.0,5.0,5.0,
                            5.0,5.1,4.9,5.0,
                            5.0,5.0,4.5,4.5) == 0);
}

/* ── Three White Soldiers ──────────────────────────────────────────── */
/* cpp/ rule: each bar bullish, body_pct >= 0.60, upper_shadow < body*0.30,
   closes strictly rising. (open-within-prev is NOT a cpp/ requirement.) */
static void test_three_white_soldiers_match(void) {
    /* Each bar: body=2, range≈2.1, upper_shadow=0.05 → all conditions met. */
    CHK(af_is_three_white_soldiers(10.0,12.05, 9.95,12.0,
                                    11.0,13.05,10.95,13.0,
                                    12.0,14.05,11.95,14.0) == 1);
}

static void test_three_white_soldiers_bearish_bar(void) {
    /* Middle bar is bearish (c < o) */
    CHK(af_is_three_white_soldiers(10.0,12.05, 9.95,12.0,
                                    13.0,13.05, 10.95,11.0,   /* bearish */
                                    12.0,14.05,11.95,14.0) == 0);
}

static void test_three_white_soldiers_body_too_small(void) {
    /* b1 has body_pct = 2/20 = 0.10 < 0.60 → no match */
    CHK(af_is_three_white_soldiers(10.0,12.05, 9.95,12.0,
                                    11.0,21.0, 1.0, 13.0,    /* tiny body in huge range */
                                    12.0,14.05,11.95,14.0) == 0);
}

static void test_three_white_soldiers_close_not_higher(void) {
    /* b1.close == b0.close → no match */
    CHK(af_is_three_white_soldiers(10.0,12.05, 9.95,12.0,
                                    11.0,12.05,10.95,12.0,
                                    12.0,13.05,11.95,13.0) == 0);
}

/* ── Three Black Crows ─────────────────────────────────────────────── */
static void test_three_black_crows_match(void) {
    /* Each bar: body=2, range≈2.1, lower_shadow=0.05 → all conditions met. */
    CHK(af_is_three_black_crows(14.0,14.05,11.95,12.0,
                                  13.0,13.05,10.95,11.0,
                                  12.0,12.05, 9.95,10.0) == -1);
}

static void test_three_black_crows_bullish_bar(void) {
    /* Last bar is bullish */
    CHK(af_is_three_black_crows(14.0,14.05,11.95,12.0,
                                  13.0,13.05,10.95,11.0,
                                  10.5,11.55,10.45,11.5) == 0);
}

static void test_three_black_crows_body_too_small(void) {
    /* b1 body_pct = 2/20 = 0.10 < 0.60 → no match */
    CHK(af_is_three_black_crows(14.0,14.05,11.95,12.0,
                                  13.0,23.0, 3.0, 11.0,
                                  12.0,12.05, 9.95,10.0) == 0);
}

static void test_three_black_crows_close_not_lower(void) {
    /* b1.close == b0.close → no match */
    CHK(af_is_three_black_crows(14.0,14.05,11.95,12.0,
                                  13.0,13.05,11.95,12.0,
                                  12.0,12.05, 9.95,10.0) == 0);
}

/* ── Scan with three-bar patterns ──────────────────────────────────── */
static void test_scan_three_bar_morning_star(void) {
    af_bar_t bars[3];
    /* b0: bearish large body */
    bars[0].open=10.0; bars[0].high=10.2; bars[0].low=6.0; bars[0].close=6.5;
    /* b1: small body */
    bars[1].open=6.3;  bars[1].high=6.4;  bars[1].low=6.0;  bars[1].close=6.1;
    /* b2: bullish, close above b0 midpoint=(10+6.5)/2=8.25 */
    bars[2].open=6.5;  bars[2].high=9.0;  bars[2].low=6.4;  bars[2].close=9.0;

    af_pattern_match_t out[16];
    size_t n = af_scan_patterns(bars, 3, out, 16);
    int saw_ms = 0;
    for (size_t i = 0; i < n && i < 16; ++i) {
        if (out[i].bar_index == 2 && out[i].name[0] == 'm' && out[i].name[1] == 'o')
            saw_ms = 1;
    }
    CHK(saw_ms);
}

static void test_scan_three_bar_three_white_soldiers(void) {
    af_bar_t bars[3];
    bars[0].open=10; bars[0].high=12.1; bars[0].low=9.9; bars[0].close=12;
    bars[1].open=11; bars[1].high=13.1; bars[1].low=10.9; bars[1].close=13;
    bars[2].open=12; bars[2].high=14.1; bars[2].low=11.9; bars[2].close=14;

    af_pattern_match_t out[16];
    size_t n = af_scan_patterns(bars, 3, out, 16);
    int saw_tws = 0;
    for (size_t i = 0; i < n && i < 16; ++i) {
        if (out[i].bar_index == 2 &&
            strcmp(out[i].name, "three_white_soldiers") == 0)
            saw_tws = 1;
    }
    CHK(saw_tws);
}

/* ── Chart patterns ─────────────────────────────────────────────────── */
/* Helper: build a flat bar with given OHL = price ± 0.5, close = price. */
static void mk_flat(af_bar_t *b, double price) {
    b->open = b->close = price;
    b->high = price + 0.5;
    b->low  = price - 0.5;
    b->volume = 1; b->spread = 0; b->timestamp = 0;
}

static void test_double_top_positive(void) {
    /* 30 bars: first half rises to peak ~110, dips, second half rises to ~110 again,
       final close drops below midpoint of (110 + low). */
    af_bar_t bars[30] = {0};
    for (int i = 0; i < 15; ++i) mk_flat(&bars[i], 100 + i * 0.7);   /* 100 → ~110 */
    for (int i = 15; i < 25; ++i) mk_flat(&bars[i], 100 + (i - 15) * 1.0); /* 100 → 110 */
    /* Tail bars: drop close below midpoint */
    for (int i = 25; i < 30; ++i) mk_flat(&bars[i], 95);
    CHK(af_is_double_top(bars, 30) == -1);
}

static void test_double_top_below_min_bars(void) {
    af_bar_t bars[20] = {0};
    for (int i = 0; i < 20; ++i) mk_flat(&bars[i], 100);
    CHK(af_is_double_top(bars, 20) == 0);   /* needs >= 30 */
}

static void test_double_bottom_positive(void) {
    /* Mirror of double top: dips to ~90, peaks ~100, dips to ~90 again, close > midpoint. */
    af_bar_t bars[30] = {0};
    for (int i = 0; i < 15; ++i) mk_flat(&bars[i], 100 - i * 0.7);   /* 100 → ~90 */
    for (int i = 15; i < 25; ++i) mk_flat(&bars[i], 100 - (i - 15) * 1.0); /* 100 → 90 */
    for (int i = 25; i < 30; ++i) mk_flat(&bars[i], 105);
    CHK(af_is_double_bottom(bars, 30) == 1);
}

static void test_ascending_triangle_positive(void) {
    /* Last 20 bars: flat resistance ~110, rising lows (95 → 100). */
    af_bar_t bars[20] = {0};
    for (int i = 0; i < 10; ++i) {
        bars[i].open = 100; bars[i].close = 105;
        bars[i].high = 110; bars[i].low = 95;
        bars[i].volume = 1;
    }
    for (int i = 10; i < 20; ++i) {
        bars[i].open = 102; bars[i].close = 107;
        bars[i].high = 110; bars[i].low = 100;   /* lows rose 95 → 100 */
        bars[i].volume = 1;
    }
    CHK(af_is_ascending_triangle(bars, 20) == 1);
}

static void test_descending_triangle_positive(void) {
    /* Last 20 bars: flat support ~90, falling highs (110 → 105). */
    af_bar_t bars[20] = {0};
    for (int i = 0; i < 10; ++i) {
        bars[i].open = 100; bars[i].close = 95;
        bars[i].high = 110; bars[i].low = 90;
        bars[i].volume = 1;
    }
    for (int i = 10; i < 20; ++i) {
        bars[i].open = 100; bars[i].close = 95;
        bars[i].high = 105; bars[i].low = 90;    /* highs fell 110 → 105 */
        bars[i].volume = 1;
    }
    CHK(af_is_descending_triangle(bars, 20) == -1);
}

/* ── Head and Shoulders ─────────────────────────────────────────────── */
static void mk_hs_bars(af_bar_t *bars, size_t n,
                       double ls_hi, double head_hi, double rs_hi,
                       double valley_lo, double close_last)
{
    /* Fill 40 bars: segments [0..12]=left shoulder, [13..26]=head, [27..39]=right.
       For each segment set high to the peak, low to valley_lo, close = 0.5*(hi+lo). */
    for (size_t i = 0; i < n; ++i) {
        double hi;
        if (i <= 12)      hi = ls_hi;
        else if (i <= 26) hi = head_hi;
        else              hi = rs_hi;
        bars[i].high   = hi;
        bars[i].low    = valley_lo;
        bars[i].open   = (hi + valley_lo) / 2.0;
        bars[i].close  = (hi + valley_lo) / 2.0;
        bars[i].volume = 1;
        bars[i].spread = 0;
        bars[i].timestamp = (int64_t)i;
    }
    /* Set the final bar's close to the requested value. */
    bars[n - 1].close = close_last;
}

static void test_head_and_shoulders_positive(void) {
    /* ls=100, head=110, rs=100, valley=80, close=75 < neckline(=80).
       h > ls*1.01=101 ✓, h > rs*1.01=101 ✓, sym=(100-100)/(200)*2=0 < 0.07 ✓,
       close=75 < neckline=80 ✓ → returns -1. */
    af_bar_t bars[40] = {0};
    mk_hs_bars(bars, 40, 100.0, 110.0, 100.0, 80.0, 75.0);
    CHK(af_is_head_and_shoulders(bars, 40) == -1);
}

static void test_head_and_shoulders_below_min_bars(void) {
    af_bar_t bars[30] = {0};
    for (int i = 0; i < 30; ++i) mk_flat(&bars[i], 100.0);
    CHK(af_is_head_and_shoulders(bars, 30) == 0);
}

static void test_head_and_shoulders_head_not_higher(void) {
    /* head = ls = rs (symmetric but head not higher than shoulders). */
    af_bar_t bars[40] = {0};
    mk_hs_bars(bars, 40, 100.0, 100.0, 100.0, 80.0, 75.0);
    CHK(af_is_head_and_shoulders(bars, 40) == 0);
}

/* ── Inverse Head and Shoulders ─────────────────────────────────────── */
static void mk_ihs_bars(af_bar_t *bars, size_t n,
                        double ls_lo, double head_lo, double rs_lo,
                        double peak_hi, double close_last)
{
    for (size_t i = 0; i < n; ++i) {
        double lo;
        if (i <= 12)      lo = ls_lo;
        else if (i <= 26) lo = head_lo;
        else              lo = rs_lo;
        bars[i].low    = lo;
        bars[i].high   = peak_hi;
        bars[i].open   = (peak_hi + lo) / 2.0;
        bars[i].close  = (peak_hi + lo) / 2.0;
        bars[i].volume = 1;
        bars[i].spread = 0;
        bars[i].timestamp = (int64_t)i;
    }
    bars[n - 1].close = close_last;
}

static void test_inverse_head_and_shoulders_positive(void) {
    /* ls=100, head=90, rs=100, peak=120, close=122 > neckline(=120).
       h=90 < ls*0.99=99 ✓, h < rs*0.99=99 ✓, sym=0 < 0.07 ✓,
       close=122 > neckline=120 ✓ → returns +1. */
    af_bar_t bars[40] = {0};
    mk_ihs_bars(bars, 40, 100.0, 90.0, 100.0, 120.0, 122.0);
    CHK(af_is_inverse_head_and_shoulders(bars, 40) == 1);
}

static void test_inverse_head_and_shoulders_below_min_bars(void) {
    af_bar_t bars[30] = {0};
    for (int i = 0; i < 30; ++i) mk_flat(&bars[i], 100.0);
    CHK(af_is_inverse_head_and_shoulders(bars, 30) == 0);
}

static void test_inverse_head_and_shoulders_head_not_lower(void) {
    /* head == ls == rs (not lower). */
    af_bar_t bars[40] = {0};
    mk_ihs_bars(bars, 40, 100.0, 100.0, 100.0, 120.0, 122.0);
    CHK(af_is_inverse_head_and_shoulders(bars, 40) == 0);
}

/* ── Bullish Flag ────────────────────────────────────────────────────── */
static void test_bullish_flag_positive(void) {
    /* 20 bars: pole (first 10): strong bullish move; flag (last 10): tight range. */
    af_bar_t bars[20] = {0};
    /* Pole: rising from 100 to 120 (range ~20). */
    for (int i = 0; i < 10; ++i) {
        double p = 100.0 + i * 2.0;
        bars[i].open  = p;
        bars[i].close = p + 1.8;
        bars[i].high  = p + 2.0;
        bars[i].low   = p - 0.2;
        bars[i].volume = 1;
    }
    /* Flag: tight consolidation ~118–120 (range ~2 < 20*0.45=9). */
    for (int i = 10; i < 20; ++i) {
        bars[i].open  = 119.0;
        bars[i].close = 119.5;
        bars[i].high  = 120.0;
        bars[i].low   = 118.0;
        bars[i].volume = 1;
    }
    CHK(af_is_bullish_flag(bars, 20) == 1);
}

static void test_bullish_flag_below_min_bars(void) {
    af_bar_t bars[15] = {0};
    for (int i = 0; i < 15; ++i) mk_flat(&bars[i], 100.0);
    CHK(af_is_bullish_flag(bars, 15) == 0);
}

static void test_bullish_flag_no_match_bearish_pole(void) {
    /* Bearish pole: close of bar n-11 < open → pole_bull = false. */
    af_bar_t bars[20] = {0};
    for (int i = 0; i < 10; ++i) {
        double p = 120.0 - i * 2.0;   /* falling */
        bars[i].open  = p;
        bars[i].close = p - 1.8;
        bars[i].high  = p + 0.2;
        bars[i].low   = p - 2.0;
        bars[i].volume = 1;
    }
    for (int i = 10; i < 20; ++i) {
        bars[i].open = 100.0; bars[i].close = 100.5;
        bars[i].high = 101.0; bars[i].low   = 99.0;
        bars[i].volume = 1;
    }
    CHK(af_is_bullish_flag(bars, 20) == 0);
}

/* ── Bearish Flag ────────────────────────────────────────────────────── */
static void test_bearish_flag_positive(void) {
    /* 20 bars: pole (first 10): strong bearish move; flag (last 10): tight range. */
    af_bar_t bars[20] = {0};
    /* Pole: falling from 120 to 100 (range ~20). */
    for (int i = 0; i < 10; ++i) {
        double p = 120.0 - i * 2.0;
        bars[i].open  = p;
        bars[i].close = p - 1.8;
        bars[i].high  = p + 0.2;
        bars[i].low   = p - 2.0;
        bars[i].volume = 1;
    }
    /* Flag: tight consolidation ~100–102 (range ~2 < 20*0.45=9). */
    for (int i = 10; i < 20; ++i) {
        bars[i].open  = 101.0;
        bars[i].close = 100.5;
        bars[i].high  = 102.0;
        bars[i].low   = 100.0;
        bars[i].volume = 1;
    }
    CHK(af_is_bearish_flag(bars, 20) == -1);
}

static void test_bearish_flag_below_min_bars(void) {
    af_bar_t bars[15] = {0};
    for (int i = 0; i < 15; ++i) mk_flat(&bars[i], 100.0);
    CHK(af_is_bearish_flag(bars, 15) == 0);
}

static void test_bearish_flag_no_match_bullish_pole(void) {
    /* Bullish pole: close of bar n-11 > close of bar n-20 → pole_bear = false. */
    af_bar_t bars[20] = {0};
    for (int i = 0; i < 10; ++i) {
        double p = 100.0 + i * 2.0;   /* rising */
        bars[i].open  = p;
        bars[i].close = p + 1.8;
        bars[i].high  = p + 2.0;
        bars[i].low   = p - 0.2;
        bars[i].volume = 1;
    }
    for (int i = 10; i < 20; ++i) {
        bars[i].open = 118.0; bars[i].close = 118.5;
        bars[i].high = 119.0; bars[i].low   = 117.0;
        bars[i].volume = 1;
    }
    CHK(af_is_bearish_flag(bars, 20) == 0);
}

/* ── Scan with new chart patterns ────────────────────────────────────── */
static void test_scan_head_and_shoulders(void) {
    /* Verify scan_patterns emits head_and_shoulders at bar n-1.
       Use a large output buffer so chart patterns aren't crowded out. */
    af_bar_t bars[40] = {0};
    mk_hs_bars(bars, 40, 100.0, 110.0, 100.0, 80.0, 75.0);
    af_pattern_match_t out[64];
    size_t n = af_scan_patterns(bars, 40, out, 64);
    int saw = 0;
    for (size_t i = 0; i < n && i < 64; ++i)
        if (out[i].bar_index == 39 && strcmp(out[i].name, "head_and_shoulders") == 0)
            saw = 1;
    CHK(saw);
}

static void test_scan_inverse_head_and_shoulders(void) {
    af_bar_t bars[40] = {0};
    mk_ihs_bars(bars, 40, 100.0, 90.0, 100.0, 120.0, 122.0);
    af_pattern_match_t out[64];
    size_t n = af_scan_patterns(bars, 40, out, 64);
    int saw = 0;
    for (size_t i = 0; i < n && i < 64; ++i)
        if (out[i].bar_index == 39 && strcmp(out[i].name, "inverse_head_and_shoulders") == 0)
            saw = 1;
    CHK(saw);
}

static void test_scan_bullish_flag(void) {
    af_bar_t bars[20] = {0};
    for (int i = 0; i < 10; ++i) {
        double p = 100.0 + i * 2.0;
        bars[i].open=p; bars[i].close=p+1.8; bars[i].high=p+2.0; bars[i].low=p-0.2; bars[i].volume=1;
    }
    for (int i = 10; i < 20; ++i) {
        bars[i].open=119.0; bars[i].close=119.5; bars[i].high=120.0; bars[i].low=118.0; bars[i].volume=1;
    }
    af_pattern_match_t out[64];
    size_t n = af_scan_patterns(bars, 20, out, 64);
    int saw = 0;
    for (size_t i = 0; i < n && i < 64; ++i)
        if (out[i].bar_index == 19 && strcmp(out[i].name, "bullish_flag") == 0)
            saw = 1;
    CHK(saw);
}

static void test_scan_bearish_flag(void) {
    af_bar_t bars[20] = {0};
    for (int i = 0; i < 10; ++i) {
        double p = 120.0 - i * 2.0;
        bars[i].open=p; bars[i].close=p-1.8; bars[i].high=p+0.2; bars[i].low=p-2.0; bars[i].volume=1;
    }
    for (int i = 10; i < 20; ++i) {
        bars[i].open=101.0; bars[i].close=100.5; bars[i].high=102.0; bars[i].low=100.0; bars[i].volume=1;
    }
    af_pattern_match_t out[64];
    size_t n = af_scan_patterns(bars, 20, out, 64);
    int saw = 0;
    for (size_t i = 0; i < n && i < 64; ++i)
        if (out[i].bar_index == 19 && strcmp(out[i].name, "bearish_flag") == 0)
            saw = 1;
    CHK(saw);
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

    test_morning_star_match();
    test_morning_star_b0_small_body();
    test_morning_star_b1_large_body();
    test_morning_star_b2_not_bullish();
    test_morning_star_b2_close_low();
    test_morning_star_zero_range();

    test_evening_star_match();
    test_evening_star_b0_bearish();
    test_evening_star_b2_bullish();
    test_evening_star_zero_range();

    test_three_white_soldiers_match();
    test_three_white_soldiers_bearish_bar();
    test_three_white_soldiers_body_too_small();
    test_three_white_soldiers_close_not_higher();

    test_three_black_crows_match();
    test_three_black_crows_bullish_bar();
    test_three_black_crows_body_too_small();
    test_three_black_crows_close_not_lower();

    test_scan_three_bar_morning_star();
    test_scan_three_bar_three_white_soldiers();

    test_double_top_positive();
    test_double_top_below_min_bars();
    test_double_bottom_positive();
    test_ascending_triangle_positive();
    test_descending_triangle_positive();

    test_head_and_shoulders_positive();
    test_head_and_shoulders_below_min_bars();
    test_head_and_shoulders_head_not_higher();

    test_inverse_head_and_shoulders_positive();
    test_inverse_head_and_shoulders_below_min_bars();
    test_inverse_head_and_shoulders_head_not_lower();

    test_bullish_flag_positive();
    test_bullish_flag_below_min_bars();
    test_bullish_flag_no_match_bearish_pole();

    test_bearish_flag_positive();
    test_bearish_flag_below_min_bars();
    test_bearish_flag_no_match_bullish_pole();

    test_scan_head_and_shoulders();
    test_scan_inverse_head_and_shoulders();
    test_scan_bullish_flag();
    test_scan_bearish_flag();

    *total_passed += g_passed;
    *total_failed += g_failed;
}
