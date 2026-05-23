/**
 * montecarlo.js — seeded LCG bootstrap Monte Carlo.
 * JS-specific: BigInt for 64-bit LCG state to avoid precision loss.
 * Spec §4 (Monte Carlo section) + §5 test series D.
 */

// LCG constants (spec §4)
const LCG_MUL = BigInt('6364136223846793005');
const LCG_ADD = BigInt('1442695040888963407');
const MASK64   = (1n << 64n) - 1n;

/**
 * Seeded LCG RNG class.
 * next() returns a uint32 (Number).
 */
export class SeededLCG {
    /**
     * @param {number|bigint} seed  uint64 seed (Number is cast to BigInt)
     */
    constructor(seed) {
        this._state = BigInt(seed) & MASK64;
    }

    /**
     * Advance state and return next uint32.
     * @returns {number}
     */
    next() {
        this._state = (this._state * LCG_MUL + LCG_ADD) & MASK64;
        return Number(this._state >> 33n);
    }
}

/**
 * Pointwise percentile across a list of equal-length paths.
 * @param {number[][]} paths  array of paths, each of length L
 * @param {number}     pct    percentile in [0,100]
 * @returns {number[]}  length L
 */
function pointwisePercentile(paths, pct) {
    if (paths.length === 0) return [];
    const L = paths[0].length;
    const result = new Array(L);
    const p = pct / 100;
    const n = paths.length;
    for (let t = 0; t < L; t++) {
        const vals = paths.map(path => path[t]);
        vals.sort((a, b) => a - b);
        const h = (n - 1) * p;
        const lo = Math.floor(h);
        const hi = Math.ceil(h);
        result[t] = lo === hi
            ? vals[lo]
            : vals[lo] + (h - lo) * (vals[hi] - vals[lo]);
    }
    return result;
}

/**
 * Percentile of a 1-D array (linear interpolation).
 * @param {number[]} arr
 * @param {number}   pct  in [0,100]
 * @returns {number}
 */
function pctile(arr, pct) {
    if (arr.length === 0) return NaN;
    const sorted = arr.slice().sort((a, b) => a - b);
    const n = sorted.length;
    const p = pct / 100;
    const h = (n - 1) * p;
    const lo = Math.floor(h);
    const hi = Math.ceil(h);
    if (lo === hi) return sorted[lo];
    return sorted[lo] + (h - lo) * (sorted[hi] - sorted[lo]);
}

/**
 * Run Monte Carlo bootstrap.
 *
 * @param {{
 *   trades:          Array<{netPnl: number}|number>,
 *   nRuns:           number,
 *   seed:            number|bigint,
 *   initialCapital?: number,
 * }} opts
 * @returns {{
 *   p5Final:    number,
 *   p50Final:   number,
 *   p95Final:   number,
 *   p5Path:     number[],
 *   p50Path:    number[],
 *   p95Path:    number[],
 *   probOfRuin: number,
 *   finalEqs:   number[],
 * }}
 */
export function mcBootstrap({ trades, nRuns, seed, initialCapital = 10000 }) {
    const rng = new SeededLCG(seed);
    const m = trades.length;
    if (m === 0) {
        return {
            p5Final: initialCapital, p50Final: initialCapital, p95Final: initialCapital,
            p5Path: [initialCapital], p50Path: [initialCapital], p95Path: [initialCapital],
            probOfRuin: 0, finalEqs: [],
        };
    }

    // Normalise trades to net PnL numbers
    const pnls = trades.map(t => (typeof t === 'number' ? t : t.netPnl));

    const finalEqs = new Array(nRuns);
    const paths = new Array(nRuns);

    for (let run = 0; run < nRuns; run++) {
        const path = new Array(m + 1);
        path[0] = initialCapital;
        let eq = initialCapital;
        for (let i = 0; i < m; i++) {
            const idx = rng.next() % m;
            eq += pnls[idx];
            path[i + 1] = eq;
        }
        finalEqs[run] = eq;
        paths[run] = path;
    }

    // Prob of ruin: fraction of runs where min(path) <= 0
    let ruinCount = 0;
    for (let run = 0; run < nRuns; run++) {
        const path = paths[run];
        let ruin = false;
        for (const v of path) {
            if (v <= 0) { ruin = true; break; }
        }
        if (ruin) ruinCount++;
    }

    return {
        p5Final:    pctile(finalEqs, 5),
        p50Final:   pctile(finalEqs, 50),
        p95Final:   pctile(finalEqs, 95),
        p5Path:     pointwisePercentile(paths, 5),
        p50Path:    pointwisePercentile(paths, 50),
        p95Path:    pointwisePercentile(paths, 95),
        probOfRuin: ruinCount / nRuns,
        finalEqs,
    };
}
