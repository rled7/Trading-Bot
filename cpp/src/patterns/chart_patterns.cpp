/**
 * AlgoForge — src/patterns/chart_patterns.cpp
 * Chart, harmonic, and volume pattern registrations.
 */
#include "patterns/pattern_engine.hpp"
#include "patterns/pattern_types.h"
#include <cmath>
#include <algorithm>

namespace af {

static void append(AF_PatternResult &r, const char *name,
                   AF_PatternCategory cat, AF_PatternDirection dir, double conf, int idx) {
    if (r.count >= AF_MAX_PATTERN_MATCHES) return;
    AF_PatternMatch &m = r.matches[r.count++];
    snprintf(m.name, sizeof(m.name), "%s", name);
    m.category=cat; m.direction=dir; m.confidence=conf; m.bar_index=idx;
    if (dir==AF_PAT_DIR_BULLISH) r.bullish_count++;
    else if (dir==AF_PAT_DIR_BEARISH) r.bearish_count++;
}

/* ── Chart pattern helpers ── */
static double max_range(const AF_Bar *b, size_t from, size_t to) {
    double mx = b[from].high;
    for (size_t i=from+1;i<=to;i++) if (b[i].high>mx) mx=b[i].high;
    return mx;
}
static double min_range(const AF_Bar *b, size_t from, size_t to) {
    double mn = b[from].low;
    for (size_t i=from+1;i<=to;i++) if (b[i].low<mn) mn=b[i].low;
    return mn;
}

void register_chart_patterns(PatternEngine *eng) {

    /* Double Bottom */
    eng->reg("DoubleBottom", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 30) return;
        /* Find two swing lows separated by at least 10 bars */
        size_t mid = n/2;
        double lo1 = min_range(b, 0,   mid-1);
        double hi   = max_range(b, 0,   n-1);
        double lo2 = min_range(b, mid, n-1);
        double tol = (lo1+lo2)*0.005;
        if (std::abs(lo1-lo2)<=tol && hi > lo1*1.01 && b[n-1].close > (lo1+hi)/2.0)
            append(r, "DoubleBottom", AF_PAT_CHART, AF_PAT_DIR_BULLISH,
                   0.78 + (1.0 - std::abs(lo1-lo2)/tol)*0.05, (int)n-1);
    });

    /* Double Top */
    eng->reg("DoubleTop", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 30) return;
        size_t mid = n/2;
        double hi1 = max_range(b, 0,   mid-1);
        double lo  = min_range(b, 0,   n-1);
        double hi2 = max_range(b, mid, n-1);
        double tol = (hi1+hi2)*0.005;
        if (std::abs(hi1-hi2)<=tol && lo < hi1*0.99 && b[n-1].close < (hi1+lo)/2.0)
            append(r, "DoubleTop", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.78, (int)n-1);
    });

    /* Ascending Triangle */
    eng->reg("AscendingTriangle", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.75,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 20) return;
        double resistance = max_range(b, n-20, n-1);
        /* Check if lows are rising */
        double lo_first = min_range(b, n-20, n-11);
        double lo_last  = min_range(b, n-10, n-1);
        double hi_var   = (max_range(b,n-20,n-11) - max_range(b,n-10,n-1));
        if (lo_last > lo_first * 1.002 && std::abs(hi_var) < resistance * 0.005)
            append(r, "AscendingTriangle", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.75, (int)n-1);
    });

    /* Descending Triangle */
    eng->reg("DescendingTriangle", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.75,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 20) return;
        double support = min_range(b, n-20, n-1);
        double hi_first = max_range(b, n-20, n-11);
        double hi_last  = max_range(b, n-10, n-1);
        double lo_var   = min_range(b,n-20,n-11) - min_range(b,n-10,n-1);
        if (hi_last < hi_first * 0.998 && std::abs(lo_var) < support * 0.005)
            append(r, "DescendingTriangle", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.75, (int)n-1);
    });

    /* Bullish Flag */
    eng->reg("BullishFlag", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.76,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 20) return;
        /* Pole: strong move in first half */
        double pole_lo  = min_range(b, n-20, n-11);
        double pole_hi  = max_range(b, n-20, n-11);
        double pole_rng = pole_hi - pole_lo;
        /* Flag: consolidation in second half with slight downward drift */
        double flag_hi  = max_range(b, n-10, n-1);
        double flag_lo  = min_range(b, n-10, n-1);
        double flag_rng = flag_hi - flag_lo;
        bool pole_bull  = b[n-11].close > b[n-20].close;
        bool tight_flag = flag_rng < pole_rng * 0.45;
        if (pole_bull && tight_flag && pole_rng > pole_lo * 0.01)
            append(r, "BullishFlag", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.76, (int)n-1);
    });

    /* Bearish Flag */
    eng->reg("BearishFlag", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.76,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 20) return;
        double pole_lo  = min_range(b, n-20, n-11);
        double pole_hi  = max_range(b, n-20, n-11);
        double pole_rng = pole_hi - pole_lo;
        double flag_rng = max_range(b,n-10,n-1) - min_range(b,n-10,n-1);
        bool pole_bear  = b[n-11].close < b[n-20].close;
        bool tight_flag = flag_rng < pole_rng * 0.45;
        if (pole_bear && tight_flag && pole_rng > pole_lo * 0.01)
            append(r, "BearishFlag", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.76, (int)n-1);
    });

    /* Head and Shoulders */
    eng->reg("HeadAndShoulders", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.82,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 40) return;
        size_t s = n-40;
        double ls = max_range(b, s,    s+12);
        double h  = max_range(b, s+13, s+26);
        double rs = max_range(b, s+27, n-1);
        double neckline = std::min(min_range(b,s,s+12), min_range(b,s+27,n-1));
        if (h > ls*1.01 && h > rs*1.01 &&
            std::abs(ls-rs)/(ls+rs)*2 < 0.07 &&
            b[n-1].close < neckline)
            append(r, "HeadAndShoulders", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.82, (int)n-1);
    });

    /* Inverse H&S */
    eng->reg("InverseHeadAndShoulders", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.82,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 40) return;
        size_t s = n-40;
        double ls = min_range(b, s,    s+12);
        double h  = min_range(b, s+13, s+26);
        double rs = min_range(b, s+27, n-1);
        double neckline = std::max(max_range(b,s,s+12), max_range(b,s+27,n-1));
        if (h < ls*0.99 && h < rs*0.99 &&
            std::abs(ls-rs)/(ls+rs)*2 < 0.07 &&
            b[n-1].close > neckline)
            append(r, "InvHeadAndShoulders", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.82, (int)n-1);
    });

    /* Cup and Handle */
    eng->reg("CupAndHandle", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.80,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 50) return;
        double cup_hi_l = max_range(b, n-50, n-40);
        double cup_lo   = min_range(b, n-40, n-15);
        double cup_hi_r = max_range(b, n-15, n-8);
        double handle_lo= min_range(b, n-8,  n-1);
        double tol = cup_hi_l * 0.02;
        bool sym_cup   = std::abs(cup_hi_l - cup_hi_r) < tol;
        bool handle_ok = handle_lo > cup_lo && handle_lo < cup_hi_r * 0.97;
        if (sym_cup && handle_ok)
            append(r, "CupAndHandle", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.80, (int)n-1);
    });

    /* Wedge Rising (bearish) */
    eng->reg("RisingWedge", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.73,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 20) return;
        double hi_e = max_range(b,n-20,n-11), hi_l = max_range(b,n-10,n-1);
        double lo_e = min_range(b,n-20,n-11), lo_l = min_range(b,n-10,n-1);
        bool highs_up = hi_l > hi_e;
        bool lows_up  = lo_l > lo_e;
        bool converge = (hi_l-lo_l) < (hi_e-lo_e)*0.85;
        if (highs_up && lows_up && converge)
            append(r, "RisingWedge", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.73, (int)n-1);
    });

    /* Wedge Falling (bullish) */
    eng->reg("FallingWedge", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.73,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 20) return;
        double hi_e = max_range(b,n-20,n-11), hi_l = max_range(b,n-10,n-1);
        double lo_e = min_range(b,n-20,n-11), lo_l = min_range(b,n-10,n-1);
        bool highs_dn = hi_l < hi_e;
        bool lows_dn  = lo_l < lo_e;
        bool converge = (hi_l-lo_l) < (hi_e-lo_e)*0.85;
        if (highs_dn && lows_dn && converge)
            append(r, "FallingWedge", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.73, (int)n-1);
    });
}

/* ── Harmonic patterns ── */
void register_harmonic_patterns(PatternEngine *eng) {

    /* Gartley .618 */
    eng->reg("GartleyBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.80,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 50) return;
        /* X A B C D — simplified swing detection */
        double x  = min_range(b, n-50, n-40);
        double a  = max_range(b, n-40, n-30);
        double bb = min_range(b, n-30, n-20);
        double cc = max_range(b, n-20, n-10);
        double d  = min_range(b, n-10, n-1);
        double xa = a - x, ab = a - bb;
        double bc = cc - bb, cd = cc - d;
        /* Gartley ratios: AB=0.618*XA, CD=1.272*BC or 1.618*BC */
        if (xa > 1e-6 && bc > 1e-6 &&
            af_fib_ratio(xa, ab, 0.618, 0.05) &&
            (af_fib_ratio(bc, cd, 1.272, 0.07) || af_fib_ratio(bc, cd, 1.618, 0.07)))
            append(r, "GartleyBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.80, (int)n-1);
    });

    eng->reg("GartleyBear", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.80,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 50) return;
        double x  = max_range(b, n-50, n-40);
        double a  = min_range(b, n-40, n-30);
        double bb = max_range(b, n-30, n-20);
        double cc = min_range(b, n-20, n-10);
        double d  = max_range(b, n-10, n-1);
        double xa = x - a, ab = bb - a;
        double bc = bb - cc, cd = d - cc;
        if (xa > 1e-6 && bc > 1e-6 &&
            af_fib_ratio(xa, ab, 0.618, 0.05) &&
            (af_fib_ratio(bc, cd, 1.272, 0.07) || af_fib_ratio(bc, cd, 1.618, 0.07)))
            append(r, "GartleyBear", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.80, (int)n-1);
    });

    /* Bat 0.886 */
    eng->reg("BatBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.82,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 50) return;
        double x  = min_range(b,n-50,n-40), a=max_range(b,n-40,n-30);
        double bb = min_range(b,n-30,n-20), d=min_range(b,n-10,n-1);
        double xa = a-x, xd = a-d;
        if (xa > 1e-6 && af_fib_ratio(xa, xd, 0.886, 0.05))
            append(r, "BatBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.82, (int)n-1);
        (void)bb;
    });

    /* Butterfly 1.272 */
    eng->reg("ButterflyBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.83,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 50) return;
        double x=min_range(b,n-50,n-40), a=max_range(b,n-40,n-30);
        double d=min_range(b,n-10,n-1);
        double xa=a-x, xd=a-d;
        if (xa > 1e-6 && af_fib_ratio(xa, xd, 1.272, 0.07))
            append(r, "ButterflyBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.83, (int)n-1);
    });

    /* Crab 1.618 */
    eng->reg("CrabBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.84,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 50) return;
        double x=min_range(b,n-50,n-40), a=max_range(b,n-40,n-30);
        double d=min_range(b,n-10,n-1);
        double xa=a-x, xd=a-d;
        if (xa > 1e-6 && af_fib_ratio(xa, xd, 1.618, 0.07))
            append(r, "CrabBull", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.84, (int)n-1);
    });
}

/* ── Volume patterns ── */
void register_volume_patterns(PatternEngine *eng) {

    eng->reg("VolumeClimax", AF_PAT_CHART, AF_PAT_DIR_NEUTRAL, 0.72,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 20) return;
        double avg_v=0;
        for (int i=1;i<=19;i++) avg_v+=b[n-1-i].volume;
        avg_v/=19.0;
        double cur_v = b[n-1].volume;
        if (cur_v > avg_v * 2.5) {
            auto dir = b[n-1].is_bullish ? AF_PAT_DIR_BULLISH : AF_PAT_DIR_BEARISH;
            append(r, "VolumeClimax", AF_PAT_CHART, dir, 0.72, (int)n-1);
        }
    });

    eng->reg("VolumeDryUp", AF_PAT_CHART, AF_PAT_DIR_NEUTRAL, 0.65,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 11) return;
        double avg_v=0;
        for (int i=1;i<=10;i++) avg_v+=b[n-1-i].volume;
        avg_v/=10.0;
        if (b[n-1].volume < avg_v * 0.35)
            append(r, "VolumeDryUp", AF_PAT_CHART, AF_PAT_DIR_NEUTRAL, 0.65, (int)n-1);
    });

    eng->reg("BullishVolumeSurge", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.74,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 10) return;
        double avg_v=0;
        for (int i=1;i<=9;i++) avg_v+=b[n-1-i].volume;
        avg_v/=9.0;
        if (b[n-1].is_bullish && b[n-1].volume > avg_v * 1.8 && b[n-1].body_pct > 0.60)
            append(r, "BullishVolumeSurge", AF_PAT_CHART, AF_PAT_DIR_BULLISH, 0.74, (int)n-1);
    });

    eng->reg("BearishVolumeSurge", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.74,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 10) return;
        double avg_v=0;
        for (int i=1;i<=9;i++) avg_v+=b[n-1-i].volume;
        avg_v/=9.0;
        if (!b[n-1].is_bullish && b[n-1].volume > avg_v * 1.8 && b[n-1].body_pct > 0.60)
            append(r, "BearishVolumeSurge", AF_PAT_CHART, AF_PAT_DIR_BEARISH, 0.74, (int)n-1);
    });
}

} /* namespace af */
