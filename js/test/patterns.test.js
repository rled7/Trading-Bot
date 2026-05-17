import { test } from "node:test";
import assert from "node:assert/strict";

import { Bar } from "../src/types.js";
import {
    isDoji, isHammer, isEngulfing, isMarubozu, isPinBar,
    isMorningStar, isEveningStar, isThreeWhiteSoldiers, isThreeBlackCrows,
    isDoubleTop, isDoubleBottom, isAscendingTriangle, isDescendingTriangle,
    isBullishFlag, isBearishFlag, isHeadAndShoulders, isInverseHeadAndShoulders,
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

// ── Double Top ────────────────────────────────────────────────────────────────

// Helper to construct a double-top scenario:
// First half: rises to peak1, second half rises to peak2 (similar highs),
// with a valley in between and final close below midpoint of (peak, valley).
function makeDoubleTopBars() {
    // 30 bars total. First 15 rise to ~120 then fall; last 15 rise to ~120 then fall.
    // Overall low ~90, overall high ~120, final close ~100 (below (120+90)/2=105).
    const bars = [];
    // First half [0..14]: price goes 100→120→100
    for (let i = 0; i < 15; i++) {
        const p = i < 8 ? 100 + i * (20 / 7) : 120 - (i - 7) * (20 / 7);
        bars.push(b(p - 0.5, p + 1, p - 1, p));
    }
    // Second half [15..29]: price goes 100→120→98 (close < (120+90)/2=105)
    for (let i = 0; i < 15; i++) {
        const p = i < 8 ? 100 + i * (20 / 7) : 120 - (i - 7) * (24 / 7);
        const clamp = Math.max(p, 88);
        bars.push(b(clamp - 0.5, clamp + 1, clamp - 1, clamp));
    }
    return bars;
}

test("doubleTop: positive match returns -1", () => {
    // Construct bars carefully matching cpp/ logic:
    // n=30, mid=15
    // hi1 = max highs[0..14], hi2 = max highs[15..29] — must be within tol=(hi1+hi2)*0.005
    // lo = min lows[0..29] — must be < hi1*0.99
    // bars[29].close must be < (hi1+lo)/2
    const bars = [];
    // First half: peak at index 7 → high=120.5
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(119, 120.5, 89, 100));
        else bars.push(b(99, 101, 89, 100));
    }
    // Second half: peak at index 22 (index 7 in second half) → high=120.5 (same as hi1)
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(119, 120.5, 89, 100));
        else if (i === 14) bars.push(b(99, 101, 89, 98)); // close=98 < (120.5+89)/2=104.75
        else bars.push(b(99, 101, 89, 100));
    }
    assert.equal(isDoubleTop(bars), -1);
});

test("doubleTop: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 29 }, () => b(100, 110, 90, 100));
    assert.equal(isDoubleTop(bars), 0);
});

test("doubleTop: highs too different → no match", () => {
    // hi1 ≈ 120, hi2 ≈ 150 — far apart, well outside tol
    const bars = [];
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(119, 120, 89, 100));
        else bars.push(b(99, 101, 89, 100));
    }
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(149, 150, 89, 100));  // hi2 = 150, very different
        else if (i === 14) bars.push(b(99, 101, 89, 98));
        else bars.push(b(99, 101, 89, 100));
    }
    assert.equal(isDoubleTop(bars), 0);
});

test("doubleTop: close above midpoint → no match", () => {
    // Same shape as positive match but final close=115 (above (120.5+89)/2=104.75)
    const bars = [];
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(119, 120.5, 89, 100));
        else bars.push(b(99, 101, 89, 100));
    }
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(119, 120.5, 89, 100));
        else if (i === 14) bars.push(b(114, 116, 89, 115)); // close=115 > midpoint
        else bars.push(b(99, 101, 89, 100));
    }
    assert.equal(isDoubleTop(bars), 0);
});

// ── Double Bottom ─────────────────────────────────────────────────────────────

test("doubleBottom: positive match returns +1", () => {
    // n=30, mid=15
    // lo1 = min lows[0..14], lo2 = min lows[15..29] — must be within tol=(lo1+lo2)*0.005
    // hi = max highs[0..29] — must be > lo1*1.01
    // bars[29].close must be > (lo1+hi)/2
    const bars = [];
    // First half: trough at index 7 → low=80
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else bars.push(b(99, 120, 89, 100));
    }
    // Second half: trough at index 7 of second half → low=80 (same as lo1)
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else if (i === 14) bars.push(b(99, 120, 89, 105)); // close=105 > (80+120)/2=100
        else bars.push(b(99, 120, 89, 100));
    }
    assert.equal(isDoubleBottom(bars), 1);
});

test("doubleBottom: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 29 }, () => b(100, 110, 90, 100));
    assert.equal(isDoubleBottom(bars), 0);
});

test("doubleBottom: lows too different → no match", () => {
    const bars = [];
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else bars.push(b(99, 120, 89, 100));
    }
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(51, 120, 50, 100));  // lo2=50, very different from lo1=80
        else if (i === 14) bars.push(b(99, 120, 89, 105));
        else bars.push(b(99, 120, 89, 100));
    }
    assert.equal(isDoubleBottom(bars), 0);
});

test("doubleBottom: close below midpoint → no match", () => {
    const bars = [];
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else bars.push(b(99, 120, 89, 100));
    }
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else if (i === 14) bars.push(b(81, 85, 80, 82)); // close=82 < (80+120)/2=100
        else bars.push(b(99, 120, 89, 100));
    }
    assert.equal(isDoubleBottom(bars), 0);
});

// ── Ascending Triangle ────────────────────────────────────────────────────────

function makeAscendingTriangleBars() {
    // 20 bars: highs flat around 120 (within 0.5%); lows rising
    // lo_first (bars 0..9) ≈ 90; lo_last (bars 10..19) ≈ 95 (> 90*1.002=90.18)
    // hi_var = max_hi(0..9) - max_hi(10..19) must be < resistance*0.005
    // resistance = max_hi(0..19) ≈ 120
    const bars = [];
    for (let i = 0; i < 10; i++) {
        bars.push(b(100, 120, 90, 105)); // first half: highs=120, lows=90
    }
    for (let i = 0; i < 10; i++) {
        bars.push(b(103, 120, 95, 108)); // second half: highs=120 (flat), lows=95 (rising)
    }
    return bars;
}

test("ascendingTriangle: positive match returns +1", () => {
    const bars = makeAscendingTriangleBars();
    assert.equal(isAscendingTriangle(bars), 1);
});

test("ascendingTriangle: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 19 }, () => b(100, 120, 90, 105));
    assert.equal(isAscendingTriangle(bars), 0);
});

test("ascendingTriangle: lows not rising → no match", () => {
    // Second half lows same as first half lows (not > lo_first*1.002)
    const bars = [];
    for (let i = 0; i < 10; i++) bars.push(b(100, 120, 95, 105));
    for (let i = 0; i < 10; i++) bars.push(b(103, 120, 95, 108)); // lows flat
    // lo_last=95, lo_first=95 → lo_last > 95*1.002=95.19 fails
    assert.equal(isAscendingTriangle(bars), 0);
});

test("ascendingTriangle: highs not flat → no match", () => {
    // hi_var = max_hi(first10) - max_hi(last10) must be < resistance*0.005
    // if first half has high=120, second has high=115, hi_var=5, resistance=120, 5 < 0.6 fails
    const bars = [];
    for (let i = 0; i < 10; i++) bars.push(b(100, 120, 90, 105));
    for (let i = 0; i < 10; i++) bars.push(b(103, 115, 95, 108)); // second half highs much lower
    assert.equal(isAscendingTriangle(bars), 0);
});

// ── Descending Triangle ───────────────────────────────────────────────────────

test("descending triangle: positive match returns -1", () => {
    // 20 bars: lows flat around 80 (within 0.5% of support); highs falling
    // hi_first (bars 0..9) ≈ 120; hi_last (bars 10..19) ≈ 115 (< 120*0.998=119.76)
    // lo_var = min_lo(0..9) - min_lo(10..19) must be < support*0.005
    // support = min_lo(0..19) ≈ 80
    const bars = [];
    for (let i = 0; i < 10; i++) bars.push(b(110, 120, 80, 105)); // first: highs=120, lows=80
    for (let i = 0; i < 10; i++) bars.push(b(100, 115, 80, 95));  // second: highs=115, lows=80
    assert.equal(isDescendingTriangle(bars), -1);
});

test("descending triangle: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 19 }, () => b(100, 120, 80, 100));
    assert.equal(isDescendingTriangle(bars), 0);
});

test("descending triangle: highs not falling → no match", () => {
    // hi_last >= hi_first*0.998 → no match
    const bars = [];
    for (let i = 0; i < 10; i++) bars.push(b(110, 120, 80, 105));
    for (let i = 0; i < 10; i++) bars.push(b(110, 120, 80, 95));  // second half highs same (not falling)
    assert.equal(isDescendingTriangle(bars), 0);
});

test("descending triangle: lows not flat → no match", () => {
    // lo_var = min_lo(0..9) - min_lo(10..19) must be < support*0.005
    // if first half low=80, second half low=70, lo_var=10, support=70, 10 < 0.35 fails
    const bars = [];
    for (let i = 0; i < 10; i++) bars.push(b(110, 120, 80, 105));
    for (let i = 0; i < 10; i++) bars.push(b(100, 115, 70, 95));  // second half lows much lower
    assert.equal(isDescendingTriangle(bars), 0);
});

// ── Scan with chart patterns ──────────────────────────────────────────────────

test("scan: detects double_bottom in sequence", () => {
    // Build 30 bars that satisfy the double_bottom condition
    const bars = [];
    // First half: trough at index 7, low=80; highs=120
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else bars.push(b(99, 120, 89, 100));
    }
    // Second half: trough at index 7, low=80; final close=105
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else if (i === 14) bars.push(b(99, 120, 89, 105));
        else bars.push(b(99, 120, 89, 100));
    }
    const matches = scanPatterns(bars);
    const names = new Set(matches.map(m => m.name));
    assert.ok(names.has("double_bottom"));
});

test("scan: chart patterns barIndex is bars.length - 1", () => {
    const bars = [];
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else bars.push(b(99, 120, 89, 100));
    }
    for (let i = 0; i < 15; i++) {
        if (i === 7) bars.push(b(81, 120, 80, 100));
        else if (i === 14) bars.push(b(99, 120, 89, 105));
        else bars.push(b(99, 120, 89, 100));
    }
    const matches = scanPatterns(bars);
    const chartMatches = matches.filter(m => m.name === "double_bottom");
    for (const m of chartMatches) {
        assert.equal(m.barIndex, bars.length - 1);
    }
});

test("scan: chart patterns not emitted with fewer than 20 bars", () => {
    const bars = Array.from({ length: 19 }, () => b(100, 110, 90, 100));
    const matches = scanPatterns(bars);
    const chartPatterns = ["double_top", "double_bottom", "ascending_triangle", "descending_triangle"];
    const found = matches.filter(m => chartPatterns.includes(m.name));
    assert.equal(found.length, 0);
});

// ── Bullish Flag ──────────────────────────────────────────────────────────────
// Pole (bars[n-20..n-11]): strong bullish move. Flag (bars[n-10..n-1]): tight consolidation.
// flag_rng < pole_rng * 0.45 and pole_rng > pole_lo * 0.01.

function makeBullishFlagBars() {
    // 20 bars: first 10 (pole): strong upward move from 100 to 150
    // last 10 (flag): tight sideways around 148-152 (flag_rng=4, pole_rng=50)
    // pole_bull: bars[9].close > bars[0].close → 150 > 100 ✓
    // tight: 4 < 50*0.45=22.5 ✓
    // pole_rng > pole_lo*0.01: 50 > 100*0.01=1 ✓
    const bars = [];
    // Pole: index 0..9, price rises from 100 to 150
    for (let i = 0; i < 10; i++) {
        const p = 100 + i * 5;
        bars.push(b(p, p + 1, p - 1, p + 0.5));
    }
    // Flag: index 10..19, tight range around 150 (148..152)
    for (let i = 0; i < 10; i++) {
        const p = 150 + (i % 3) * 0.5;
        bars.push(b(p, p + 1, p - 1, p));
    }
    return bars;
}

test("bullishFlag: positive match returns +1", () => {
    assert.equal(isBullishFlag(makeBullishFlagBars()), 1);
});

test("bullishFlag: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 19 }, () => b(100, 110, 90, 100));
    assert.equal(isBullishFlag(bars), 0);
});

test("bullishFlag: pole not bullish → no match", () => {
    // Make pole bearish: bars[n-20].close > bars[n-11].close
    const bars = [];
    // Pole: bearish (price falls from 150 to 100)
    for (let i = 0; i < 10; i++) {
        const p = 150 - i * 5;
        bars.push(b(p, p + 1, p - 1, p + 0.5));
    }
    // Flag: tight
    for (let i = 0; i < 10; i++) {
        bars.push(b(100, 101, 99, 100));
    }
    assert.equal(isBullishFlag(bars), 0);
});

test("bullishFlag: flag too wide → no match", () => {
    // flag_rng >= pole_rng * 0.45
    const bars = [];
    // Pole: bullish, small (pole_rng=10)
    for (let i = 0; i < 10; i++) {
        const p = 100 + i;
        bars.push(b(p, p + 0.5, p - 0.5, p + 0.3));
    }
    // Flag: wide range = 8 (>= 10*0.45=4.5)
    for (let i = 0; i < 10; i++) {
        const p = 109 + (i % 2) * 8; // swings between 109 and 117
        bars.push(b(p, p + 1, p - 1, p));
    }
    assert.equal(isBullishFlag(bars), 0);
});

// ── Bearish Flag ──────────────────────────────────────────────────────────────

function makeBearishFlagBars() {
    // 20 bars: first 10 (pole): strong downward move from 150 to 100
    // last 10 (flag): tight sideways (flag_rng=4, pole_rng=50)
    // pole_bear: bars[9].close < bars[0].close → 100 < 150 ✓
    const bars = [];
    // Pole: price falls from 150 to 100
    for (let i = 0; i < 10; i++) {
        const p = 150 - i * 5;
        bars.push(b(p, p + 1, p - 1, p - 0.5));
    }
    // Flag: tight range around 100 (98..102)
    for (let i = 0; i < 10; i++) {
        const p = 100 + (i % 3) * 0.5;
        bars.push(b(p, p + 1, p - 1, p));
    }
    return bars;
}

test("bearishFlag: positive match returns -1", () => {
    assert.equal(isBearishFlag(makeBearishFlagBars()), -1);
});

test("bearishFlag: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 19 }, () => b(100, 110, 90, 100));
    assert.equal(isBearishFlag(bars), 0);
});

test("bearishFlag: pole not bearish → no match", () => {
    // Make pole bullish
    const bars = [];
    for (let i = 0; i < 10; i++) {
        const p = 100 + i * 5;
        bars.push(b(p, p + 1, p - 1, p + 0.5));
    }
    for (let i = 0; i < 10; i++) {
        bars.push(b(150, 151, 149, 150));
    }
    assert.equal(isBearishFlag(bars), 0);
});

test("bearishFlag: flag too wide → no match", () => {
    const bars = [];
    // Pole: bearish, small (pole_rng=10)
    for (let i = 0; i < 10; i++) {
        const p = 110 - i;
        bars.push(b(p, p + 0.5, p - 0.5, p - 0.3));
    }
    // Flag: wide (flag_rng >= pole_rng*0.45)
    for (let i = 0; i < 10; i++) {
        const p = 100 + (i % 2) * 8;
        bars.push(b(p, p + 1, p - 1, p));
    }
    assert.equal(isBearishFlag(bars), 0);
});

// ── Head and Shoulders ────────────────────────────────────────────────────────
// n >= 40; s = n-40
// ls = max_range(s, s+12), h = max_range(s+13, s+26), rs = max_range(s+27, n-1)
// neckline = min(min_range(s,s+12), min_range(s+27,n-1))
// h > ls*1.01, h > rs*1.01, |ls-rs|/(ls+rs)*2 < 0.07, bars[n-1].close < neckline

function makeHAndSBars() {
    // 40 bars:
    // s=0, left shoulder region [0..12]: peak ~110, lows ~95
    // head region [13..26]: peak ~130, lows ~95
    // right shoulder region [27..39]: peak ~110, lows ~95
    // neckline = min(minLow[0..12], minLow[27..39]) = 95
    // last bar close must be < 95
    const bars = [];
    // Left shoulder [0..12]: peak ~110, all lows = 95
    for (let i = 0; i <= 12; i++) {
        const peakFrac = 1 - Math.abs(i - 6) / 6;
        const high  = 96 + 15 * peakFrac;
        const low   = 95;
        const close = 96 + 14 * peakFrac;
        bars.push(b(close - 0.5, high, low, close));
    }
    // Head [13..26]: peak ~130, all lows = 95
    for (let i = 0; i <= 13; i++) {
        const peakFrac = 1 - Math.abs(i - 6.5) / 6.5;
        const high  = 96 + 35 * peakFrac;
        const low   = 95;
        const close = 96 + 34 * peakFrac;
        bars.push(b(close - 0.5, high, low, close));
    }
    // Right shoulder [27..38]: peak ~110, lows = 95
    for (let i = 0; i <= 11; i++) {
        const peakFrac = 1 - Math.abs(i - 5.5) / 5.5;
        const high  = 96 + 15 * peakFrac;
        const low   = 95;
        const close = 96 + 14 * peakFrac;
        bars.push(b(close - 0.5, high, low, close));
    }
    // Bar 39 (last): close = 93 (below neckline=95), low=95 to not change neckline
    bars.push(b(94, 95, 95, 93));
    return bars;
}

test("headAndShoulders: positive match returns -1", () => {
    assert.equal(isHeadAndShoulders(makeHAndSBars()), -1);
});

test("headAndShoulders: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 39 }, () => b(100, 110, 90, 100));
    assert.equal(isHeadAndShoulders(bars), 0);
});

test("headAndShoulders: head not higher than shoulders → no match", () => {
    // Make head same height as shoulders (h = ls, not h > ls*1.01)
    const bars = Array.from({ length: 40 }, (_, i) => {
        // All regions have similar max high of ~110
        return b(100, 110, 90, 100);
    });
    assert.equal(isHeadAndShoulders(bars), 0);
});

// ── Inverse Head and Shoulders ────────────────────────────────────────────────

function makeInvHAndSBars() {
    // 40 bars: inverse H&S
    // s=0, left shoulder [0..12], head [13..26], right shoulder [27..39].
    // ls = min_range(0..12) = trough of left shoulder
    // h  = min_range(13..26) = deeper head trough (must be < ls*0.99 and rs*0.99)
    // rs = min_range(27..39) = trough of right shoulder (~= ls)
    // neckline = max(max_range(0..12), max_range(27..39))
    // bars[39].close > neckline
    //
    // Strategy: neckline dominated by left shoulder max high = 110.
    // Right shoulder highs <= 109 (so neckline = 110 from left shoulder).
    // Last bar (39): high = 109, close = 111.  close > high is physically unusual
    // but the Bar class allows it; it tests the detector logic, not OHLC validity.
    const bars = [];
    // Left shoulder [0..12]: troughs ~80, max high = 110
    for (let i = 0; i <= 12; i++) {
        const peakFrac   = 1 - Math.abs(i - 6) / 6;
        const troughFrac = Math.abs(i - 6) / 6;
        const low  = 80 + 5 * peakFrac; // trough at ~80
        const high = 95 + 15 * peakFrac; // peak high at ~110 (i=6)
        bars.push(b(high - 1, high, low, high - 0.5));
    }
    // Head [13..26]: troughs ~60, highs <= 108 (so head doesn't raise neckline above 110)
    for (let i = 0; i <= 13; i++) {
        const peakFrac = 1 - Math.abs(i - 6.5) / 6.5;
        const low  = 60 + 5 * peakFrac; // deep trough ~60
        const high = 90 + 18 * peakFrac; // max ~108 < 110
        bars.push(b(high - 1, high, low, high - 0.5));
    }
    // Right shoulder [27..38]: troughs ~80, highs <= 109 (< left shoulder max 110)
    for (let i = 0; i <= 11; i++) {
        const peakFrac = 1 - Math.abs(i - 5.5) / 5.5;
        const low  = 80 + 5 * peakFrac;
        const high = 95 + 14 * peakFrac; // max ~109 < 110
        bars.push(b(high - 1, high, low, high - 0.5));
    }
    // Bar 39 (last): max_high[27..39] = max(109, last_high)
    // To get close > neckline=110, set close=111, high=111.
    // neckline = max(110, max(109, 111)) = 111, and close=111 NOT > 111.
    // Fix: drop left shoulder max to 109 so neckline = max(109, right_max).
    // Right shoulder [27..38] max = 108. Last bar: high=110, close=110.
    // neckline = max(109, max(108, 110)) = 110, close=110 NOT > 110.
    // Only solution: close > high on last bar.
    // Bar 39: high=109 (doesn't exceed left shoulder 110), close=111 > 110.
    bars.push(b(109, 109, 108, 111));
    return bars;
}

test("inverseHeadAndShoulders: positive match returns +1", () => {
    assert.equal(isInverseHeadAndShoulders(makeInvHAndSBars()), 1);
});

test("inverseHeadAndShoulders: below minimum bars returns 0", () => {
    const bars = Array.from({ length: 39 }, () => b(100, 110, 90, 100));
    assert.equal(isInverseHeadAndShoulders(bars), 0);
});

test("inverseHeadAndShoulders: head not lower than shoulders → no match", () => {
    // All regions have similar min low ~90: head not lower than shoulders
    const bars = Array.from({ length: 40 }, () => b(100, 110, 90, 100));
    assert.equal(isInverseHeadAndShoulders(bars), 0);
});

// ── Scan: new chart patterns ──────────────────────────────────────────────────

test("scan: detects bullish_flag", () => {
    const bars = makeBullishFlagBars();
    const matches = scanPatterns(bars);
    const names = new Set(matches.map(m => m.name));
    assert.ok(names.has("bullish_flag"));
});

test("scan: detects bearish_flag", () => {
    const bars = makeBearishFlagBars();
    const matches = scanPatterns(bars);
    const names = new Set(matches.map(m => m.name));
    assert.ok(names.has("bearish_flag"));
});

test("scan: bullish_flag barIndex is bars.length - 1", () => {
    const bars = makeBullishFlagBars();
    const matches = scanPatterns(bars);
    const flagMatches = matches.filter(m => m.name === "bullish_flag");
    for (const m of flagMatches) {
        assert.equal(m.barIndex, bars.length - 1);
    }
});

test("scan: head_and_shoulders not emitted with fewer than 40 bars", () => {
    const bars = Array.from({ length: 39 }, () => b(100, 110, 90, 100));
    const matches = scanPatterns(bars);
    const found = matches.filter(m =>
        m.name === "head_and_shoulders" || m.name === "inverse_head_and_shoulders"
    );
    assert.equal(found.length, 0);
});
