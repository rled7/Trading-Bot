'use strict';

/**
 * Concrete algorithm implementations + AlgorithmRegistry.
 * Mirrors the C++ TrendFollower, MeanReversion, BreakoutTrader,
 * SwingTrader, and AlgorithmRegistry exactly.
 *
 * The `ind` parameter passed to evaluate() is expected to be an object
 * whose named keys map to indicator result objects of the form:
 *   { value: number, value2?: number, value3?: number, atrValue?: number }
 *
 * If a required indicator key is absent or null, the algorithm returns
 * AlgoDecision.none() defensively.
 *
 * The `pat` parameter may be:
 *   - An object with { bullishCount: number, bearishCount: number }
 *   - An array of PatternMatch objects (signal: +1 bullish, -1 bearish)
 *   - null / undefined
 */

import { AlgoSignal, AlgoDecision, IAlgorithm } from './algorithm.js';
import { Timeframe } from './types.js';

// ── Internal helper ───────────────────────────────────────────────────────────

/**
 * Retrieve a named indicator result from ind.
 * Returns null if absent or if the result's .value is null/undefined.
 * @param {object} ind
 * @param {string} key
 * @returns {{ value: number, value2?: number, value3?: number, atrValue?: number }|null}
 */
function _get(ind, key) {
    if (!ind) return null;
    // Support both plain-object lookup and Map-style get()
    const r = typeof ind.get === 'function' ? ind.get(key) : ind[key];
    if (!r) return null;
    if (r.value === null || r.value === undefined) return null;
    return r;
}

/**
 * Normalise the pattern scan result into { bullishCount, bearishCount }.
 * Accepts either an array of PatternMatch objects or a pre-computed summary.
 * @param {object|Array|null} pat
 * @returns {{ bullishCount: number, bearishCount: number }}
 */
function _patCounts(pat) {
    if (!pat) return { bullishCount: 0, bearishCount: 0 };
    if (typeof pat.bullishCount === 'number') {
        return { bullishCount: pat.bullishCount, bearishCount: pat.bearishCount || 0 };
    }
    // Array of PatternMatch from scanPatterns()
    if (Array.isArray(pat)) {
        let bullishCount = 0;
        let bearishCount = 0;
        for (const m of pat) {
            if (m.signal > 0) bullishCount++;
            else if (m.signal < 0) bearishCount++;
        }
        return { bullishCount, bearishCount };
    }
    // pat.matches array (from backtest _buildPatterns)
    if (Array.isArray(pat.matches)) {
        return _patCounts(pat.matches);
    }
    return { bullishCount: 0, bearishCount: 0 };
}

// ── 1. TrendFollower ──────────────────────────────────────────────────────────

export class TrendFollower extends IAlgorithm {
    /**
     * @param {number} fast     Fast EMA period (default 21)
     * @param {number} slow     Slow EMA period (default 50)
     * @param {number} adxMin   Minimum ADX for trend filter (default 22.0)
     * @param {number} magicNum Magic number / unique ID (default 1001)
     */
    constructor(fast = 21, slow = 50, adxMin = 22.0, magicNum = 1001) {
        super();
        this._fast    = fast;
        this._slow    = slow;
        this._adxMin  = adxMin;
        this._magic   = magicNum;
        this._name    = `TrendFollower_EMA${fast}/${slow}`;
    }

    name()        { return this._name; }
    description() { return `EMA ${this._fast}/${this._slow} crossover with ADX trend filter`; }
    magic()       { return this._magic; }

    evaluate(symbol, tf, bars, count, ind /*, pat */) {
        // Look up required indicators
        const emaFastR = _get(ind, `EMA${this._fast}`);
        const emaSlowR = _get(ind, `EMA${this._slow}`);
        const ema200R  = _get(ind, 'EMA200');
        const adxR     = _get(ind, 'ADX_14');
        const rsiR     = _get(ind, 'RSI_14');

        if (!emaFastR || !emaSlowR || !ema200R || !adxR || !rsiR) {
            return AlgoDecision.none(symbol);
        }

        const emaFast = emaFastR.value;
        const emaSlow = emaSlowR.value;
        const ema200  = ema200R.value;
        const adx     = adxR.value;
        const pdi     = adxR.value2;   // +DI
        const mdi     = adxR.value3;   // -DI
        const rsi     = rsiR.value;
        const atr     = emaFastR.atrValue || 0;

        const price = bars[count - 1].close;

        // ADX filter: trend must be strong enough
        if (adx < this._adxMin) {
            return AlgoDecision.none(symbol);
        }

        const conf = Math.min(0.90, 0.60 + adx / 100 * 0.30);

        // Bullish: fast > slow AND price > EMA200 AND +DI > -DI
        if (emaFast > emaSlow && price > ema200 && pdi > mdi) {
            if (rsi < 75) {
                return new AlgoDecision({
                    signal    : AlgoSignal.BUY,
                    symbol,
                    direction : 1,
                    confidence: conf,
                    reason    : `EMA cross up ADX=${Math.floor(adx)}`,
                    atr,
                });
            }
        }

        // Bearish: fast < slow AND price < EMA200 AND -DI > +DI
        if (emaFast < emaSlow && price < ema200 && mdi > pdi) {
            if (rsi > 25) {
                return new AlgoDecision({
                    signal    : AlgoSignal.SELL,
                    symbol,
                    direction : -1,
                    confidence: conf,
                    reason    : `EMA cross dn ADX=${Math.floor(adx)}`,
                    atr,
                });
            }
        }

        return AlgoDecision.none(symbol);
    }
}

// ── 2. MeanReversion ──────────────────────────────────────────────────────────

export class MeanReversion extends IAlgorithm {
    /**
     * @param {number} rsiOs   RSI oversold threshold (default 30)
     * @param {number} rsiOb   RSI overbought threshold (default 70)
     * @param {number} adxMax  Maximum ADX (trending market excluded) (default 25)
     */
    constructor(rsiOs = 30, rsiOb = 70, adxMax = 25) {
        super();
        this._rsiOs  = rsiOs;
        this._rsiOb  = rsiOb;
        this._adxMax = adxMax;
    }

    name()        { return 'MeanReversion_RSI_BB'; }
    description() { return 'RSI + Bollinger Band mean-reversion'; }
    magic()       { return 1002; }

    evaluate(symbol, tf, bars, count, ind /*, pat */) {
        const rsiR = _get(ind, 'RSI_14');
        const bbR  = _get(ind, 'BB_20_2');
        const adxR = _get(ind, 'ADX_14');

        if (!rsiR || !bbR || !adxR) {
            return AlgoDecision.none(symbol);
        }

        const rsi    = rsiR.value;
        const upper  = bbR.value2;
        const lower  = bbR.value3;
        const adx    = adxR.value;
        const atr    = bbR.atrValue || 0;

        const price = bars[count - 1].close;

        // Squeeze: BB bandwidth too narrow → not suitable for mean reversion
        const squeeze = bbR.extra && bbR.extra.squeeze !== undefined
            ? bbR.extra.squeeze
            : 0;

        // Trending market or squeeze → skip
        if (adx > this._adxMax || squeeze > 0.5) {
            return AlgoDecision.none(symbol);
        }

        // Oversold: RSI below threshold AND price near/below lower band
        if (rsi <= this._rsiOs && price <= lower * 1.002) {
            const conf = Math.min(0.85, 0.55 + (this._rsiOs - rsi) / 30 * 0.30);
            return new AlgoDecision({
                signal    : AlgoSignal.BUY,
                symbol,
                direction : 1,
                confidence: conf,
                reason    : `RSI oversold ${rsi.toFixed(1)} at lower BB`,
                atr,
            });
        }

        // Overbought: RSI above threshold AND price near/above upper band
        if (rsi >= this._rsiOb && price >= upper * 0.998) {
            const conf = Math.min(0.85, 0.55 + (rsi - this._rsiOb) / 30 * 0.30);
            return new AlgoDecision({
                signal    : AlgoSignal.SELL,
                symbol,
                direction : -1,
                confidence: conf,
                reason    : `RSI overbought ${rsi.toFixed(1)} at upper BB`,
                atr,
            });
        }

        return AlgoDecision.none(symbol);
    }
}

// ── 3. BreakoutTrader ─────────────────────────────────────────────────────────

export class BreakoutTrader extends IAlgorithm {
    /**
     * @param {number} dcPeriod  Donchian channel period (default 20)
     * @param {number} volMult   Volume surge multiplier vs average (default 1.5)
     */
    constructor(dcPeriod = 20, volMult = 1.5) {
        super();
        this._dcPeriod = dcPeriod;
        this._volMult  = volMult;
    }

    name()        { return 'Breakout_Donchian'; }
    description() { return `Donchian channel breakout with volume/ATR confirmation`; }
    magic()       { return 1003; }

    evaluate(symbol, tf, bars, count, ind /*, pat */) {
        const dcR   = _get(ind, `DC_${this._dcPeriod}`);
        const macdR = _get(ind, 'MACD_12_26_9');

        if (!dcR || !macdR) {
            return AlgoDecision.none(symbol);
        }

        const upper    = dcR.value2;
        const lower    = dcR.value3;
        const macdHist = macdR.value3;   // histogram
        const atr      = dcR.atrValue || 0;

        const price = bars[count - 1].close;

        // Volume surge: current volume vs average of last dcPeriod bars
        const lookback = Math.min(this._dcPeriod, count);
        let sumVol = 0;
        for (let i = count - lookback; i < count; i++) {
            sumVol += bars[i].volume;
        }
        const avgVol  = sumVol / lookback;
        const curVol  = bars[count - 1].volume;
        const volSurge = curVol > avgVol * this._volMult;

        // ATR expansion: current range vs average range of last dcPeriod bars
        let sumRange = 0;
        for (let i = count - lookback; i < count; i++) {
            sumRange += bars[i].high - bars[i].low;
        }
        const rangeAvg  = sumRange / lookback;
        const curRange  = bars[count - 1].high - bars[count - 1].low;
        const atrExpand = curRange > rangeAvg * 1.1;

        const conf = Math.min(0.88, 0.60 + (volSurge ? 0.15 : 0) + (atrExpand ? 0.10 : 0));

        // Bullish breakout: price at/above upper channel, volume surge, ATR expand, positive MACD hist
        if (price >= upper && volSurge && atrExpand && macdHist > 0) {
            return new AlgoDecision({
                signal    : AlgoSignal.BUY,
                symbol,
                direction : 1,
                confidence: conf,
                reason    : `Donchian breakout up vol=${curVol.toFixed(0)}`,
                atr,
            });
        }

        // Bearish breakout: price at/below lower channel, volume surge, ATR expand, negative MACD hist
        if (price <= lower && volSurge && atrExpand && macdHist < 0) {
            return new AlgoDecision({
                signal    : AlgoSignal.SELL,
                symbol,
                direction : -1,
                confidence: conf,
                reason    : `Donchian breakout dn vol=${curVol.toFixed(0)}`,
                atr,
            });
        }

        return AlgoDecision.none(symbol);
    }
}

// ── 4. SwingTrader ────────────────────────────────────────────────────────────

export class SwingTrader extends IAlgorithm {
    constructor() {
        super();
    }

    name()        { return 'SwingTrader_PullbackEMA'; }
    description() { return 'Pullback to EMA21 in trending market (H4/D1 only)'; }
    magic()       { return 1004; }

    evaluate(symbol, tf, bars, count, ind, pat) {
        // Only fires on H4 or D1 timeframes
        if (tf !== Timeframe.H4 && tf !== Timeframe.D1) {
            return AlgoDecision.none(symbol);
        }

        const ema21R  = _get(ind, 'EMA21');
        const ema50R  = _get(ind, 'EMA50');
        const ema200R = _get(ind, 'EMA200');
        const rsiR    = _get(ind, 'RSI_14');

        if (!ema21R || !ema50R || !ema200R || !rsiR) {
            return AlgoDecision.none(symbol);
        }

        const ema21 = ema21R.value;
        const ema50 = ema50R.value;
        const ema200 = ema200R.value;
        const rsi   = rsiR.value;
        const atr   = ema21R.atrValue || 0;

        const price = bars[count - 1].close;

        // Price within 0.3% of EMA21
        const atEma21 = Math.abs(price - ema21) / ema21 < 0.003;

        // Trend alignment
        const bullTrend = ema21 > ema50 && ema50 > ema200;
        const bearTrend = ema21 < ema50 && ema50 < ema200;

        const { bullishCount, bearishCount } = _patCounts(pat);

        // Bullish: uptrend, price near EMA21, RSI in neutral zone, bullish candle pattern
        if (bullTrend && atEma21 && rsi > 38 && rsi < 58 && bullishCount > 0) {
            return new AlgoDecision({
                signal    : AlgoSignal.BUY,
                symbol,
                direction : 1,
                confidence: 0.75,
                reason    : `Pullback to EMA21 in uptrend RSI=${rsi.toFixed(1)}`,
                atr,
            });
        }

        // Bearish: downtrend, price near EMA21, RSI in neutral zone, bearish candle pattern
        if (bearTrend && atEma21 && rsi > 42 && rsi < 62 && bearishCount > 0) {
            return new AlgoDecision({
                signal    : AlgoSignal.SELL,
                symbol,
                direction : -1,
                confidence: 0.75,
                reason    : `Pullback to EMA21 in downtrend RSI=${rsi.toFixed(1)}`,
                atr,
            });
        }

        return AlgoDecision.none(symbol);
    }
}

// ── AlgorithmRegistry ─────────────────────────────────────────────────────────

export class AlgorithmRegistry {
    constructor() {
        this._algos = [];

        // Default registrations matching C++ registry initialisation
        this.register(new TrendFollower(21, 50, 22.0, 1001));
        this.register(new TrendFollower(9,  21, 20.0, 1005));
        this.register(new MeanReversion());
        this.register(new BreakoutTrader());
        this.register(new SwingTrader());
    }

    /**
     * Register an algorithm instance.
     * @param {IAlgorithm} algo
     */
    register(algo) {
        this._algos.push(algo);
    }

    /**
     * Look up a registered algorithm by name.
     * @param {string} name
     * @returns {IAlgorithm|undefined}
     */
    get(name) {
        return this._algos.find(a => a.name() === name);
    }

    /**
     * Enable a named algorithm.
     * @param {string} name
     */
    enable(name) {
        const algo = this.get(name);
        if (algo) algo.enable();
    }

    /**
     * Disable a named algorithm.
     * @param {string} name
     */
    disable(name) {
        const algo = this.get(name);
        if (algo) algo.disable();
    }

    /**
     * Evaluate all registered (and enabled) algorithms and return only
     * actionable decisions.
     *
     * @param {string}  symbol
     * @param {number}  tf      Timeframe constant (seconds)
     * @param {Bar[]}   bars    Full bar history
     * @param {number}  count   Number of valid bars
     * @param {object}  ind     Pre-computed indicator result map
     * @param {object}  pat     Pattern scan results
     * @returns {AlgoDecision[]}  Only decisions where isActionable() === true
     */
    evaluateAll(symbol, tf, bars, count, ind, pat) {
        const results = [];
        for (const algo of this._algos) {
            const decision = algo.safeEvaluate(symbol, tf, bars, count, ind, pat);
            if (decision.isActionable()) {
                results.push(decision);
            }
        }
        return results;
    }
}
