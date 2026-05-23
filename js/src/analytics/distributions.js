/**
 * distributions.js — skewness, excess kurtosis, quantiles, full stats.
 * Spec §4 (Distributions section).
 * Uses Fisher-Pearson adjusted skewness and Fisher's adjusted kurtosis.
 */

/**
 * Sample mean.
 * @param {number[]} xs
 * @returns {number}
 */
function mean(xs) {
    if (xs.length === 0) return 0;
    let s = 0;
    for (const x of xs) s += x;
    return s / xs.length;
}

/**
 * Sample (biased) standard deviation — denominator n.
 * @param {number[]} xs
 * @param {number}   mu  precomputed mean
 * @returns {number}
 */
function stdBiased(xs, mu) {
    if (xs.length < 2) return 0;
    let s = 0;
    for (const x of xs) { const d = x - mu; s += d * d; }
    return Math.sqrt(s / xs.length);
}

/**
 * Fisher-Pearson adjusted skewness (G1).
 * Returns 0 for n < 3.
 * @param {number[]} xs
 * @returns {number}
 */
export function skewness(xs) {
    const n = xs.length;
    if (n < 3) return 0;
    const mu = mean(xs);
    const sb = stdBiased(xs, mu);
    if (sb < 1e-14) return 0;
    let s = 0;
    for (const x of xs) { const d = (x - mu) / sb; s += d * d * d; }
    const g1 = s / n;
    return Math.sqrt(n * (n - 1)) / (n - 2) * g1;
}

/**
 * Fisher's excess kurtosis (adjusted for sample).
 * Returns 0 for n < 4.
 * @param {number[]} xs
 * @returns {number}
 */
export function excessKurtosis(xs) {
    const n = xs.length;
    if (n < 4) return 0;
    const mu = mean(xs);
    const sb = stdBiased(xs, mu);
    if (sb < 1e-14) return 0;
    let s = 0;
    for (const x of xs) { const d = (x - mu) / sb; s += d * d * d * d; }
    const g2 = s / n - 3;
    return ((n - 1) / ((n - 2) * (n - 3))) * ((n + 1) * g2 + 6);
}

/**
 * Linear interpolation quantile (R-7 / numpy method='linear').
 * @param {number[]} xs   data array (need not be sorted)
 * @param {number}   p    quantile in [0, 1]
 * @returns {number}
 */
export function quantile(xs, p) {
    const n = xs.length;
    if (n === 0) return NaN;
    if (n === 1) return xs[0];
    const sorted = xs.slice().sort((a, b) => a - b);
    const h = (n - 1) * p;
    const lo = Math.floor(h);
    const hi = Math.ceil(h);
    if (lo === hi) return sorted[lo];
    return sorted[lo] + (h - lo) * (sorted[hi] - sorted[lo]);
}

/**
 * Full distribution statistics for an array.
 * @param {number[]} xs
 * @returns {{
 *   n: number, mean: number, std: number, skewness: number,
 *   excessKurtosis: number,
 *   min: number, max: number,
 *   p5: number, p25: number, p50: number, p75: number, p95: number,
 * }}
 */
export function distributionStats(xs) {
    const n = xs.length;
    if (n === 0) {
        return {
            n: 0, mean: 0, std: 0, skewness: 0, excessKurtosis: 0,
            min: NaN, max: NaN,
            p5: NaN, p25: NaN, p50: NaN, p75: NaN, p95: NaN,
        };
    }
    const mu = mean(xs);
    // sample std (n-1)
    let sq = 0;
    for (const x of xs) { const d = x - mu; sq += d * d; }
    const sampleStd = n > 1 ? Math.sqrt(sq / (n - 1)) : 0;

    let mn = xs[0], mx = xs[0];
    for (const x of xs) { if (x < mn) mn = x; if (x > mx) mx = x; }

    return {
        n,
        mean:           mu,
        std:            sampleStd,
        skewness:       skewness(xs),
        excessKurtosis: excessKurtosis(xs),
        min:            mn,
        max:            mx,
        p5:             quantile(xs, 0.05),
        p25:            quantile(xs, 0.25),
        p50:            quantile(xs, 0.50),
        p75:            quantile(xs, 0.75),
        p95:            quantile(xs, 0.95),
    };
}
