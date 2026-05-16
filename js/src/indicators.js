// SMA / EMA / RSI / ATR — same math as the cpp/ reference.
// Each fn returns an Array of length n. Indices where the indicator is not
// yet defined come back as null. Throws Error for invalid args.

function checkPeriod(period) {
    if (!Number.isInteger(period) || period <= 0) {
        throw new Error(`period must be a positive integer, got ${period}`);
    }
}

export function sma(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n < period) return out;

    let window = 0;
    for (let i = 0; i < period; i++) window += values[i];
    out[period - 1] = window / period;
    for (let i = period; i < n; i++) {
        window += values[i] - values[i - period];
        out[i] = window / period;
    }
    return out;
}

export function ema(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n < period) return out;

    const alpha = 2.0 / (period + 1);
    let seed = 0;
    for (let i = 0; i < period; i++) seed += values[i];
    seed /= period;
    out[period - 1] = seed;
    let prev = seed;
    for (let i = period; i < n; i++) {
        prev = values[i] * alpha + prev * (1 - alpha);
        out[i] = prev;
    }
    return out;
}

export function rsi(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n <= period) return out;

    const alpha = 1.0 / period;
    let ag = 0, al = 0;
    for (let i = 1; i <= period; i++) {
        const d = values[i] - values[i - 1];
        if (d > 0) ag += d;
        else       al += -d;
    }
    ag /= period;
    al /= period;

    for (let i = period; i < n; i++) {
        if (i > period) {
            const d = values[i] - values[i - 1];
            const u = d > 0 ?  d : 0;
            const v = d < 0 ? -d : 0;
            ag = ag * (1 - alpha) + u * alpha;
            al = al * (1 - alpha) + v * alpha;
        }
        const rs = al > 1e-12 ? ag / al : 100;
        out[i]   = 100 - 100 / (1 + rs);
    }
    return out;
}

export function atr(highs, lows, closes, period) {
    checkPeriod(period);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }
    const out = new Array(n).fill(null);
    if (n <= period) return out;

    const tr = new Array(n);
    tr[0] = highs[0] - lows[0];
    for (let i = 1; i < n; i++) {
        const pc = closes[i - 1];
        tr[i] = Math.max(
            highs[i] - lows[i],
            Math.abs(highs[i] - pc),
            Math.abs(lows[i]  - pc),
        );
    }

    const alpha = 1.0 / period;
    let seed = 0;
    for (let i = 0; i < period; i++) seed += tr[i];
    seed /= period;
    out[period - 1] = seed;
    let prev = seed;
    for (let i = period; i < n; i++) {
        prev = tr[i] * alpha + prev * (1 - alpha);
        out[i] = prev;
    }
    return out;
}
