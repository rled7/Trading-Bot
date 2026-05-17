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
    return out;
}
