/**
 * drawdown.js — drawdown series, episodes, and top-5 summary.
 * Spec §4 (Drawdown section).
 */

/**
 * Compute peak array and drawdown series from equity curve.
 * @param {number[]} eq  equity curve, strictly positive values expected
 * @returns {{
 *   peaks:    number[],
 *   ddPct:    number[],
 *   ddAbs:    number[],
 *   maxDdPct: number,
 *   maxDdAbs: number,
 * }}
 */
export function drawdownSeries(eq) {
    const n = eq.length;
    if (n === 0) {
        return { peaks: [], ddPct: [], ddAbs: [], maxDdPct: 0, maxDdAbs: 0 };
    }
    const peaks = new Array(n);
    const ddPct = new Array(n);
    const ddAbs = new Array(n);
    let peak = eq[0];
    let maxDdPct = 0;
    let maxDdAbs = 0;

    for (let i = 0; i < n; i++) {
        if (eq[i] > peak) peak = eq[i];
        peaks[i] = peak;
        const pct = peak === 0 ? 0 : (eq[i] - peak) / peak * 100;
        const abs = eq[i] - peak;
        ddPct[i] = pct;
        ddAbs[i] = abs;
        if (pct < maxDdPct) maxDdPct = pct;
        if (abs < maxDdAbs) maxDdAbs = abs;
    }

    return { peaks, ddPct, ddAbs, maxDdPct, maxDdAbs };
}

/**
 * Compute drawdown episodes: contiguous runs of ddPct < 0.
 * Each episode: { startIdx, troughIdx, recoverIdx, depthPct, depthAbs, durationBars, recoveryBars }
 * recoverIdx = -1 if not recovered by end of series.
 * @param {number[]} eq
 * @returns {Array<{startIdx:number,troughIdx:number,recoverIdx:number,depthPct:number,depthAbs:number,durationBars:number,recoveryBars:number}>}
 */
export function drawdownEpisodes(eq) {
    const n = eq.length;
    if (n === 0) return [];
    const { peaks, ddPct, ddAbs } = drawdownSeries(eq);

    const episodes = [];
    let inDd = false;
    let startIdx = -1;
    let troughIdx = -1;
    let depthPct = 0;
    let depthAbs = 0;

    for (let i = 0; i < n; i++) {
        if (!inDd) {
            if (ddPct[i] < 0) {
                // Start of new episode
                inDd = true;
                startIdx = i;
                troughIdx = i;
                depthPct = ddPct[i];
                depthAbs = ddAbs[i];
            }
        } else {
            // Inside drawdown
            if (ddPct[i] < depthPct) {
                depthPct = ddPct[i];
                depthAbs = ddAbs[i];
                troughIdx = i;
            }
            if (ddPct[i] === 0) {
                // Recovered
                const durationBars = i - startIdx;
                const recoveryBars = i - troughIdx;
                episodes.push({
                    startIdx,
                    troughIdx,
                    recoverIdx: i,
                    depthPct,
                    depthAbs,
                    durationBars,
                    recoveryBars,
                });
                inDd = false;
                startIdx = -1;
                troughIdx = -1;
                depthPct = 0;
                depthAbs = 0;
            }
        }
    }

    // Handle open episode at end of series
    if (inDd) {
        const durationBars = (n - 1) - startIdx;
        episodes.push({
            startIdx,
            troughIdx,
            recoverIdx: -1,
            depthPct,
            depthAbs,
            durationBars,
            recoveryBars: -1,
        });
    }

    return episodes;
}

/**
 * Return top-5 drawdown episodes sorted by depthPct ascending (deepest first).
 * @param {number[]} eq
 * @returns {Array}
 */
export function top5Drawdowns(eq) {
    const eps = drawdownEpisodes(eq);
    eps.sort((a, b) => a.depthPct - b.depthPct);
    return eps.slice(0, 5);
}
