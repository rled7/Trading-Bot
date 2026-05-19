'use strict';

/**
 * Algorithm interface and signal types — mirrors C++ IAlgorithm / AlgoDecision.
 */

export const AlgoSignal = Object.freeze({ NONE: 0, BUY: 1, SELL: -1 });

export class AlgoDecision {
    constructor({
        signal     = AlgoSignal.NONE,
        symbol     = '',
        direction  = 0,
        confidence = 0,
        reason     = '',
        atr        = 0,
    } = {}) {
        this.signal     = signal;
        this.symbol     = symbol;
        this.direction  = direction;
        this.confidence = confidence;
        this.reason     = reason;
        this.atr        = atr;
    }

    /** Returns true when the decision has an actionable signal with sufficient confidence. */
    isActionable() {
        return this.signal !== AlgoSignal.NONE && this.confidence >= 0.55;
    }

    /** Factory: a neutral / no-signal decision for a given symbol. */
    static none(symbol = '') {
        return new AlgoDecision({ symbol });
    }
}

export class IAlgorithm {
    constructor() {
        this._enabled = true;
    }

    /** Unique human-readable name (must override). */
    name()        { throw new Error('not implemented'); }

    /** One-line description (must override). */
    description() { throw new Error('not implemented'); }

    /** Magic number — unique integer ID (must override). */
    magic()       { throw new Error('not implemented'); }

    /**
     * Core evaluation — called with the current bar window.
     * @param {string}  symbol
     * @param {number}  tf       Timeframe constant (seconds)
     * @param {Bar[]}   bars     Full bar history up to and including current bar
     * @param {number}  count    Number of valid bars
     * @param {object}  ind      Pre-computed indicator arrays
     * @param {object}  pat      Pattern scan results
     * @returns {AlgoDecision}
     */
    evaluate(symbol, tf, bars, count, ind, pat) {
        throw new Error('not implemented');
    }

    /**
     * Safe wrapper: returns AlgoDecision.none on error or when disabled.
     */
    safeEvaluate(symbol, tf, bars, count, ind, pat) {
        if (!this._enabled) return AlgoDecision.none(symbol);
        try {
            return this.evaluate(symbol, tf, bars, count, ind, pat);
        } catch (_) {
            return AlgoDecision.none(symbol);
        }
    }

    enable()    { this._enabled = true; }
    disable()   { this._enabled = false; }
    isEnabled() { return this._enabled; }
}

/** Stub implementation — always returns NONE. Useful for testing the engine. */
export class StubAlgo extends IAlgorithm {
    name()        { return 'stub'; }
    description() { return 'Stub algo — always returns NONE'; }
    magic()       { return 0; }
    evaluate(symbol) { return AlgoDecision.none(symbol); }
}
