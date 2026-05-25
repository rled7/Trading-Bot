/**
 * runtime.js — ManifestAlgo: compiles an AlgoManifest into a runtime IAlgorithm.
 *
 * Mirrors python/algoforge/algo_gen/runtime.py.
 * Computes declared indicators per-bar, evaluates DSL, emits AlgoDecision objects.
 */

import { IAlgorithm, AlgoDecision, AlgoSignal } from '../algorithm.js';
import {
    sma, ema, rsi, atr as atrFn, macd, bollinger, stochastic,
    obv, wma, cci, williamsR, roc, mfi, vwap, keltner, adx,
    hma, dema, tema, trix, momentum, trueRange, wilderEma,
    vwma, histVol, cmf, accDist, forceIndex, volOsc, donchian,
} from '../indicators.js';
import { compileExpr, PATTERN_NAMES } from './dsl.js';

// ── Indicator dispatch table ──────────────────────────────────────────────────

const IND_DISPATCH = {
    sma,
    ema,
    rsi,
    atr:        atrFn,
    macd,
    bollinger,
    stochastic,
    obv,
    wma,
    cci,
    williams_r: williamsR,
    roc,
    mfi,
    vwap,
    keltner,
    adx,
    hma,
    dema,
    tema,
    trix,
    momentum,
    true_range: trueRange,
    wilder_ema: wilderEma,
    vwma,
    hist_vol:   histVol,
    cmf,
    acc_dist:   accDist,
    force_index: forceIndex,
    vol_osc:    volOsc,
    donchian,
};

/**
 * Call an indicator function with the correct argument shape.
 * Mirrors Python _call_indicator.
 */
function callIndicator(kind, params, bars) {
    const fn      = IND_DISPATCH[kind];
    const closes  = bars.map(b => b.close);
    const highs   = bars.map(b => b.high);
    const lows    = bars.map(b => b.low);
    const volumes = bars.map(b => b.volume);
    const p       = params;

    switch (kind) {
        case 'sma': case 'ema': case 'rsi': case 'wma':
        case 'hma': case 'dema': case 'tema': case 'trix':
        case 'momentum': case 'wilder_ema': case 'roc': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 14));
            return fn(closes, period);
        }
        case 'atr': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 14));
            return atrFn(highs, lows, closes, period);
        }
        case 'macd': {
            const fast   = Math.max(1, Math.round(p.fast   !== undefined ? p.fast   : 12));
            const slow   = Math.max(1, Math.round(p.slow   !== undefined ? p.slow   : 26));
            const signal = Math.max(1, Math.round(p.signal !== undefined ? p.signal : 9));
            return macd(closes, fast, slow, signal);
        }
        case 'bollinger': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 20));
            const mult   = p.mult !== undefined ? p.mult : 2.0;
            return bollinger(closes, period, mult);
        }
        case 'stochastic': {
            const kPeriod = Math.max(1, Math.round(p.k_period !== undefined ? p.k_period : 14));
            const dPeriod = Math.max(1, Math.round(p.d_period !== undefined ? p.d_period : 3));
            return stochastic(highs, lows, closes, kPeriod, dPeriod);
        }
        case 'obv': {
            return obv(closes, volumes);
        }
        case 'cci': {
            const period   = Math.max(1, Math.round(p.period   !== undefined ? p.period   : 20));
            const constant = p.constant !== undefined ? p.constant : 0.015;
            return cci(highs, lows, closes, period, constant);
        }
        case 'williams_r': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 14));
            return williamsR(highs, lows, closes, period);
        }
        case 'mfi': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 14));
            return mfi(highs, lows, closes, volumes, period);
        }
        case 'vwap': {
            return vwap(highs, lows, closes, volumes);
        }
        case 'keltner': {
            const emaPeriod = Math.max(1, Math.round(p.ema_period !== undefined ? p.ema_period : 20));
            const atrPeriod = Math.max(1, Math.round(p.atr_period !== undefined ? p.atr_period : 10));
            const mult      = p.mult !== undefined ? p.mult : 2.0;
            return keltner(highs, lows, closes, emaPeriod, atrPeriod, mult);
        }
        case 'adx': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 14));
            return adx(highs, lows, closes, period);
        }
        case 'vwma': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 14));
            return vwma(closes, volumes, period);
        }
        case 'hist_vol': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 20));
            const tdays  = p.tdays  !== undefined ? p.tdays  : 252;
            return histVol(closes, period, tdays);
        }
        case 'cmf': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 20));
            return cmf(highs, lows, closes, volumes, period);
        }
        case 'acc_dist': {
            return accDist(highs, lows, closes, volumes);
        }
        case 'force_index': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 13));
            return forceIndex(closes, volumes, period);
        }
        case 'vol_osc': {
            const fast = Math.max(1, Math.round(p.fast !== undefined ? p.fast : 14));
            const slow = Math.max(1, Math.round(p.slow !== undefined ? p.slow : 28));
            return volOsc(volumes, fast, slow);
        }
        case 'donchian': {
            const period = Math.max(1, Math.round(p.period !== undefined ? p.period : 20));
            return donchian(highs, lows, period);
        }
        case 'true_range': {
            return trueRange(highs, lows, closes);
        }
        default:
            throw new Error(`Unknown indicator kind: '${kind}'`);
    }
}

/**
 * Return the last non-null, non-NaN value from a result array or value.
 */
function lastValue(result) {
    if (result === null || result === undefined) return null;
    if (Array.isArray(result)) {
        if (result.length === 0) return null;
        const v = result[result.length - 1];
        if (v === null || v === undefined || (typeof v === 'number' && isNaN(v))) return null;
        return v;
    }
    if (typeof result === 'number' && isNaN(result)) return null;
    return result;
}

/**
 * For multi-output indicators (tuple/array of arrays), return last of first output.
 */
function lastTuple(result) {
    if (Array.isArray(result) && Array.isArray(result[0])) {
        return lastValue(result[0]);
    }
    return lastValue(result);
}

// ── PatternProxy ──────────────────────────────────────────────────────────────

class PatternProxy {
    constructor(values) {
        this._vals = values;
        // Expose each known pattern as a direct property
        for (const [k, v] of Object.entries(values)) {
            this[k] = v;
        }
    }
}

// ── ManifestAlgo ──────────────────────────────────────────────────────────────

/**
 * ManifestAlgo — runtime algorithm compiled from an AlgoManifest.
 * Implements the IAlgorithm interface.
 */
export class ManifestAlgo extends IAlgorithm {
    /**
     * @param {import('./types.js').AlgoManifest} manifest
     */
    constructor(manifest) {
        super();
        this._manifest = manifest;
        this._indIds   = new Set(manifest.indicators.map(ind => ind.id));
        this._coolDown = manifest.risk.coolDownBars;
        this._barsSinceExit = 9999;  // starts "ready"

        /** @type {object|null} metadata dict populated by promote() */
        this.metadata = null;

        // Compile entry DSL expressions
        this._entryExprs = [];
        for (const rule of manifest.entries) {
            if (rule.when) {
                const fn = compileExpr(rule.when, this._indIds);
                this._entryExprs.push({ side: rule.side, fn });
            }
        }

        // Compile exit DSL expressions
        this._exitExprs = [];
        for (const rule of manifest.exits) {
            if (rule.when) {
                const fn = compileExpr(rule.when, this._indIds);
                this._exitExprs.push({ side: rule.side, fn });
            }
        }

        // Find ATR indicator id (first declared atr indicator)
        this._atrIndId = null;
        for (const ind of manifest.indicators) {
            if (ind.kind === 'atr') {
                this._atrIndId = ind.id;
                break;
            }
        }

        // Extract bracket specs
        this._slAtr = {};
        this._tpAtr = {};
        for (const rule of manifest.exits) {
            if (rule.slAtr !== null) this._slAtr[rule.side] = rule.slAtr;
            if (rule.tpAtr !== null) this._tpAtr[rule.side] = rule.tpAtr;
        }
    }

    name()        { return this._manifest.name; }
    description() { return this._manifest.description; }
    magic()       { return (Math.abs(hashCode(this._manifest.name)) % 100000) + 10000; }

    evaluate(symbol, tf, bars, count) {
        const sub = bars.slice(0, count + 1);
        if (sub.length < 2) return AlgoDecision.none(symbol);

        const env = this._buildEnv(sub);
        if (!env) return AlgoDecision.none(symbol);

        const atrVal = this._getAtr(sub, env);

        // Cooldown check
        this._barsSinceExit++;
        if (this._barsSinceExit < this._coolDown) return AlgoDecision.none(symbol);

        // Size multiplier from promote() metadata (backward-compatible: null if not promoted)
        const sizeMult = (this.metadata && this.metadata.sizeMult != null)
            ? this.metadata.sizeMult : null;

        // Check entry rules
        for (const { side, fn } of this._entryExprs) {
            try {
                if (fn(env)) {
                    const signal    = side === 'long' ? AlgoSignal.BUY : AlgoSignal.SELL;
                    const direction = side === 'long' ? 1 : -1;
                    return new AlgoDecision({
                        signal,
                        symbol,
                        direction,
                        confidence: 0.70,
                        reason:     `manifest-entry:${side}`,
                        atr:        atrVal || 0,
                        sizeMult,
                    });
                }
            } catch (_) {
                // continue
            }
        }

        return AlgoDecision.none(symbol);
    }

    _buildEnv(bars) {
        const env = {};
        const lastBar = bars[bars.length - 1];

        env.close  = lastBar.close;
        env.open   = lastBar.open;
        env.high   = lastBar.high;
        env.low    = lastBar.low;
        env.volume = lastBar.volume;
        env.spread = lastBar.spread || 0;

        // Compute all declared indicators
        for (const indDecl of this._manifest.indicators) {
            try {
                const result = callIndicator(indDecl.kind, indDecl.params, bars);
                // For multi-output indicators (tuple of arrays), use last of first
                let val;
                if (Array.isArray(result) && Array.isArray(result[0])) {
                    val = lastValue(result[0]);
                } else {
                    val = lastValue(result);
                }
                env[indDecl.id] = val;
            } catch (_) {
                env[indDecl.id] = null;
            }
        }

        // Pattern proxy (empty — backtest engine doesn't provide pattern data)
        const patternVals = {};
        for (const pname of PATTERN_NAMES) {
            patternVals[pname] = false;
        }
        env.pattern = new PatternProxy(patternVals);

        return env;
    }

    _getAtr(bars, env) {
        if (this._atrIndId && env[this._atrIndId] !== null && env[this._atrIndId] !== undefined) {
            return env[this._atrIndId];
        }
        // Fallback: compute ATR(14) directly
        try {
            const highs  = bars.map(b => b.high);
            const lows   = bars.map(b => b.low);
            const closes = bars.map(b => b.close);
            const result = atrFn(highs, lows, closes, 14);
            return lastValue(result) || 0;
        } catch (_) {
            return 0;
        }
    }
}

/**
 * Simple non-cryptographic hash code for strings.
 */
function hashCode(str) {
    let h = 0;
    for (let i = 0; i < str.length; i++) {
        h = ((h << 5) - h) + str.charCodeAt(i);
        h |= 0;  // convert to 32-bit int
    }
    return h;
}
