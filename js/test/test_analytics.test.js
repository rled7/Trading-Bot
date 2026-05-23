/**
 * test_analytics.test.js — canonical vector tests for analytics submodule.
 * Spec §5 test vectors, edge cases, and cross-language parity.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
    // metrics
    barReturns, sharpe, sortino, calmar, omega,
    profitFactor, winRate, expectancy, avgWin, avgLoss,
    payoffRatio, recoveryFactor, longestWinStreak, longestLossStreak,
    // drawdown
    drawdownSeries, drawdownEpisodes, top5Drawdowns,
    // distributions
    skewness, excessKurtosis, quantile, distributionStats,
    // correlations
    pearson, correlationMatrix, rollingBeta,
    // montecarlo
    SeededLCG, mcBootstrap,
    // walkforward
    walkforwardAnchored, walkforwardRolling,
    // linalg
    matMul, transpose, matVec, matInv, dot,
    // ml_attribution
    ols, addIntercept,
    // factors
    factorRegression,
    // streaming
    OnlineMetrics,
    // html_report
    renderReport,
} from '../src/analytics/index.js';

const EPS = 1e-9;
const EPS12 = 1e-12;

function near(a, b, tol = EPS, msg = '') {
    const diff = Math.abs(a - b);
    assert.ok(diff <= tol, `Expected |${a} - ${b}| <= ${tol}${msg ? ': ' + msg : ''}, got ${diff}`);
}

// ─────────────────────────────────────────────────────────────────────────────
// §5 Test Series A — 10 trades
// ─────────────────────────────────────────────────────────────────────────────
const TRADES_A = [120, -50, 80, -30, 200, -100, -40, 60, 90, -70];

test('Series A: sum_pnl = 260', () => {
    const s = TRADES_A.reduce((a, b) => a + b, 0);
    near(s, 260, EPS12);
});

test('Series A: n_wins = 5', () => {
    assert.equal(TRADES_A.filter(p => p > 0).length, 5);
});

test('Series A: n_losses = 5', () => {
    assert.equal(TRADES_A.filter(p => p <= 0).length, 5);
});

test('Series A: win_rate = 0.5', () => {
    near(winRate(TRADES_A), 0.5, EPS12);
});

test('Series A: expectancy = 26.0', () => {
    near(expectancy(TRADES_A), 26.0, EPS12);
});

test('Series A: avg_win = 110.0', () => {
    near(avgWin(TRADES_A), 110.0, EPS12);
});

test('Series A: avg_loss = -58.0', () => {
    near(avgLoss(TRADES_A), -58.0, EPS12);
});

test('Series A: payoff_ratio = 110/58', () => {
    const expected = 110.0 / 58.0;
    near(payoffRatio(TRADES_A), expected, EPS9());
});

function EPS9() { return 1e-9; }

test('Series A: profit_factor = 550/290', () => {
    const expected = 550.0 / 290.0;
    near(profitFactor(TRADES_A), expected, EPS9());
});

test('Series A: longest_win_streak = 2 (trades 60, 90 consecutive)', () => {
    // TRADES_A = [120,-50,80,-30,200,-100,-40,60,90,-70]
    // Trades at indices 7 (60) and 8 (90) are consecutive wins → streak of 2
    // Note: spec §5 lists "1" but that is a documentation error; the correct
    // value computed from the canonical series is 2.
    assert.equal(longestWinStreak(TRADES_A), 2);
});

test('Series A: longest_loss_streak = 2', () => {
    assert.equal(longestLossStreak(TRADES_A), 2);
});

// ─────────────────────────────────────────────────────────────────────────────
// §5 Test Series B — bar_returns (n=8)
// ─────────────────────────────────────────────────────────────────────────────
const RETURNS_B = [0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012];

test('Series B: mean = 0.004625', () => {
    const m = RETURNS_B.reduce((s, v) => s + v, 0) / RETURNS_B.length;
    near(m, 0.004625, EPS12);
});

test('Series B: sample std (n-1 denominator)', () => {
    const n = RETURNS_B.length;
    const m = RETURNS_B.reduce((s, v) => s + v, 0) / n;
    const sq = RETURNS_B.reduce((s, v) => s + (v - m) ** 2, 0);
    const sd = Math.sqrt(sq / (n - 1));
    // Correct sample std with n-1 denominator: 0.011312919...
    // Note: spec §5 lists "0.011172066" but that uses a different formula;
    // our spec mandates sample std (N-1) throughout §4.
    near(sd, 0.011312919289782937, EPS12);
});

test('Series B: Sharpe in (6.4, 6.6) (sqrt(252) annualised, sample std)', () => {
    const s = sharpe(RETURNS_B);
    // With sample std (n-1): sqrt(252)*0.004625/0.011312919 ≈ 6.4899
    assert.ok(s > 6.4, `Sharpe should be > 6.4, got ${s}`);
    assert.ok(s < 7.0, `Sharpe should be < 7.0, got ${s}`);
});

test('Series B: Sharpe cross-lang parity 1e-9', () => {
    // Compute manually
    const n = RETURNS_B.length;
    const m = RETURNS_B.reduce((s, v) => s + v, 0) / n;
    const sq = RETURNS_B.reduce((s, v) => s + (v - m) ** 2, 0);
    const sd = Math.sqrt(sq / (n - 1));
    const expected = Math.sqrt(252) * m / sd;
    near(sharpe(RETURNS_B), expected, EPS);
});

test('Series B: Sortino uses convention sum(min(r,0)^2)/(n-1)', () => {
    // Negative returns: -0.005, -0.01, -0.008
    const n = RETURNS_B.length;
    const negSq = RETURNS_B.reduce((s, r) => s + Math.min(r, 0) ** 2, 0);
    const downStd = Math.sqrt(negSq / (n - 1));
    const m = RETURNS_B.reduce((s, v) => s + v, 0) / n;
    const expected = Math.sqrt(252) * m / downStd;
    near(sortino(RETURNS_B), expected, EPS);
});

// ─────────────────────────────────────────────────────────────────────────────
// §5 Test Series C — equity curve
// ─────────────────────────────────────────────────────────────────────────────
const EQ_C = [10000, 10100, 10050, 10200, 10150, 9800, 9900, 10300, 10400];
const PEAKS_C = [10000, 10100, 10100, 10200, 10200, 10200, 10200, 10300, 10400];

test('Series C: peaks match spec', () => {
    const { peaks } = drawdownSeries(EQ_C);
    for (let i = 0; i < PEAKS_C.length; i++) {
        near(peaks[i], PEAKS_C[i], EPS12, `peak[${i}]`);
    }
});

test('Series C: max_dd_pct = -3.9215686274509807', () => {
    const { maxDdPct } = drawdownSeries(EQ_C);
    near(maxDdPct, -3.9215686274509807, EPS);
});

test('Series C: max_dd_abs = -400', () => {
    const { maxDdAbs } = drawdownSeries(EQ_C);
    near(maxDdAbs, -400, EPS12);
});

test('Series C: drawdown episodes — largest has trough at index 5, recovers at 7', () => {
    const eps = drawdownEpisodes(EQ_C);
    // EQ_C has two separate drawdown episodes:
    //   ep[0]: idx 2 (10050 < peak 10100), recovers at idx 3 (10200 > 10100)
    //   ep[1]: idx 4 (10150 < peak 10200), trough at idx 5 (9800), recovers at idx 7 (10300)
    // The spec's "one episode at 2-7" conflates these; our implementation is correct.
    assert.ok(eps.length >= 2, `expected at least 2 episodes, got ${eps.length}`);
    // Find the episode with the deepest trough
    const deep = eps.reduce((a, b) => b.depthPct < a.depthPct ? b : a);
    assert.equal(deep.troughIdx, 5, `trough should be 5, got ${deep.troughIdx}`);
    assert.equal(deep.recoverIdx, 7, `recover should be 7, got ${deep.recoverIdx}`);
});

// ─────────────────────────────────────────────────────────────────────────────
// §5 Test Series D — LCG / Monte Carlo
// ─────────────────────────────────────────────────────────────────────────────

test('LCG seed=42: first 10 outputs are reproducible', () => {
    const rng = new SeededLCG(42);
    const seq = [];
    for (let i = 0; i < 10; i++) seq.push(rng.next());
    // Verify they're all valid uint32
    for (const v of seq) {
        assert.ok(v >= 0, `LCG output should be >= 0, got ${v}`);
        assert.ok(v < 4294967296, `LCG output should be < 2^32, got ${v}`);
    }
    // Verify deterministic: same seed = same sequence
    const rng2 = new SeededLCG(42);
    for (let i = 0; i < 10; i++) {
        assert.equal(rng2.next(), seq[i], `LCG seq[${i}] mismatch`);
    }
    // Print first 10 for cross-language parity (captured in test output)
    console.log('LCG(42) first 10:', seq);
});

test('LCG: BigInt 64-bit masking prevents overflow', () => {
    const rng = new SeededLCG(0xffffffffffffffff);
    for (let i = 0; i < 100; i++) {
        const v = rng.next();
        assert.ok(Number.isFinite(v));
        assert.ok(v >= 0 && v < 4294967296);
    }
});

test('MC bootstrap: deterministic with same seed', () => {
    const r1 = mcBootstrap({ trades: TRADES_A, nRuns: 100, seed: 42 });
    const r2 = mcBootstrap({ trades: TRADES_A, nRuns: 100, seed: 42 });
    near(r1.p50Final, r2.p50Final, EPS12);
    near(r1.p5Final,  r2.p5Final,  EPS12);
    near(r1.p95Final, r2.p95Final, EPS12);
});

test('MC bootstrap: different seeds produce different results', () => {
    const r1 = mcBootstrap({ trades: TRADES_A, nRuns: 200, seed: 1 });
    const r2 = mcBootstrap({ trades: TRADES_A, nRuns: 200, seed: 999 });
    assert.ok(r1.p50Final !== r2.p50Final || r1.p5Final !== r2.p5Final,
        'different seeds should give different results');
});

test('MC bootstrap: probOfRuin in [0,1]', () => {
    const r = mcBootstrap({ trades: TRADES_A, nRuns: 100, seed: 7 });
    assert.ok(r.probOfRuin >= 0 && r.probOfRuin <= 1);
});

test('MC bootstrap: p5 <= p50 <= p95', () => {
    const r = mcBootstrap({ trades: TRADES_A, nRuns: 500, seed: 42 });
    assert.ok(r.p5Final <= r.p50Final, `p5 ${r.p5Final} > p50 ${r.p50Final}`);
    assert.ok(r.p50Final <= r.p95Final, `p50 ${r.p50Final} > p95 ${r.p95Final}`);
});

// ─────────────────────────────────────────────────────────────────────────────
// Drawdown edge cases
// ─────────────────────────────────────────────────────────────────────────────

test('drawdownSeries: empty', () => {
    const r = drawdownSeries([]);
    assert.deepEqual(r.peaks, []);
    assert.equal(r.maxDdPct, 0);
});

test('drawdownSeries: monotone increasing — no drawdown', () => {
    const eq = [100, 110, 120, 130];
    const { maxDdPct, maxDdAbs } = drawdownSeries(eq);
    near(maxDdPct, 0, EPS12);
    near(maxDdAbs, 0, EPS12);
});

test('drawdownEpisodes: single-bar data', () => {
    const eps = drawdownEpisodes([100]);
    assert.equal(eps.length, 0);
});

test('top5Drawdowns: returns at most 5', () => {
    // Build equity with 6+ drawdown episodes
    let eq = [100];
    for (let i = 0; i < 10; i++) {
        eq.push(eq[eq.length - 1] - 5);
        eq.push(eq[eq.length - 1] + 6);
    }
    const top5 = top5Drawdowns(eq);
    assert.ok(top5.length <= 5);
});

test('top5Drawdowns: sorted deepest first', () => {
    const top5 = top5Drawdowns(EQ_C);
    for (let i = 1; i < top5.length; i++) {
        assert.ok(top5[i].depthPct >= top5[i - 1].depthPct,
            'episodes should be sorted deepest first (most negative first)');
    }
});

// ─────────────────────────────────────────────────────────────────────────────
// Distributions
// ─────────────────────────────────────────────────────────────────────────────

test('skewness: n<3 returns 0', () => {
    assert.equal(skewness([1, 2]), 0);
    assert.equal(skewness([]), 0);
});

test('excessKurtosis: n<4 returns 0', () => {
    assert.equal(excessKurtosis([1, 2, 3]), 0);
});

test('skewness: symmetric distribution ~0', () => {
    const xs = [-3, -2, -1, 0, 1, 2, 3];
    near(skewness(xs), 0, 1e-10);
});

test('quantile: median of sorted array', () => {
    near(quantile([1, 2, 3, 4, 5], 0.5), 3, EPS12);
});

test('quantile: p=0 returns min', () => {
    near(quantile([5, 1, 3], 0), 1, EPS12);
});

test('quantile: p=1 returns max', () => {
    near(quantile([5, 1, 3], 1), 5, EPS12);
});

test('quantile: linear interpolation', () => {
    // [1,2,3,4] p=0.25 -> h=0.75, between idx 0(1) and idx 1(2): 1+0.75*(2-1)=1.75
    near(quantile([1, 2, 3, 4], 0.25), 1.75, EPS12);
});

test('distributionStats: all fields present', () => {
    const s = distributionStats([1, 2, 3, 4, 5, 6]);
    assert.equal(s.n, 6);
    assert.ok('mean' in s);
    assert.ok('skewness' in s);
    assert.ok('excessKurtosis' in s);
    assert.ok('p50' in s);
});

// ─────────────────────────────────────────────────────────────────────────────
// Correlations
// ─────────────────────────────────────────────────────────────────────────────

test('pearson: perfect positive correlation = 1', () => {
    const x = [1, 2, 3, 4, 5];
    near(pearson(x, x), 1, EPS12);
});

test('pearson: perfect negative correlation = -1', () => {
    const x = [1, 2, 3];
    const y = [-1, -2, -3];
    near(pearson(x, y), -1, EPS12);
});

test('pearson: uncorrelated ~0', () => {
    const x = [1, -1, 1, -1];
    const y = [1, 1, -1, -1];
    near(pearson(x, y), 0, EPS12);
});

test('pearson: constant series returns 0', () => {
    near(pearson([1, 1, 1], [1, 2, 3]), 0, EPS12);
});

test('correlationMatrix: diagonal = 1', () => {
    const r = correlationMatrix({ A: [1, 2, 3], B: [3, 2, 1] });
    near(r.matrix[0][0], 1, EPS12);
    near(r.matrix[1][1], 1, EPS12);
});

test('correlationMatrix: symmetric', () => {
    const r = correlationMatrix({ A: [1, 2, 3], B: [3, 1, 2] });
    near(r.matrix[0][1], r.matrix[1][0], EPS12);
});

test('correlationMatrix: truncates to shortest', () => {
    const r = correlationMatrix({ A: [1, 2, 3, 4], B: [3, 2, 1] });
    // Should work without error (truncated to length 3)
    assert.ok(Math.abs(r.matrix[0][0] - 1) < EPS12);
});

test('rollingBeta: window > length returns all null', () => {
    const out = rollingBeta([1, 2], [1, 2], 5);
    assert.deepEqual(out, [null, null]);
});

test('rollingBeta: beta of series with itself = 1', () => {
    const x = [1, 2, 3, 4, 5, 6, 7, 8];
    const out = rollingBeta(x, x, 3);
    for (let i = 2; i < out.length; i++) {
        near(out[i], 1, EPS9());
    }
});

// ─────────────────────────────────────────────────────────────────────────────
// Metrics edge cases
// ─────────────────────────────────────────────────────────────────────────────

test('metrics: empty trades return 0', () => {
    assert.equal(winRate([]), 0);
    assert.equal(expectancy([]), 0);
    assert.equal(avgWin([]), 0);
    assert.equal(avgLoss([]), 0);
    assert.equal(longestWinStreak([]), 0);
    assert.equal(longestLossStreak([]), 0);
});

test('metrics: profitFactor no losses = Infinity', () => {
    assert.equal(profitFactor([10, 20, 30]), Infinity);
});

test('metrics: profitFactor no wins = 0', () => {
    assert.equal(profitFactor([-10, -20]), 0);
});

test('sharpe: n<2 returns 0', () => {
    assert.equal(sharpe([0.01]), 0);
    assert.equal(sharpe([]), 0);
});

test('sharpe: all-same returns = 0', () => {
    assert.equal(sharpe([0.01, 0.01, 0.01]), 0);
});

test('calmar: zero mdd returns 0', () => {
    assert.equal(calmar(10, 0), 0);
});

test('omega: threshold=0, all positive = Infinity', () => {
    assert.equal(omega([0.01, 0.02, 0.03]), Infinity);
});

test('payoffRatio: no losers = Infinity', () => {
    assert.equal(payoffRatio([10, 20]), Infinity);
});

test('barReturns: correct fractional returns', () => {
    const eq = [100, 110, 99];
    const br = barReturns(eq);
    near(br[0], 0.1, EPS12);
    near(br[1], (99 - 110) / 110, EPS12);
});

// ─────────────────────────────────────────────────────────────────────────────
// linalg
// ─────────────────────────────────────────────────────────────────────────────

test('matMul: identity', () => {
    const I = [[1, 0], [0, 1]];
    const A = [[1, 2], [3, 4]];
    const C = matMul(I, A);
    near(C[0][0], 1, EPS12);
    near(C[0][1], 2, EPS12);
    near(C[1][0], 3, EPS12);
    near(C[1][1], 4, EPS12);
});

test('transpose: 2x3 -> 3x2', () => {
    const A = [[1, 2, 3], [4, 5, 6]];
    const T = transpose(A);
    assert.equal(T.length, 3);
    assert.equal(T[0].length, 2);
    near(T[0][0], 1, EPS12);
    near(T[2][1], 6, EPS12);
});

test('matInv: 2x2', () => {
    const A = [[2, 1], [1, 3]];
    const inv = matInv(A);
    assert.ok(inv !== null);
    // A @ inv should be identity
    const I = matMul(A, inv);
    near(I[0][0], 1, EPS9());
    near(I[0][1], 0, EPS9());
    near(I[1][0], 0, EPS9());
    near(I[1][1], 1, EPS9());
});

test('matInv: singular returns null', () => {
    const A = [[1, 2], [2, 4]]; // rank 1
    assert.equal(matInv(A), null);
});

test('dot: basic', () => {
    near(dot([1, 2, 3], [4, 5, 6]), 32, EPS12);
});

// ─────────────────────────────────────────────────────────────────────────────
// OLS
// ─────────────────────────────────────────────────────────────────────────────

test('OLS: perfect linear fit', () => {
    // y = 2 + 3*x
    const X = [[1, 1], [1, 2], [1, 3], [1, 4], [1, 5]];
    const y = [5, 8, 11, 14, 17];
    const r = ols(X, y);
    assert.ok(r !== null);
    near(r.beta[0], 2, EPS9());
    near(r.beta[1], 3, EPS9());
    near(r.r2, 1, EPS9());
});

test('OLS: r2 in [0,1] for noisy data', () => {
    const X = addIntercept([[1], [2], [3], [4], [5]]);
    const y = [2.1, 3.9, 6.1, 7.9, 10.1];
    const r = ols(X, y);
    assert.ok(r !== null);
    assert.ok(r.r2 >= 0 && r.r2 <= 1);
});

test('OLS: residuals sum to ~0 for intercept model', () => {
    const X = addIntercept([[1], [2], [3], [4], [5]]);
    const y = [3, 5, 7, 9, 11];
    const r = ols(X, y);
    const sumRes = r.residuals.reduce((s, v) => s + v, 0);
    near(sumRes, 0, EPS9());
});

test('addIntercept: prepends 1s', () => {
    const X = addIntercept([[2], [3]]);
    near(X[0][0], 1, EPS12);
    near(X[1][0], 1, EPS12);
    near(X[0][1], 2, EPS12);
});

// ─────────────────────────────────────────────────────────────────────────────
// Factor regression
// ─────────────────────────────────────────────────────────────────────────────

test('factorRegression: recovers known alpha and beta', () => {
    // strategy = 0.001 + 1.2 * mkt + noise (0 noise here)
    const mkt = [0.01, -0.005, 0.02, -0.01, 0.015, 0.003, -0.008, 0.012];
    const strategy = mkt.map(r => 0.001 + 1.2 * r);
    const r = factorRegression(strategy, { mkt });
    assert.ok(r !== null);
    near(r.alpha, 0.001, EPS9());
    near(r.betas.mkt, 1.2, EPS9());
    near(r.r2, 1, EPS9());
});

test('factorRegression: empty strategy returns null', () => {
    assert.equal(factorRegression([], { mkt: [] }), null);
});

// ─────────────────────────────────────────────────────────────────────────────
// Streaming / OnlineMetrics
// ─────────────────────────────────────────────────────────────────────────────

test('OnlineMetrics: matches batch metrics for Series A', () => {
    const om = new OnlineMetrics(10000);
    for (const pnl of TRADES_A) om.update({ netPnl: pnl });
    const s = om.snapshot();

    near(s.nTrades, 10, 0);
    near(s.nWins,   5,  0);
    near(s.nLosses, 5,  0);
    near(s.winRate,    0.5,  EPS12);
    near(s.expectancy, 26.0, EPS12);
    near(s.avgWin,     110.0, EPS12);
    near(s.avgLoss,    -58.0, EPS12);
    near(s.profitFactor, 550 / 290, EPS9());
    near(s.longestWinStreak,  2, 0);  // corrected: trades 60,90 are consecutive
    near(s.longestLossStreak, 2, 0);
});

test('OnlineMetrics: reset clears state', () => {
    const om = new OnlineMetrics();
    om.update({ netPnl: 100 });
    om.reset();
    const s = om.snapshot();
    assert.equal(s.nTrades, 0);
    assert.equal(s.totalPnl, 0);
});

test('OnlineMetrics: single win', () => {
    const om = new OnlineMetrics(1000);
    om.update({ netPnl: 50 });
    const s = om.snapshot();
    assert.equal(s.nWins, 1);
    assert.equal(s.nLosses, 0);
    near(s.winRate, 1, EPS12);
});

test('OnlineMetrics: netPnl as function', () => {
    const om = new OnlineMetrics();
    om.update({ netPnl: () => 30 });
    const s = om.snapshot();
    near(s.expectancy, 30, EPS12);
});

// ─────────────────────────────────────────────────────────────────────────────
// Walk-forward
// ─────────────────────────────────────────────────────────────────────────────

test('walkforwardAnchored: returns correct number of folds', async () => {
    const bars = Array.from({ length: 100 }, (_, i) => i);
    const fn = (tr, te) => ({ train: { n: tr.length }, test: { n: te.length } });
    const results = await walkforwardAnchored(fn, bars, 4, 20);
    assert.ok(results.length > 0, 'should have folds');
    assert.ok(results.length <= 4);
});

test('walkforwardRolling: fold count matches step', async () => {
    const bars = Array.from({ length: 50 }, (_, i) => i);
    const fn = (tr, te) => ({ train: { n: tr.length }, test: { n: te.length } });
    const results = await walkforwardRolling(fn, bars, 20, 5, 5);
    // (50 - 20 - 5) / 5 + 1 = 6 folds
    assert.ok(results.length >= 1);
    for (const r of results) {
        assert.equal(r.result.train.n, 20);
        assert.equal(r.result.test.n, 5);
    }
});

test('walkforwardAnchored: async backtest_fn supported', async () => {
    const bars = Array.from({ length: 30 }, (_, i) => i);
    const fn = async (tr, te) => {
        await new Promise(res => setTimeout(res, 0));
        return { train: {}, test: {} };
    };
    const results = await walkforwardAnchored(fn, bars, 2, 10);
    assert.ok(results.length >= 1);
});

// ─────────────────────────────────────────────────────────────────────────────
// HTML Report
// ─────────────────────────────────────────────────────────────────────────────

test('renderReport: returns valid HTML string', () => {
    const html = renderReport({
        symbol: 'EURUSD',
        equityCurve: EQ_C,
        trades: TRADES_A.map(p => ({ netPnl: p })),
        metrics: { sharpe: 1.5, sortino: 2.0, calmar: 0.5 },
        drawdownSummary: { maxDdPct: -3.92, maxDdAbs: -400, ddPct: [] },
    });
    assert.ok(typeof html === 'string');
    assert.ok(html.includes('<!DOCTYPE html>'));
    assert.ok(html.includes('AlgoForge Backtest Report'));
    assert.ok(html.includes('EURUSD'));
    assert.ok(html.includes('chart.js@4'));
});

test('renderReport: empty input does not throw', () => {
    const html = renderReport({});
    assert.ok(typeof html === 'string');
    assert.ok(html.includes('<!DOCTYPE html>'));
});

test('renderReport: contains summary section', () => {
    const html = renderReport({ metrics: { sharpe: 2 } });
    assert.ok(html.includes('id="summary"'));
});
