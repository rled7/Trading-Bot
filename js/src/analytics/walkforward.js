/**
 * walkforward.js — anchored + rolling walk-forward harnesses.
 * backtest_fn may return a plain object or a Promise.
 * Spec §4 (Walk-forward section).
 */

/**
 * Anchored walk-forward.
 * Train slice always starts at bar 0; test slice follows immediately.
 *
 * @param {function} backtestFn  (trainBars, testBars) -> {train, test} or Promise
 * @param {any[]}    bars        full bar array
 * @param {number}   nFolds      number of folds
 * @param {number}   trainMinBars minimum bars for first training window
 * @returns {Promise<Array<{fold:number, trainBars:any[], testBars:any[], result:any}>>}
 */
export async function walkforwardAnchored(backtestFn, bars, nFolds, trainMinBars) {
    const n = bars.length;
    if (nFolds <= 0 || n === 0) return [];
    const results = [];
    const extra = n - trainMinBars;

    for (let k = 0; k < nFolds; k++) {
        const trainEnd = trainMinBars + Math.floor(k * extra / nFolds);
        const testLen  = Math.floor(extra / nFolds);
        const testStart = trainEnd;
        const testEnd   = testStart + testLen;

        if (testEnd > n) break;
        if (trainEnd <= 0 || testLen <= 0) continue;

        const trainBars = bars.slice(0, trainEnd);
        const testBars  = bars.slice(testStart, testEnd);

        const result = await Promise.resolve(backtestFn(trainBars, testBars));
        results.push({ fold: k, trainBars, testBars, result });
    }
    return results;
}

/**
 * Rolling walk-forward.
 * Sliding train/test windows stepped by `step` bars.
 *
 * @param {function} backtestFn  (trainBars, testBars) -> {train, test} or Promise
 * @param {any[]}    bars
 * @param {number}   trainWindow  number of bars in training window
 * @param {number}   testWindow   number of bars in test window
 * @param {number}   step         bars to advance each fold
 * @returns {Promise<Array<{fold:number, trainStart:number, testStart:number, result:any}>>}
 */
export async function walkforwardRolling(backtestFn, bars, trainWindow, testWindow, step) {
    const n = bars.length;
    if (n === 0 || step <= 0) return [];
    const results = [];
    let fold = 0;

    for (let i = 0; i + trainWindow + testWindow <= n; i += step) {
        const trainBars = bars.slice(i, i + trainWindow);
        const testBars  = bars.slice(i + trainWindow, i + trainWindow + testWindow);
        const result = await Promise.resolve(backtestFn(trainBars, testBars));
        results.push({
            fold,
            trainStart: i,
            testStart:  i + trainWindow,
            result,
        });
        fold++;
    }
    return results;
}
