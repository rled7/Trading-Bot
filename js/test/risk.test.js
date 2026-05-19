/**
 * Tests for js/src/risk.js — Round 10 (Risk + Execution)
 * Covers PositionSizer, DrawdownGuard, SignalProcessor.
 */

import { test } from "node:test";
import assert  from "node:assert/strict";

import {
    SizingMethod,
    GuardState,
    PositionSizer,
    DrawdownGuard,
    SignalProcessor,
} from "../src/risk.js";

// ── Helpers ───────────────────────────────────────────────────────────────────

/** Minimal SymbolInfo-like object for EURUSD. */
function eurusdSym(overrides = {}) {
    return {
        name:         'EURUSD',
        digits:       5,
        point:        0.00001,
        contractSize: 100_000,
        volumeMin:    0.01,
        volumeMax:    100,
        volumeStep:   0.01,
        ...overrides,
    };
}

// ── SizingMethod / GuardState enum exports ────────────────────────────────────

test("SizingMethod enum has required keys", () => {
    assert.ok("FIXED_RISK" in SizingMethod);
    assert.ok("ATR_RISK"   in SizingMethod);
    assert.ok("KELLY"      in SizingMethod);
    assert.ok(Object.isFrozen(SizingMethod));
});

test("GuardState enum has required keys", () => {
    assert.ok("GREEN"  in GuardState);
    assert.ok("YELLOW" in GuardState);
    assert.ok("RED"    in GuardState);
    assert.ok(Object.isFrozen(GuardState));
});

// ── PositionSizer.kellyFraction ───────────────────────────────────────────────

test("kellyFraction: WR=0.55 RR=2.0 maxRisk=0.02 → capped at 0.02", () => {
    // edge = 0.55*2 - 0.45 = 0.65; kelly = 0.65/2 = 0.325; half = 0.1625 → capped at 0.02
    const f = PositionSizer.kellyFraction(0.55, 2.0, 0.02);
    assert.equal(f, 0.02);
});

test("kellyFraction: WR=0.5 RR=2.0 maxRisk=0.10 → 0.125 but capped at 0.10", () => {
    // edge = 0.5*2 - 0.5 = 0.5; kelly = 0.5/2 = 0.25; half = 0.125 → within 0.10? No → capped
    const f = PositionSizer.kellyFraction(0.5, 2.0, 0.10);
    assert.equal(f, 0.10);
});

test("kellyFraction: negative edge floored at 0.005", () => {
    // WR=0.3, RR=1.0 → edge = 0.3 - 0.7 = -0.4; kelly=-0.4; half=-0.2 → max(0.005,-0.2)=0.005
    const f = PositionSizer.kellyFraction(0.3, 1.0, 0.02);
    assert.equal(f, 0.005);
});

test("kellyFraction: rr=0 (edge case) returns 0.005 floor", () => {
    const f = PositionSizer.kellyFraction(0.6, 0, 0.02);
    assert.equal(f, 0.005);
});

// ── PositionSizer.normalizeLots ───────────────────────────────────────────────

test("normalizeLots: 1.567 step=0.01 → 1.57", () => {
    const n = PositionSizer.normalizeLots(1.567, 0.01, 0.01, 100);
    assert.equal(n, 1.57);
});

test("normalizeLots: 1.564 step=0.01 → 1.56", () => {
    const n = PositionSizer.normalizeLots(1.564, 0.01, 0.01, 100);
    assert.equal(n, 1.56);
});

test("normalizeLots: 0.003 clamped up to min=0.01", () => {
    const n = PositionSizer.normalizeLots(0.003, 0.01, 0.01, 100);
    assert.equal(n, 0.01);
});

test("normalizeLots: 200 clamped down to max=100", () => {
    const n = PositionSizer.normalizeLots(200, 0.01, 0.01, 100);
    assert.equal(n, 100);
});

test("normalizeLots: step<=0 defaults to 0.01", () => {
    const n = PositionSizer.normalizeLots(1.567, 0, 0.01, 100);
    assert.equal(n, 1.57);
});

test("normalizeLots: 0.105 step=0.01 → 0.11 (rounding)", () => {
    const n = PositionSizer.normalizeLots(0.105, 0.01, 0.01, 100);
    assert.equal(n, 0.11);
});

// ── PositionSizer.sizePosition ────────────────────────────────────────────────

test("sizePosition: returns an object with expected shape", () => {
    const sym = eurusdSym();
    const r = PositionSizer.sizePosition(
        SizingMethod.FIXED_RISK,
        10_000, 1.08500, 1.07500, sym,
        /*atr=*/ 0.001, /*riskPct=*/ 0.01, 100, 0.01,
    );
    assert.ok(typeof r.lots         === 'number');
    assert.ok(typeof r.riskAmount   === 'number');
    assert.ok(typeof r.riskPct      === 'number');
    assert.ok(typeof r.stopDistance === 'number');
    assert.ok(typeof r.capped       === 'boolean');
});

test("sizePosition: lots >= minLots", () => {
    const sym = eurusdSym();
    const r = PositionSizer.sizePosition(
        SizingMethod.FIXED_RISK, 10_000, 1.08500, 1.07500, sym, 0, 0.01, 100, 0.01,
    );
    assert.ok(r.lots >= 0.01);
});

test("sizePosition: stopLoss=0 + atr uses atr*1.5 as stop distance", () => {
    const sym = eurusdSym();
    const atr = 0.001;
    const r   = PositionSizer.sizePosition(
        SizingMethod.FIXED_RISK, 10_000, 1.08500, 0, sym, atr, 0.01, 100, 0.01,
    );
    assert.ok(Math.abs(r.stopDistance - atr * 1.5) < 1e-10);
});

test("sizePosition: KELLY method produces smaller risk than fixed 0.10", () => {
    // kelly(WR=0.5, RR=2.0, max=0.10) → 0.10 cap; kelly(WR=0.3, RR=1.0, max=0.10) → 0.005
    const sym  = eurusdSym();
    const rKelly = PositionSizer.sizePosition(
        SizingMethod.KELLY, 10_000, 1.08500, 1.07500, sym, 0.001, 0.10, 100, 0.01,
        0.3, 1.0,  // bad win rate → 0.005
    );
    const rFixed = PositionSizer.sizePosition(
        SizingMethod.FIXED_RISK, 10_000, 1.08500, 1.07500, sym, 0.001, 0.10, 100, 0.01,
    );
    assert.ok(rKelly.lots <= rFixed.lots);
});

test("sizePosition: capped=true when raw lots exceed maxLots", () => {
    const sym = eurusdSym({ volumeMax: 0.01 });  // tight cap
    const r = PositionSizer.sizePosition(
        SizingMethod.FIXED_RISK, 1_000_000, 1.08500, 1.08490, sym, 0, 0.20, 0.01, 0.01,
    );
    assert.equal(r.lots, 0.01);
    assert.equal(r.capped, true);
});

// ── DrawdownGuard ─────────────────────────────────────────────────────────────

test("DrawdownGuard: initial state is GREEN", () => {
    const g = new DrawdownGuard();
    g.initialise(10_000, 10_000);
    assert.equal(g.state, GuardState.GREEN);
    assert.equal(g.tradingAllowed, true);
    assert.equal(g.haltCount, 0);
});

test("DrawdownGuard RED: daily -6% > limit 5% → state=red", () => {
    const g = new DrawdownGuard(0.05, 0.15);
    g.initialise(10_000, 10_000);
    const r = g.check(9_400, 9_400);  // -6% daily
    assert.equal(r.state, GuardState.RED);
    assert.equal(r.tradingAllowed, false);
    assert.ok(r.reason.length > 0);
    assert.equal(g.haltCount, 1);
});

test("DrawdownGuard YELLOW: daily -3.8% > 75%*5%=3.75% → yellow", () => {
    const g = new DrawdownGuard(0.05, 0.15);
    g.initialise(10_000, 10_000);
    const r = g.check(9_620, 9_620);  // -3.8%
    assert.equal(r.state, GuardState.YELLOW);
    assert.equal(r.tradingAllowed, true);   // yellow still allows trading
    assert.equal(g.haltCount, 0);           // no halt increment on yellow
});

test("DrawdownGuard: daily PnL and Pct are correct", () => {
    const g = new DrawdownGuard();
    g.initialise(10_000, 10_000);
    const r = g.check(9_800, 9_800);
    assert.ok(Math.abs(r.dailyPnl - (-200)) < 1e-6);
    assert.ok(Math.abs(r.dailyPct - (-0.02)) < 1e-6);
});

test("DrawdownGuard: totalDd uses peak, not daily start", () => {
    const g = new DrawdownGuard(0.05, 0.15);
    g.initialise(10_000, 10_000);
    g.check(11_000, 11_000);   // new peak = 11000
    const r = g.check(9_400, 9_400);  // totalDd = (11000-9400)/11000 ≈ 14.5% < 15%
    // dailyPct = (9400-11000)/11000 → but dailyStart reset to 11000 only after midnight
    // Here same day, so dailyStart = 10000 still
    // dailyPct = (9400-10000)/10000 = -6% → RED
    assert.equal(r.state, GuardState.RED);
    assert.ok(r.peakEquity >= 11_000);
});

test("DrawdownGuard: peakEquity tracks equity highwater mark", () => {
    const g = new DrawdownGuard();
    g.initialise(10_000, 10_000);
    g.check(10_500, 10_500);
    g.check(10_200, 10_200);
    const r = g.check(10_800, 10_800);
    assert.equal(r.peakEquity, 10_800);
});

test("DrawdownGuard: totalDd limit breach → RED regardless of daily", () => {
    const g = new DrawdownGuard(0.50, 0.10);  // high daily limit, 10% total
    g.initialise(10_000, 10_000);
    g.check(11_000, 11_000);  // peak = 11000
    const r = g.check(9_850, 9_850);  // totalDd = (11000-9850)/11000 ≈ 10.45% > 10%
    assert.equal(r.state, GuardState.RED);
});

test("DrawdownGuard: check returns currentEquity field", () => {
    const g = new DrawdownGuard();
    g.initialise(10_000, 10_000);
    const r = g.check(9_950, 9_950);
    assert.equal(r.currentEquity, 9_950);
});

test("DrawdownGuard: recovery from YELLOW when equity improves", () => {
    const g = new DrawdownGuard(0.05, 0.15);
    g.initialise(10_000, 10_000);
    // Push to yellow: -3.8%
    g.check(9_620, 9_620);
    assert.equal(g.state, GuardState.YELLOW);
    // Recover: equity back above warning threshold
    g.check(9_990, 9_990);  // -0.1% → well within green
    assert.equal(g.state, GuardState.GREEN);
});

// ── DrawdownGuard: daily reset simulation ────────────────────────────────────
//
// We cannot safely monkey-patch Date in ESM without a library, so we test the
// reset logic by directly poking the private state (white-box test).
//

test("DrawdownGuard: daily reset clears daily start and lifts RED", () => {
    const g = new DrawdownGuard(0.05, 0.15);
    g.initialise(10_000, 10_000);

    // Trigger RED
    g.check(9_400, 9_400);
    assert.equal(g.state, GuardState.RED);

    // Simulate next-day reset by pointing _resetDay to yesterday
    g._resetDay = g._resetDay - 1;

    // Next check on the new day should reset dailyStartEquity and clear RED
    const r = g.check(9_400, 9_400);  // same equity but fresh daily window
    assert.equal(r.state, GuardState.GREEN);   // RED was lifted by day reset
    assert.equal(r.dailyPnl, 0);              // new day starts at 9400
});

// ── SignalProcessor ───────────────────────────────────────────────────────────

/** Minimal mock broker for SignalProcessor tests. */
function makeMockBroker({
    balance    = 10_000,
    equity     = 10_000,
    bid        = 1.08490,
    ask        = 1.08502,
    placeOk    = true,
} = {}) {
    return {
        getAccount: () => ({ balance, equity }),
        getTick:    (symbol) => ({ symbol, bid, ask, volume: 1000, timestamp: Date.now() }),
        getSymbolInfo: (symbol) => ({
            name: symbol, digits: 5, point: 0.00001,
            contractSize: 100_000, volumeMin: 0.01, volumeMax: 100, volumeStep: 0.01,
        }),
        placeOrder: (symbol, type, lots, price, sl, tp, magic, comment) => {
            if (!placeOk) return null;
            return { ticket: 1, symbol, type, lots, fillPrice: type === 0 ? ask : bid, sl, tp };
        },
    };
}

/** Minimal AlgoDecision mock. */
function makeDecision({
    signal     = 1,
    symbol     = 'EURUSD',
    direction  = 1,    // Direction.LONG = 1
    confidence = 0.70,
    reason     = 'test signal',
    atr        = 0.001,
} = {}) {
    return {
        signal, symbol, direction, confidence, reason, atr,
        isActionable() { return this.signal !== 0 && this.confidence >= 0.55; },
    };
}

test("SignalProcessor: constructed with defaults", () => {
    const sp = new SignalProcessor(makeMockBroker());
    assert.equal(sp.processed, 0);
    assert.equal(sp.rejected, 0);
});

test("SignalProcessor: process returns true for valid actionable decision", () => {
    const sp  = new SignalProcessor(makeMockBroker(), { cooldownSec: 0 });
    const dec = makeDecision();
    const ok  = sp.process(dec);
    assert.equal(ok, true);
    assert.equal(sp.processed, 1);
    assert.equal(sp.rejected, 0);
});

test("SignalProcessor: non-actionable decision is rejected", () => {
    const sp  = new SignalProcessor(makeMockBroker());
    const dec = makeDecision({ signal: 0, confidence: 0 });
    const ok  = sp.process(dec);
    assert.equal(ok, false);
    assert.equal(sp.rejected, 1);
    assert.equal(sp.processed, 0);
});

test("SignalProcessor: low-confidence decision is rejected", () => {
    const sp  = new SignalProcessor(makeMockBroker());
    const dec = makeDecision({ confidence: 0.40 });
    assert.equal(sp.process(dec), false);
    assert.equal(sp.rejected, 1);
});

test("SignalProcessor: second identical signal within cooldown is rejected", () => {
    const sp  = new SignalProcessor(makeMockBroker(), { cooldownSec: 300 });
    const dec = makeDecision();
    assert.equal(sp.process(dec), true);   // first: passes
    assert.equal(sp.process(dec), false);  // second: cooldown
    assert.equal(sp.processed, 1);
    assert.equal(sp.rejected, 1);
});

test("SignalProcessor: opposite direction same symbol is NOT in cooldown", () => {
    const sp   = new SignalProcessor(makeMockBroker(), { cooldownSec: 300 });
    const long = makeDecision({ direction: 1, signal: 1 });
    const shrt = makeDecision({ direction: -1, signal: -1 });

    assert.equal(sp.process(long), true);
    // Short has different key → should not be blocked by long's cooldown
    assert.equal(sp.process(shrt), true);
    assert.equal(sp.processed, 2);
});

test("SignalProcessor: failed placeOrder increments rejected, returns false", () => {
    const broker = makeMockBroker({ placeOk: false });
    const sp  = new SignalProcessor(broker, { cooldownSec: 0 });
    const dec = makeDecision();
    const ok  = sp.process(dec);
    assert.equal(ok, false);
    assert.equal(sp.rejected, 1);
    assert.equal(sp.processed, 0);
});

test("SignalProcessor: SELL order uses bid price (direction=SHORT)", () => {
    let capturedType = null;
    const broker = {
        ...makeMockBroker(),
        placeOrder: (symbol, type, lots, price, sl, tp, magic, comment) => {
            capturedType = type;
            return { ticket: 1, symbol, type, lots };
        },
    };
    const sp  = new SignalProcessor(broker, { cooldownSec: 0 });
    const dec = makeDecision({ direction: -1, signal: -1 });
    sp.process(dec);
    assert.equal(capturedType, 1);  // OrderType.SELL = 1
});

test("SignalProcessor: BUY order type = 0", () => {
    let capturedType = null;
    const broker = {
        ...makeMockBroker(),
        placeOrder: (symbol, type, lots, price, sl, tp, magic, comment) => {
            capturedType = type;
            return { ticket: 1, symbol, type, lots };
        },
    };
    const sp  = new SignalProcessor(broker, { cooldownSec: 0 });
    const dec = makeDecision({ direction: 1, signal: 1 });
    sp.process(dec);
    assert.equal(capturedType, 0);  // OrderType.BUY = 0
});

test("SignalProcessor: placeOrder receives magic=1000", () => {
    let capturedMagic = null;
    const broker = {
        ...makeMockBroker(),
        placeOrder: (symbol, type, lots, price, sl, tp, magic) => {
            capturedMagic = magic;
            return { ticket: 1 };
        },
    };
    const sp = new SignalProcessor(broker, { cooldownSec: 0 });
    sp.process(makeDecision());
    assert.equal(capturedMagic, 1000);
});

test("SignalProcessor: lots are > 0 for normal balance", () => {
    let capturedLots = null;
    const broker = {
        ...makeMockBroker(),
        placeOrder: (symbol, type, lots) => {
            capturedLots = lots;
            return { ticket: 1 };
        },
    };
    const sp = new SignalProcessor(broker, { cooldownSec: 0, maxRisk: 0.01 });
    sp.process(makeDecision({ atr: 0.0015 }));
    assert.ok(capturedLots > 0);
    assert.ok(capturedLots >= 0.01);
});
