import { test } from "node:test";
import assert from "node:assert/strict";

import { Bar } from "../src/types.js";
import {
    isDoji, isHammer, isEngulfing, isMarubozu, isPinBar,
    scanPatterns,
} from "../src/patterns.js";

const b = (o, h, l, c) =>
    new Bar({ timestamp: 0, open: o, high: h, low: l, close: c, volume: 1 });

// ── Doji ────────────────────────────────────────────────────────────────
test("doji: tiny body matches", () => {
    assert.equal(isDoji(b(1.0, 1.5, 0.5, 1.005)), 1);
});
test("doji: big body does not match", () => {
    assert.equal(isDoji(b(1.0, 1.5, 0.5, 1.5)), 0);
});
test("doji: zero range never matches", () => {
    assert.equal(isDoji(b(1.0, 1.0, 1.0, 1.0)), 0);
});
test("doji: threshold below matches, above does not", () => {
    assert.equal(isDoji(b(1.0, 1.5, 0.5, 1.09)), 1);
    assert.equal(isDoji(b(1.0, 1.5, 0.5, 1.11)), 0);
});

// ── Hammer / shooting star ──────────────────────────────────────────────
test("hammer: bullish", () => {
    assert.equal(isHammer(b(1.0, 1.06, 0.40, 1.05)), 1);
});
test("hammer: shooting star", () => {
    assert.equal(isHammer(b(1.05, 1.70, 0.99, 1.0)), -1);
});
test("hammer: zero range no match", () => {
    assert.equal(isHammer(b(1.0, 1.0, 1.0, 1.0)), 0);
});
test("hammer: balanced candle no match", () => {
    assert.equal(isHammer(b(1.0, 1.10, 0.90, 1.05)), 0);
});

// ── Engulfing ───────────────────────────────────────────────────────────
test("engulfing: bullish", () => {
    assert.equal(
        isEngulfing(b(100, 100.5, 94.5, 95), b(94, 101, 94, 101)), 1);
});
test("engulfing: bearish", () => {
    assert.equal(
        isEngulfing(b(95, 100, 95, 100), b(101, 101, 94, 94)), -1);
});
test("engulfing: same direction no match", () => {
    assert.equal(
        isEngulfing(b(95, 100, 95, 100), b(96, 101, 96, 101)), 0);
});
test("engulfing: inside-bar no match", () => {
    assert.equal(
        isEngulfing(b(95, 100, 95, 100), b(99, 99.5, 96, 96)), 0);
});

// ── Marubozu ────────────────────────────────────────────────────────────
test("marubozu: bullish", () => {
    assert.equal(isMarubozu(b(1.00, 1.02, 1.00, 1.95)), 1);
});
test("marubozu: bearish", () => {
    assert.equal(isMarubozu(b(1.95, 1.95, 0.97, 1.00)), -1);
});
test("marubozu: long shadow no match", () => {
    assert.equal(isMarubozu(b(1.00, 1.95, 0.50, 1.95)), 0);
});
test("marubozu: zero range no match", () => {
    assert.equal(isMarubozu(b(1.0, 1.0, 1.0, 1.0)), 0);
});

// ── Pin bar ─────────────────────────────────────────────────────────────
test("pin bar: bullish", () => {
    assert.equal(isPinBar(b(1.0, 1.06, 0.40, 1.05)), 1);
});
test("pin bar: bearish", () => {
    assert.equal(isPinBar(b(1.05, 1.70, 0.99, 1.0)), -1);
});
test("pin bar: tall body no match", () => {
    assert.equal(isPinBar(b(1.0, 1.95, 1.0, 1.95)), 0);
});
test("pin bar: zero range no match", () => {
    assert.equal(isPinBar(b(1.0, 1.0, 1.0, 1.0)), 0);
});

// ── Scan ───────────────────────────────────────────────────────────────
test("scan: collects engulfing and doji", () => {
    const bars = [
        b(100, 100.5, 94.5, 95),
        b(94,  101,   94,   101),
        b(101, 102,   100,  101.05),
    ];
    const matches = scanPatterns(bars);
    const set = new Set(matches.map(m => `${m.barIndex}:${m.name}`));
    assert.ok(set.has("1:engulfing"));
    assert.ok(set.has("2:doji"));
});

test("scan: empty input returns empty", () => {
    assert.deepEqual(scanPatterns([]), []);
});
