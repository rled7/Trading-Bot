import { test } from "node:test";
import assert from "node:assert/strict";

import { sma, ema, rsi, atr, macd, bollinger, stochastic, obv, adx,
         wma, cci, williamsR, roc, mfi, vwap, keltner } from "../src/indicators.js";

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
