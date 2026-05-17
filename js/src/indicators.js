// SMA / EMA / RSI / ATR / MACD / Bollinger / Stochastic / OBV / ADX
// — same math as the cpp/ reference.
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

// ── MACD ─────────────────────────────────────────────────────────────────────
// macdLine = EMA(fast) - EMA(slow); signal = EMA(macdLine, signal);
// histogram = macdLine - signal. Nulls propagate where either input is null.
export function macd(values, fast = 12, slow = 26, signal = 9) {
    checkPeriod(fast);
    checkPeriod(slow);
    checkPeriod(signal);
    const n = values.length;
    const ef = ema(values, fast);
    const es = ema(values, slow);

    const macdLine = new Array(n).fill(null);
    for (let i = 0; i < n; i++) {
        if (ef[i] !== null && es[i] !== null) macdLine[i] = ef[i] - es[i];
    }

    // EMA of macdLine — skip leading nulls to find seed
    const sigLine = new Array(n).fill(null);
    // find first non-null in macdLine
    let firstValid = -1;
    for (let i = 0; i < n; i++) {
        if (macdLine[i] !== null) { firstValid = i; break; }
    }
    if (firstValid >= 0 && n - firstValid >= signal) {
        // seed = average of first `signal` non-null macdLine values
        let seed = 0;
        for (let i = firstValid; i < firstValid + signal; i++) seed += macdLine[i];
        seed /= signal;
        sigLine[firstValid + signal - 1] = seed;
        const alpha = 2.0 / (signal + 1);
        let prev = seed;
        for (let i = firstValid + signal; i < n; i++) {
            prev = macdLine[i] * alpha + prev * (1 - alpha);
            sigLine[i] = prev;
        }
    }

    const histogram = new Array(n).fill(null);
    for (let i = 0; i < n; i++) {
        if (macdLine[i] !== null && sigLine[i] !== null) {
            histogram[i] = macdLine[i] - sigLine[i];
        }
    }

    return { macd: macdLine, signal: sigLine, histogram };
}

// ── Bollinger Bands ───────────────────────────────────────────────────────────
// middle = SMA(period); upper/lower = middle ± mult * population-stddev.
// Population variance = sum((x - mean)^2) / period  (matches cpp/).
export function bollinger(values, period = 20, mult = 2.0) {
    checkPeriod(period);
    const n = values.length;
    const middle = sma(values, period);
    const upper  = new Array(n).fill(null);
    const lower  = new Array(n).fill(null);

    for (let i = period - 1; i < n; i++) {
        if (middle[i] === null) continue;
        let variance = 0;
        for (let j = 0; j < period; j++) {
            const d = values[i - j] - middle[i];
            variance += d * d;
        }
        const sd = Math.sqrt(variance / period);
        upper[i] = middle[i] + mult * sd;
        lower[i] = middle[i] - mult * sd;
    }

    return { upper, middle, lower };
}

// ── Stochastic Oscillator ─────────────────────────────────────────────────────
// rawK[i] = 100 * (close[i] - lowestLow[kPeriod]) / (highestHigh[kPeriod] - lowestLow[kPeriod])
// k = SMA(rawK, dPeriod); d = SMA(k, dPeriod)
// Matches cpp/ af_stochastic with smooth=dPeriod.
export function stochastic(highs, lows, closes, kPeriod = 14, dPeriod = 3) {
    checkPeriod(kPeriod);
    checkPeriod(dPeriod);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }

    const rawK = new Array(n).fill(null);
    const EPSILON = 1e-10;
    for (let i = kPeriod - 1; i < n; i++) {
        let lo = lows[i], hi = highs[i];
        for (let j = 1; j < kPeriod; j++) {
            if (lows[i - j]  < lo) lo = lows[i - j];
            if (highs[i - j] > hi) hi = highs[i - j];
        }
        const r = hi - lo;
        rawK[i] = r > EPSILON ? 100.0 * (closes[i] - lo) / r : 50.0;
    }

    // k = SMA(rawK, dPeriod)
    const kArr = new Array(n).fill(null);
    {
        // Treat rawK as a dense array starting at kPeriod-1
        const start = kPeriod - 1;
        if (n - start >= dPeriod) {
            let win = 0;
            for (let i = start; i < start + dPeriod; i++) win += rawK[i];
            kArr[start + dPeriod - 1] = win / dPeriod;
            for (let i = start + dPeriod; i < n; i++) {
                win += rawK[i] - rawK[i - dPeriod];
                kArr[i] = win / dPeriod;
            }
        }
    }

    // d = SMA(k, dPeriod)
    const dArr = new Array(n).fill(null);
    {
        // k is non-null starting at (kPeriod-1 + dPeriod-1)
        const kStart = kPeriod - 1 + dPeriod - 1;
        if (n - kStart >= dPeriod) {
            let win = 0;
            for (let i = kStart; i < kStart + dPeriod; i++) win += kArr[i];
            dArr[kStart + dPeriod - 1] = win / dPeriod;
            for (let i = kStart + dPeriod; i < n; i++) {
                win += kArr[i] - kArr[i - dPeriod];
                dArr[i] = win / dPeriod;
            }
        }
    }

    return { k: kArr, d: dArr };
}

// ── OBV (On-Balance Volume) ───────────────────────────────────────────────────
// out[0] = 0; out[i] = out[i-1] + (closes[i] > closes[i-1] ? vol[i] : closes[i] < closes[i-1] ? -vol[i] : 0)
export function obv(closes, volumes) {
    const n = closes.length;
    if (volumes.length !== n) {
        throw new Error("closes and volumes must have the same length");
    }
    const out = new Array(n);
    if (n === 0) return out;
    out[0] = 0;
    for (let i = 1; i < n; i++) {
        const d = closes[i] - closes[i - 1];
        out[i] = out[i - 1] + (d > 0 ? volumes[i] : d < 0 ? -volumes[i] : 0);
    }
    return out;
}

// ── WMA (Weighted Moving Average) ────────────────────────────────────────────
// Most recent bar gets weight = period, oldest gets weight = 1.
// sum of weights = period*(period+1)/2. Matches cpp/ af_wma.
export function wma(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n < period) return out;

    const wsum = period * (period + 1) / 2;
    for (let i = period - 1; i < n; i++) {
        let s = 0;
        for (let j = 0; j < period; j++) {
            s += values[i - (period - 1 - j)] * (j + 1);
        }
        out[i] = s / wsum;
    }
    return out;
}

// ── CCI (Commodity Channel Index) ────────────────────────────────────────────
// TP = (H+L+C)/3; CCI = (TP - SMA(TP)) / (constant * mean_abs_dev(TP))
// Matches cpp/ af_cci.
export function cci(highs, lows, closes, period = 20, constant = 0.015) {
    checkPeriod(period);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }
    const out = new Array(n).fill(null);
    if (n < period) return out;

    const EPSILON = 1e-10;
    for (let i = period - 1; i < n; i++) {
        let sum = 0;
        for (let j = 0; j < period; j++) {
            sum += (highs[i - j] + lows[i - j] + closes[i - j]) / 3.0;
        }
        const mean = sum / period;
        let mad = 0;
        for (let j = 0; j < period; j++) {
            mad += Math.abs((highs[i - j] + lows[i - j] + closes[i - j]) / 3.0 - mean);
        }
        mad /= period;
        const tp = (highs[i] + lows[i] + closes[i]) / 3.0;
        out[i] = mad > EPSILON ? (tp - mean) / (constant * mad) : 0.0;
    }
    return out;
}

// ── Williams %R ───────────────────────────────────────────────────────────────
// = -100 * (highest_high - close) / (highest_high - lowest_low)
// Returns -50 when range is zero (matches cpp/ af_williams_r).
export function williamsR(highs, lows, closes, period = 14) {
    checkPeriod(period);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }
    const out = new Array(n).fill(null);
    if (n < period) return out;

    const EPSILON = 1e-10;
    for (let i = period - 1; i < n; i++) {
        let hi = highs[i], lo = lows[i];
        for (let j = 1; j < period; j++) {
            if (highs[i - j] > hi) hi = highs[i - j];
            if (lows[i - j]  < lo) lo = lows[i - j];
        }
        const r = hi - lo;
        out[i] = r > EPSILON ? -100.0 * (hi - closes[i]) / r : -50.0;
    }
    return out;
}

// ── ROC (Rate of Change) ──────────────────────────────────────────────────────
// = 100 * (close[i] - close[i-period]) / close[i-period]
// Returns 0 when past price is ~0. First valid index: period (not period-1).
// Matches cpp/ af_roc (n <= period guard → first valid at index period).
export function roc(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n <= period) return out;

    const EPSILON = 1e-10;
    for (let i = period; i < n; i++) {
        const p = values[i - period];
        out[i] = Math.abs(p) > EPSILON ? (values[i] - p) / p * 100.0 : 0.0;
    }
    return out;
}

// ── MFI (Money Flow Index) ───────────────────────────────────────────────────
// Typical price money flow; 100 - 100/(1 + pos/neg).
// Comparison: tp >= prev_tp → positive money flow (matches cpp/ af_mfi).
// First valid index: period (guard n <= period).
export function mfi(highs, lows, closes, volumes, period = 14) {
    checkPeriod(period);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n || volumes.length !== n) {
        throw new Error("highs, lows, closes, volumes must all have the same length");
    }
    const out = new Array(n).fill(null);
    if (n <= period) return out;

    const EPSILON = 1e-10;
    for (let i = period; i < n; i++) {
        let pmf = 0, nmf = 0;
        for (let j = 0; j < period; j++) {
            const k = i - j;
            const tp = (highs[k] + lows[k] + closes[k]) / 3.0;
            const mflow = tp * volumes[k];
            const pp = k > 0 ? (highs[k - 1] + lows[k - 1] + closes[k - 1]) / 3.0 : tp;
            if (tp >= pp) pmf += mflow;
            else          nmf += mflow;
        }
        const r = nmf > EPSILON ? pmf / nmf : 100.0;
        out[i] = 100.0 - 100.0 / (1.0 + r);
    }
    return out;
}

// ── VWAP (Volume Weighted Average Price) ─────────────────────────────────────
// Cumulative (TP*vol)/cumulative vol. Volume 0 treated as 1 (matches cpp/ af_vwap).
export function vwap(highs, lows, closes, volumes) {
    const n = highs.length;
    if (lows.length !== n || closes.length !== n || volumes.length !== n) {
        throw new Error("highs, lows, closes, volumes must all have the same length");
    }
    const out = new Array(n);
    if (n === 0) return out;

    let cpv = 0, cv = 0;
    for (let i = 0; i < n; i++) {
        const vv = volumes[i] > 0 ? volumes[i] : 1;
        const tp = (highs[i] + lows[i] + closes[i]) / 3.0;
        cpv += tp * vv;
        cv  += vv;
        out[i] = cpv / cv;
    }
    return out;
}

// ── Keltner Channel ───────────────────────────────────────────────────────────
// middle = EMA(close, emaPeriod); upper/lower = middle ± mult * ATR(atrPeriod)
// Matches cpp/ af_keltner.
export function keltner(highs, lows, closes, emaPeriod = 20, atrPeriod = 10, mult = 2.0) {
    checkPeriod(emaPeriod);
    checkPeriod(atrPeriod);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }
    const middle = ema(closes, emaPeriod);
    const atrArr = atr(highs, lows, closes, atrPeriod);
    const upper  = new Array(n).fill(null);
    const lower  = new Array(n).fill(null);

    for (let i = 0; i < n; i++) {
        if (middle[i] === null || atrArr[i] === null) continue;
        upper[i] = middle[i] + mult * atrArr[i];
        lower[i] = middle[i] - mult * atrArr[i];
    }
    return { upper, middle, lower };
}

// ── HMA (Hull Moving Average) ─────────────────────────────────────────────────
// Matches cpp/ af_hma exactly.
// half = trunc(sqrt(period/2)); sq = trunc(sqrt(period)); sq clamped to >= 2.
// raw[i] = 2*WMA(half)[i] - WMA(period)[i]  (null if either is null)
// out = WMA(raw, sq)
export function hma(values, period) {
    checkPeriod(period);
    if (period < 2) throw new Error(`hma period must be >= 2, got ${period}`);
    const n = values.length;
    const out = new Array(n).fill(null);

    const half = Math.max(1, Math.trunc(Math.sqrt(period / 2.0)));
    const sq   = Math.max(2, Math.trunc(Math.sqrt(period)));

    const wh  = wma(values, half);
    const wf  = wma(values, period);
    const raw = new Array(n);
    for (let i = 0; i < n; i++) {
        raw[i] = (wh[i] === null || wf[i] === null) ? null : 2.0 * wh[i] - wf[i];
    }

    // WMA of raw — reuse wma but raw has leading nulls; handle manually
    // wma expects a flat array and uses period-length windows; here raw may have nulls
    // at the start. We need WMA(raw, sq) treating nulls as unavailable.
    const wsum = sq * (sq + 1) / 2;
    for (let i = sq - 1; i < n; i++) {
        let s = 0;
        let valid = true;
        for (let j = 0; j < sq; j++) {
            const v = raw[i - (sq - 1 - j)];
            if (v === null) { valid = false; break; }
            s += v * (j + 1);
        }
        if (valid) out[i] = s / wsum;
    }
    return out;
}

// ── DEMA (Double EMA) ─────────────────────────────────────────────────────────
// DEMA = 2*EMA1 - EMA(EMA1). Null propagates where either is null.
// Matches cpp/ af_dema.
export function dema(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    const e1 = ema(values, period);
    const e2 = emaOfSparse(e1, period);
    for (let i = 0; i < n; i++) {
        if (e1[i] !== null && e2[i] !== null) out[i] = 2.0 * e1[i] - e2[i];
    }
    return out;
}

// ── TEMA (Triple EMA) ─────────────────────────────────────────────────────────
// TEMA = 3*EMA1 - 3*EMA2 + EMA3. Null propagates.
// Matches cpp/ af_tema.
export function tema(values, period) {
    checkPeriod(period);
    const n = values.length;
    const e1 = ema(values, period);
    const e2 = emaOfSparse(e1, period);
    const e3 = emaOfSparse(e2, period);
    const out = new Array(n).fill(null);
    for (let i = 0; i < n; i++) {
        if (e1[i] !== null && e2[i] !== null && e3[i] !== null) {
            out[i] = 3.0 * e1[i] - 3.0 * e2[i] + e3[i];
        }
    }
    return out;
}

// ── TRIX ──────────────────────────────────────────────────────────────────────
// Triple-smoothed EMA, then ROC of that.
// trix[i] = (e3[i] - e3[i-1]) / e3[i-1] * 100  when both are valid and e3[i-1] != 0.
// Returns only the TRIX line (no signal). Matches cpp/ af_trix (signal ignored).
export function trix(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    const e1 = ema(values, period);
    const e2 = emaOfSparse(e1, period);
    const e3 = emaOfSparse(e2, period);
    const EPSILON = 1e-10;
    for (let i = 1; i < n; i++) {
        if (e3[i] !== null && e3[i - 1] !== null && Math.abs(e3[i - 1]) > EPSILON) {
            out[i] = (e3[i] - e3[i - 1]) / e3[i - 1] * 100.0;
        }
    }
    return out;
}

// ── Internal: EMA of a sparse array (with leading nulls) ─────────────────────
// Mirrors cpp/ af_ema behaviour: seeds from the first `period` consecutive values
// starting at the first non-null index, then runs the EMA forward.
// Nulls in the middle are NOT supported (matches cpp/ which uses NaN-filled arrays).
function emaOfSparse(values, period) {
    const n = values.length;
    const out = new Array(n).fill(null);
    // Find first non-null index
    let start = -1;
    for (let i = 0; i < n; i++) {
        if (values[i] !== null) { start = i; break; }
    }
    if (start < 0 || n - start < period) return out;
    const alpha = 2.0 / (period + 1);
    let seed = 0;
    for (let i = start; i < start + period; i++) seed += values[i];
    seed /= period;
    out[start + period - 1] = seed;
    let prev = seed;
    for (let i = start + period; i < n; i++) {
        prev = values[i] * alpha + prev * (1 - alpha);
        out[i] = prev;
    }
    return out;
}

// ── Momentum ──────────────────────────────────────────────────────────────────
// out[i] = src[i] - src[i-period]. First valid index: period (n <= period guard).
// Matches cpp/ af_momentum.
export function momentum(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n <= period) return out;
    for (let i = period; i < n; i++) {
        out[i] = values[i] - values[i - period];
    }
    return out;
}

// ── True Range ────────────────────────────────────────────────────────────────
// tr[0] = high[0] - low[0]; tr[i] = max(H-L, |H-prevC|, |L-prevC|).
// Matches cpp/ af_true_range.
export function trueRange(highs, lows, closes) {
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }
    const out = new Array(n);
    if (n === 0) return out;
    out[0] = highs[0] - lows[0];
    for (let i = 1; i < n; i++) {
        const pc = closes[i - 1];
        out[i] = Math.max(
            highs[i] - lows[i],
            Math.abs(highs[i] - pc),
            Math.abs(lows[i]  - pc),
        );
    }
    return out;
}

// ── Wilder EMA ────────────────────────────────────────────────────────────────
// Wilder smoothing: alpha = 1/period (matches cpp/ af_wilder_ema).
// Seed = average of first `period` values; then out[i] = src[i]*a + out[i-1]*(1-a).
export function wilderEma(values, period) {
    checkPeriod(period);
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n < period) return out;

    const alpha = 1.0 / period;
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

// ── VWMA (Volume Weighted Moving Average) ─────────────────────────────────────
// Sum(price*vol) / Sum(vol) over period. vol<=0 treated as 1. Matches cpp/ af_vwma.
export function vwma(values, volumes, period) {
    checkPeriod(period);
    const n = values.length;
    if (volumes.length !== n) {
        throw new Error("values and volumes must have the same length");
    }
    const out = new Array(n).fill(null);
    if (n < period) return out;

    const EPSILON = 1e-10;
    for (let i = period - 1; i < n; i++) {
        let pv = 0, v = 0;
        for (let j = 0; j < period; j++) {
            let vv = volumes[i - j];
            if (vv <= 0) vv = 1;
            pv += values[i - j] * vv;
            v  += vv;
        }
        out[i] = v > EPSILON ? pv / v : values[i];
    }
    return out;
}

// ── Historical Volatility ─────────────────────────────────────────────────────
// Annualized std-dev of log returns (sample std, period-1 denominator).
// tdays defaults to 252 (trading days per year), matching cpp/ af_hist_vol.
// First valid index: period (guard n <= period).
export function histVol(values, period, tdays = 252) {
    checkPeriod(period);
    if (!Number.isInteger(period) || period < 2) {
        throw new Error(`histVol period must be an integer >= 2, got ${period}`);
    }
    const n = values.length;
    const out = new Array(n).fill(null);
    if (n <= period) return out;

    const EPSILON = 1e-10;
    for (let i = period; i < n; i++) {
        let mean = 0;
        for (let j = 0; j < period; j++) {
            const p = values[i - 1 - j];
            if (p > EPSILON) mean += Math.log(values[i - j] / p);
        }
        mean /= period;
        let variance = 0;
        for (let j = 0; j < period; j++) {
            const p = values[i - 1 - j];
            if (p > EPSILON) {
                const r = Math.log(values[i - j] / p) - mean;
                variance += r * r;
            }
        }
        const sd = Math.sqrt(variance / (period - 1));
        out[i] = sd * Math.sqrt(tdays) * 100.0;
    }
    return out;
}

// ── CMF (Chaikin Money Flow) ──────────────────────────────────────────────────
// CLV = ((C-L)-(H-C))/(H-L); CMF = sum(CLV*V) / sum(V) over period.
// Matches cpp/ af_cmf.
export function cmf(highs, lows, closes, volumes, period = 20) {
    checkPeriod(period);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n || volumes.length !== n) {
        throw new Error("highs, lows, closes, volumes must all have the same length");
    }
    const out = new Array(n).fill(null);
    if (n < period) return out;

    const EPSILON = 1e-10;
    for (let i = period - 1; i < n; i++) {
        let mfvs = 0, vs = 0;
        for (let j = 0; j < period; j++) {
            const r = highs[i - j] - lows[i - j];
            const clv = r > EPSILON
                ? ((closes[i - j] - lows[i - j]) - (highs[i - j] - closes[i - j])) / r
                : 0;
            mfvs += clv * volumes[i - j];
            vs   += volumes[i - j];
        }
        out[i] = vs > EPSILON ? mfvs / vs : 0;
    }
    return out;
}

// ── Accumulation/Distribution ─────────────────────────────────────────────────
// out[0] = 0; out[i] = out[i-1] + CLV * volume.
// Matches cpp/ af_acc_dist.
export function accDist(highs, lows, closes, volumes) {
    const n = highs.length;
    if (lows.length !== n || closes.length !== n || volumes.length !== n) {
        throw new Error("highs, lows, closes, volumes must all have the same length");
    }
    const out = new Array(n);
    if (n === 0) return out;

    const EPSILON = 1e-10;
    for (let i = 0; i < n; i++) {
        const r = highs[i] - lows[i];
        const clv = r > EPSILON
            ? ((closes[i] - lows[i]) - (highs[i] - closes[i])) / r
            : 0;
        out[i] = (i === 0 ? 0 : out[i - 1]) + clv * volumes[i];
    }
    return out;
}

// ── Force Index ───────────────────────────────────────────────────────────────
// raw[0]=0; raw[i] = (close[i] - close[i-1]) * volume[i]; then EMA(raw, period).
// Matches cpp/ af_force_index.
export function forceIndex(closes, volumes, period = 13) {
    checkPeriod(period);
    const n = closes.length;
    if (volumes.length !== n) {
        throw new Error("closes and volumes must have the same length");
    }
    const raw = new Array(n);
    raw[0] = 0;
    for (let i = 1; i < n; i++) {
        raw[i] = (closes[i] - closes[i - 1]) * volumes[i];
    }
    return ema(raw, period);
}

// ── Volume Oscillator ─────────────────────────────────────────────────────────
// (EMA(vol, fast) - EMA(vol, slow)) / EMA(vol, slow) * 100.
// Null when slow EMA is null or ~0. Matches cpp/ af_vol_osc.
export function volOsc(volumes, fast = 14, slow = 28) {
    checkPeriod(fast);
    checkPeriod(slow);
    const n = volumes.length;
    const ef = ema(volumes, fast);
    const es = ema(volumes, slow);
    const out = new Array(n).fill(null);
    const EPSILON = 1e-10;
    for (let i = 0; i < n; i++) {
        if (ef[i] !== null && es[i] !== null && Math.abs(es[i]) > EPSILON) {
            out[i] = (ef[i] - es[i]) / es[i] * 100.0;
        }
    }
    return out;
}

// ── Donchian Channel ──────────────────────────────────────────────────────────
// upper = max(high, period); lower = min(low, period); middle = (upper+lower)/2.
// Matches cpp/ af_donchian.
export function donchian(highs, lows, period = 20) {
    checkPeriod(period);
    const n = highs.length;
    if (lows.length !== n) {
        throw new Error("highs and lows must have the same length");
    }
    const upper  = new Array(n).fill(null);
    const middle = new Array(n).fill(null);
    const lower  = new Array(n).fill(null);
    if (n < period) return { upper, middle, lower };

    for (let i = period - 1; i < n; i++) {
        let hi = highs[i], lo = lows[i];
        for (let j = 1; j < period; j++) {
            if (highs[i - j] > hi) hi = highs[i - j];
            if (lows[i - j]  < lo) lo = lows[i - j];
        }
        upper[i]  = hi;
        lower[i]  = lo;
        middle[i] = (hi + lo) * 0.5;
    }
    return { upper, middle, lower };
}

// ── Pivot Points (Classic) ────────────────────────────────────────────────────
// p = (H+L+C)/3; r1=2p-L; r2=p+(H-L); r3=p+2(H-L); s1=2p-H; s2=p-(H-L); s3=p-2(H-L).
// Matches cpp/ af_pivot_classic.
export function pivotClassic(high, low, close) {
    const p  = (high + low + close) / 3.0;
    const hl = high - low;
    return {
        p,
        r1: 2.0 * p - low,
        r2: p + hl,
        r3: p + 2.0 * hl,
        s1: 2.0 * p - high,
        s2: p - hl,
        s3: p - 2.0 * hl,
    };
}

// ── Pivot Points (Fibonacci) ──────────────────────────────────────────────────
// r1=p+r*0.382; r2=p+r*0.618; r3=p+r*1.000; s1..s3 mirror. Matches cpp/ af_pivot_fibonacci.
export function pivotFibonacci(high, low, close) {
    const p = (high + low + close) / 3.0;
    const r = high - low;
    return {
        p,
        r1: p + r * 0.382,
        r2: p + r * 0.618,
        r3: p + r * 1.000,
        s1: p - r * 0.382,
        s2: p - r * 0.618,
        s3: p - r * 1.000,
    };
}

// ── Pivot Points (Camarilla) ──────────────────────────────────────────────────
// r4=c+r*1.1/2; r3=c+r*1.1/4; r2=c+r*1.1/6; r1=c+r*1.1/12; s mirrored.
// Returns { r4, r3, r2, r1, s1, s2, s3, s4 }. Matches cpp/ af_pivot_camarilla.
export function pivotCamarilla(high, low, close) {
    const r = high - low;
    return {
        r4: close + r * 1.1 / 2.0,
        r3: close + r * 1.1 / 4.0,
        r2: close + r * 1.1 / 6.0,
        r1: close + r * 1.1 / 12.0,
        s1: close - r * 1.1 / 12.0,
        s2: close - r * 1.1 / 6.0,
        s3: close - r * 1.1 / 4.0,
        s4: close - r * 1.1 / 2.0,
    };
}

// ── Fibonacci Retracement ─────────────────────────────────────────────────────
// Levels at 0%, 23.6%, 38.2%, 50%, 61.8%, 78.6%, 100%.
// The JS API mirrors af_fibonacci with uptrend=true (default; swingHigh is the top).
// Returns 7 levels: level[i] = swingHigh - F[i] * (swingHigh - swingLow).
// Matches cpp/ af_fibonacci with up=true.
export function fibonacci(swingHigh, swingLow) {
    const F = [0.0, 0.236, 0.382, 0.500, 0.618, 0.786, 1.000];
    const r = swingHigh - swingLow;
    return F.map(f => swingHigh - f * r);
}

// ── SAR (Parabolic Stop and Reverse) ─────────────────────────────────────────
// Matches cpp/ af_sar exactly. Returns { sar: Array<number|null>, isBullish: Array<number|null> }.
// sar[0] = lows[0], isBullish[0] = 1.0; then iterates forward.
// isBullish uses 1.0 / 0.0 flags.
export function sar(highs, lows, start = 0.02, step = 0.02, maxAf = 0.2) {
    const n = highs.length;
    if (lows.length !== n) {
        throw new Error("highs and lows must have the same length");
    }
    const sarArr      = new Array(n).fill(null);
    const isBullish   = new Array(n).fill(null);
    if (n < 2) return { sar: sarArr, isBullish: isBullish };

    sarArr[0]    = lows[0];
    isBullish[0] = 1.0;
    let ep    = highs[0];
    let accel = start;

    for (let i = 1; i < n; i++) {
        const bull = isBullish[i - 1] > 0.5;
        let sarVal, nep = ep;

        if (bull) {
            sarVal = sarArr[i - 1] + accel * (ep - sarArr[i - 1]);
            sarVal = Math.min(sarVal, Math.min(lows[i - 1], i >= 2 ? lows[i - 2] : lows[i - 1]));
            if (lows[i] < sarVal) {
                sarVal = ep; nep = lows[i]; accel = start;
                isBullish[i] = 0.0;
            } else {
                if (highs[i] > ep) { nep = highs[i]; accel = Math.min(accel + step, maxAf); }
                isBullish[i] = 1.0;
            }
        } else {
            sarVal = sarArr[i - 1] - accel * (sarArr[i - 1] - ep);
            sarVal = Math.max(sarVal, Math.max(highs[i - 1], i >= 2 ? highs[i - 2] : highs[i - 1]));
            if (highs[i] > sarVal) {
                sarVal = ep; nep = highs[i]; accel = start;
                isBullish[i] = 1.0;
            } else {
                if (lows[i] < ep) { nep = lows[i]; accel = Math.min(accel + step, maxAf); }
                isBullish[i] = 0.0;
            }
        }
        ep = nep;
        sarArr[i] = sarVal;
    }
    return { sar: sarArr, isBullish };
}

// ── Ichimoku Cloud ────────────────────────────────────────────────────────────
// tenkan = (max(H,t) + min(L,t)) / 2 for tenkan bars.
// kijun  = (max(H,k) + min(L,k)) / 2 for kijun bars.
// senkouA[i+kijun] = (tenkan[i] + kijun[i]) / 2   (shifted forward by kijun).
// senkouB[i+kijun] = (max(H,sb) + min(L,sb)) / 2  (shifted forward by kijun).
// chikou[i-kijun]  = close[i]                      (shifted backward by kijun).
// The cpp/ uses shift=kijun for all displacements. All nulls where undefined.
// Matches cpp/ af_ichimoku with shift=kijun.
export function ichimoku(highs, lows, closes, tenkan = 9, kijun = 26, senkouB = 52) {
    checkPeriod(tenkan);
    checkPeriod(kijun);
    checkPeriod(senkouB);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }

    const tenkanArr  = new Array(n).fill(null);
    const kijunArr   = new Array(n).fill(null);
    const senkouAArr = new Array(n).fill(null);
    const senkouBArr = new Array(n).fill(null);
    const chikouArr  = new Array(n).fill(null);

    const shift = kijun;

    for (let i = 0; i < n; i++) {
        // Tenkan-sen
        if (i >= tenkan - 1) {
            let hi = highs[i], lo = lows[i];
            for (let j = 1; j < tenkan; j++) {
                if (highs[i - j] > hi) hi = highs[i - j];
                if (lows[i - j]  < lo) lo = lows[i - j];
            }
            tenkanArr[i] = (hi + lo) / 2.0;
        }
        // Kijun-sen
        if (i >= kijun - 1) {
            let hi = highs[i], lo = lows[i];
            for (let j = 1; j < kijun; j++) {
                if (highs[i - j] > hi) hi = highs[i - j];
                if (lows[i - j]  < lo) lo = lows[i - j];
            }
            kijunArr[i] = (hi + lo) / 2.0;
        }
    }

    // Senkou A: (tenkan + kijun) / 2, shifted forward by shift
    for (let i = 0; i + shift < n; i++) {
        if (tenkanArr[i] !== null && kijunArr[i] !== null) {
            senkouAArr[i + shift] = (tenkanArr[i] + kijunArr[i]) / 2.0;
        }
    }

    // Senkou B: (max H + min L over senkouB bars), shifted forward by shift
    for (let i = 0; i < n; i++) {
        if (i < senkouB - 1) continue;
        let hi = highs[i], lo = lows[i];
        for (let j = 1; j < senkouB; j++) {
            if (highs[i - j] > hi) hi = highs[i - j];
            if (lows[i - j]  < lo) lo = lows[i - j];
        }
        const dst = i + shift;
        if (dst < n) senkouBArr[dst] = (hi + lo) / 2.0;
    }

    // Chikou span: close[i] shifted back by shift → chikou[i-shift] = close[i]
    for (let i = shift; i < n; i++) {
        chikouArr[i - shift] = closes[i];
    }

    return {
        tenkan:  tenkanArr,
        kijun:   kijunArr,
        senkouA: senkouAArr,
        senkouB: senkouBArr,
        chikou:  chikouArr,
    };
}

// ── ADX (Average Directional Index) ──────────────────────────────────────────
// Wilder smoothing (alpha = 1/period) over TR, +DM, -DM.
// Requires n >= 2*period + 1 (matching cpp/ af_adx guard).
export function adx(highs, lows, closes, period = 14) {
    checkPeriod(period);
    const n = highs.length;
    if (lows.length !== n || closes.length !== n) {
        throw new Error("highs, lows, closes must all have the same length");
    }
    const adxArr    = new Array(n).fill(null);
    const plusDiArr = new Array(n).fill(null);
    const minusDiArr= new Array(n).fill(null);
    if (n < 2 * period + 1) return { adx: adxArr, plusDi: plusDiArr, minusDi: minusDiArr };

    const tr  = new Array(n);
    const pdm = new Array(n);
    const mdm = new Array(n);
    tr[0] = pdm[0] = mdm[0] = 0;
    for (let i = 1; i < n; i++) {
        const hl  = highs[i] - lows[i];
        const hpc = Math.abs(highs[i] - closes[i - 1]);
        const lpc = Math.abs(lows[i]  - closes[i - 1]);
        tr[i] = Math.max(hl, hpc, lpc);
        const up = highs[i] - highs[i - 1];
        const dn = lows[i - 1] - lows[i];
        pdm[i] = (up > dn && up > 0) ? up : 0;
        mdm[i] = (dn > up && dn > 0) ? dn : 0;
    }

    const EPSILON = 1e-10;
    const alpha = 1.0 / period;

    // seed: sum of tr/pdm/mdm from index 1..period (inclusive)
    let atrV = 0, pdmS = 0, mdmS = 0;
    for (let i = 1; i <= period; i++) {
        atrV += tr[i];
        pdmS += pdm[i];
        mdmS += mdm[i];
    }

    for (let i = period; i < n; i++) {
        if (i > period) {
            atrV = atrV * (1 - alpha) + tr[i];
            pdmS = pdmS * (1 - alpha) + pdm[i];
            mdmS = mdmS * (1 - alpha) + mdm[i];
        }
        const pdi = atrV > EPSILON ? 100.0 * pdmS / atrV : 0;
        const mdi = atrV > EPSILON ? 100.0 * mdmS / atrV : 0;
        plusDiArr[i]  = pdi;
        minusDiArr[i] = mdi;
        const denom = pdi + mdi;
        const dx = denom > EPSILON ? 100.0 * Math.abs(pdi - mdi) / denom : 0;
        adxArr[i] = (i === period) ? dx : adxArr[i - 1] * (1 - alpha) + dx * alpha;
    }

    return { adx: adxArr, plusDi: plusDiArr, minusDi: minusDiArr };
}
