// Candlestick pattern detectors — same math as the cpp/ reference.
//
// Each detector returns +1 (bullish), -1 (bearish), or 0 (no match).
// Bars with range == 0 never match. Detectors take a Bar instance.

const EPSILON = 1e-12;

function geom(o, h, l, c) {
    const body  = Math.abs(c - o);
    const range = h - l;
    const top   = c > o ? c : o;
    const bot   = c < o ? c : o;
    return {
        body, range,
        upper: h - top,
        lower: bot - l,
        bull: c > o,
    };
}

export function isDoji(bar, dojiPct = 0.10) {
    const { body, range } = geom(bar.open, bar.high, bar.low, bar.close);
    if (range < EPSILON) return 0;
    return body / range <= dojiPct ? 1 : 0;
}

export function isHammer(bar) {
    const { range, upper, lower } = geom(bar.open, bar.high, bar.low, bar.close);
    if (range < EPSILON) return 0;
    const loRatio = lower / range;
    const hiRatio = upper / range;
    if (loRatio >= 0.60 && hiRatio <= 0.20) return  1;
    if (hiRatio >= 0.60 && loRatio <= 0.20) return -1;
    return 0;
}

export function isEngulfing(prev, curr) {
    const prevBull = prev.close > prev.open;
    const currBull = curr.close > curr.open;
    if (currBull && !prevBull && curr.open < prev.close && curr.close > prev.open) return  1;
    if (!currBull && prevBull && curr.open > prev.close && curr.close < prev.open) return -1;
    return 0;
}

export function isMarubozu(bar, bodyMinPct = 0.90) {
    const { body, range, upper, lower, bull } = geom(bar.open, bar.high, bar.low, bar.close);
    if (range < EPSILON) return 0;
    if (body / range < bodyMinPct) return 0;
    const shadowMax = range * 0.05;
    if (upper < shadowMax && lower < shadowMax) return bull ? 1 : -1;
    return 0;
}

export function isPinBar(bar, minWickMult = 2.0) {
    const { body, range, upper, lower } = geom(bar.open, bar.high, bar.low, bar.close);
    if (range < EPSILON) return 0;
    // Tiny-body fix from cpp/: treat very small bodies as 3% of range.
    const effectiveBody = body > range * 0.03 ? body : range * 0.03;
    const lwm = lower / effectiveBody;
    const uwm = upper / effectiveBody;
    const oppMax = range * 0.25;
    if (lwm >= minWickMult && upper <= oppMax) return  1;
    if (uwm >= minWickMult && lower <= oppMax) return -1;
    return 0;
}

// ── Three-candle patterns ────────────────────────────────────────────────────
// All match cpp/ candlestick_patterns.cpp logic exactly.

// Morning Star: bearish first bar (body > range*0.60), small middle (body < b0.body*0.30),
// bullish last closing above midpoint of first bar's body.
export function isMorningStar(b0, b1, b2) {
    const g0 = geom(b0.open, b0.high, b0.low, b0.close);
    const g1 = geom(b1.open, b1.high, b1.low, b1.close);
    const g2 = geom(b2.open, b2.high, b2.low, b2.close);
    if (g0.range < EPSILON || g1.range < EPSILON || g2.range < EPSILON) return 0;
    // b0: bearish with large body (close < open in cpp: !is_bullish means close < open since is_bullish = close >= open)
    const bearFirst = !g0.bull && g0.body > g0.range * 0.60;
    // b1: small body relative to b0's body
    const smallMid  = g1.body < g0.body * 0.30;
    // b2: bullish, close above midpoint of b0 body
    const midpoint  = (b0.open + b0.close) / 2.0;
    const bullLast  = g2.bull && b2.close > midpoint;
    if (bearFirst && smallMid && bullLast) return 1;
    return 0;
}

// Evening Star: bullish first bar (body > range*0.60), small middle (body < b0.body*0.30),
// bearish last closing below midpoint of first bar's body.
export function isEveningStar(b0, b1, b2) {
    const g0 = geom(b0.open, b0.high, b0.low, b0.close);
    const g1 = geom(b1.open, b1.high, b1.low, b1.close);
    const g2 = geom(b2.open, b2.high, b2.low, b2.close);
    if (g0.range < EPSILON || g1.range < EPSILON || g2.range < EPSILON) return 0;
    const bullFirst = g0.bull && g0.body > g0.range * 0.60;
    const smallMid  = g1.body < g0.body * 0.30;
    const midpoint  = (b0.open + b0.close) / 2.0;
    const bearLast  = !g2.bull && b2.close < midpoint;
    if (bullFirst && smallMid && bearLast) return -1;
    return 0;
}

// Three White Soldiers: all three bars bullish, body_pct >= 0.60,
// upper_shadow < body * 0.30, and each close higher than the previous.
// Matches cpp/ ThreeWhiteSoldiers.
export function isThreeWhiteSoldiers(b0, b1, b2) {
    const bars = [b0, b1, b2];
    for (const bar of bars) {
        const g = geom(bar.open, bar.high, bar.low, bar.close);
        if (!g.bull) return 0;
        if (g.range < EPSILON) return 0;
        if (g.body / g.range < 0.60) return 0;    // body_pct < 0.60
        if (g.upper > g.body * 0.30) return 0;    // upper_shadow >= body*0.30
    }
    if (b1.close <= b0.close || b2.close <= b1.close) return 0;
    return 1;
}

// Three Black Crows: all three bars bearish, body_pct >= 0.60,
// lower_shadow < body * 0.30, and each close lower than the previous.
// Matches cpp/ ThreeBlackCrows.
export function isThreeBlackCrows(b0, b1, b2) {
    const bars = [b0, b1, b2];
    for (const bar of bars) {
        const g = geom(bar.open, bar.high, bar.low, bar.close);
        if (g.bull) return 0;
        if (g.range < EPSILON) return 0;
        if (g.body / g.range < 0.60) return 0;    // body_pct < 0.60
        if (g.lower > g.body * 0.30) return 0;    // lower_shadow >= body*0.30
    }
    if (b1.close >= b0.close || b2.close >= b1.close) return 0;
    return -1;
}

// ── Chart Pattern Helpers ─────────────────────────────────────────────────────

function maxRange(bars, from, to) {
    let mx = bars[from].high;
    for (let i = from + 1; i <= to; i++) {
        if (bars[i].high > mx) mx = bars[i].high;
    }
    return mx;
}

function minRange(bars, from, to) {
    let mn = bars[from].low;
    for (let i = from + 1; i <= to; i++) {
        if (bars[i].low < mn) mn = bars[i].low;
    }
    return mn;
}

// ── Double Top ────────────────────────────────────────────────────────────────
// Bearish reversal. Two similar highs separated by at least 10 bars (n/2 split),
// with a valley below and closing below midpoint. Matches cpp/ DoubleTop.
export function isDoubleTop(bars) {
    const n = bars.length;
    if (n < 30) return 0;
    const mid = Math.floor(n / 2);
    const hi1 = maxRange(bars, 0, mid - 1);
    const lo  = minRange(bars, 0, n - 1);
    const hi2 = maxRange(bars, mid, n - 1);
    const tol = (hi1 + hi2) * 0.005;
    if (Math.abs(hi1 - hi2) <= tol && lo < hi1 * 0.99 && bars[n - 1].close < (hi1 + lo) / 2.0) {
        return -1;
    }
    return 0;
}

// ── Double Bottom ─────────────────────────────────────────────────────────────
// Bullish reversal. Two similar lows separated by at least 10 bars (n/2 split),
// with a peak above and closing above midpoint. Matches cpp/ DoubleBottom.
export function isDoubleBottom(bars) {
    const n = bars.length;
    if (n < 30) return 0;
    const mid = Math.floor(n / 2);
    const lo1 = minRange(bars, 0, mid - 1);
    const hi  = maxRange(bars, 0, n - 1);
    const lo2 = minRange(bars, mid, n - 1);
    const tol = (lo1 + lo2) * 0.005;
    if (Math.abs(lo1 - lo2) <= tol && hi > lo1 * 1.01 && bars[n - 1].close > (lo1 + hi) / 2.0) {
        return 1;
    }
    return 0;
}

// ── Ascending Triangle ────────────────────────────────────────────────────────
// Bullish. Flat resistance with rising lows over last 20 bars.
// Matches cpp/ AscendingTriangle.
export function isAscendingTriangle(bars) {
    const n = bars.length;
    if (n < 20) return 0;
    const resistance = maxRange(bars, n - 20, n - 1);
    const loFirst    = minRange(bars, n - 20, n - 11);
    const loLast     = minRange(bars, n - 10, n - 1);
    const hiVar      = maxRange(bars, n - 20, n - 11) - maxRange(bars, n - 10, n - 1);
    if (loLast > loFirst * 1.002 && Math.abs(hiVar) < resistance * 0.005) {
        return 1;
    }
    return 0;
}

// ── Descending Triangle ───────────────────────────────────────────────────────
// Bearish. Flat support with falling highs over last 20 bars.
// Matches cpp/ DescendingTriangle.
export function isDescendingTriangle(bars) {
    const n = bars.length;
    if (n < 20) return 0;
    const support  = minRange(bars, n - 20, n - 1);
    const hiFirst  = maxRange(bars, n - 20, n - 11);
    const hiLast   = maxRange(bars, n - 10, n - 1);
    const loVar    = minRange(bars, n - 20, n - 11) - minRange(bars, n - 10, n - 1);
    if (hiLast < hiFirst * 0.998 && Math.abs(loVar) < support * 0.005) {
        return -1;
    }
    return 0;
}

// ── Bullish Flag ──────────────────────────────────────────────────────────────
// Pole: strong bullish move in first 10 bars (bars[n-20..n-11]).
// Flag: tight consolidation in last 10 bars (bars[n-10..n-1]).
// flag_rng < pole_rng * 0.45 and pole_rng > pole_lo * 0.01.
// Matches cpp/ BullishFlag.
export function isBullishFlag(bars) {
    const n = bars.length;
    if (n < 20) return 0;
    const poleLo  = minRange(bars, n - 20, n - 11);
    const poleHi  = maxRange(bars, n - 20, n - 11);
    const poleRng = poleHi - poleLo;
    const flagHi  = maxRange(bars, n - 10, n - 1);
    const flagLo  = minRange(bars, n - 10, n - 1);
    const flagRng = flagHi - flagLo;
    const poleBull  = bars[n - 11].close > bars[n - 20].close;
    const tightFlag = flagRng < poleRng * 0.45;
    if (poleBull && tightFlag && poleRng > poleLo * 0.01) return 1;
    return 0;
}

// ── Bearish Flag ──────────────────────────────────────────────────────────────
// Pole: strong bearish move in first 10 bars (bars[n-20..n-11]).
// Flag: tight consolidation in last 10 bars (bars[n-10..n-1]).
// Matches cpp/ BearishFlag.
export function isBearishFlag(bars) {
    const n = bars.length;
    if (n < 20) return 0;
    const poleLo  = minRange(bars, n - 20, n - 11);
    const poleHi  = maxRange(bars, n - 20, n - 11);
    const poleRng = poleHi - poleLo;
    const flagRng = maxRange(bars, n - 10, n - 1) - minRange(bars, n - 10, n - 1);
    const poleBear  = bars[n - 11].close < bars[n - 20].close;
    const tightFlag = flagRng < poleRng * 0.45;
    if (poleBear && tightFlag && poleRng > poleLo * 0.01) return -1;
    return 0;
}

// ── Head and Shoulders ────────────────────────────────────────────────────────
// Bearish reversal. Left shoulder, head (higher), right shoulder (similar to left).
// Neckline = min of lows of left-shoulder and right-shoulder regions.
// Close below neckline confirms. Matches cpp/ HeadAndShoulders.
export function isHeadAndShoulders(bars) {
    const n = bars.length;
    if (n < 40) return 0;
    const s  = n - 40;
    const ls = maxRange(bars, s,      s + 12);
    const h  = maxRange(bars, s + 13, s + 26);
    const rs = maxRange(bars, s + 27, n - 1);
    const neckline = Math.min(
        minRange(bars, s, s + 12),
        minRange(bars, s + 27, n - 1)
    );
    if (h > ls * 1.01 && h > rs * 1.01 &&
        Math.abs(ls - rs) / (ls + rs) * 2 < 0.07 &&
        bars[n - 1].close < neckline) {
        return -1;
    }
    return 0;
}

// ── Inverse Head and Shoulders ────────────────────────────────────────────────
// Bullish reversal. Left shoulder, head (lower), right shoulder (similar to left).
// Neckline = max of highs of left-shoulder and right-shoulder regions.
// Close above neckline confirms. Matches cpp/ InverseHeadAndShoulders.
export function isInverseHeadAndShoulders(bars) {
    const n = bars.length;
    if (n < 40) return 0;
    const s  = n - 40;
    const ls = minRange(bars, s,      s + 12);
    const h  = minRange(bars, s + 13, s + 26);
    const rs = minRange(bars, s + 27, n - 1);
    const neckline = Math.max(
        maxRange(bars, s, s + 12),
        maxRange(bars, s + 27, n - 1)
    );
    if (h < ls * 0.99 && h < rs * 0.99 &&
        Math.abs(ls - rs) / (ls + rs) * 2 < 0.07 &&
        bars[n - 1].close > neckline) {
        return 1;
    }
    return 0;
}

function fibRatio(a, b, target, tol) {
    if (Math.abs(a) < 1e-12) return false;
    return Math.abs(b / a - target) <= tol;
}

function isGartleyBull(bars) {
    const n = bars.length;
    if (n < 50) return 0;
    const x  = minRange(bars, n - 50, n - 40);
    const a  = maxRange(bars, n - 40, n - 30);
    const bb = minRange(bars, n - 30, n - 20);
    const cc = maxRange(bars, n - 20, n - 10);
    const d  = minRange(bars, n - 10, n - 1);
    const xa = a - x, ab = a - bb, bc = cc - bb, cd = cc - d;
    if (xa > 1e-6 && bc > 1e-6 &&
        fibRatio(xa, ab, 0.618, 0.05) &&
        (fibRatio(bc, cd, 1.272, 0.07) || fibRatio(bc, cd, 1.618, 0.07)))
        return 1;
    return 0;
}

function isGartleyBear(bars) {
    const n = bars.length;
    if (n < 50) return 0;
    const x  = maxRange(bars, n - 50, n - 40);
    const a  = minRange(bars, n - 40, n - 30);
    const bb = maxRange(bars, n - 30, n - 20);
    const cc = minRange(bars, n - 20, n - 10);
    const d  = maxRange(bars, n - 10, n - 1);
    const xa = x - a, ab = bb - a, bc = bb - cc, cd = d - cc;
    if (xa > 1e-6 && bc > 1e-6 &&
        fibRatio(xa, ab, 0.618, 0.05) &&
        (fibRatio(bc, cd, 1.272, 0.07) || fibRatio(bc, cd, 1.618, 0.07)))
        return -1;
    return 0;
}

function isBatBull(bars) {
    const n = bars.length;
    if (n < 50) return 0;
    const x  = minRange(bars, n - 50, n - 40);
    const a  = maxRange(bars, n - 40, n - 30);
    const d  = minRange(bars, n - 10, n - 1);
    const xa = a - x, xd = a - d;
    if (xa > 1e-6 && fibRatio(xa, xd, 0.886, 0.05)) return 1;
    return 0;
}

function isButterflyBull(bars) {
    const n = bars.length;
    if (n < 50) return 0;
    const x  = minRange(bars, n - 50, n - 40);
    const a  = maxRange(bars, n - 40, n - 30);
    const d  = minRange(bars, n - 10, n - 1);
    const xa = a - x, xd = a - d;
    if (xa > 1e-6 && fibRatio(xa, xd, 1.272, 0.07)) return 1;
    return 0;
}

function isCrabBull(bars) {
    const n = bars.length;
    if (n < 50) return 0;
    const x  = minRange(bars, n - 50, n - 40);
    const a  = maxRange(bars, n - 40, n - 30);
    const d  = minRange(bars, n - 10, n - 1);
    const xa = a - x, xd = a - d;
    if (xa > 1e-6 && fibRatio(xa, xd, 1.618, 0.07)) return 1;
    return 0;
}

export class PatternMatch {
    constructor(barIndex, name, signal) {
        this.barIndex = barIndex;
        this.name     = name;
        this.signal   = signal;
    }
}

export function scanPatterns(bars) {
    const out = [];
    for (let i = 0; i < bars.length; i++) {
        const b = bars[i];
        let s;
        if ((s = isDoji(b)))     out.push(new PatternMatch(i, "doji",     s));
        if ((s = isHammer(b)))   out.push(new PatternMatch(i, "hammer",   s));
        if ((s = isMarubozu(b))) out.push(new PatternMatch(i, "marubozu", s));
        if ((s = isPinBar(b)))   out.push(new PatternMatch(i, "pin_bar",  s));
        if (i > 0) {
            if ((s = isEngulfing(bars[i - 1], b))) {
                out.push(new PatternMatch(i, "engulfing", s));
            }
        }
        if (i >= 2) {
            if ((s = isMorningStar(bars[i - 2], bars[i - 1], b))) {
                out.push(new PatternMatch(i, "morning_star", s));
            }
            if ((s = isEveningStar(bars[i - 2], bars[i - 1], b))) {
                out.push(new PatternMatch(i, "evening_star", s));
            }
            if ((s = isThreeWhiteSoldiers(bars[i - 2], bars[i - 1], b))) {
                out.push(new PatternMatch(i, "three_white_soldiers", s));
            }
            if ((s = isThreeBlackCrows(bars[i - 2], bars[i - 1], b))) {
                out.push(new PatternMatch(i, "three_black_crows", s));
            }
        }
    }
    // Chart patterns — only checked once when we have enough bars.
    // barIndex for chart patterns is always bars.length - 1 (the latest bar).
    const last = bars.length - 1;
    if (bars.length >= 30) {
        let s;
        if ((s = isDoubleTop(bars)))    out.push(new PatternMatch(last, "double_top",    s));
        if ((s = isDoubleBottom(bars))) out.push(new PatternMatch(last, "double_bottom", s));
    }
    if (bars.length >= 20) {
        let s;
        if ((s = isAscendingTriangle(bars)))  out.push(new PatternMatch(last, "ascending_triangle",  s));
        if ((s = isDescendingTriangle(bars))) out.push(new PatternMatch(last, "descending_triangle", s));
        if ((s = isBullishFlag(bars)))        out.push(new PatternMatch(last, "bullish_flag",        s));
        if ((s = isBearishFlag(bars)))        out.push(new PatternMatch(last, "bearish_flag",        s));
    }
    if (bars.length >= 40) {
        let s;
        if ((s = isHeadAndShoulders(bars)))        out.push(new PatternMatch(last, "head_and_shoulders",         s));
        if ((s = isInverseHeadAndShoulders(bars))) out.push(new PatternMatch(last, "inverse_head_and_shoulders", s));
    }
    if (bars.length >= 50) {
        let s;
        if ((s = isGartleyBull(bars)))    out.push(new PatternMatch(last, "gartley_bull",    s));
        if ((s = isGartleyBear(bars)))    out.push(new PatternMatch(last, "gartley_bear",    s));
        if ((s = isBatBull(bars)))        out.push(new PatternMatch(last, "bat_bull",        s));
        if ((s = isButterflyBull(bars)))  out.push(new PatternMatch(last, "butterfly_bull",  s));
        if ((s = isCrabBull(bars)))       out.push(new PatternMatch(last, "crab_bull",       s));
    }
    return out;
}
