import { test } from "node:test";
import assert from "node:assert/strict";

import { sma, ema, rsi, atr } from "../src/indicators.js";

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
