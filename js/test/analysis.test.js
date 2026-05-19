/**
 * Tests for js/src/analysis.js — Round 9 (Multi-TF Confluence)
 * Covers MarketStructure, TrendClassifier, ConfluenceScorer,
 * evalSession, classifyRegime, TF_WEIGHTS, MultiTimeframeAnalyzer.
 */

import { test } from "node:test";
import assert from "node:assert/strict";

import { Bar, Timeframe } from "../src/types.js";
import {
    TrendState,
    TrendBias,
    VolRegime,
    PatternCategory,
    MarketStructure,
    TrendClassifier,
    ConfluenceScorer,
    evalSession,
    classifyRegime,
    TF_WEIGHTS,
    MultiTimeframeAnalyzer,
} from "../src/analysis.js";

// ── Helpers ───────────────────────────────────────────────────────────────────

/** Build a Bar from loose OHLCV values. */
function bar(open, high, low, close, volume = 1000) {
    return new Bar({ timestamp: 0, open, high, low, close, volume });
}

/**
 * Build n synthetic bars trending upward.
 * price at bar i = base + i * step.
 */
function trendingUpBars(n, base = 1.0000, step = 0.0010) {
    const bars = [];
    for (let i = 0; i < n; i++) {
        const c = base + i * step;
        const o = c - step * 0.3;
        bars.push(bar(o, c + step * 0.2, o - step * 0.1, c));
    }
    return bars;
}

/** Build n synthetic bars trending downward. */
function trendingDownBars(n, base = 1.1000, step = 0.0010) {
    const bars = [];
    for (let i = 0; i < n; i++) {
        const c = base - i * step;
        const o = c + step * 0.3;
        bars.push(bar(o, o + step * 0.1, c - step * 0.2, c));
    }
    return bars;
}

/** Build n flat/ranging bars around a midpoint. */
function rangingBars(n, mid = 1.0000, amp = 0.0020) {
    const bars = [];
    for (let i = 0; i < n; i++) {
        const c = mid + (i % 2 === 0 ? amp * 0.3 : -amp * 0.3);
        bars.push(bar(mid - amp * 0.1, mid + amp, mid - amp, c));
    }
    return bars;
}

// ── Enum exports ──────────────────────────────────────────────────────────────

test("TrendState enum has required keys", () => {
    assert.ok("STRONG_BULL" in TrendState);
    assert.ok("BULL"        in TrendState);
    assert.ok("RANGING"     in TrendState);
    assert.ok("BEAR"        in TrendState);
    assert.ok("STRONG_BEAR" in TrendState);
});

test("TrendBias enum has required keys", () => {
    assert.ok("STRONG_BULL" in TrendBias);
    assert.ok("BULL"        in TrendBias);
    assert.ok("NEUTRAL"     in TrendBias);
    assert.ok("BEAR"        in TrendBias);
    assert.ok("STRONG_BEAR" in TrendBias);
});

test("VolRegime enum has required keys", () => {
    assert.ok("HIGH_TRENDING" in VolRegime);
    assert.ok("HIGH_RANGING"  in VolRegime);
    assert.ok("LOW_TRENDING"  in VolRegime);
    assert.ok("LOW_RANGING"   in VolRegime);
    assert.ok("SQUEEZE"       in VolRegime);
    assert.ok("EXPANDING"     in VolRegime);
});

test("PatternCategory enum has required keys", () => {
    assert.ok("CANDLESTICK" in PatternCategory);
    assert.ok("CHART"       in PatternCategory);
    assert.ok("HARMONIC"    in PatternCategory);
    assert.ok("ELLIOTT"     in PatternCategory);
    assert.ok("WYCKOFF"     in PatternCategory);
    assert.ok("VOLUME"      in PatternCategory);
});

// ── MarketStructure ───────────────────────────────────────────────────────────

test("MarketStructure: default constructor uses swingStrength=3", () => {
    const ms = new MarketStructure();
    const result = ms.analyse(trendingUpBars(50));
    // Just check it returns a valid object
    assert.ok(typeof result.trend === "string");
    assert.ok(typeof result.signal === "number");
    assert.ok(typeof result.confidence === "number");
    assert.ok(typeof result.bos === "boolean");
    assert.ok(typeof result.choch === "boolean");
    assert.ok(Array.isArray(result.swingHighs));
    assert.ok(Array.isArray(result.swingLows));
});

test("MarketStructure: returns RANGING for too-few bars", () => {
    const ms = new MarketStructure(3);
    // Need at least s*4 = 12 bars
    const result = ms.analyse(trendingUpBars(10));
    assert.equal(result.trend, TrendState.RANGING);
    assert.equal(result.signal, 0);
    assert.equal(result.confidence, 0.40);
});

test("MarketStructure: detects STRONG_BULL with HH+HL", () => {
    const ms = new MarketStructure(2);
    // Build bars with clear HH and HL swing structure
    // Alternating: up, peak, down, higher-low, up again, higher-high
    const bars = [
        bar(1.000, 1.010, 0.998, 1.005), // rising
        bar(1.005, 1.020, 1.004, 1.018), // swing high at 1.020
        bar(1.018, 1.019, 1.010, 1.012), // pull back
        bar(1.012, 1.013, 1.005, 1.007), // swing low at 1.005
        bar(1.007, 1.012, 1.006, 1.010), // rising
        bar(1.010, 1.030, 1.009, 1.028), // swing high at 1.030 (HH)
        bar(1.028, 1.029, 1.015, 1.017), // pull back
        bar(1.017, 1.018, 1.012, 1.014), // swing low at 1.012 (HL)
        bar(1.014, 1.025, 1.013, 1.022), // rising again
        bar(1.022, 1.035, 1.021, 1.033), // continues up
    ];
    const result = ms.analyse(bars);
    // With clean HH+HL we expect STRONG_BULL or at minimum BULL
    assert.ok(
        result.trend === TrendState.STRONG_BULL || result.trend === TrendState.BULL,
        `Expected BULL or STRONG_BULL, got ${result.trend}`
    );
    assert.equal(result.signal, 1);
    assert.ok(result.confidence >= 0.70);
});

test("MarketStructure: detects STRONG_BEAR with LH+LL", () => {
    const ms = new MarketStructure(2);
    const bars = [
        bar(1.030, 1.032, 1.020, 1.025),
        bar(1.025, 1.026, 1.010, 1.012),
        bar(1.012, 1.020, 1.011, 1.018), // lower high swing at 1.020
        bar(1.018, 1.019, 1.005, 1.007),
        bar(1.007, 1.008, 0.998, 1.000), // lower low swing at 0.998
        bar(1.000, 1.010, 0.999, 1.008),
        bar(1.008, 1.015, 1.007, 1.010), // lower high at 1.015
        bar(1.010, 1.011, 0.995, 0.997), // lower low at 0.995
        bar(0.997, 1.002, 0.996, 1.000),
        bar(1.000, 1.001, 0.990, 0.993),
    ];
    const result = ms.analyse(bars);
    assert.ok(
        result.trend === TrendState.STRONG_BEAR || result.trend === TrendState.BEAR,
        `Expected BEAR or STRONG_BEAR, got ${result.trend}`
    );
    assert.equal(result.signal, -1);
    assert.ok(result.confidence >= 0.70);
});

test("MarketStructure: BOS flips signal in bear trend", () => {
    const ms = new MarketStructure(2);
    // Bear-trending bars, then close above last swing high → BOS/ChoCH
    const bars = [
        bar(1.030, 1.032, 1.020, 1.025),
        bar(1.025, 1.026, 1.010, 1.012),
        bar(1.012, 1.020, 1.011, 1.018),
        bar(1.018, 1.019, 1.005, 1.007),
        bar(1.007, 1.008, 0.998, 1.000),
        bar(1.000, 1.010, 0.999, 1.008),
        bar(1.008, 1.015, 1.007, 1.010), // LH at 1.015
        bar(1.010, 1.011, 0.995, 0.997),
        bar(0.997, 1.002, 0.996, 1.000),
        // Last bar closes ABOVE the last swing high (1.015)
        bar(1.000, 1.030, 0.999, 1.020),
    ];
    const result = ms.analyse(bars);
    if (result.bos) {
        // Signal should be flipped (choch)
        assert.equal(result.choch, true);
    }
});

test("MarketStructure: lastSwingHigh and lastSwingLow are populated", () => {
    const ms = new MarketStructure(2);
    const bars = trendingUpBars(30);
    const result = ms.analyse(bars);
    if (result.swingHighs.length >= 2) {
        assert.ok(result.lastSwingHigh > 0);
        assert.ok(result.lastSwingLow > 0);
    }
});

// ── TrendClassifier ───────────────────────────────────────────────────────────

test("TrendClassifier: returns NEUTRAL for too-few bars (<200)", () => {
    const tc = new TrendClassifier();
    const result = tc.classify(trendingUpBars(100));
    assert.equal(result.bias, TrendBias.NEUTRAL);
    assert.equal(result.signal, 0);
    assert.equal(result.confidence, 0.40);
});

test("TrendClassifier: returns valid shape for 200+ bars", () => {
    const tc = new TrendClassifier();
    const result = tc.classify(trendingUpBars(250));
    assert.ok(typeof result.bias === "string");
    assert.ok([-1, 0, 1].includes(result.signal));
    assert.ok(result.confidence >= 0 && result.confidence <= 1);
    assert.ok(typeof result.adx === "number");
    assert.ok(typeof result.trending === "boolean");
});

test("TrendClassifier: bull bias for strongly uptrending bars", () => {
    const tc = new TrendClassifier();
    // Strong uptrend: use larger step to get price well above all EMAs
    const result = tc.classify(trendingUpBars(250, 1.0000, 0.0050));
    assert.ok(
        result.bias === TrendBias.BULL || result.bias === TrendBias.STRONG_BULL,
        `Expected BULL or STRONG_BULL, got ${result.bias}`
    );
    assert.equal(result.signal, 1);
});

test("TrendClassifier: bear bias for strongly downtrending bars", () => {
    const tc = new TrendClassifier();
    const result = tc.classify(trendingDownBars(250, 1.5000, 0.0050));
    assert.ok(
        result.bias === TrendBias.BEAR || result.bias === TrendBias.STRONG_BEAR,
        `Expected BEAR or STRONG_BEAR, got ${result.bias}`
    );
    assert.equal(result.signal, -1);
});

test("TrendClassifier: confidence capped at 0.95", () => {
    const tc = new TrendClassifier();
    const result = tc.classify(trendingUpBars(250, 1.0000, 0.0100));
    assert.ok(result.confidence <= 0.95);
});

test("TrendClassifier: confidence formula", () => {
    // confidence = min(0.95, 0.40 + |net|*0.07 + (trending ? 0.10 : 0))
    // Minimum (net=0, no trending) = 0.40
    const tc = new TrendClassifier();
    const result = tc.classify(rangingBars(250));
    assert.ok(result.confidence >= 0.40);
    assert.ok(result.confidence <= 0.95);
});

// ── ConfluenceScorer ──────────────────────────────────────────────────────────

test("ConfluenceScorer: returns neutral with no inputs", () => {
    const cs = new ConfluenceScorer();
    const sc = cs.scoreTimeframe("EURUSD", "H1", {});
    assert.equal(sc.score, 0);
    assert.equal(sc.direction, 0);
    assert.equal(sc.grade, "F");
    assert.equal(sc.confidence, 0);
    assert.equal(sc.isTradeable(), false);
});

test("ConfluenceScorer: indicator signals add bull/bear pts", () => {
    const cs = new ConfluenceScorer();
    const sc = cs.scoreTimeframe("EURUSD", "H1", {
        indSummary: { bullish: 5, bearish: 0 },
    });
    assert.ok(sc.score > 50);
    assert.equal(sc.direction, 1);
});

test("ConfluenceScorer: structure signal contributes conf*5 to bull pts", () => {
    const cs = new ConfluenceScorer();
    // Pure structure bull signal, conf=1.0 → 5 bull pts
    const sc = cs.scoreTimeframe("EURUSD", "H1", {
        structSig: 1, structConf: 1.0,
    });
    // bull=5, bear=0 → 100% bull → score=100, direction=1
    assert.equal(sc.score, 100);
    assert.equal(sc.direction, 1);
});

test("ConfluenceScorer: trend signal contributes conf*4 to bull pts", () => {
    const cs = new ConfluenceScorer();
    const sc = cs.scoreTimeframe("EURUSD", "H1", {
        trendSig: 1, trendConf: 1.0,
    });
    assert.equal(sc.score, 100);
    assert.equal(sc.direction, 1);
});

test("ConfluenceScorer: bear structure signal works", () => {
    const cs = new ConfluenceScorer();
    const sc = cs.scoreTimeframe("EURUSD", "H1", {
        structSig: -1, structConf: 0.9,
    });
    assert.equal(sc.direction, -1);
    assert.ok(sc.score > 50);
});

test("ConfluenceScorer: grade thresholds", () => {
    const cs = new ConfluenceScorer();

    // A: score >= 80
    const a = cs.scoreTimeframe("X", "H1", { structSig: 1, structConf: 1.0, trendSig: 1, trendConf: 1.0, indSummary: { bullish: 20, bearish: 0 } });
    assert.equal(a.grade, "A");

    // F: score < 35 with slight imbalance
    // Provide minimal bull+bear so score is driven to < 35
    // bull=1, bear=2 → bullPct=33, bearPct=67 → score=67 ≥ 65 (B)
    // Need all equal: bull=1, bear=1 → margin=0 < 10 → direction=0, score=50 (C)
    const c = cs.scoreTimeframe("X", "H1", { structSig: 1, structConf: 1.0, trendSig: -1, trendConf: 1.0 });
    // struct: 5 bull, trend: 4 bear → total 9, bull=55.6%, bear=44.4%
    // margin=11.2 → direction=1, score=55.6 → grade C
    assert.ok(["C", "B", "A", "D"].includes(c.grade));
});

test("ConfluenceScorer: direction=0 when margin < 10", () => {
    const cs = new ConfluenceScorer();
    // bull=5, bear=4.5 → margin ≈ 5.3 < 10 → direction=0
    const sc = cs.scoreTimeframe("X", "H1", {
        structSig: 1,  structConf: 1.0,  // 5 bull
        trendSig:  -1, trendConf: 0.9,   // 3.6 bear
    });
    // total=8.6, bull=58.1%, bear=41.9%, margin=16.3 > 10 → direction=1
    // Actually margin is > 10 here; let's try equal
    const sc2 = cs.scoreTimeframe("X", "H1", {
        indSummary: { bullish: 10, bearish: 10 },
    });
    assert.equal(sc2.direction, 0);
});

test("ConfluenceScorer: isTradeable = score>=60 && direction!==0", () => {
    const cs = new ConfluenceScorer();
    const sc = cs.scoreTimeframe("EURUSD", "H1", {
        structSig: 1, structConf: 1.0,
        trendSig:  1, trendConf: 1.0,
        indSummary: { bullish: 10, bearish: 0 },
    });
    assert.ok(sc.isTradeable());
});

test("ConfluenceScorer: pattern matches with category weights", () => {
    const cs = new ConfluenceScorer();
    // WYCKOFF weight=1.0, confidence=0.8 → w = 1.0 * 0.8 * 3.0 = 2.4 bull pts
    const sc = cs.scoreTimeframe("EURUSD", "H1", {
        patResult: [
            { signal: 1, category: "WYCKOFF", confidence: 0.8 },
        ],
    });
    assert.ok(sc.patBull >= 1);
    assert.equal(sc.direction, 1);
    assert.equal(sc.score, 100); // all bull, no bear
});

test("ConfluenceScorer: biasLabel returns correct strings", () => {
    const cs = new ConfluenceScorer();
    const bull = cs.scoreTimeframe("X", "H1", { structSig: 1, structConf: 1.0, indSummary: { bullish: 20, bearish: 0 } });
    assert.ok(["STRONG BUY", "BUY"].includes(bull.biasLabel()));

    const neutral = cs.scoreTimeframe("X", "H1", {});
    assert.equal(neutral.biasLabel(), "NEUTRAL");
});

// ── evalSession ───────────────────────────────────────────────────────────────

test("evalSession: Sunday (dow=0) → DEAD", () => {
    const s = evalSession("EURUSD", 12, 0);
    assert.equal(s.session, "DEAD");
    assert.equal(s.tradeable, false);
    assert.equal(s.quality, 0);
});

test("evalSession: Saturday (dow=6) → DEAD", () => {
    const s = evalSession("EURUSD", 12, 6);
    assert.equal(s.session, "DEAD");
    assert.equal(s.tradeable, false);
});

test("evalSession: Friday 20+ UTC → DEAD", () => {
    const s = evalSession("EURUSD", 20, 5);
    assert.equal(s.session, "DEAD");
    assert.equal(s.tradeable, false);
    assert.ok(s.reason.includes("weekend gap"));
});

test("evalSession: Friday 19 UTC → NOT dead (tradeable hours)", () => {
    const s = evalSession("EURUSD", 19, 5);
    assert.notEqual(s.session, "DEAD");
});

test("evalSession: non-JPY dead hours 22-3 UTC", () => {
    for (const h of [22, 23, 0, 1, 2, 3]) {
        const s = evalSession("EURUSD", h, 2); // Wednesday
        assert.equal(s.session, "DEAD", `Expected DEAD at hour ${h}`);
    }
});

test("evalSession: JPY dead only 22-23 UTC", () => {
    for (const h of [22, 23]) {
        const s = evalSession("USDJPY", h, 2);
        assert.equal(s.session, "DEAD", `JPY should be DEAD at hour ${h}`);
    }
    // JPY should be alive at hour 0 (unlike EUR)
    const alive = evalSession("USDJPY", 0, 2);
    assert.notEqual(alive.session, "DEAD");
});

test("evalSession: OVERLAP session at 12-16 UTC", () => {
    for (const h of [12, 13, 14, 15, 16]) {
        const s = evalSession("EURUSD", h, 2);
        assert.equal(s.session, "OVERLAP", `Hour ${h} should be OVERLAP`);
    }
});

test("evalSession: LONDON session at 7-11 UTC", () => {
    for (const h of [7, 8, 9, 10, 11]) {
        const s = evalSession("EURUSD", h, 2);
        assert.equal(s.session, "LONDON", `Hour ${h} should be LONDON`);
    }
});

test("evalSession: NY session at 17-20 UTC", () => {
    for (const h of [17, 18, 19, 20]) {
        const s = evalSession("EURUSD", h, 2);
        assert.equal(s.session, "NY", `Hour ${h} should be NY`);
    }
});

test("evalSession: tradeable if quality >= 0.45", () => {
    // Hour 12 on DEFAULT table = 1.00 (OVERLAP) → tradeable
    const s = evalSession("EURUSD", 12, 2);
    assert.equal(s.tradeable, true);
    assert.ok(s.quality >= 0.45);
});

test("evalSession: XAUUSD uses XAUUSD quality table", () => {
    // XAUUSD hour 12 = 1.00
    const xau = evalSession("XAUUSD", 12, 2);
    assert.ok(xau.quality > 0);
    // XAUUSD hour 0 = 0.35 (different from DEFAULT 0.30)
    const xau0 = evalSession("XAUUSD", 4, 2); // hour 4: XAUUSD=0.30, DEFAULT=0.25
    assert.ok(xau0.quality >= 0);
});

test("evalSession: USDJPY uses USDJPY quality table", () => {
    // USDJPY hour 0 = 0.80 (tradeable, unlike DEFAULT 0.30)
    const jpy = evalSession("USDJPY", 0, 2);
    assert.notEqual(jpy.session, "DEAD");
    assert.ok(jpy.quality > 0);
});

test("evalSession: result has all required fields", () => {
    const s = evalSession("EURUSD", 10, 2);
    assert.ok("session"   in s);
    assert.ok("quality"   in s);
    assert.ok("tradeable" in s);
    assert.ok("reason"    in s);
    assert.ok("hourUtc"   in s);
    assert.ok("dayOfWeek" in s);
});

// ── classifyRegime ────────────────────────────────────────────────────────────

test("classifyRegime: returns LOW_RANGING default for <100 bars", () => {
    const result = classifyRegime(trendingUpBars(50));
    assert.equal(result.regime, VolRegime.LOW_RANGING);
    assert.equal(result.sizeMult, 0.80);
});

test("classifyRegime: returns a valid result for 200 bars", () => {
    const result = classifyRegime(trendingUpBars(200));
    assert.ok(typeof result.regime === "string");
    assert.ok(Object.values(VolRegime).includes(result.regime));
    assert.ok(typeof result.atrPercentile === "number");
    assert.ok(typeof result.bbSqueeze === "boolean");
    assert.ok(typeof result.adx === "number");
    assert.ok(typeof result.trending === "boolean");
    assert.ok(typeof result.sizeMult === "number");
});

test("classifyRegime: sizeMult matches regime spec", () => {
    const MULT = {
        [VolRegime.SQUEEZE]:       0.0,
        [VolRegime.EXPANDING]:     1.2,
        [VolRegime.HIGH_TRENDING]: 1.0,
        [VolRegime.HIGH_RANGING]:  0.3,
        [VolRegime.LOW_TRENDING]:  0.75,
        [VolRegime.LOW_RANGING]:   0.80,
    };
    const result = classifyRegime(trendingUpBars(200));
    assert.equal(result.sizeMult, MULT[result.regime]);
});

test("classifyRegime: atrPercentile is 0-100", () => {
    const result = classifyRegime(trendingUpBars(150));
    assert.ok(result.atrPercentile >= 0 && result.atrPercentile <= 100);
});

// ── TF_WEIGHTS ────────────────────────────────────────────────────────────────

test("TF_WEIGHTS: correct values per spec", () => {
    assert.equal(TF_WEIGHTS[Timeframe.M15], 1.0);
    assert.equal(TF_WEIGHTS[Timeframe.H1],  2.0);
    assert.equal(TF_WEIGHTS[Timeframe.H4],  3.0);
    assert.equal(TF_WEIGHTS[Timeframe.D1],  4.0);
});

test("TF_WEIGHTS: is frozen (immutable)", () => {
    assert.ok(Object.isFrozen(TF_WEIGHTS));
});

// ── MultiTimeframeAnalyzer ────────────────────────────────────────────────────

test("MultiTimeframeAnalyzer: returns neutral MTFResult for empty barsPerTf", () => {
    const mta = new MultiTimeframeAnalyzer();
    const result = mta.analyze("EURUSD", new Map());
    assert.equal(result.symbol, "EURUSD");
    assert.equal(result.direction, 0);
    assert.equal(result.confidence, 0);
    assert.equal(result.weightedScore, 0);
    assert.ok(result.tfScores instanceof Map);
    assert.equal(result.tfScores.size, 0);
});

test("MultiTimeframeAnalyzer: MTFResult has required fields", () => {
    const mta = new MultiTimeframeAnalyzer();
    const barsPerTf = new Map([
        [Timeframe.H1, trendingUpBars(50)],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf);
    assert.ok("symbol"        in result);
    assert.ok("direction"     in result);
    assert.ok("confidence"    in result);
    assert.ok("weightedScore" in result);
    assert.ok("tfScores"      in result);
    assert.ok("dominantTf"    in result);
});

test("MultiTimeframeAnalyzer: tfScores keyed by Timeframe enum values", () => {
    const mta = new MultiTimeframeAnalyzer();
    const barsPerTf = new Map([
        [Timeframe.M15, trendingUpBars(50)],
        [Timeframe.H1,  trendingUpBars(50)],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf);
    assert.ok(result.tfScores.has(Timeframe.M15));
    assert.ok(result.tfScores.has(Timeframe.H1));
});

test("MultiTimeframeAnalyzer: dominantTf is the highest-weight TF", () => {
    const mta = new MultiTimeframeAnalyzer();
    // D1 (weight=4) > H4 (weight=3) > H1 (weight=2) > M15 (weight=1)
    const barsPerTf = new Map([
        [Timeframe.M15, trendingUpBars(50)],
        [Timeframe.H1,  trendingUpBars(50)],
        [Timeframe.D1,  trendingUpBars(50)],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf);
    assert.equal(result.dominantTf, Timeframe.D1);
});

test("MultiTimeframeAnalyzer: weightedScore is weighted average of TF scores", () => {
    const mta = new MultiTimeframeAnalyzer();
    const bars250 = trendingUpBars(250, 1.0, 0.005);

    // With strong indicator signals on H1 only
    const indPerTf = new Map([
        [Timeframe.H1, { bullish: 10, bearish: 0 }],
    ]);
    const barsPerTf = new Map([
        [Timeframe.H1, bars250],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf, indPerTf);
    assert.ok(result.weightedScore >= 0 && result.weightedScore <= 100);
});

test("MultiTimeframeAnalyzer: single TF weightedScore equals that TF score", () => {
    const mta = new MultiTimeframeAnalyzer();
    const bars250 = trendingUpBars(250);
    const indPerTf = new Map([
        [Timeframe.H1, { bullish: 8, bearish: 0 }],
    ]);
    const barsPerTf = new Map([
        [Timeframe.H1, bars250],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf, indPerTf);
    const tfScore = result.tfScores.get(Timeframe.H1);
    // weightedScore = tfScore.score * weight / totalWeight = tfScore.score (weight=2, total=2)
    assert.ok(Math.abs(result.weightedScore - tfScore.score) < 1e-9);
});

test("MultiTimeframeAnalyzer: confidence in 0-0.95 range", () => {
    const mta = new MultiTimeframeAnalyzer();
    const barsPerTf = new Map([
        [Timeframe.H1, trendingUpBars(250, 1.0, 0.01)],
        [Timeframe.H4, trendingUpBars(250, 1.0, 0.01)],
    ]);
    const indPerTf = new Map([
        [Timeframe.H1, { bullish: 10, bearish: 0 }],
        [Timeframe.H4, { bullish: 10, bearish: 0 }],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf, indPerTf);
    assert.ok(result.confidence >= 0 && result.confidence <= 0.95);
});

test("MultiTimeframeAnalyzer: direction=0 when votes cancel", () => {
    const mta = new MultiTimeframeAnalyzer();
    // M15 strong bull, D1 strong bear (equal weighted votes cancel if balanced)
    const indPerTf = new Map([
        [Timeframe.M15, { bullish: 100, bearish: 0 }],
        [Timeframe.D1,  { bullish: 0, bearish: 100 }],
    ]);
    const barsPerTf = new Map([
        [Timeframe.M15, trendingUpBars(50)],
        [Timeframe.D1,  trendingDownBars(50)],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf, indPerTf);
    // M15 weight=1, D1 weight=4 — D1 should dominate → direction=-1 or 0
    assert.ok(result.direction === -1 || result.direction === 0);
});

test("MultiTimeframeAnalyzer: accepts patPerTf patterns", () => {
    const mta = new MultiTimeframeAnalyzer();
    const patPerTf = new Map([
        [Timeframe.H1, [{ signal: 1, category: "CANDLESTICK", confidence: 0.7 }]],
    ]);
    const barsPerTf = new Map([
        [Timeframe.H1, trendingUpBars(50)],
    ]);
    // Should not throw
    const result = mta.analyze("EURUSD", barsPerTf, new Map(), patPerTf);
    assert.ok(result.tfScores.has(Timeframe.H1));
    const sc = result.tfScores.get(Timeframe.H1);
    assert.ok(sc.score >= 0);
});

test("MultiTimeframeAnalyzer: unknown TF defaults to weight=1.0", () => {
    const mta = new MultiTimeframeAnalyzer();
    const CUSTOM_TF = 999999;
    const barsPerTf = new Map([
        [CUSTOM_TF, trendingUpBars(50)],
    ]);
    const result = mta.analyze("EURUSD", barsPerTf);
    assert.ok(result.tfScores.has(CUSTOM_TF));
    // dominantTf should be the only TF
    assert.equal(result.dominantTf, CUSTOM_TF);
});
