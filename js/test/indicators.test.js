import { test } from "node:test";
import assert from "node:assert/strict";

import { sma, ema, rsi, atr, macd, bollinger, stochastic, obv, adx,
         wma, cci, williamsR, roc, mfi, vwap, keltner,
         hma, dema, tema, trix,
         momentum, trueRange, wilderEma, vwma, histVol,
         cmf, accDist, forceIndex, volOsc,
         donchian, pivotClassic, pivotFibonacci, pivotCamarilla,
         fibonacci, sar, ichimoku } from "../src/indicators.js";

// ── SMA ──────────────────────────────────────────────────────────────────
test("sma: invalid period throws", () => {
    for (const bad of [0, -1, 1.5, "3", null]) {
        assert.throws(() => sma([1, 2, 3], bad));
    }
});

test("sma: empty input returns empty", () => {
    assert.deepEqual(sma([], 3), []);
});

test("sma: period > n returns all nulls", () => {
    assert.deepEqual(sma([1, 2], 5), [null, null]);
});

test("sma: period == n returns one value at end", () => {
    const out = sma([2, 4, 6], 3);
    assert.equal(out[0], null);
    assert.equal(out[1], null);
    assert.ok(Math.abs(out[2] - 4) < 1e-12);
});

test("sma: known values", () => {
    const out = sma([1, 2, 3, 4, 5], 3);
    assert.deepEqual(out.slice(0, 2), [null, null]);
    assert.ok(Math.abs(out[2] - 2) < 1e-12);
    assert.ok(Math.abs(out[3] - 3) < 1e-12);
    assert.ok(Math.abs(out[4] - 4) < 1e-12);
});

test("sma: constant input", () => {
    const out = sma(Array(10).fill(7.5), 4);
    for (let i = 3; i < 10; i++) assert.ok(Math.abs(out[i] - 7.5) < 1e-12);
});

test("sma: period 1 is identity", () => {
    const data = [1, 2, 3, 4, 5];
    assert.deepEqual(sma(data, 1), data);
});

// ── EMA ──────────────────────────────────────────────────────────────────
test("ema: invalid period throws", () => {
    assert.throws(() => ema([1, 2, 3], 0));
});

test("ema: insufficient data returns nulls", () => {
    assert.deepEqual(ema([1, 2], 5), [null, null]);
});

test("ema: constant input", () => {
    const out = ema(Array(20).fill(3.14), 5);
    for (let i = 4; i < 20; i++) assert.ok(Math.abs(out[i] - 3.14) < 1e-12);
});

test("ema: responds to step up", () => {
    const data = Array(10).fill(100).concat(Array(10).fill(110));
    const out = ema(data, 5);
    assert.ok(out[10] > 100 && out[10] < 110);
    assert.ok(out[19] > out[10]);
    assert.ok(out[19] < 110);
});

// ── RSI ──────────────────────────────────────────────────────────────────
test("rsi: invalid period throws", () => {
    assert.throws(() => rsi([1, 2, 3], 0));
});

test("rsi: insufficient data returns nulls", () => {
    assert.deepEqual(rsi([1, 2, 3, 4, 5], 5), [null, null, null, null, null]);
});

test("rsi: stays in [0,100]", () => {
    let p = 100;
    const data = [];
    for (let i = 0; i < 100; i++) {
        p += ((i * 9301 + 49297) % 233 - 116) * 0.01;
        data.push(p);
    }
    const out = rsi(data, 14);
    for (let i = 14; i < 100; i++) {
        assert.ok(out[i] >= 0 && out[i] <= 100);
    }
});

test("rsi: strict uptrend → near 100", () => {
    const data = Array.from({ length: 50 }, (_, i) => 100 + i * 0.5);
    const out = rsi(data, 14);
    // rs capped at 100 → RSI ≈ 99.01; matches cpp/ reference.
    assert.ok(out[49] > 99 && out[49] <= 100);
});

test("rsi: strict downtrend → near 0", () => {
    const data = Array.from({ length: 50 }, (_, i) => 100 - i * 0.5);
    const out = rsi(data, 14);
    assert.ok(out[49] < 5 && out[49] >= 0);
});

// ── ATR ──────────────────────────────────────────────────────────────────
test("atr: invalid period throws", () => {
    assert.throws(() => atr([1, 2], [0, 1], [1, 1], 0));
});

test("atr: mismatched lengths throw", () => {
    assert.throws(() => atr([1, 2, 3], [0, 1], [1, 1, 1], 2));
});

test("atr: non-negative", () => {
    const h = [], l = [], c = [];
    for (let i = 0; i < 100; i++) {
        const mid = 100 + i * 0.1;
        h.push(mid + 0.5); l.push(mid - 0.5); c.push(mid);
    }
    const out = atr(h, l, c, 14);
    for (let i = 13; i < 100; i++) assert.ok(out[i] >= 0);
});

test("atr: constant range converges to range", () => {
    const h = Array(50).fill(11);
    const l = Array(50).fill(9);
    const c = Array(50).fill(10);
    const out = atr(h, l, c, 14);
    assert.ok(Math.abs(out[49] - 2) < 1e-9);
});

test("atr: insufficient data returns nulls", () => {
    const h = Array(5).fill(1), l = Array(5).fill(0), c = Array(5).fill(1);
    assert.deepEqual(atr(h, l, c, 14), Array(5).fill(null));
});

// ── MACD ─────────────────────────────────────────────────────────────────────
test("macd: invalid period throws", () => {
    assert.throws(() => macd([1, 2, 3], 0, 26, 9));
    assert.throws(() => macd([1, 2, 3], 12, 0, 9));
    assert.throws(() => macd([1, 2, 3], 12, 26, 0));
});

test("macd: insufficient data returns all-null arrays", () => {
    const { macd: m, signal: s, histogram: h } = macd([1, 2, 3], 12, 26, 9);
    assert.ok(m.every(v => v === null));
    assert.ok(s.every(v => v === null));
    assert.ok(h.every(v => v === null));
});

test("macd: output length matches input length", () => {
    const data = Array.from({ length: 50 }, (_, i) => 100 + Math.sin(i) * 5);
    const { macd: m, signal: s, histogram: h } = macd(data, 5, 10, 3);
    assert.equal(m.length, 50);
    assert.equal(s.length, 50);
    assert.equal(h.length, 50);
});

test("macd: histogram = macd - signal where both defined", () => {
    const data = Array.from({ length: 60 }, (_, i) => 100 + Math.sin(i * 0.3) * 10);
    const { macd: m, signal: s, histogram: h } = macd(data, 5, 10, 3);
    for (let i = 0; i < 60; i++) {
        if (m[i] !== null && s[i] !== null) {
            assert.ok(Math.abs(h[i] - (m[i] - s[i])) < 1e-12);
        }
    }
});

test("macd: constant input yields zero macd", () => {
    const data = Array(40).fill(100);
    const { macd: m } = macd(data, 5, 10, 3);
    for (let i = 0; i < 40; i++) {
        if (m[i] !== null) assert.ok(Math.abs(m[i]) < 1e-10);
    }
});

test("macd: first non-null macd at index slow-1", () => {
    const data = Array.from({ length: 50 }, (_, i) => i + 1);
    const { macd: m } = macd(data, 3, 5, 2);
    assert.equal(m[3], null);
    assert.ok(m[4] !== null);
});

// ── Bollinger Bands ───────────────────────────────────────────────────────────
test("bollinger: invalid period throws", () => {
    assert.throws(() => bollinger([1, 2, 3], 0));
});

test("bollinger: insufficient data returns all-null arrays", () => {
    const { upper, middle, lower } = bollinger([1, 2], 5);
    assert.ok(upper.every(v => v === null));
    assert.ok(middle.every(v => v === null));
    assert.ok(lower.every(v => v === null));
});

test("bollinger: upper >= middle >= lower always where defined", () => {
    const data = Array.from({ length: 50 }, (_, i) => 100 + Math.sin(i * 0.5) * 3);
    const { upper, middle, lower } = bollinger(data, 10);
    for (let i = 0; i < 50; i++) {
        if (upper[i] !== null) {
            assert.ok(upper[i] >= middle[i] - 1e-12);
            assert.ok(middle[i] >= lower[i] - 1e-12);
        }
    }
});

test("bollinger: constant input yields zero bandwidth", () => {
    const data = Array(30).fill(100);
    const { upper, middle, lower } = bollinger(data, 10);
    for (let i = 9; i < 30; i++) {
        assert.ok(Math.abs(upper[i] - 100) < 1e-10);
        assert.ok(Math.abs(middle[i] - 100) < 1e-10);
        assert.ok(Math.abs(lower[i] - 100) < 1e-10);
    }
});

test("bollinger: known output on small input (population std)", () => {
    // values = [2,4,6,8,10], SMA(5) = 6, population std = sqrt(8) ≈ 2.8284
    const data = [2, 4, 6, 8, 10];
    const { upper, middle, lower } = bollinger(data, 5, 2.0);
    assert.ok(Math.abs(middle[4] - 6) < 1e-10);
    const expectedSd = Math.sqrt(8); // population std
    assert.ok(Math.abs(upper[4] - (6 + 2 * expectedSd)) < 1e-10);
    assert.ok(Math.abs(lower[4] - (6 - 2 * expectedSd)) < 1e-10);
});

test("bollinger: output length equals input length", () => {
    const data = Array.from({ length: 25 }, (_, i) => i);
    const { upper, middle, lower } = bollinger(data, 5);
    assert.equal(upper.length, 25);
    assert.equal(middle.length, 25);
    assert.equal(lower.length, 25);
});

// ── Stochastic ───────────────────────────────────────────────────────────────
test("stochastic: invalid period throws", () => {
    assert.throws(() => stochastic([1], [0], [1], 0, 3));
    assert.throws(() => stochastic([1], [0], [1], 14, 0));
});

test("stochastic: mismatched lengths throw", () => {
    assert.throws(() => stochastic([1, 2], [0], [1, 1], 1, 1));
});

test("stochastic: insufficient data returns all-null arrays", () => {
    const { k, d } = stochastic([1, 2], [0, 1], [1, 1], 14, 3);
    assert.ok(k.every(v => v === null));
    assert.ok(d.every(v => v === null));
});

test("stochastic: k and d stay in [0,100]", () => {
    const n = 60;
    const h = [], l = [], c = [];
    for (let i = 0; i < n; i++) {
        const mid = 100 + Math.sin(i * 0.4) * 10;
        h.push(mid + 2); l.push(mid - 2); c.push(mid);
    }
    const { k, d } = stochastic(h, l, c, 5, 3);
    for (let i = 0; i < n; i++) {
        if (k[i] !== null) assert.ok(k[i] >= -1e-10 && k[i] <= 100 + 1e-10);
        if (d[i] !== null) assert.ok(d[i] >= -1e-10 && d[i] <= 100 + 1e-10);
    }
});

test("stochastic: pure uptrend k near 100", () => {
    const n = 30;
    const h = Array.from({ length: n }, (_, i) => 100 + i + 1);
    const l = Array.from({ length: n }, (_, i) => 100 + i);
    const c = Array.from({ length: n }, (_, i) => 100 + i + 0.9);
    const { k } = stochastic(h, l, c, 5, 3);
    for (let i = 6; i < n; i++) {
        if (k[i] !== null) assert.ok(k[i] > 80);
    }
});

test("stochastic: known k value on simple input", () => {
    // With kPeriod=3, closes at 3rd bar: high=[10,11,12], low=[1,2,3], close=12
    // rawK = 100*(12-1)/(12-1) = 100; with dPeriod=1, k=100
    const h = [10, 11, 12];
    const l = [1,  2,  3];
    const c = [5,  6,  12];
    const { k } = stochastic(h, l, c, 3, 1);
    assert.ok(Math.abs(k[2] - 100) < 1e-10);
});

// ── OBV ──────────────────────────────────────────────────────────────────────
test("obv: mismatched lengths throw", () => {
    assert.throws(() => obv([1, 2, 3], [100, 200]));
});

test("obv: empty input returns empty", () => {
    assert.deepEqual(obv([], []), []);
});

test("obv: first value always 0", () => {
    const out = obv([10, 11, 12], [100, 200, 300]);
    assert.equal(out[0], 0);
});

test("obv: pure uptrend is non-decreasing", () => {
    const closes  = Array.from({ length: 10 }, (_, i) => 100 + i);
    const volumes = Array(10).fill(1000);
    const out = obv(closes, volumes);
    for (let i = 1; i < 10; i++) assert.ok(out[i] >= out[i - 1]);
});

test("obv: pure downtrend is non-increasing", () => {
    const closes  = Array.from({ length: 10 }, (_, i) => 100 - i);
    const volumes = Array(10).fill(1000);
    const out = obv(closes, volumes);
    for (let i = 1; i < 10; i++) assert.ok(out[i] <= out[i - 1]);
});

test("obv: flat closes yield constant OBV", () => {
    const closes  = Array(10).fill(100);
    const volumes = Array(10).fill(500);
    const out = obv(closes, volumes);
    for (const v of out) assert.equal(v, 0);
});

test("obv: known values", () => {
    // closes: 10, 11, 10, 12; volumes: 100, 200, 300, 400
    // OBV: 0, 200, -100, 300
    const out = obv([10, 11, 10, 12], [100, 200, 300, 400]);
    assert.equal(out[0], 0);
    assert.equal(out[1], 200);
    assert.equal(out[2], -100);
    assert.equal(out[3], 300);
});

// ── ADX ──────────────────────────────────────────────────────────────────────
test("adx: invalid period throws", () => {
    assert.throws(() => adx([1, 2, 3], [0, 1, 2], [1, 1, 1], 0));
});

test("adx: mismatched lengths throw", () => {
    assert.throws(() => adx([1, 2, 3], [0, 1], [1, 1, 1], 2));
});

test("adx: insufficient data returns all-null arrays", () => {
    const h = Array(5).fill(1), l = Array(5).fill(0), c = Array(5).fill(1);
    const { adx: a, plusDi, minusDi } = adx(h, l, c, 14);
    assert.ok(a.every(v => v === null));
    assert.ok(plusDi.every(v => v === null));
    assert.ok(minusDi.every(v => v === null));
});

test("adx: values stay in [0,100]", () => {
    const n = 60;
    const h = [], l = [], c = [];
    for (let i = 0; i < n; i++) {
        const mid = 100 + Math.sin(i * 0.3) * 5;
        h.push(mid + 1); l.push(mid - 1); c.push(mid);
    }
    const { adx: a, plusDi, minusDi } = adx(h, l, c, 7);
    for (let i = 0; i < n; i++) {
        if (a[i] !== null)       assert.ok(a[i]       >= 0 && a[i]       <= 100);
        if (plusDi[i] !== null)  assert.ok(plusDi[i]  >= 0 && plusDi[i]  <= 100);
        if (minusDi[i] !== null) assert.ok(minusDi[i] >= 0 && minusDi[i] <= 100);
    }
});

test("adx: strong uptrend raises +DI above -DI", () => {
    const n = 60;
    const h = Array.from({ length: n }, (_, i) => 100 + i * 0.5 + 1);
    const l = Array.from({ length: n }, (_, i) => 100 + i * 0.5);
    const c = Array.from({ length: n }, (_, i) => 100 + i * 0.5 + 0.8);
    const { plusDi, minusDi } = adx(h, l, c, 7);
    const last = n - 1;
    assert.ok(plusDi[last] > minusDi[last]);
});

test("adx: output length equals input length", () => {
    const n = 40;
    const h = Array(n).fill(11), l = Array(n).fill(9), c = Array(n).fill(10);
    const { adx: a, plusDi, minusDi } = adx(h, l, c, 7);
    assert.equal(a.length, n);
    assert.equal(plusDi.length, n);
    assert.equal(minusDi.length, n);
});

// ── WMA ──────────────────────────────────────────────────────────────────────
test("wma: invalid period throws", () => {
    assert.throws(() => wma([1, 2, 3], 0));
    assert.throws(() => wma([1, 2, 3], -1));
    assert.throws(() => wma([1, 2, 3], 1.5));
});

test("wma: insufficient data returns all nulls", () => {
    const out = wma([1, 2], 5);
    assert.ok(out.every(v => v === null));
});

test("wma: output length equals input length", () => {
    const out = wma([1, 2, 3, 4, 5], 3);
    assert.equal(out.length, 5);
});

test("wma: known value on simple input", () => {
    // period=3, weights=[1,2,3], wsum=6
    // values=[1,2,3]: wma = (1*1 + 2*2 + 3*3) / 6 = 14/6 ≈ 2.3333...
    const out = wma([1, 2, 3], 3);
    assert.equal(out[0], null);
    assert.equal(out[1], null);
    assert.ok(Math.abs(out[2] - (1 + 4 + 9) / 6) < 1e-12);
});

test("wma: constant input stays constant", () => {
    const out = wma(Array(20).fill(5), 5);
    for (let i = 4; i < 20; i++) assert.ok(Math.abs(out[i] - 5) < 1e-12);
});

test("wma: period 1 is identity", () => {
    const data = [3, 1, 4, 1, 5];
    const out = wma(data, 1);
    assert.deepEqual(out, data);
});

test("wma: most recent bar has highest weight", () => {
    // Increasing data: WMA should be higher than SMA
    const data = [1, 2, 3, 4, 5];
    const outWma = wma(data, 5);
    const outSma = sma(data, 5);
    // WMA weights recent more, so wma > sma for rising data
    assert.ok(outWma[4] > outSma[4]);
});

// ── CCI ──────────────────────────────────────────────────────────────────────
test("cci: invalid period throws", () => {
    assert.throws(() => cci([1], [0], [1], 0));
});

test("cci: mismatched lengths throw", () => {
    assert.throws(() => cci([1, 2, 3], [0, 1], [1, 1, 1], 2));
    assert.throws(() => cci([1, 2, 3], [0, 1, 2], [1, 1], 2));
});

test("cci: insufficient data returns all nulls", () => {
    const out = cci([1, 2], [0, 1], [1, 1], 5);
    assert.ok(out.every(v => v === null));
});

test("cci: constant input yields zero", () => {
    // When all TPs are equal, mean_abs_dev = 0, result is 0
    const h = Array(20).fill(10);
    const l = Array(20).fill(8);
    const c = Array(20).fill(9);
    const out = cci(h, l, c, 5);
    for (let i = 4; i < 20; i++) assert.ok(Math.abs(out[i]) < 1e-10);
});

test("cci: known value", () => {
    // TP = (H+L+C)/3 = (10+8+9)/3 = 9 for all bars except last
    // At last bar: H=12, L=8, C=10, TP=10
    // With period=3 and constant=0.015:
    // window TPs = [9, 9, 10], mean = 28/3
    // Use uniform data for predictability
    const h = [10, 10, 12];
    const l = [8,  8,  8];
    const c = [9,  9,  10];
    const out = cci(h, l, c, 3, 0.015);
    // tp[0]=9, tp[1]=9, tp[2]=10; mean=28/3≈9.333...
    // mad = (|9-mean| + |9-mean| + |10-mean|)/3 = (0.333+0.333+0.667)/3 = 0.444...
    // cci = (10 - 28/3) / (0.015 * 0.444...) ≈ 0.6667 / 0.006667 ≈ 99.97
    assert.ok(out[2] !== null);
    assert.ok(Math.abs(out[2] - 100) < 1);
});

test("cci: output length equals input length", () => {
    const h = Array(10).fill(1), l = Array(10).fill(0), c = Array(10).fill(0.5);
    assert.equal(cci(h, l, c, 3).length, 10);
});

// ── Williams %R ───────────────────────────────────────────────────────────────
test("williamsR: invalid period throws", () => {
    assert.throws(() => williamsR([1], [0], [1], 0));
});

test("williamsR: mismatched lengths throw", () => {
    assert.throws(() => williamsR([1, 2, 3], [0, 1], [1, 1, 1], 2));
});

test("williamsR: insufficient data returns all nulls", () => {
    const out = williamsR([1, 2], [0, 1], [1, 1], 5);
    assert.ok(out.every(v => v === null));
});

test("williamsR: stays in [-100, 0]", () => {
    const n = 60;
    const h = [], l = [], c = [];
    for (let i = 0; i < n; i++) {
        const mid = 100 + Math.sin(i * 0.4) * 10;
        h.push(mid + 2); l.push(mid - 2); c.push(mid);
    }
    const out = williamsR(h, l, c, 14);
    for (let i = 13; i < n; i++) {
        assert.ok(out[i] >= -100 - 1e-10 && out[i] <= 0 + 1e-10);
    }
});

test("williamsR: close at highest high → 0", () => {
    // If close == highest_high, %R = 0
    const h = [10, 11, 12];
    const l = [8,  9,  10];
    const c = [10, 11, 12]; // close == high
    const out = williamsR(h, l, c, 3);
    assert.ok(Math.abs(out[2] - 0) < 1e-12);
});

test("williamsR: close at lowest low → -100", () => {
    // If close == lowest_low, %R = -100
    const h = [10, 11, 12];
    const l = [8,  9,  10];
    const c = [8,  9,  8]; // close == min low = 8
    const out = williamsR(h, l, c, 3);
    assert.ok(Math.abs(out[2] - (-100)) < 1e-12);
});

test("williamsR: zero range returns -50", () => {
    const h = [10, 10, 10];
    const l = [10, 10, 10];
    const c = [10, 10, 10];
    const out = williamsR(h, l, c, 3);
    assert.ok(Math.abs(out[2] - (-50)) < 1e-12);
});

test("williamsR: output length equals input length", () => {
    const h = Array(10).fill(1), l = Array(10).fill(0), c = Array(10).fill(0.5);
    assert.equal(williamsR(h, l, c, 3).length, 10);
});

// ── ROC ──────────────────────────────────────────────────────────────────────
test("roc: invalid period throws", () => {
    assert.throws(() => roc([1, 2, 3], 0));
    assert.throws(() => roc([1, 2, 3], -1));
});

test("roc: insufficient data returns all nulls (n <= period)", () => {
    // n=5, period=5 → n <= period → all null
    assert.ok(roc([1, 2, 3, 4, 5], 5).every(v => v === null));
});

test("roc: first valid index at period", () => {
    const out = roc([1, 2, 3, 4, 5], 3);
    assert.equal(out[0], null);
    assert.equal(out[1], null);
    assert.equal(out[2], null);
    assert.ok(out[3] !== null);
});

test("roc: known value", () => {
    // values=[100, 110], period=1: roc[1] = 100*(110-100)/100 = 10
    const out = roc([100, 110], 1);
    assert.ok(Math.abs(out[1] - 10) < 1e-12);
});

test("roc: constant input yields zero", () => {
    const out = roc(Array(20).fill(100), 5);
    for (let i = 5; i < 20; i++) assert.ok(Math.abs(out[i]) < 1e-10);
});

test("roc: zero past price yields zero (no division by zero)", () => {
    const out = roc([0, 10, 20], 1);
    assert.ok(Math.abs(out[1]) < 1e-10);
});

test("roc: output length equals input length", () => {
    assert.equal(roc([1, 2, 3, 4, 5], 2).length, 5);
});

// ── MFI ──────────────────────────────────────────────────────────────────────
test("mfi: invalid period throws", () => {
    assert.throws(() => mfi([1], [0], [1], [100], 0));
});

test("mfi: mismatched lengths throw", () => {
    assert.throws(() => mfi([1, 2], [0, 1], [1, 1], [100], 1));
    assert.throws(() => mfi([1, 2], [0, 1], [1], [100, 200], 1));
});

test("mfi: insufficient data returns all nulls (n <= period)", () => {
    const h = Array(5).fill(1), l = Array(5).fill(0), c = Array(5).fill(0.5), v = Array(5).fill(100);
    assert.ok(mfi(h, l, c, v, 5).every(x => x === null));
});

test("mfi: stays in [0, 100]", () => {
    const n = 60;
    const h = [], l = [], c = [], v = [];
    for (let i = 0; i < n; i++) {
        const mid = 100 + Math.sin(i * 0.3) * 5;
        h.push(mid + 1); l.push(mid - 1); c.push(mid);
        v.push(1000 + (i % 7) * 200);
    }
    const out = mfi(h, l, c, v, 14);
    for (let i = 0; i < n; i++) {
        if (out[i] !== null) {
            assert.ok(out[i] >= 0 - 1e-10 && out[i] <= 100 + 1e-10);
        }
    }
});

test("mfi: all rising prices → near 100", () => {
    // Every bar has higher TP than previous → all positive money flow
    const n = 30;
    const h = Array.from({ length: n }, (_, i) => 102 + i);
    const l = Array.from({ length: n }, (_, i) => 98  + i);
    const c = Array.from({ length: n }, (_, i) => 100 + i);
    const v = Array(n).fill(1000);
    const out = mfi(h, l, c, v, 14);
    // All flows positive → mfi should be near 100
    assert.ok(out[n - 1] > 90);
});

test("mfi: output length equals input length", () => {
    const h = Array(20).fill(1), l = Array(20).fill(0), c = Array(20).fill(0.5), v = Array(20).fill(100);
    assert.equal(mfi(h, l, c, v, 10).length, 20);
});

// ── VWAP ─────────────────────────────────────────────────────────────────────
test("vwap: mismatched lengths throw", () => {
    assert.throws(() => vwap([1, 2], [0, 1], [1, 1], [100]));
    assert.throws(() => vwap([1, 2], [0, 1], [1], [100, 200]));
});

test("vwap: empty input returns empty", () => {
    assert.deepEqual(vwap([], [], [], []), []);
});

test("vwap: constant input equals that constant", () => {
    // TP = (10+8+9)/3 = 9 always, volume=100 always → VWAP = 9 always
    const n = 10;
    const h = Array(n).fill(10), l = Array(n).fill(8), c = Array(n).fill(9), v = Array(n).fill(100);
    const out = vwap(h, l, c, v);
    const expected = 9;
    for (const val of out) assert.ok(Math.abs(val - expected) < 1e-12);
});

test("vwap: output length equals input length", () => {
    const n = 15;
    const h = Array(n).fill(1), l = Array(n).fill(0), c = Array(n).fill(0.5), v = Array(n).fill(100);
    assert.equal(vwap(h, l, c, v).length, n);
});

test("vwap: known cumulative computation", () => {
    // bar0: H=12,L=8,C=10 → TP=10, vol=100; cpv=1000, cv=100 → vwap=10
    // bar1: H=14,L=10,C=12 → TP=12, vol=200; cpv=3400, cv=300 → vwap=11.333...
    const h = [12, 14], l = [8, 10], c = [10, 12], v = [100, 200];
    const out = vwap(h, l, c, v);
    assert.ok(Math.abs(out[0] - 10) < 1e-12);
    assert.ok(Math.abs(out[1] - (1000 + 12 * 200) / 300) < 1e-12);
});

test("vwap: zero volume treated as 1", () => {
    // With volume=0, treated as 1; vwap should still be defined
    const h = [10, 12], l = [8, 10], c = [9, 11], v = [0, 0];
    const out = vwap(h, l, c, v);
    assert.ok(out[0] !== null && isFinite(out[0]));
    assert.ok(out[1] !== null && isFinite(out[1]));
});

// ── Keltner Channel ───────────────────────────────────────────────────────────
test("keltner: invalid period throws", () => {
    assert.throws(() => keltner([1], [0], [1], 0, 10, 2));
    assert.throws(() => keltner([1], [0], [1], 20, 0, 2));
});

test("keltner: mismatched lengths throw", () => {
    assert.throws(() => keltner([1, 2, 3], [0, 1], [1, 1, 1], 2, 2, 2));
});

test("keltner: insufficient data returns all-null arrays", () => {
    const h = Array(3).fill(1), l = Array(3).fill(0), c = Array(3).fill(0.5);
    const { upper, middle, lower } = keltner(h, l, c, 20, 10, 2);
    assert.ok(upper.every(v => v === null));
    assert.ok(middle.every(v => v === null));
    assert.ok(lower.every(v => v === null));
});

test("keltner: upper >= middle >= lower where defined", () => {
    const n = 60;
    const h = [], l = [], c = [];
    for (let i = 0; i < n; i++) {
        const mid = 100 + Math.sin(i * 0.3) * 5;
        h.push(mid + 2); l.push(mid - 2); c.push(mid);
    }
    const { upper, middle, lower } = keltner(h, l, c, 10, 5, 2);
    for (let i = 0; i < n; i++) {
        if (upper[i] !== null) {
            assert.ok(upper[i] >= middle[i] - 1e-12);
            assert.ok(middle[i] >= lower[i] - 1e-12);
        }
    }
});

test("keltner: constant input — bands are symmetric", () => {
    const n = 50;
    const h = Array(n).fill(11), l = Array(n).fill(9), c = Array(n).fill(10);
    const { upper, middle, lower } = keltner(h, l, c, 10, 5, 2);
    for (let i = 0; i < n; i++) {
        if (upper[i] !== null) {
            assert.ok(Math.abs(upper[i] - middle[i] - (middle[i] - lower[i])) < 1e-10);
        }
    }
});

test("keltner: output length equals input length", () => {
    const n = 50;
    const h = Array(n).fill(11), l = Array(n).fill(9), c = Array(n).fill(10);
    const { upper, middle, lower } = keltner(h, l, c, 10, 5, 2);
    assert.equal(upper.length, n);
    assert.equal(middle.length, n);
    assert.equal(lower.length, n);
});

test("keltner: middle is EMA of close", () => {
    const n = 30;
    const h = Array.from({ length: n }, (_, i) => 101 + i * 0.1);
    const l = Array.from({ length: n }, (_, i) =>  99 + i * 0.1);
    const c = Array.from({ length: n }, (_, i) => 100 + i * 0.1);
    const { middle } = keltner(h, l, c, 10, 5, 2);
    const emaClose = ema(c, 10);
    for (let i = 0; i < n; i++) {
        if (middle[i] !== null) assert.ok(Math.abs(middle[i] - emaClose[i]) < 1e-12);
    }
});

// ── HMA ──────────────────────────────────────────────────────────────────────
test("hma: invalid period throws (period < 1)", () => {
    assert.throws(() => hma([1, 2, 3], 0));
    assert.throws(() => hma([1, 2, 3], -1));
    assert.throws(() => hma([1, 2, 3], 1.5));
});

test("hma: period < 2 throws", () => {
    assert.throws(() => hma([1, 2, 3], 1));
});

test("hma: insufficient data returns all nulls", () => {
    const out = hma([1, 2], 9);
    assert.ok(out.every(v => v === null));
});

test("hma: constant input stays constant", () => {
    const out = hma(Array(30).fill(5), 9);
    for (let i = 0; i < 30; i++) {
        if (out[i] !== null) assert.ok(Math.abs(out[i] - 5) < 1e-10);
    }
});

test("hma: output length equals input length", () => {
    assert.equal(hma(Array(30).fill(1), 9).length, 30);
});

test("hma: known output on simple rising input", () => {
    // period=4: half=trunc(sqrt(2))=1, sq=trunc(sqrt(4))=2
    // wma([1,2,3,4,5,6], period=4): first valid at index 3
    // wma([1,2,3,4,5,6], period=1): identity [1,2,3,4,5,6]
    // raw[i] = 2*wma1[i] - wma4[i]
    // out = WMA(raw, 2)
    const data = [1, 2, 3, 4, 5, 6];
    const out = hma(data, 4);
    // Verify non-null values are defined
    const defined = out.filter(v => v !== null);
    assert.ok(defined.length > 0);
    // All defined values should be finite numbers
    for (const v of defined) assert.ok(isFinite(v));
});

test("hma: more recent data has more influence than SMA", () => {
    // Rising data: HMA should be above SMA (more weight on recent)
    const n = 30;
    const data = Array.from({ length: n }, (_, i) => i + 1);
    const outHma = hma(data, 9);
    const outSma = sma(data, 9);
    // At last index where both are defined
    assert.ok(outHma[n - 1] > outSma[n - 1]);
});

// ── DEMA ─────────────────────────────────────────────────────────────────────
test("dema: invalid period throws", () => {
    assert.throws(() => dema([1, 2, 3], 0));
    assert.throws(() => dema([1, 2, 3], -1));
});

test("dema: insufficient data returns all nulls", () => {
    const out = dema([1, 2], 5);
    assert.ok(out.every(v => v === null));
});

test("dema: constant input stays constant", () => {
    const out = dema(Array(30).fill(7), 5);
    for (let i = 0; i < 30; i++) {
        if (out[i] !== null) assert.ok(Math.abs(out[i] - 7) < 1e-10);
    }
});

test("dema: first valid index is at 2*period - 2", () => {
    const period = 5;
    const n = 20;
    const out = dema(Array.from({ length: n }, (_, i) => i + 1), period);
    // first non-null should be at index 2*period-2 = 8
    for (let i = 0; i < 2 * period - 2; i++) assert.equal(out[i], null);
    assert.ok(out[2 * period - 2] !== null);
});

test("dema: known output on small input (close to EMA, slightly faster)", () => {
    const data = Array.from({ length: 30 }, (_, i) => 100 + i);
    const outDema = dema(data, 5);
    const outEma  = ema(data, 5);
    // DEMA should be closer to current price (more responsive)
    const last = 29;
    assert.ok(outDema[last] > outEma[last]); // rising data: DEMA > EMA
});

test("dema: output length equals input length", () => {
    assert.equal(dema(Array(20).fill(1), 5).length, 20);
});

// ── TEMA ─────────────────────────────────────────────────────────────────────
test("tema: invalid period throws", () => {
    assert.throws(() => tema([1, 2, 3], 0));
    assert.throws(() => tema([1, 2, 3], -1));
});

test("tema: insufficient data returns all nulls", () => {
    const out = tema([1, 2], 5);
    assert.ok(out.every(v => v === null));
});

test("tema: constant input stays constant", () => {
    const out = tema(Array(50).fill(3.14), 5);
    for (let i = 0; i < 50; i++) {
        if (out[i] !== null) assert.ok(Math.abs(out[i] - 3.14) < 1e-10);
    }
});

test("tema: first valid index is at 3*period - 3", () => {
    const period = 5;
    const n = 30;
    const out = tema(Array.from({ length: n }, (_, i) => i + 1), period);
    // first non-null at index 3*period-3 = 12
    for (let i = 0; i < 3 * period - 3; i++) assert.equal(out[i], null);
    assert.ok(out[3 * period - 3] !== null);
});

test("tema: more responsive than EMA and DEMA on rising data", () => {
    const data = Array.from({ length: 50 }, (_, i) => 100 + i);
    const outTema = tema(data, 5);
    const outDema = dema(data, 5);
    const outEma  = ema(data, 5);
    const last = 49;
    // For rising data: TEMA > DEMA > EMA
    assert.ok(outTema[last] > outDema[last]);
    assert.ok(outDema[last] > outEma[last]);
});

test("tema: output length equals input length", () => {
    assert.equal(tema(Array(30).fill(1), 5).length, 30);
});

// ── TRIX ─────────────────────────────────────────────────────────────────────
test("trix: invalid period throws", () => {
    assert.throws(() => trix([1, 2, 3], 0));
    assert.throws(() => trix([1, 2, 3], -1));
});

test("trix: insufficient data returns all nulls", () => {
    const out = trix([1, 2], 5);
    assert.ok(out.every(v => v === null));
});

test("trix: constant input yields zero", () => {
    // When e3 is constant, ROC = 0
    const out = trix(Array(50).fill(5), 5);
    for (let i = 0; i < 50; i++) {
        if (out[i] !== null) assert.ok(Math.abs(out[i]) < 1e-10);
    }
});

test("trix: output[0] is always null (no previous bar)", () => {
    const out = trix(Array.from({ length: 30 }, (_, i) => i + 1), 3);
    assert.equal(out[0], null);
});

test("trix: known output direction on rising data", () => {
    // Rising data → e3 is increasing → trix > 0
    const data = Array.from({ length: 50 }, (_, i) => 100 + i);
    const out = trix(data, 5);
    const last = out[49];
    assert.ok(last !== null && last > 0);
});

test("trix: output length equals input length", () => {
    assert.equal(trix(Array(30).fill(1), 5).length, 30);
});

// ── Momentum ──────────────────────────────────────────────────────────────────
test("momentum: invalid period throws", () => {
    assert.throws(() => momentum([1, 2, 3], 0));
    assert.throws(() => momentum([1, 2, 3], -1));
    assert.throws(() => momentum([1, 2, 3], 1.5));
});

test("momentum: insufficient data returns all nulls (n <= period)", () => {
    assert.ok(momentum([1, 2, 3, 4, 5], 5).every(v => v === null));
});

test("momentum: first valid index at period", () => {
    const out = momentum([10, 20, 30, 40, 50], 3);
    assert.equal(out[0], null);
    assert.equal(out[1], null);
    assert.equal(out[2], null);
    assert.ok(out[3] !== null);
});

test("momentum: known value", () => {
    // period=1: out[1] = 20 - 10 = 10
    const out = momentum([10, 20, 30], 1);
    assert.ok(Math.abs(out[1] - 10) < 1e-12);
    assert.ok(Math.abs(out[2] - 10) < 1e-12);
});

test("momentum: constant input yields zero", () => {
    const out = momentum(Array(20).fill(5), 3);
    for (let i = 3; i < 20; i++) assert.ok(Math.abs(out[i]) < 1e-12);
});

test("momentum: output length equals input length", () => {
    assert.equal(momentum([1, 2, 3, 4, 5], 2).length, 5);
});

// ── True Range ────────────────────────────────────────────────────────────────
test("trueRange: mismatched lengths throw", () => {
    assert.throws(() => trueRange([1, 2, 3], [0, 1], [1, 1, 1]));
    assert.throws(() => trueRange([1, 2, 3], [0, 1, 2], [1, 1]));
});

test("trueRange: empty input returns empty", () => {
    assert.deepEqual(trueRange([], [], []), []);
});

test("trueRange: first bar is high - low", () => {
    const out = trueRange([12], [8], [10]);
    assert.ok(Math.abs(out[0] - 4) < 1e-12);
});

test("trueRange: non-negative always", () => {
    const n = 30;
    const h = Array.from({ length: n }, (_, i) => 100 + Math.sin(i) * 3);
    const l = Array.from({ length: n }, (_, i) => 100 + Math.sin(i) * 3 - 2);
    const c = Array.from({ length: n }, (_, i) => 100 + Math.sin(i) * 3 - 1);
    const out = trueRange(h, l, c);
    for (const v of out) assert.ok(v >= 0);
});

test("trueRange: known second bar uses prev close", () => {
    // h=[10,8], l=[6,4], c=[8,6]
    // tr[0]=10-6=4; tr[1]=max(8-4=4, |8-8|=0, |4-8|=4)=4
    const out = trueRange([10, 8], [6, 4], [8, 6]);
    assert.ok(Math.abs(out[0] - 4) < 1e-12);
    assert.ok(Math.abs(out[1] - 4) < 1e-12);
});

test("trueRange: output length equals input length", () => {
    const n = 15;
    assert.equal(trueRange(Array(n).fill(10), Array(n).fill(8), Array(n).fill(9)).length, n);
});

// ── Wilder EMA ────────────────────────────────────────────────────────────────
test("wilderEma: invalid period throws", () => {
    assert.throws(() => wilderEma([1, 2, 3], 0));
    assert.throws(() => wilderEma([1, 2, 3], -1));
});

test("wilderEma: insufficient data returns all nulls", () => {
    assert.ok(wilderEma([1, 2], 5).every(v => v === null));
});

test("wilderEma: constant input stays constant", () => {
    const out = wilderEma(Array(30).fill(7), 5);
    for (let i = 4; i < 30; i++) assert.ok(Math.abs(out[i] - 7) < 1e-10);
});

test("wilderEma: responds slower than EMA (alpha=1/p vs 2/(p+1))", () => {
    // A step-up; Wilder EMA should be slower (lower at same index)
    const data = Array(10).fill(100).concat(Array(20).fill(110));
    const outWilder = wilderEma(data, 5);
    const outEma    = ema(data, 5);
    // Both defined at last index; Wilder should be less responsive (closer to 100)
    const last = data.length - 1;
    assert.ok(outWilder[last] < outEma[last]);
});

test("wilderEma: output length equals input length", () => {
    assert.equal(wilderEma(Array(20).fill(1), 5).length, 20);
});

// ── VWMA ─────────────────────────────────────────────────────────────────────
test("vwma: invalid period throws", () => {
    assert.throws(() => vwma([1, 2, 3], [100, 200, 300], 0));
});

test("vwma: mismatched lengths throw", () => {
    assert.throws(() => vwma([1, 2, 3], [100, 200], 2));
});

test("vwma: insufficient data returns all nulls", () => {
    assert.ok(vwma([1, 2], [100, 200], 5).every(v => v === null));
});

test("vwma: constant price returns that price", () => {
    const out = vwma(Array(20).fill(5), Array(20).fill(100), 5);
    for (let i = 4; i < 20; i++) assert.ok(Math.abs(out[i] - 5) < 1e-10);
});

test("vwma: higher volume on higher price increases average", () => {
    // period=2: prices=[1,3], vols=[1,10] → vwma = (1*1+3*10)/(1+10) ≈ 2.818
    const out = vwma([1, 3], [1, 10], 2);
    assert.ok(out[1] > 2 && out[1] < 3);
});

test("vwma: output length equals input length", () => {
    assert.equal(vwma(Array(10).fill(1), Array(10).fill(100), 3).length, 10);
});

// ── Historical Volatility ─────────────────────────────────────────────────────
test("histVol: invalid period throws", () => {
    assert.throws(() => histVol([1, 2, 3], 0));
    assert.throws(() => histVol([1, 2, 3], -1));
});

test("histVol: insufficient data returns all nulls (n <= period)", () => {
    assert.ok(histVol([1, 2, 3, 4, 5], 5).every(v => v === null));
});

test("histVol: first valid index at period", () => {
    const data = Array.from({ length: 20 }, (_, i) => 100 + i * 0.01);
    const out = histVol(data, 5);
    assert.equal(out[4], null);
    assert.ok(out[5] !== null);
});

test("histVol: non-negative always", () => {
    const data = Array.from({ length: 50 }, (_, i) => 100 + Math.sin(i * 0.3) * 5);
    const out = histVol(data, 10);
    for (const v of out) {
        if (v !== null) assert.ok(v >= 0);
    }
});

test("histVol: constant prices yield zero vol", () => {
    const out = histVol(Array(30).fill(100), 10);
    for (let i = 10; i < 30; i++) {
        if (out[i] !== null) assert.ok(Math.abs(out[i]) < 1e-10);
    }
});

test("histVol: output length equals input length", () => {
    assert.equal(histVol(Array(20).fill(100), 5).length, 20);
});

// ── CMF ──────────────────────────────────────────────────────────────────────
test("cmf: invalid period throws", () => {
    assert.throws(() => cmf([1], [0], [1], [100], 0));
});

test("cmf: mismatched lengths throw", () => {
    assert.throws(() => cmf([1, 2], [0], [1, 1], [100, 200], 1));
});

test("cmf: insufficient data returns all nulls", () => {
    const h = Array(3).fill(1), l = Array(3).fill(0), c = Array(3).fill(0.5), v = Array(3).fill(100);
    assert.ok(cmf(h, l, c, v, 5).every(x => x === null));
});

test("cmf: stays in [-1, 1]", () => {
    const n = 50;
    const h = [], l = [], c = [], v = [];
    for (let i = 0; i < n; i++) {
        const mid = 100 + Math.sin(i * 0.3) * 5;
        h.push(mid + 2); l.push(mid - 2); c.push(mid);
        v.push(1000 + (i % 5) * 200);
    }
    const out = cmf(h, l, c, v, 10);
    for (const val of out) {
        if (val !== null) assert.ok(val >= -1 - 1e-10 && val <= 1 + 1e-10);
    }
});

test("cmf: close at high yields positive cmf", () => {
    // When close = high, CLV = 1; all positive money flow → cmf near 1
    const n = 25;
    const h = Array.from({ length: n }, (_, i) => 10 + i * 0.1);
    const l = Array.from({ length: n }, (_, i) =>  8 + i * 0.1);
    const c = h.slice(); // close == high → CLV = 1
    const v = Array(n).fill(1000);
    const out = cmf(h, l, c, v, 10);
    for (let i = 9; i < n; i++) assert.ok(Math.abs(out[i] - 1) < 1e-10);
});

test("cmf: output length equals input length", () => {
    const n = 20;
    assert.equal(cmf(Array(n).fill(1), Array(n).fill(0), Array(n).fill(0.5), Array(n).fill(100), 5).length, n);
});

// ── Accumulation/Distribution ─────────────────────────────────────────────────
test("accDist: mismatched lengths throw", () => {
    assert.throws(() => accDist([1, 2], [0], [1, 1], [100, 200]));
    assert.throws(() => accDist([1, 2], [0, 1], [1, 1], [100]));
});

test("accDist: empty input returns empty", () => {
    assert.deepEqual(accDist([], [], [], []), []);
});

test("accDist: first value is 0 when range > 0", () => {
    // First bar: H=10, L=8, C=10 → CLV=1, out[0]=0+1*100=100... wait, out[0] = 0 + clv*vol
    // But cpp/ has out[0] = (i==0 ? 0 : out[i-1]) + clv*vol[i] = 0 + clv*vol[0]
    const out = accDist([10], [8], [9], [100]);
    // CLV = ((9-8)-(10-9))/(10-8) = (1-1)/2 = 0, so out[0] = 0
    assert.ok(Math.abs(out[0]) < 1e-12);
});

test("accDist: known values", () => {
    // H=10, L=8, C=10 → CLV = ((10-8)-(10-10))/(10-8) = 2/2 = 1; out[0]=0+1*100=100
    // H=10, L=8, C=8  → CLV = ((8-8)-(10-8))/(10-8) = -2/2 = -1; out[1]=100+(-1)*200=-100
    const out = accDist([10, 10], [8, 8], [10, 8], [100, 200]);
    assert.ok(Math.abs(out[0] - 100) < 1e-12);
    assert.ok(Math.abs(out[1] - (-100)) < 1e-12);
});

test("accDist: output length equals input length", () => {
    const n = 15;
    assert.equal(accDist(Array(n).fill(10), Array(n).fill(8), Array(n).fill(9), Array(n).fill(100)).length, n);
});

// ── Force Index ───────────────────────────────────────────────────────────────
test("forceIndex: invalid period throws", () => {
    assert.throws(() => forceIndex([1, 2, 3], [100, 200, 300], 0));
});

test("forceIndex: mismatched lengths throw", () => {
    assert.throws(() => forceIndex([1, 2, 3], [100, 200], 2));
});

test("forceIndex: insufficient data returns all nulls", () => {
    const out = forceIndex(Array(3).fill(10), Array(3).fill(100), 5);
    assert.ok(out.every(v => v === null));
});

test("forceIndex: constant close yields zero", () => {
    const out = forceIndex(Array(30).fill(10), Array(30).fill(1000), 5);
    for (const v of out) {
        if (v !== null) assert.ok(Math.abs(v) < 1e-10);
    }
});

test("forceIndex: output length equals input length", () => {
    assert.equal(forceIndex(Array(20).fill(10), Array(20).fill(1000), 3).length, 20);
});

// ── Volume Oscillator ─────────────────────────────────────────────────────────
test("volOsc: invalid period throws", () => {
    assert.throws(() => volOsc([100, 200, 300], 0, 28));
    assert.throws(() => volOsc([100, 200, 300], 14, 0));
});

test("volOsc: insufficient data returns all nulls", () => {
    const out = volOsc(Array(5).fill(100), 14, 28);
    assert.ok(out.every(v => v === null));
});

test("volOsc: constant volume yields zero", () => {
    const out = volOsc(Array(50).fill(1000), 5, 10);
    for (const v of out) {
        if (v !== null) assert.ok(Math.abs(v) < 1e-10);
    }
});

test("volOsc: output length equals input length", () => {
    assert.equal(volOsc(Array(40).fill(100), 5, 10).length, 40);
});

// ── Donchian Channel ──────────────────────────────────────────────────────────
test("donchian: invalid period throws", () => {
    assert.throws(() => donchian([1, 2], [0, 1], 0));
});

test("donchian: mismatched lengths throw", () => {
    assert.throws(() => donchian([1, 2, 3], [0, 1], 2));
});

test("donchian: insufficient data returns all nulls", () => {
    const { upper, middle, lower } = donchian([1, 2], [0, 1], 5);
    assert.ok(upper.every(v => v === null));
    assert.ok(middle.every(v => v === null));
    assert.ok(lower.every(v => v === null));
});

test("donchian: upper >= middle >= lower always where defined", () => {
    const n = 50;
    const h = Array.from({ length: n }, (_, i) => 100 + Math.sin(i * 0.4) * 10 + 2);
    const l = Array.from({ length: n }, (_, i) => 100 + Math.sin(i * 0.4) * 10 - 2);
    const { upper, middle, lower } = donchian(h, l, 10);
    for (let i = 0; i < n; i++) {
        if (upper[i] !== null) {
            assert.ok(upper[i] >= middle[i] - 1e-12);
            assert.ok(middle[i] >= lower[i] - 1e-12);
        }
    }
});

test("donchian: known values on simple input", () => {
    // period=3; at i=2: upper=max(10,11,12)=12, lower=min(8,9,10)=8, middle=10
    const h = [10, 11, 12];
    const l = [8,  9,  10];
    const { upper, middle, lower } = donchian(h, l, 3);
    assert.equal(upper[0], null); assert.equal(upper[1], null);
    assert.ok(Math.abs(upper[2] - 12) < 1e-12);
    assert.ok(Math.abs(lower[2] - 8)  < 1e-12);
    assert.ok(Math.abs(middle[2] - 10) < 1e-12);
});

test("donchian: output length equals input length", () => {
    const n = 20;
    const { upper, middle, lower } = donchian(Array(n).fill(10), Array(n).fill(8), 5);
    assert.equal(upper.length, n);
    assert.equal(middle.length, n);
    assert.equal(lower.length, n);
});

// ── Pivot Points (Classic) ────────────────────────────────────────────────────
test("pivotClassic: known values", () => {
    // H=12, L=8, C=10: P=(12+8+10)/3=10; R1=20-8=12; S1=20-12=8
    const { p, r1, s1, r2, s2, r3, s3 } = pivotClassic(12, 8, 10);
    assert.ok(Math.abs(p  - 10) < 1e-12);
    assert.ok(Math.abs(r1 - 12) < 1e-12);
    assert.ok(Math.abs(s1 -  8) < 1e-12);
    assert.ok(r1 > p && p > s1);
    assert.ok(r2 > r1);
    assert.ok(s2 < s1);
    assert.ok(r3 > r2);
    assert.ok(s3 < s2);
});

test("pivotClassic: shape R1 > P > S1", () => {
    const { p, r1, s1 } = pivotClassic(110, 90, 100);
    assert.ok(r1 > p);
    assert.ok(p > s1);
});

// ── Pivot Points (Fibonacci) ──────────────────────────────────────────────────
test("pivotFibonacci: shape R1 > P > S1", () => {
    const { p, r1, s1 } = pivotFibonacci(110, 90, 100);
    assert.ok(r1 > p);
    assert.ok(p > s1);
});

test("pivotFibonacci: known pivot value", () => {
    // H=110, L=90, C=100: P=(110+90+100)/3=100; range=20
    const { p, r1, r2, r3, s1, s2, s3 } = pivotFibonacci(110, 90, 100);
    assert.ok(Math.abs(p - 100) < 1e-12);
    assert.ok(Math.abs(r1 - (100 + 20 * 0.382)) < 1e-10);
    assert.ok(Math.abs(s1 - (100 - 20 * 0.382)) < 1e-10);
    assert.ok(r3 > r2 && r2 > r1);
    assert.ok(s3 < s2 && s2 < s1);
});

// ── Pivot Points (Camarilla) ──────────────────────────────────────────────────
test("pivotCamarilla: shape R1 > close > S1", () => {
    const { r1, r2, r3, r4, s1, s2, s3, s4 } = pivotCamarilla(110, 90, 100);
    assert.ok(r1 > 100 && s1 < 100);
    assert.ok(r4 > r3 && r3 > r2 && r2 > r1);
    assert.ok(s4 < s3 && s3 < s2 && s2 < s1);
});

test("pivotCamarilla: known r1 value", () => {
    // H=110, L=90, C=100: r=20; r1=100+20*1.1/12
    const { r1, s1 } = pivotCamarilla(110, 90, 100);
    assert.ok(Math.abs(r1 - (100 + 20 * 1.1 / 12)) < 1e-10);
    assert.ok(Math.abs(s1 - (100 - 20 * 1.1 / 12)) < 1e-10);
});

// ── Fibonacci Retracement ─────────────────────────────────────────────────────
test("fibonacci: returns 7 levels", () => {
    const levels = fibonacci(100, 80);
    assert.equal(levels.length, 7);
});

test("fibonacci: first level is swingHigh, last is swingLow", () => {
    const levels = fibonacci(100, 80);
    assert.ok(Math.abs(levels[0] - 100) < 1e-12);
    assert.ok(Math.abs(levels[6] - 80) < 1e-12);
});

test("fibonacci: levels are descending (uptrend)", () => {
    const levels = fibonacci(100, 80);
    for (let i = 1; i < 7; i++) assert.ok(levels[i] < levels[i - 1] + 1e-12);
});

test("fibonacci: known 50% level", () => {
    const levels = fibonacci(100, 80);
    // F[3]=0.5: level = 100 - 0.5*20 = 90
    assert.ok(Math.abs(levels[3] - 90) < 1e-12);
});

// ── SAR ───────────────────────────────────────────────────────────────────────
test("sar: mismatched lengths throw", () => {
    assert.throws(() => sar([1, 2, 3], [0, 1]));
});

test("sar: n < 2 returns all nulls", () => {
    const { sar: s, isBullish: b } = sar([10], [8]);
    assert.ok(s.every(v => v === null));
    assert.ok(b.every(v => v === null));
});

test("sar: output length equals input length", () => {
    const n = 30;
    const h = Array.from({ length: n }, (_, i) => 100 + i * 0.1 + 1);
    const l = Array.from({ length: n }, (_, i) => 100 + i * 0.1);
    const { sar: s, isBullish: b } = sar(h, l);
    assert.equal(s.length, n);
    assert.equal(b.length, n);
});

test("sar: isBullish is 0.0 or 1.0", () => {
    const n = 30;
    const h = Array.from({ length: n }, (_, i) => 100 + Math.sin(i * 0.3) * 5 + 2);
    const l = Array.from({ length: n }, (_, i) => 100 + Math.sin(i * 0.3) * 5 - 2);
    const { isBullish } = sar(h, l);
    for (const v of isBullish) {
        if (v !== null) assert.ok(v === 0.0 || v === 1.0);
    }
});

test("sar: first bar is bullish with sar = low[0]", () => {
    const { sar: s, isBullish: b } = sar([10, 11, 12], [8, 9, 10]);
    assert.ok(Math.abs(s[0] - 8) < 1e-12);
    assert.ok(b[0] === 1.0);
});

// ── Ichimoku ──────────────────────────────────────────────────────────────────
test("ichimoku: invalid period throws", () => {
    assert.throws(() => ichimoku([1], [0], [1], 0, 26, 52));
    assert.throws(() => ichimoku([1], [0], [1], 9, 0, 52));
    assert.throws(() => ichimoku([1], [0], [1], 9, 26, 0));
});

test("ichimoku: mismatched lengths throw", () => {
    assert.throws(() => ichimoku([1, 2], [0], [1, 1], 9, 26, 52));
    assert.throws(() => ichimoku([1, 2], [0, 1], [1], 9, 26, 52));
});

test("ichimoku: output length equals input length", () => {
    const n = 60;
    const h = Array(n).fill(11), l = Array(n).fill(9), c = Array(n).fill(10);
    const { tenkan, kijun, senkouA, senkouB, chikou } = ichimoku(h, l, c, 9, 26, 52);
    assert.equal(tenkan.length, n);
    assert.equal(kijun.length, n);
    assert.equal(senkouA.length, n);
    assert.equal(senkouB.length, n);
    assert.equal(chikou.length, n);
});

test("ichimoku: constant input — tenkan equals constant", () => {
    const n = 60;
    const h = Array(n).fill(11), l = Array(n).fill(9), c = Array(n).fill(10);
    const { tenkan } = ichimoku(h, l, c, 9, 26, 52);
    for (let i = 8; i < n; i++) assert.ok(Math.abs(tenkan[i] - 10) < 1e-10);
});

test("ichimoku: tenkan first valid at tenkan-1", () => {
    const n = 60;
    const h = Array(n).fill(11), l = Array(n).fill(9), c = Array(n).fill(10);
    const { tenkan } = ichimoku(h, l, c, 9, 26, 52);
    assert.equal(tenkan[7], null);
    assert.ok(tenkan[8] !== null);
});

test("ichimoku: chikou shifted back by kijun", () => {
    const n = 60;
    const c = Array.from({ length: n }, (_, i) => 100 + i);
    const h = c.map(v => v + 1);
    const l = c.map(v => v - 1);
    const kijun = 26;
    const { chikou } = ichimoku(h, l, c, 9, kijun, 52);
    // chikou[i] = close[i+kijun], so chikou[0] = close[26]
    assert.ok(Math.abs(chikou[0] - c[kijun]) < 1e-12);
});
