import { test } from "node:test";
import assert from "node:assert/strict";

import { Bar } from "../src/types.js";
import {
    isDoji, isHammer, isEngulfing, isMarubozu, isPinBar,
    isMorningStar, isEveningStar, isThreeWhiteSoldiers, isThreeBlackCrows,
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

// ── Morning Star ─────────────────────────────────────────────────────────────
// b0: bearish large body (body > range*0.60)
// b1: small body (body < b0.body*0.30)
// b2: bullish, close above midpoint of b0 body
test("morning star: positive match returns +1", () => {
    // b0: bearish, open=110, close=90 → body=20, range=22 → body/range=0.909 > 0.60; midpoint=100
    const b0 = b(110, 111, 89, 90);   // bearish, body=20, range=22
    // b1: small body < 20*0.30 = 6; use body=1
    const b1 = b(89, 90, 88, 89.5);   // body=0.5, range=2 → well under 6
    // b2: bullish, close > 100
    const b2 = b(90, 112, 89, 105);   // bullish, close=105 > 100
    assert.equal(isMorningStar(b0, b1, b2), 1);
});

test("morning star: b0 not bearish → no match", () => {
    const b0 = b(90, 111, 89, 110);   // bullish
    const b1 = b(89, 90, 88, 89.5);
    const b2 = b(90, 112, 89, 105);
    assert.equal(isMorningStar(b0, b1, b2), 0);
});

test("morning star: b0 small body → no match", () => {
    // b0: bearish but body/range < 0.60
    const b0 = b(101, 110, 89, 99);   // body=2, range=21 → 0.095 < 0.60
    const b1 = b(89, 90, 88, 89.5);
    const b2 = b(90, 112, 89, 105);
    assert.equal(isMorningStar(b0, b1, b2), 0);
});

test("morning star: b2 not bullish → no match", () => {
    const b0 = b(110, 111, 89, 90);
    const b1 = b(89, 90, 88, 89.5);
    const b2 = b(105, 112, 89, 90);   // bearish
    assert.equal(isMorningStar(b0, b1, b2), 0);
});

test("morning star: zero-range bar never matches", () => {
    const b0 = b(110, 111, 89, 90);
    const b1 = b(89, 89, 89, 89);     // zero range
    const b2 = b(90, 112, 89, 105);
    assert.equal(isMorningStar(b0, b1, b2), 0);
});

// ── Evening Star ─────────────────────────────────────────────────────────────
test("evening star: positive match returns -1", () => {
    // b0: bullish large body, open=90, close=110 → body=20, range=22; midpoint=100
    const b0 = b(90, 111, 89, 110);   // bullish, body=20, range=22
    // b1: small body < 20*0.30 = 6
    const b1 = b(110, 112, 110, 110.5);  // body=0.5
    // b2: bearish, close < 100
    const b2 = b(110, 112, 88, 92);   // bearish, close=92 < 100
    assert.equal(isEveningStar(b0, b1, b2), -1);
});

test("evening star: b0 not bullish → no match", () => {
    const b0 = b(110, 111, 89, 90);   // bearish
    const b1 = b(110, 112, 110, 110.5);
    const b2 = b(110, 112, 88, 92);
    assert.equal(isEveningStar(b0, b1, b2), 0);
});

test("evening star: b2 not bearish → no match", () => {
    const b0 = b(90, 111, 89, 110);
    const b1 = b(110, 112, 110, 110.5);
    const b2 = b(88, 112, 88, 110);   // bullish
    assert.equal(isEveningStar(b0, b1, b2), 0);
});

test("evening star: zero-range bar never matches", () => {
    const b0 = b(90, 111, 89, 110);
    const b1 = b(110, 110, 110, 110); // zero range
    const b2 = b(110, 112, 88, 92);
    assert.equal(isEveningStar(b0, b1, b2), 0);
});

// ── Three White Soldiers ──────────────────────────────────────────────────────
// Each bar: bullish, body_pct >= 0.60, upper_shadow < body*0.30, closes higher
test("three white soldiers: positive match returns +1", () => {
    // Each bar: open=X, close=X+10, high=close+1, low=open-0.1
    // body=10, range≈11.1, body_pct=0.9 > 0.60; upper_shadow=1 < 10*0.30=3
    const b0 = b(100, 111, 99.9, 110);
    const b1 = b(105, 121, 104.9, 120);
    const b2 = b(115, 131, 114.9, 130);
    assert.equal(isThreeWhiteSoldiers(b0, b1, b2), 1);
});

test("three white soldiers: one bearish bar → no match", () => {
    const b0 = b(100, 111, 99.9, 110);
    const b1 = b(115, 121, 104.9, 105);  // bearish
    const b2 = b(115, 131, 114.9, 130);
    assert.equal(isThreeWhiteSoldiers(b0, b1, b2), 0);
});

test("three white soldiers: closes not strictly rising → no match", () => {
    const b0 = b(100, 111, 99.9, 110);
    const b1 = b(105, 121, 104.9, 120);
    const b2 = b(115, 121, 114.9, 115);  // close < b1.close
    assert.equal(isThreeWhiteSoldiers(b0, b1, b2), 0);
});

test("three white soldiers: large upper shadow → no match", () => {
    // upper_shadow = high - close = 5; body = 10; 5 >= 10*0.30 → fail
    const b0 = b(100, 116, 99.9, 111);   // upper_shadow=5, body=11
    const b1 = b(105, 121, 104.9, 120);
    const b2 = b(115, 131, 114.9, 130);
    assert.equal(isThreeWhiteSoldiers(b0, b1, b2), 0);
});

test("three white soldiers: zero-range bar never matches", () => {
    const b0 = b(100, 100, 100, 100);    // zero range
    const b1 = b(105, 121, 104.9, 120);
    const b2 = b(115, 131, 114.9, 130);
    assert.equal(isThreeWhiteSoldiers(b0, b1, b2), 0);
});

// ── Three Black Crows ─────────────────────────────────────────────────────────
test("three black crows: positive match returns -1", () => {
    // Each bar: bearish, body_pct >= 0.60, lower_shadow < body*0.30
    // open=X+10, close=X, low=close-1, high=open+0.1
    // body=10, range≈11.1, body_pct≈0.9; lower_shadow=1 < 10*0.30=3
    const b0 = b(130, 130.1, 119, 120);
    const b1 = b(120, 120.1, 109, 110);
    const b2 = b(110, 110.1,  99, 100);
    assert.equal(isThreeBlackCrows(b0, b1, b2), -1);
});

test("three black crows: one bullish bar → no match", () => {
    const b0 = b(130, 130.1, 119, 120);
    const b1 = b(109, 120.1, 109, 120);  // bullish
    const b2 = b(110, 110.1,  99, 100);
    assert.equal(isThreeBlackCrows(b0, b1, b2), 0);
});

test("three black crows: closes not strictly falling → no match", () => {
    const b0 = b(130, 130.1, 119, 120);
    const b1 = b(120, 120.1, 109, 110);
    const b2 = b(115, 115.1, 110, 111);  // close > b1.close
    assert.equal(isThreeBlackCrows(b0, b1, b2), 0);
});

test("three black crows: large lower shadow → no match", () => {
    // lower_shadow = close - low = 5; body = 10; 5 >= 10*0.30 → fail
    const b0 = b(130, 130.1, 114, 120);  // lower_shadow=6, body=10
    const b1 = b(120, 120.1, 109, 110);
    const b2 = b(110, 110.1,  99, 100);
    assert.equal(isThreeBlackCrows(b0, b1, b2), 0);
});

test("three black crows: zero-range bar never matches", () => {
    const b0 = b(130, 130, 130, 130);    // zero range
    const b1 = b(120, 120.1, 109, 110);
    const b2 = b(110, 110.1,  99, 100);
    assert.equal(isThreeBlackCrows(b0, b1, b2), 0);
});

// ── Scan with three-bar patterns ─────────────────────────────────────────────
test("scan: detects three_white_soldiers in sequence", () => {
    // Three bullish bars with strong bodies and rising closes
    const bars = [
        b(100, 111, 99.9, 110),
        b(105, 121, 104.9, 120),
        b(115, 131, 114.9, 130),
    ];
    const matches = scanPatterns(bars);
    const names = new Set(matches.map(m => m.name));
    assert.ok(names.has("three_white_soldiers"));
});

test("scan: detects morning_star in sequence", () => {
    const bars = [
        b(110, 111, 89, 90),       // b0: bearish large body
        b(89, 90, 88, 89.5),       // b1: small body
        b(90, 112, 89, 105),       // b2: bullish, close > 100 (midpoint)
    ];
    const matches = scanPatterns(bars);
    const names = new Set(matches.map(m => m.name));
    assert.ok(names.has("morning_star"));
});

test("scan: three-bar patterns not emitted before index 2", () => {
    const bars = [
        b(110, 111, 89, 90),
        b(89, 90, 88, 89.5),
    ];
    const matches = scanPatterns(bars);
    const threeBar = matches.filter(m =>
        ["morning_star", "evening_star", "three_white_soldiers", "three_black_crows"].includes(m.name)
    );
    assert.equal(threeBar.length, 0);
});
