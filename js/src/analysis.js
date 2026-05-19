/**
 * AlgoForge — src/analysis/analysis.js
 * Market structure, trend classification, confluence scoring, session filter,
 * volatility regime, and multi-timeframe analysis.
 * Mirrors cpp/include/analysis/analysis.hpp + cpp/src/analysis/*.cpp exactly.
 *
 * No external dependencies — uses only functions exported from indicators.js.
 */

import { ema, atr, adx, bollinger } from './indicators.js';
import { Timeframe } from './types.js';

// ── Enums ────────────────────────────────────────────────────────────────────

export const TrendState = Object.freeze({
    STRONG_BULL: 'STRONG_BULL',
    BULL:        'BULL',
    RANGING:     'RANGING',
    BEAR:        'BEAR',
    STRONG_BEAR: 'STRONG_BEAR',
});

export const TrendBias = Object.freeze({
    STRONG_BULL: 'STRONG_BULL',
    BULL:        'BULL',
    NEUTRAL:     'NEUTRAL',
    BEAR:        'BEAR',
    STRONG_BEAR: 'STRONG_BEAR',
});

export const VolRegime = Object.freeze({
    HIGH_TRENDING: 'HIGH_TRENDING',
    HIGH_RANGING:  'HIGH_RANGING',
    LOW_TRENDING:  'LOW_TRENDING',
    LOW_RANGING:   'LOW_RANGING',
    SQUEEZE:       'SQUEEZE',
    EXPANDING:     'EXPANDING',
});

export const PatternCategory = Object.freeze({
    CANDLESTICK: 'CANDLESTICK',
    CHART:       'CHART',
    HARMONIC:    'HARMONIC',
    ELLIOTT:     'ELLIOTT',
    WYCKOFF:     'WYCKOFF',
    VOLUME:      'VOLUME',
});

// ── Pattern category weights (mirrors cpp/ pat_weight()) ─────────────────────

const PAT_WEIGHTS = {
    CANDLESTICK: 0.60,
    CHART:       0.90,
    HARMONIC:    0.85,
    ELLIOTT:     0.80,
    WYCKOFF:     1.00,
    VOLUME:      0.75,
};

function patWeight(category) {
    return PAT_WEIGHTS[category] ?? 0.75;
}

// ── MarketStructure ───────────────────────────────────────────────────────────

/**
 * Swing point descriptor.
 * @typedef {{ index: number, price: number, isHigh: boolean }} SwingPoint
 */

/**
 * @typedef {{
 *   trend:         string,   // TrendState
 *   signal:        number,   // 1, 0, -1
 *   confidence:    number,   // 0–1
 *   bos:           boolean,
 *   choch:         boolean,
 *   lastSwingHigh: number,
 *   lastSwingLow:  number,
 *   swingHighs:    SwingPoint[],
 *   swingLows:     SwingPoint[],
 * }} StructureResult
 */

export class MarketStructure {
    /**
     * @param {number} swingStrength  Number of bars on each side for swing detection (default 3)
     */
    constructor(swingStrength = 3) {
        this._swingStrength = swingStrength;
    }

    /**
     * Analyse market structure over bars[0..n-1].
     * @param {import('./types.js').Bar[]} bars
     * @param {number} [n]  Number of bars to use (default: bars.length)
     * @returns {StructureResult}
     */
    analyse(bars, n = bars.length) {
        const result = {
            trend:         TrendState.RANGING,
            signal:        0,
            confidence:    0.40,
            bos:           false,
            choch:         false,
            lastSwingHigh: 0,
            lastSwingLow:  0,
            swingHighs:    [],
            swingLows:     [],
        };

        const s = this._swingStrength;
        if (n < s * 4) return result;

        // Detect swing highs and lows
        // i must have s bars on each side: range is [s, n-s-1] (exclusive end)
        for (let i = s; i < n - s; i++) {
            let isSH = true;
            let isSL = true;
            for (let j = 1; j <= s; j++) {
                if (bars[i].high <= bars[i - j].high || bars[i].high <= bars[i + j].high) isSH = false;
                if (bars[i].low  >= bars[i - j].low  || bars[i].low  >= bars[i + j].low)  isSL = false;
            }
            if (isSH) result.swingHighs.push({ index: i, price: bars[i].high, isHigh: true });
            if (isSL) result.swingLows.push({ index: i, price: bars[i].low,  isHigh: false });
        }

        if (result.swingHighs.length < 2 || result.swingLows.length < 2) return result;

        const sh = result.swingHighs;
        const sl = result.swingLows;
        const lastSH  = sh[sh.length - 1].price;
        const prevSH  = sh[sh.length - 2].price;
        const lastSL  = sl[sl.length - 1].price;
        const prevSL  = sl[sl.length - 2].price;

        const hh = lastSH > prevSH;
        const hl = lastSL > prevSL;
        const lh = lastSH < prevSH;
        const ll = lastSL < prevSL;

        if      (hh && hl) { result.trend = TrendState.STRONG_BULL; result.signal =  1; result.confidence = 0.90; }
        else if (hh || hl) { result.trend = TrendState.BULL;        result.signal =  1; result.confidence = 0.70; }
        else if (ll && lh) { result.trend = TrendState.STRONG_BEAR; result.signal = -1; result.confidence = 0.90; }
        else if (ll || lh) { result.trend = TrendState.BEAR;        result.signal = -1; result.confidence = 0.70; }
        else               { result.trend = TrendState.RANGING;     result.signal =  0; result.confidence = 0.40; }

        result.lastSwingHigh = lastSH;
        result.lastSwingLow  = lastSL;

        // Break of Structure / Change of Character
        const curClose = bars[n - 1].close;
        if (result.trend === TrendState.BEAR || result.trend === TrendState.STRONG_BEAR) {
            result.bos = result.choch = curClose > lastSH;
        } else if (result.trend === TrendState.BULL || result.trend === TrendState.STRONG_BULL) {
            result.bos = result.choch = curClose < lastSL;
        }

        if (result.choch) result.signal = -result.signal;

        return result;
    }
}

// ── TrendClassifier ───────────────────────────────────────────────────────────

/**
 * @typedef {{
 *   bias:       string,   // TrendBias
 *   signal:     number,   // 1, 0, -1
 *   confidence: number,   // 0–1
 *   adx:        number,
 *   trending:   boolean,
 * }} TrendClassification
 */

export class TrendClassifier {
    /**
     * Classify trend over bars[0..n-1].
     * Requires at least 200 bars; returns NEUTRAL/0 if fewer.
     * @param {import('./types.js').Bar[]} bars
     * @param {number} [n]
     * @returns {TrendClassification}
     */
    classify(bars, n = bars.length) {
        const result = {
            bias:       TrendBias.NEUTRAL,
            signal:     0,
            confidence: 0.40,
            adx:        0,
            trending:   false,
        };

        if (n < 200) return result;

        const closes = new Array(n);
        const highs  = new Array(n);
        const lows   = new Array(n);
        for (let i = 0; i < n; i++) {
            closes[i] = bars[i].close;
            highs[i]  = bars[i].high;
            lows[i]   = bars[i].low;
        }

        const e9   = ema(closes, 9);
        const e21  = ema(closes, 21);
        const e50  = ema(closes, 50);
        const e200 = ema(closes, 200);
        const { adx: adxArr } = adx(highs, lows, closes, 14);

        const price  = closes[n - 1];
        const v9     = e9[n - 1];
        const v21    = e21[n - 1];
        const v50    = e50[n - 1];
        const v200   = e200[n - 1];
        const adxVal = adxArr[n - 1];

        let bullScore = 0;
        let bearScore = 0;

        // Price vs each EMA
        if (v9   !== null) { if (price > v9)   bullScore++; else if (price < v9)   bearScore++; }
        if (v21  !== null) { if (price > v21)  bullScore++; else if (price < v21)  bearScore++; }
        if (v50  !== null) { if (price > v50)  bullScore++; else if (price < v50)  bearScore++; }
        if (v200 !== null) { if (price > v200) bullScore++; else if (price < v200) bearScore++; }

        // EMA alignment
        if (v9  !== null && v21  !== null) { if (v9  > v21)  bullScore++; else if (v9  < v21)  bearScore++; }
        if (v21 !== null && v50  !== null) { if (v21 > v50)  bullScore++; else if (v21 < v50)  bearScore++; }
        if (v50 !== null && v200 !== null) { if (v50 > v200) bullScore++; else if (v50 < v200) bearScore++; }

        result.adx      = adxVal !== null ? adxVal : 0;
        result.trending = result.adx > 20.0;
        const net = bullScore - bearScore;

        if      (net >=  6 && result.trending) { result.bias = TrendBias.STRONG_BULL; result.signal =  1; }
        else if (net >=  3)                     { result.bias = TrendBias.BULL;        result.signal =  1; }
        else if (net <= -6 && result.trending)  { result.bias = TrendBias.STRONG_BEAR; result.signal = -1; }
        else if (net <= -3)                     { result.bias = TrendBias.BEAR;        result.signal = -1; }
        else                                    { result.bias = TrendBias.NEUTRAL;     result.signal =  0; }

        result.confidence = Math.min(0.95, 0.40 + Math.abs(net) * 0.07 + (result.trending ? 0.10 : 0));

        return result;
    }
}

// ── ConfluenceScorer ──────────────────────────────────────────────────────────

/**
 * @typedef {{
 *   symbol:           string,
 *   timeframe:        string,
 *   score:            number,   // 0–100
 *   direction:        number,   // 1, 0, -1
 *   grade:            string,   // 'A','B','C','D','F'
 *   confidence:       number,   // 0–1
 *   indBull:          number,
 *   indBear:          number,
 *   patBull:          number,
 *   patBear:          number,
 *   structureSignal:  number,
 *   trendSignal:      number,
 *   isTradeable():    boolean,
 *   biasLabel():      string,
 * }} ConfluenceScore
 */

export class ConfluenceScorer {
    /**
     * Score a single timeframe.
     *
     * @param {string} symbol
     * @param {string} timeframe   human-readable label, e.g. "H1"
     * @param {object} [opts]
     * @param {object} [opts.indSummary]   { bullish: number, bearish: number }
     * @param {object[]} [opts.patResult]  Array of PatternMatch objects (from scanPatterns)
     *                                     OR { bullishCount, bearishCount, matches[] }
     *                                     Each match should have: { signal, category, confidence }
     *                                     If confidence is absent, 0.5 is used.
     * @param {number} [opts.structSig]   structure signal: 1, 0, -1
     * @param {number} [opts.structConf]  structure confidence: 0–1
     * @param {number} [opts.trendSig]    trend signal: 1, 0, -1
     * @param {number} [opts.trendConf]   trend confidence: 0–1
     * @returns {ConfluenceScore}
     */
    scoreTimeframe(symbol, timeframe, {
        indSummary  = null,
        patResult   = null,
        structSig   = 0,
        structConf  = 0,
        trendSig    = 0,
        trendConf   = 0,
    } = {}) {
        let bullPts = 0;
        let bearPts = 0;
        let indBull = 0, indBear = 0;
        let patBull = 0, patBear = 0;

        // Indicator signals
        if (indSummary) {
            indBull = indSummary.bullish || 0;
            indBear = indSummary.bearish || 0;
            bullPts += indBull;
            bearPts += indBear;
        }

        // Pattern signals
        if (patResult) {
            // Normalise: may be an array of PatternMatch or an object with .matches / pre-counted
            let matches = null;
            if (Array.isArray(patResult)) {
                matches = patResult;
            } else if (Array.isArray(patResult.matches)) {
                matches = patResult.matches;
                patBull = patResult.bullishCount || 0;
                patBear = patResult.bearishCount || 0;
            }

            if (matches) {
                let bc = 0, brc = 0;
                for (const m of matches) {
                    const sig  = m.signal ?? 0;
                    const cat  = m.category ?? 'CANDLESTICK';
                    const conf = m.confidence ?? 0.5;
                    const w = patWeight(cat) * conf * 3.0;
                    if (sig > 0) { bullPts += w; bc++;  }
                    if (sig < 0) { bearPts += w; brc++; }
                }
                // Only override if not pre-set from .bullishCount
                if (!Array.isArray(patResult.matches)) {
                    patBull = bc;
                    patBear = brc;
                } else {
                    // patBull/patBear already set from pre-counted; don't recount
                }
                // If raw array was passed, use our counts
                if (Array.isArray(patResult)) {
                    patBull = bc;
                    patBear = brc;
                }
            }
        }

        // Structure
        if (structSig ===  1) bullPts += structConf * 5.0;
        if (structSig === -1) bearPts += structConf * 5.0;

        // Trend
        if (trendSig ===  1) bullPts += trendConf * 4.0;
        if (trendSig === -1) bearPts += trendConf * 4.0;

        const total = bullPts + bearPts;

        const sc = {
            symbol,
            timeframe,
            score:           0,
            direction:       0,
            grade:           'F',
            confidence:      0,
            indBull,
            indBear,
            patBull,
            patBear,
            structureSignal: structSig,
            trendSignal:     trendSig,
            isTradeable() { return this.score >= 60 && this.direction !== 0; },
            biasLabel() {
                if (this.score >= 80 && this.direction ===  1) return 'STRONG BUY';
                if (this.score >= 60 && this.direction ===  1) return 'BUY';
                if (this.score >= 80 && this.direction === -1) return 'STRONG SELL';
                if (this.score >= 60 && this.direction === -1) return 'SELL';
                return 'NEUTRAL';
            },
        };

        if (total < 1e-10) return sc;

        const bullPct = bullPts / total * 100.0;
        const bearPct = bearPts / total * 100.0;
        sc.score     = Math.max(bullPct, bearPct);
        sc.direction = bullPts >= bearPts ? 1 : -1;

        const margin = Math.abs(bullPct - bearPct);
        if (margin < 10.0) sc.direction = 0;

        sc.grade = sc.score >= 80 ? 'A'
                 : sc.score >= 65 ? 'B'
                 : sc.score >= 50 ? 'C'
                 : sc.score >= 35 ? 'D' : 'F';

        sc.confidence = Math.min(1.0, sc.score / 100.0 * (1 + margin / 200.0));

        return sc;
    }
}

// ── Session Filter ────────────────────────────────────────────────────────────

const SESS_DEFAULT = [
    0.30, 0.25, 0.20, 0.20, 0.25, 0.35,
    0.55, 0.80, 0.85, 0.90, 0.90, 0.90,
    1.00, 1.00, 1.00, 0.95, 0.85, 0.70,
    0.55, 0.45, 0.40, 0.30, 0.20, 0.25,
];

const SESS_USDJPY = [
    0.80, 0.85, 0.85, 0.80, 0.75, 0.70,
    0.75, 0.90, 0.95, 0.80, 0.70, 0.65,
    0.85, 0.90, 0.85, 0.75, 0.60, 0.50,
    0.40, 0.35, 0.30, 0.40, 0.55, 0.70,
];

const SESS_XAUUSD = [
    0.35, 0.30, 0.25, 0.25, 0.30, 0.40,
    0.60, 0.75, 0.85, 0.90, 0.90, 0.90,
    1.00, 1.00, 0.95, 0.85, 0.70, 0.50,
    0.40, 0.35, 0.30, 0.25, 0.20, 0.25,
];

/**
 * @typedef {{
 *   session:   string,  // 'OVERLAP','LONDON','NY','ASIA','DEAD'
 *   quality:   number,  // 0–1
 *   tradeable: boolean,
 *   reason:    string,
 *   hourUtc:   number,
 *   dayOfWeek: number,
 * }} SessionScore
 */

/**
 * Evaluate trading session quality for a symbol at a given UTC hour and day.
 * @param {string} symbol      e.g. 'EURUSD', 'USDJPY', 'XAUUSD'
 * @param {number} hourUtc     0–23
 * @param {number} dayOfWeek   0=Sunday … 6=Saturday
 * @returns {SessionScore}
 */
export function evalSession(symbol, hourUtc, dayOfWeek) {
    const s = {
        session:   'DEAD',
        quality:   0,
        tradeable: false,
        reason:    '',
        hourUtc,
        dayOfWeek,
    };

    // Sunday (dow=0) — C++ checks dow==6 but that's Saturday in C time_t (0=Sun).
    // The spec says "Sunday(dow=0 or 6) → DEAD". C++ uses dow==6 for Sunday
    // because tm_wday has 0=Sun but they store day_of_week from a different source.
    // Match the spec literally: dow=0 OR dow=6 → DEAD (Sunday).
    if (dayOfWeek === 0 || dayOfWeek === 6) {
        s.session   = 'DEAD';
        s.quality   = 0.0;
        s.tradeable = false;
        s.reason    = 'Sunday — thin market';
        return s;
    }

    // Friday 20+ UTC → weekend gap risk
    // C++ uses dow==4 for Friday (with dow=6 being Sunday in their encoding).
    // The spec says "Friday 20+ UTC → DEAD". We normalise: 5=Friday in standard JS.
    if (dayOfWeek === 5 && hourUtc >= 20) {
        s.session   = 'DEAD';
        s.quality   = 0.0;
        s.tradeable = false;
        s.reason    = 'Friday 20:00+ UTC — weekend gap risk';
        return s;
    }

    const sym = symbol ? symbol.toUpperCase() : '';
    const isJpy = sym.includes('JPY');
    const isXau = sym.includes('XAU');

    // Dead hours
    const dead = isJpy
        ? (hourUtc === 22 || hourUtc === 23)
        : (hourUtc >= 22 || hourUtc <= 3);

    if (dead) {
        s.session   = 'DEAD';
        s.quality   = 0.05;
        s.tradeable = false;
        s.reason    = `Dead hours ${String(hourUtc).padStart(2, '0')}:00 UTC`;
        return s;
    }

    const map = isJpy ? SESS_USDJPY : isXau ? SESS_XAUUSD : SESS_DEFAULT;
    s.quality = map[hourUtc];

    if      (hourUtc >= 12 && hourUtc <= 16) s.session = 'OVERLAP';
    else if (hourUtc >=  7 && hourUtc <= 11) s.session = 'LONDON';
    else if (hourUtc >= 17 && hourUtc <= 20) s.session = 'NY';
    else                                     s.session = 'ASIA';

    s.tradeable = s.quality >= 0.45;
    s.reason    = `${s.session} session quality=${Math.round(s.quality * 100)}%`;
    return s;
}

// ── Volatility Regime ─────────────────────────────────────────────────────────

/**
 * @typedef {{
 *   regime:         string,   // VolRegime
 *   atrPercentile:  number,   // 0–100
 *   bbSqueeze:      boolean,
 *   adx:            number,
 *   trending:       boolean,
 *   sizeMult:       number,
 * }} RegimeResult
 */

/**
 * Classify the current volatility regime.
 * Requires at least 100 bars. Returns LOW_RANGING / 0.80 if insufficient.
 * @param {import('./types.js').Bar[]} bars
 * @param {number} [n]
 * @returns {RegimeResult}
 */
export function classifyRegime(bars, n = bars.length) {
    const result = {
        regime:        VolRegime.LOW_RANGING,
        atrPercentile: 0,
        bbSqueeze:     false,
        adx:           0,
        trending:      false,
        sizeMult:      0.80,
    };

    if (n < 100) return result;

    const highs  = new Array(n);
    const lows   = new Array(n);
    const closes = new Array(n);
    for (let i = 0; i < n; i++) {
        highs[i]  = bars[i].high;
        lows[i]   = bars[i].low;
        closes[i] = bars[i].close;
    }

    // ATR percentile
    const atrArr = atr(highs, lows, closes, 14);
    const atrNow = atrArr[n - 1] !== null ? atrArr[n - 1] : 0.001;
    const look   = Math.min(n, 100);
    let lessThan = 0;
    for (let i = n - look; i < n; i++) {
        if (atrArr[i] !== null && atrArr[i] < atrNow) lessThan++;
    }
    result.atrPercentile = lessThan / look * 100.0;

    // Bollinger bandwidth for squeeze
    const { upper: bu, lower: bl, middle: bm } = bollinger(closes, 20, 2.0);
    // bandwidth[i] = (upper - lower) / middle (avoid div-by-zero)
    const bwArr = new Array(n).fill(null);
    for (let i = 0; i < n; i++) {
        if (bu[i] !== null && bl[i] !== null && bm[i] !== null && Math.abs(bm[i]) > 1e-10) {
            bwArr[i] = (bu[i] - bl[i]) / bm[i];
        }
    }

    const bwNow = bwArr[n - 1] !== null ? bwArr[n - 1] : 1.0;
    let bwMin = bwNow;
    const lookBB = Math.min(n, 100);
    for (let i = n - lookBB; i < n; i++) {
        if (bwArr[i] !== null && bwArr[i] < bwMin) bwMin = bwArr[i];
    }
    result.bbSqueeze = bwNow <= bwMin * 1.05;

    // BB bandwidth slope over last 5 bars
    const bwPrev = n >= 5 ? (bwArr[n - 5] !== null ? bwArr[n - 5] : null) : null;
    const bwSlope = (bwPrev !== null && bwArr[n - 1] !== null) ? bwArr[n - 1] - bwPrev : 0;
    const expanding = bwSlope > 0 && !result.bbSqueeze;

    // ADX
    const { adx: adxArr } = adx(highs, lows, closes, 14);
    result.adx      = adxArr[n - 1] !== null ? adxArr[n - 1] : 20;
    result.trending = result.adx >= 23.0;

    // Regime classification — mirrors cpp/ classify_regime exactly
    if      (result.bbSqueeze)                                     { result.regime = VolRegime.SQUEEZE;       result.sizeMult = 0.0;  }
    else if (expanding && result.atrPercentile > 50)               { result.regime = VolRegime.EXPANDING;     result.sizeMult = 1.2;  }
    else if (result.atrPercentile >= 65 && result.trending)        { result.regime = VolRegime.HIGH_TRENDING; result.sizeMult = 1.0;  }
    else if (result.atrPercentile >= 65 && !result.trending)       { result.regime = VolRegime.HIGH_RANGING;  result.sizeMult = 0.3;  }
    else if (result.atrPercentile <= 35 && result.trending)        { result.regime = VolRegime.LOW_TRENDING;  result.sizeMult = 0.75; }
    else                                                           { result.regime = VolRegime.LOW_RANGING;   result.sizeMult = 0.80; }

    return result;
}

// ── MultiTimeframeAnalyzer ────────────────────────────────────────────────────

/**
 * TF_WEIGHTS: importance weighting per timeframe value.
 * M15=1.0, H1=2.0, H4=3.0, D1=4.0; others default to 1.0.
 */
export const TF_WEIGHTS = Object.freeze({
    [Timeframe.M15]: 1.0,
    [Timeframe.H1]:  2.0,
    [Timeframe.H4]:  3.0,
    [Timeframe.D1]:  4.0,
});

/**
 * @typedef {{
 *   symbol:        string,
 *   direction:     number,   // 1, 0, -1
 *   confidence:    number,   // 0–1
 *   weightedScore: number,   // weighted-average score 0–100
 *   tfScores:      Map<number, ConfluenceScore>,
 *   dominantTf:    number,   // Timeframe enum value with highest weight contribution
 * }} MTFResult
 */

export class MultiTimeframeAnalyzer {
    /**
     * @param {string} [symbol]  Symbol label passed through to ConfluenceScorer
     */
    constructor(symbol = '') {
        this._symbol          = symbol;
        this._msAnalyser      = new MarketStructure();
        this._trendClassifier = new TrendClassifier();
        this._confluenceScorer = new ConfluenceScorer();
    }

    /**
     * Analyse multiple timeframes and produce a weighted MTF result.
     *
     * @param {string}                            symbol     e.g. 'EURUSD'
     * @param {Map<number, Bar[]>}                barsPerTf  Timeframe → bars[]
     * @param {Map<number, object>}               [indPerTf] Timeframe → { bullish, bearish }
     * @param {Map<number, object[]>}             [patPerTf] Timeframe → PatternMatch[]
     * @returns {MTFResult}
     */
    analyze(symbol, barsPerTf, indPerTf = new Map(), patPerTf = new Map()) {
        const tfScores = new Map();

        let totalWeight    = 0;
        let bullWeighted   = 0;
        let bearWeighted   = 0;
        let weightedScore  = 0;

        let dominantTf     = -1;
        let dominantWeight = -1;

        for (const [tf, bars] of barsPerTf) {
            const n      = bars.length;
            const weight = TF_WEIGHTS[tf] ?? 1.0;
            const tfLabel = _tfLabel(tf);

            // Structure
            const structResult = this._msAnalyser.analyse(bars, n);
            // Trend
            const trendResult  = this._trendClassifier.classify(bars, n);
            // Indicator summary
            const indSummary   = indPerTf.get(tf) ?? null;
            // Pattern results
            const patResult    = patPerTf.get(tf) ?? null;

            const cs = this._confluenceScorer.scoreTimeframe(symbol, tfLabel, {
                indSummary,
                patResult,
                structSig:  structResult.signal,
                structConf: structResult.confidence,
                trendSig:   trendResult.signal,
                trendConf:  trendResult.confidence,
            });

            tfScores.set(tf, cs);

            // Weighted aggregation
            totalWeight   += weight;
            weightedScore += cs.score * weight;

            if (cs.direction ===  1) bullWeighted += weight * cs.score;
            if (cs.direction === -1) bearWeighted += weight * cs.score;

            if (weight > dominantWeight) {
                dominantWeight = weight;
                dominantTf     = tf;
            }
        }

        const result = {
            symbol,
            direction:     0,
            confidence:    0,
            weightedScore: 0,
            tfScores,
            dominantTf,
        };

        if (totalWeight < 1e-10) return result;

        result.weightedScore = weightedScore / totalWeight;

        const totalVotes = bullWeighted + bearWeighted;
        if (totalVotes > 1e-10) {
            const bullPct = bullWeighted / totalVotes * 100.0;
            const bearPct = bearWeighted / totalVotes * 100.0;
            const margin  = Math.abs(bullPct - bearPct);
            if (margin >= 10.0) {
                result.direction = bullWeighted >= bearWeighted ? 1 : -1;
            }
            result.confidence = Math.min(0.95, result.weightedScore / 100.0 * (1 + margin / 200.0));
        }

        return result;
    }
}

// ── Internal helpers ──────────────────────────────────────────────────────────

/** Map Timeframe enum value → human-readable string label */
function _tfLabel(tf) {
    switch (tf) {
        case Timeframe.S15: return 'S15';
        case Timeframe.M1:  return 'M1';
        case Timeframe.M5:  return 'M5';
        case Timeframe.M15: return 'M15';
        case Timeframe.M30: return 'M30';
        case Timeframe.H1:  return 'H1';
        case Timeframe.H4:  return 'H4';
        case Timeframe.D1:  return 'D1';
        case Timeframe.W1:  return 'W1';
        default:            return String(tf);
    }
}
