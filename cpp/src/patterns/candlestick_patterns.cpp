/**
 * AlgoForge — src/patterns/candlestick_patterns.cpp
 * Registration of all candlestick pattern detectors.
 * Called by PatternEngine::register_all().
 */
#include "patterns/pattern_engine.hpp"
#include "patterns/pattern_types.h"
#include <cmath>

namespace af {

/* Helper to safely append a match */
static void append(AF_PatternResult &r, const char *name,
                   AF_PatternCategory cat, AF_PatternDirection dir,
                   double conf, int bar_idx)
{
    if (r.count >= AF_MAX_PATTERN_MATCHES) return;
    AF_PatternMatch &m = r.matches[r.count++];
    snprintf(m.name, sizeof(m.name), "%s", name);
    m.category   = cat;
    m.direction  = dir;
    m.confidence = conf;
    m.bar_index  = bar_idx;
    if (dir == AF_PAT_DIR_BULLISH) r.bullish_count++;
    else if (dir == AF_PAT_DIR_BEARISH) r.bearish_count++;
}

void register_candlestick_patterns(PatternEngine *eng) {

    /* ── Single Candle ── */

    eng->reg("Doji", AF_PAT_CANDLESTICK, AF_PAT_DIR_NEUTRAL, 0.65,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        const AF_Bar &last = b[n-1];
        if (af_is_doji(&last, 0.10))
            append(r, "Doji", AF_PAT_CANDLESTICK, AF_PAT_DIR_NEUTRAL, 0.65, (int)n-1);
    });

    eng->reg("Hammer", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.72,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 4) return;
        const AF_Bar &last = b[n-1];
        /* Downtrend: last 3 bars declining */
        bool downtrend = b[n-2].close < b[n-3].close && b[n-3].close < b[n-4].close;
        if (downtrend && af_is_hammer(&last) == 1)
            append(r, "Hammer", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH,
                   0.72 + last.lower_shadow / last.range * 0.10, (int)n-1);
    });

    eng->reg("ShootingStar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.72,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 4) return;
        const AF_Bar &last = b[n-1];
        bool uptrend = b[n-2].close > b[n-3].close && b[n-3].close > b[n-4].close;
        if (uptrend && af_is_hammer(&last) == -1)
            append(r, "ShootingStar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH,
                   0.72 + last.upper_shadow / last.range * 0.10, (int)n-1);
    });

    eng->reg("BullishMarubozu", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        if (af_is_marubozu(&b[n-1], 0.90) == 1)
            append(r, "BullishMarubozu", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.78, (int)n-1);
    });

    eng->reg("BearishMarubozu", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        if (af_is_marubozu(&b[n-1], 0.90) == -1)
            append(r, "BearishMarubozu", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.78, (int)n-1);
    });

    eng->reg("BullishPinBar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.73,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        if (af_is_pin_bar(&b[n-1], 2.5) == 1)
            append(r, "BullishPinBar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.73, (int)n-1);
    });

    eng->reg("BearishPinBar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.73,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        if (af_is_pin_bar(&b[n-1], 2.5) == -1)
            append(r, "BearishPinBar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.73, (int)n-1);
    });

    eng->reg("SpinningTop", AF_PAT_CANDLESTICK, AF_PAT_DIR_NEUTRAL, 0.60,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        const AF_Bar &last = b[n-1];
        if (last.body_pct >= 0.15 && last.body_pct <= 0.35 &&
            last.upper_shadow > last.body && last.lower_shadow > last.body)
            append(r, "SpinningTop", AF_PAT_CANDLESTICK, AF_PAT_DIR_NEUTRAL, 0.60, (int)n-1);
    });

    eng->reg("LongLowerShadow", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.65,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        const AF_Bar &last = b[n-1];
        if (last.range > 0 && last.lower_shadow / last.range >= 0.55)
            append(r, "LongLowerShadow", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.65, (int)n-1);
    });

    eng->reg("LongUpperShadow", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.65,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 1) return;
        const AF_Bar &last = b[n-1];
        if (last.range > 0 && last.upper_shadow / last.range >= 0.55)
            append(r, "LongUpperShadow", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.65, (int)n-1);
    });

    /* ── Two-Candle ── */

    eng->reg("BullishEngulfing", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        int e = af_is_engulfing(&b[n-2], &b[n-1]);
        if (e == 1) append(r, "BullishEngulfing", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.78, (int)n-1);
    });

    eng->reg("BearishEngulfing", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        int e = af_is_engulfing(&b[n-2], &b[n-1]);
        if (e == -1) append(r, "BearishEngulfing", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.78, (int)n-1);
    });

    eng->reg("BullishHarami", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.65,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        if (!p.is_bullish && c.is_bullish &&
            c.open > p.close && c.close < p.open &&
            c.body < p.body * 0.50)
            append(r, "BullishHarami", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.65, (int)n-1);
    });

    eng->reg("BearishHarami", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.65,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        if (p.is_bullish && !c.is_bullish &&
            c.open < p.close && c.close > p.open &&
            c.body < p.body * 0.50)
            append(r, "BearishHarami", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.65, (int)n-1);
    });

    eng->reg("Tweezer Bottom", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.68,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        double tol = (p.low + c.low) * 0.001;
        if (!p.is_bullish && c.is_bullish && std::abs(p.low - c.low) <= tol)
            append(r, "TweezerBottom", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.68, (int)n-1);
    });

    eng->reg("Tweezer Top", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.68,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        double tol = (p.high + c.high) * 0.001;
        if (p.is_bullish && !c.is_bullish && std::abs(p.high - c.high) <= tol)
            append(r, "TweezerTop", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.68, (int)n-1);
    });

    eng->reg("PiercingLine", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.73,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        double mid = (p.open + p.close) / 2.0;
        if (!p.is_bullish && c.is_bullish &&
            c.open < p.low && c.close > mid && c.close < p.open)
            append(r, "PiercingLine", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.73, (int)n-1);
    });

    eng->reg("DarkCloudCover", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.73,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        double mid = (p.open + p.close) / 2.0;
        if (p.is_bullish && !c.is_bullish &&
            c.open > p.high && c.close < mid && c.close > p.open)
            append(r, "DarkCloudCover", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.73, (int)n-1);
    });

    eng->reg("BullishKicker", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.85,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        if (!p.is_bullish && c.is_bullish && c.open > p.open && c.open > p.close)
            append(r, "BullishKicker", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.85, (int)n-1);
    });

    eng->reg("BearishKicker", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.85,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 2) return;
        const AF_Bar &p=b[n-2], &c=b[n-1];
        if (p.is_bullish && !c.is_bullish && c.open < p.open && c.open < p.close)
            append(r, "BearishKicker", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.85, (int)n-1);
    });

    /* ── Three-Candle ── */

    eng->reg("MorningStar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.80,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 3) return;
        const AF_Bar &a=b[n-3], &m=b[n-2], &c=b[n-1];
        bool bear_first = !a.is_bullish && a.body > a.range*0.60;
        bool small_mid  = m.body < a.body*0.30;
        bool bull_last  = c.is_bullish && c.close > (a.open+a.close)/2.0;
        if (bear_first && small_mid && bull_last)
            append(r, "MorningStar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.80, (int)n-1);
    });

    eng->reg("EveningStar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.80,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 3) return;
        const AF_Bar &a=b[n-3], &m=b[n-2], &c=b[n-1];
        bool bull_first = a.is_bullish && a.body > a.range*0.60;
        bool small_mid  = m.body < a.body*0.30;
        bool bear_last  = !c.is_bullish && c.close < (a.open+a.close)/2.0;
        if (bull_first && small_mid && bear_last)
            append(r, "EveningStar", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.80, (int)n-1);
    });

    eng->reg("ThreeWhiteSoldiers", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.85,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 3) return;
        bool ok = true;
        for (int i=1; i<=3; i++) {
            const AF_Bar &bar = b[n-i];
            ok = ok && bar.is_bullish && bar.body_pct >= 0.60
                    && bar.upper_shadow < bar.body * 0.30;
        }
        bool higher = b[n-1].close > b[n-2].close && b[n-2].close > b[n-3].close;
        if (ok && higher)
            append(r, "ThreeWhiteSoldiers", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.85, (int)n-1);
    });

    eng->reg("ThreeBlackCrows", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.85,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 3) return;
        bool ok = true;
        for (int i=1; i<=3; i++) {
            const AF_Bar &bar = b[n-i];
            ok = ok && !bar.is_bullish && bar.body_pct >= 0.60
                    && bar.lower_shadow < bar.body * 0.30;
        }
        bool lower = b[n-1].close < b[n-2].close && b[n-2].close < b[n-3].close;
        if (ok && lower)
            append(r, "ThreeBlackCrows", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.85, (int)n-1);
    });

    eng->reg("ThreeInsideUp", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 3) return;
        const AF_Bar &a=b[n-3], &m=b[n-2], &c=b[n-1];
        /* Bearish, bullish harami, then bullish close above harami */
        if (!a.is_bullish && m.is_bullish &&
            m.open > a.close && m.close < a.open &&
            c.is_bullish && c.close > m.close)
            append(r, "ThreeInsideUp", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.78, (int)n-1);
    });

    eng->reg("ThreeInsideDown", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.78,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 3) return;
        const AF_Bar &a=b[n-3], &m=b[n-2], &c=b[n-1];
        if (a.is_bullish && !m.is_bullish &&
            m.open < a.close && m.close > a.open &&
            !c.is_bullish && c.close < m.close)
            append(r, "ThreeInsideDown", AF_PAT_CANDLESTICK, AF_PAT_DIR_BEARISH, 0.78, (int)n-1);
    });

    eng->reg("AbandonedBabyBull", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.88,
    [](const AF_Bar *b, size_t n, AF_PatternResult &r) {
        if (n < 3) return;
        const AF_Bar &a=b[n-3], &m=b[n-2], &c=b[n-1];
        bool gap_down = m.high < a.low;   /* doji gapped down */
        bool gap_up   = c.low > m.high;   /* bull bar gapped up */
        if (!a.is_bullish && af_is_doji(&m,0.10) && c.is_bullish && gap_down && gap_up)
            append(r, "AbandonedBabyBull", AF_PAT_CANDLESTICK, AF_PAT_DIR_BULLISH, 0.88, (int)n-1);
    });
}

} /* namespace af */
