import { test } from "node:test";
import assert from "node:assert/strict";

import { Bar } from "../src/types.js";
import {
    isDoji, isHammer, isEngulfing, isMarubozu, isPinBar,
    isMorningStar, isEveningStar, isThreeWhiteSoldiers, isThreeBlackCrows,
    isDoubleTop, isDoubleBottom, isAscendingTriangle, isDescendingTriangle,
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
