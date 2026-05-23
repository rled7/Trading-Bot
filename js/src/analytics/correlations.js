/**
 * correlations.js — Pearson correlation, matrix, rolling beta.
 * Spec §4 (Correlations section).
 */

/**
 * Pearson correlation between two same-length series.
 * Returns 0 if either series has zero variance or n < 2.
 * @param {number[]} x
 * @param {number[]} y
 * @returns {number}  in [-1, 1]
 */
export function pearson(x, y) {
    const n = x.length;
    if (n < 2 || y.length < 2) return 0;
    let mx = 0, my = 0;
    for (let i = 0; i < n; i++) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;
    let cov = 0, vx = 0, vy = 0;
    for (let i = 0; i < n; i++) {
        const dx = x[i] - mx;
        const dy = y[i] - my;
        cov += dx * dy;
        vx  += dx * dx;
        vy  += dy * dy;
    }
    const denom = Math.sqrt(vx * vy);
    if (denom < 1e-14) return 0;
    return cov / denom;
}

/**
 * Correlation matrix for a dict of return series.
 * All series are truncated to the shortest common length.
 * @param {Record<string, number[]>} returnsPerSymbol
 * @returns {{ symbols: string[], matrix: number[][] }}
 */
export function correlationMatrix(returnsPerSymbol) {
    const symbols = Object.keys(returnsPerSymbol);
    const k = symbols.length;
    if (k === 0) return { symbols: [], matrix: [] };

    // Truncate to shortest
    let minLen = Infinity;
    for (const s of symbols) {
        const l = returnsPerSymbol[s].length;
        if (l < minLen) minLen = l;
    }
    if (minLen === Infinity) minLen = 0;

    const series = symbols.map(s => returnsPerSymbol[s].slice(0, minLen));
    const matrix = [];
    for (let i = 0; i < k; i++) {
        const row = new Array(k);
        for (let j = 0; j < k; j++) {
            row[j] = i === j ? 1 : pearson(series[i], series[j]);
        }
        matrix.push(row);
    }
    return { symbols, matrix };
}

/**
 * Rolling beta of target vs baseline over a sliding window.
 * beta_i = cov(target[i-w+1..i], baseline[i-w+1..i]) / var(baseline[i-w+1..i])
 * Returns null for positions before window is full.
 * @param {number[]} target
 * @param {number[]} baseline
 * @param {number}   window
 * @returns {Array<number|null>}
 */
export function rollingBeta(target, baseline, window) {
    const n = target.length;
    const out = new Array(n).fill(null);
    for (let i = window - 1; i < n; i++) {
        const tx = target.slice(i - window + 1, i + 1);
        const bx = baseline.slice(i - window + 1, i + 1);
        let mt = 0, mb = 0;
        for (let j = 0; j < window; j++) { mt += tx[j]; mb += bx[j]; }
        mt /= window; mb /= window;
        let cov = 0, varB = 0;
        for (let j = 0; j < window; j++) {
            cov  += (tx[j] - mt) * (bx[j] - mb);
            varB += (bx[j] - mb) * (bx[j] - mb);
        }
        out[i] = varB < 1e-14 ? 0 : cov / varB;
    }
    return out;
}
