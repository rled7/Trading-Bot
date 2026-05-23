/**
 * metrics.js — 14 performance metrics from spec §4.
 * All formulas use sample std (N-1 denominator). A = sqrt(252).
 * No external dependencies.
 */

const SQRT_252 = Math.sqrt(252);
const EPS = 1e-14;

// ── Internal helpers ──────────────────────────────────────────────────────────

/**
 * Compute sample mean and sample std of an array.
 * @param {number[]} xs
 * @returns {{ mean: number, std: number, n: number }}
 */
function sampleStats(xs) {
    const n = xs.length;
    if (n === 0) return { mean: 0, std: 0, n: 0 };
    let sum = 0;
    for (const x of xs) sum += x;
    const mean = sum / n;
    if (n === 1) return { mean, std: 0, n };
    let sq = 0;
    for (const x of xs) { const d = x - mean; sq += d * d; }
    return { mean, std: Math.sqrt(sq / (n - 1)), n };
}

/**
 * Compute bar_returns from equity curve: (eq[i]-eq[i-1])/eq[i-1].
 * @param {number[]} eq
 * @returns {number[]}
 */
export function barReturns(eq) {
    const out = [];
    for (let i = 1; i < eq.length; i++) {
        const prev = eq[i - 1];
        out.push(prev === 0 ? 0 : (eq[i] - prev) / prev);
    }
    return out;
}

// ── Public metrics ────────────────────────────────────────────────────────────

/**
 * Sharpe ratio: A * mean(r) / std(r). 0 if std==0 or n<2.
 * @param {number[]} barRets  fractional bar returns
 * @param {number}   [annFactor=sqrt(252)]
 * @returns {number}
 */
export function sharpe(barRets, annFactor = SQRT_252) {
    const { mean, std, n } = sampleStats(barRets);
    if (n < 2 || std < EPS) return 0;
    return annFactor * mean / std;
}

/**
 * Sortino ratio.
 * Downside std = sqrt( sum(min(r,0)^2) / (n-1) ) — n is full sample size,
 * demeaned at 0 (not at mean). Matches spec §5 convention.
 * @param {number[]} barRets
 * @param {number}   [annFactor=sqrt(252)]
 * @returns {number}
 */
export function sortino(barRets, annFactor = SQRT_252) {
    const n = barRets.length;
    if (n < 2) return 0;
    let sumNeg = 0;
    for (const r of barRets) {
        const d = Math.min(r, 0);
        sumNeg += d * d;
    }
    const downStd = Math.sqrt(sumNeg / (n - 1));
    if (downStd < EPS) return 0;
    let sum = 0;
    for (const r of barRets) sum += r;
    const mean = sum / n;
    return annFactor * mean / downStd;
}

/**
 * Calmar ratio: totalReturnPct / maxDrawdownPct. 0 if mdd==0.
 * @param {number} totalReturnPct  e.g. 15.3 for 15.3%
 * @param {number} maxDdPct        positive number e.g. 3.92
 * @returns {number}
 */
export function calmar(totalReturnPct, maxDdPct) {
    if (Math.abs(maxDdPct) < EPS) return 0;
    return totalReturnPct / Math.abs(maxDdPct);
}

/**
 * Omega ratio: sum(max(r-thr,0)) / sum(max(thr-r,0)), threshold=0.
 * Returns Infinity if denominator==0, 0 if numerator==0.
 * @param {number[]} barRets
 * @param {number}   [threshold=0]
 * @returns {number}
 */
export function omega(barRets, threshold = 0) {
    let pos = 0, neg = 0;
    for (const r of barRets) {
        pos += Math.max(r - threshold, 0);
        neg += Math.max(threshold - r, 0);
    }
    if (neg < EPS) return pos < EPS ? 0 : Infinity;
    return pos / neg;
}

/**
 * Profit factor: sum(wins) / abs(sum(losses)).
 * Infinity if no losses; 0 if no wins.
 * @param {number[]} tradePnls  net PnL per trade
 * @returns {number}
 */
export function profitFactor(tradePnls) {
    let wins = 0, losses = 0;
    for (const p of tradePnls) {
        if (p > 0) wins += p;
        else if (p < 0) losses += -p;
    }
    if (losses < EPS) return wins < EPS ? 0 : Infinity;
    return wins / losses;
}

/**
 * Win rate: count(pnl > 0) / count(trades).
 * @param {number[]} tradePnls
 * @returns {number}
 */
export function winRate(tradePnls) {
    if (tradePnls.length === 0) return 0;
    let wins = 0;
    for (const p of tradePnls) if (p > 0) wins++;
    return wins / tradePnls.length;
}

/**
 * Expectancy: mean(net_pnl).
 * @param {number[]} tradePnls
 * @returns {number}
 */
export function expectancy(tradePnls) {
    if (tradePnls.length === 0) return 0;
    let s = 0;
    for (const p of tradePnls) s += p;
    return s / tradePnls.length;
}

/**
 * Average winning trade PnL. 0 if no winners.
 * @param {number[]} tradePnls
 * @returns {number}
 */
export function avgWin(tradePnls) {
    const ws = tradePnls.filter(p => p > 0);
    if (ws.length === 0) return 0;
    let s = 0;
    for (const p of ws) s += p;
    return s / ws.length;
}

/**
 * Average losing trade PnL (negative value). 0 if no losers.
 * @param {number[]} tradePnls
 * @returns {number}
 */
export function avgLoss(tradePnls) {
    const ls = tradePnls.filter(p => p < 0);
    if (ls.length === 0) return 0;
    let s = 0;
    for (const p of ls) s += p;
    return s / ls.length;
}

/**
 * Payoff ratio: avgWin / abs(avgLoss). Infinity if no losers.
 * @param {number[]} tradePnls
 * @returns {number}
 */
export function payoffRatio(tradePnls) {
    const aw = avgWin(tradePnls);
    const al = avgLoss(tradePnls);
    if (Math.abs(al) < EPS) return Infinity;
    return aw / Math.abs(al);
}

/**
 * Recovery factor: totalPnl / maxDrawdownAbs. 0 if mdd_abs==0.
 * @param {number} totalPnl
 * @param {number} maxDdAbs  positive number (absolute drawdown magnitude)
 * @returns {number}
 */
export function recoveryFactor(totalPnl, maxDdAbs) {
    if (Math.abs(maxDdAbs) < EPS) return 0;
    return totalPnl / Math.abs(maxDdAbs);
}

/**
 * Time in market: sum(barsHeld) / totalBars.
 * @param {number[]} barsHeldArr  barsHeld per trade
 * @param {number}   totalBars
 * @returns {number}
 */
export function timeInMarket(barsHeldArr, totalBars) {
    if (totalBars <= 0) return 0;
    let s = 0;
    for (const b of barsHeldArr) s += b;
    return s / totalBars;
}

/**
 * Longest consecutive winning streak (net_pnl > 0).
 * @param {number[]} tradePnls
 * @returns {number}
 */
export function longestWinStreak(tradePnls) {
    let best = 0, cur = 0;
    for (const p of tradePnls) {
        if (p > 0) { cur++; if (cur > best) best = cur; }
        else cur = 0;
    }
    return best;
}

/**
 * Longest consecutive losing streak (net_pnl <= 0).
 * @param {number[]} tradePnls
 * @returns {number}
 */
export function longestLossStreak(tradePnls) {
    let best = 0, cur = 0;
    for (const p of tradePnls) {
        if (p <= 0) { cur++; if (cur > best) best = cur; }
        else cur = 0;
    }
    return best;
}

/** Alias for longestLossStreak (spec ergonomics). */
export const maxConsecLosses = longestLossStreak;

/**
 * Compute all 14 metrics in one call.
 * @param {{
 *   tradePnls:   number[],
 *   barRets:     number[],
 *   barsHeld:    number[],
 *   totalBars:   number,
 *   totalPnl:    number,
 *   maxDdPct:    number,
 *   maxDdAbs:    number,
 *   annFactor?:  number,
 * }} opts
 * @returns {object}
 */
export function allMetrics({
    tradePnls,
    barRets,
    barsHeld   = [],
    totalBars  = 0,
    totalPnl   = null,
    maxDdPct   = 0,
    maxDdAbs   = 0,
    annFactor  = SQRT_252,
}) {
    const aw = avgWin(tradePnls);
    const al = avgLoss(tradePnls);
    const wr = winRate(tradePnls);
    const tp = totalPnl !== null ? totalPnl
        : tradePnls.reduce((s, p) => s + p, 0);
    const totalReturnPct = maxDdAbs !== 0
        ? tp / Math.abs(tp) * Math.abs(maxDdPct) * (tp / Math.abs(maxDdAbs))
        : 0;
    // totalReturnPct for calmar: we compute from pnls directly if needed
    // The caller typically passes totalReturnPct as totalPnl/initialCapital*100
    const calmarVal = calmar(totalReturnPct, maxDdPct);

    return {
        sharpe:            sharpe(barRets, annFactor),
        sortino:           sortino(barRets, annFactor),
        calmar:            calmarVal,
        omega:             omega(barRets),
        profitFactor:      profitFactor(tradePnls),
        winRate:           wr,
        expectancy:        expectancy(tradePnls),
        avgWin:            aw,
        avgLoss:           al,
        payoffRatio:       payoffRatio(tradePnls),
        recoveryFactor:    recoveryFactor(tp, maxDdAbs),
        timeInMarket:      timeInMarket(barsHeld, totalBars),
        longestWinStreak:  longestWinStreak(tradePnls),
        longestLossStreak: longestLossStreak(tradePnls),
        maxConsecLosses:   longestLossStreak(tradePnls),
        nTrades:           tradePnls.length,
        nWins:             tradePnls.filter(p => p > 0).length,
        nLosses:           tradePnls.filter(p => p <= 0).length,
        totalPnl:          tp,
        maxDdPct,
        maxDdAbs,
    };
}
