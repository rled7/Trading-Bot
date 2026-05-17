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
