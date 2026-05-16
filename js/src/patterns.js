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
    }
    return out;
}
