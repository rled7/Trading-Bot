'use strict';

/**
 * BacktestEngine — mirrors C++ BacktestEngine exactly.
 *
 * Indicator and pattern functions are imported from the sibling ESM modules.
 * Only atr() is used internally; the full ind/pat objects are passed to
 * algo.safeEvaluate() so real algorithms can inspect them.
 */

import { atr as _atrFn } from './indicators.js';
import { AlgoSignal }     from './algorithm.js';

// ── Private helpers ───────────────────────────────────────────────────────────

/**
 * Build indicator arrays for the current bar window.
 * Exposes atr14 (14-period ATR) as the primary risk sizing input.
 * @param {object[]} bars
 * @returns {{ atr14: Array<number|null> }}
 */
function _buildIndicators(bars) {
    const highs  = bars.map(b => b.high);
    const lows   = bars.map(b => b.low);
    const closes = bars.map(b => b.close);
    return { atr14: _atrFn(highs, lows, closes, 14) };
}

/**
 * Build a lightweight pattern context passed to algo.evaluate().
 * Real algorithms may perform their own full scan; the engine itself
 * only needs the indicator values for SL/TP sizing.
 * @param {object[]} bars
 * @returns {{ matches: [] }}
 */
function _buildPatterns(/* bars */) {
    return { matches: [] };
}

// ── BTConfig ──────────────────────────────────────────────────────────────────

export class BTConfig {
    constructor({
        initialCapital = 10000,
        riskPct        = 0.01,
        commissionUsd  = 7.0,
        slippagePts    = 2,
        atrSlMult      = 1.5,
        rrRatio        = 2.0,
        warmupBars     = 200,
    } = {}) {
        this.initialCapital = initialCapital;
        this.riskPct        = riskPct;
        this.commissionUsd  = commissionUsd;
        this.slippagePts    = slippagePts;
        this.atrSlMult      = atrSlMult;
        this.rrRatio        = rrRatio;
        this.warmupBars     = warmupBars;
    }
}

// ── BTTrade ───────────────────────────────────────────────────────────────────

/**
 * Factory that creates a BTTrade plain object with methods attached.
 * All fields mirror C++ BTTrade exactly.
 *
 * @param {{
 *   entryTime?: number, exitTime?: number, symbol?: string,
 *   direction?: number, lots?: number, entryPrice?: number,
 *   exitPrice?: number, sl?: number, tp?: number,
 *   grossPnl?: number, commission?: number, barsHeld?: number,
 *   closeReason?: string, algoName?: string
 * }} opts
 */
export function makeBTTrade({
    entryTime   = 0,
    exitTime    = 0,
    symbol      = '',
    direction   = 0,   // 0=NONE, 1=LONG, -1=SHORT
    lots        = 0,
    entryPrice  = 0,
    exitPrice   = 0,
    sl          = 0,
    tp          = 0,
    grossPnl    = 0,
    commission  = 0,
    barsHeld    = 0,
    closeReason = '',  // "SL" | "TP" | "END"
    algoName    = '',
} = {}) {
    return {
        entryTime,
        exitTime,
        symbol,
        direction,
        lots,
        entryPrice,
        exitPrice,
        sl,
        tp,
        grossPnl,
        commission,
        barsHeld,
        closeReason,
        algoName,
        netPnl()   { return this.grossPnl - this.commission; },
        isWinner() { return this.netPnl() > 0; },
    };
}

// ── BTResult ──────────────────────────────────────────────────────────────────

export class BTResult {
    /**
     * @param {string}   symbol
     * @param {number}   timeframe
     * @param {number}   initialCapital
     * @param {object[]} trades        Array of BTTrade objects
     * @param {number[]} equityCurve   Equity value at each bar (from warmupBars onward)
     * @param {number}   durationSec   Wall-clock seconds the engine took
     */
    constructor(symbol, timeframe, initialCapital, trades, equityCurve, durationSec) {
        this.symbol         = symbol;
        this.timeframe      = timeframe;
        this.initialCapital = initialCapital;
        this.trades         = trades;
        this.equityCurve    = equityCurve;
        this.durationSec    = durationSec;
    }

    // ── Derived metrics ────────────────────────────────────────────────────────

    netProfit() {
        return this.trades.reduce((s, t) => s + t.netPnl(), 0);
    }

    finalCapital() {
        return this.initialCapital + this.netProfit();
    }

    returnPct() {
        return this.netProfit() / this.initialCapital * 100;
    }

    winRate() {
        if (this.trades.length === 0) return 0;
        const winners = this.trades.filter(t => t.isWinner()).length;
        return winners / this.trades.length;
    }

    profitFactor() {
        let grossWin = 0, grossLoss = 0;
        for (const t of this.trades) {
            const p = t.netPnl();
            if (p > 0) grossWin  += p;
            else        grossLoss += Math.abs(p);
        }
        return grossLoss < 1e-10 ? 999 : grossWin / grossLoss;
    }

    maxDrawdownPct() {
        const eq = this.equityCurve;
        if (eq.length === 0) return 0;
        let peak = eq[0];
        let maxDD = 0;
        for (const v of eq) {
            if (v > peak) peak = v;
            const dd = (peak - v) / peak;
            if (dd > maxDD) maxDD = dd;
        }
        return maxDD * 100;
    }

    sharpeRatio() {
        const eq = this.equityCurve;
        if (eq.length < 2) return 0;
        // bar-to-bar absolute returns
        const rets = [];
        for (let i = 1; i < eq.length; i++) {
            rets.push(eq[i] - eq[i - 1]);
        }
        const mean     = rets.reduce((s, r) => s + r, 0) / rets.length;
        const variance = rets.reduce((s, r) => s + (r - mean) ** 2, 0) / rets.length;
        const std      = Math.sqrt(variance);
        if (std < 1e-10) return 0;
        return (mean / std) * Math.sqrt(252);
    }

    sortinoRatio() {
        const eq = this.equityCurve;
        if (eq.length < 2) return 0;
        const rets = [];
        for (let i = 1; i < eq.length; i++) {
            rets.push(eq[i] - eq[i - 1]);
        }
        const mean     = rets.reduce((s, r) => s + r, 0) / rets.length;
        // downside deviation — only negative returns
        const negSqSum = rets.reduce((s, r) => s + (r < 0 ? r * r : 0), 0);
        const downStd  = Math.sqrt(negSqSum / rets.length);
        if (downStd < 1e-10) return 0;
        return (mean / downStd) * Math.sqrt(252);
    }

    calmarRatio() {
        const dd = this.maxDrawdownPct();
        if (dd < 1e-10) return 0;
        return this.returnPct() / dd;
    }

    avgWin() {
        const winners = this.trades.filter(t => t.isWinner());
        if (winners.length === 0) return 0;
        return winners.reduce((s, t) => s + t.netPnl(), 0) / winners.length;
    }

    avgLoss() {
        const losers = this.trades.filter(t => !t.isWinner());
        if (losers.length === 0) return 0;
        return losers.reduce((s, t) => s + t.netPnl(), 0) / losers.length;
    }

    expectancy() {
        const wr = this.winRate();
        return wr * this.avgWin() + (1 - wr) * this.avgLoss();
    }

    summary() {
        const pct = n => n.toFixed(2) + '%';
        const usd = n => '$' + n.toFixed(2);
        const f2  = n => n.toFixed(2);
        return [
            `Symbol        : ${this.symbol}`,
            `Timeframe     : ${this.timeframe}`,
            `Initial Cap   : ${usd(this.initialCapital)}`,
            `Final Cap     : ${usd(this.finalCapital())}`,
            `Net Profit    : ${usd(this.netProfit())}`,
            `Return        : ${pct(this.returnPct())}`,
            `Trades        : ${this.trades.length}`,
            `Win Rate      : ${pct(this.winRate() * 100)}`,
            `Profit Factor : ${f2(this.profitFactor())}`,
            `Max Drawdown  : ${pct(this.maxDrawdownPct())}`,
            `Sharpe Ratio  : ${f2(this.sharpeRatio())}`,
            `Sortino Ratio : ${f2(this.sortinoRatio())}`,
            `Calmar Ratio  : ${f2(this.calmarRatio())}`,
            `Avg Win       : ${usd(this.avgWin())}`,
            `Avg Loss      : ${usd(this.avgLoss())}`,
            `Expectancy    : ${usd(this.expectancy())}`,
            `Duration (s)  : ${this.durationSec.toFixed(3)}`,
        ].join('\n');
    }
}

// ── BacktestEngine ────────────────────────────────────────────────────────────

export class BacktestEngine {
    /**
     * @param {IAlgorithm} algo
     * @param {BTConfig|object} [cfg]  BTConfig instance or plain config object.
     */
    constructor(algo, cfg = new BTConfig()) {
        this._algo = algo;
        this._cfg  = cfg instanceof BTConfig ? cfg : new BTConfig(cfg);
    }

    /**
     * Run a backtest over the provided bar array.
     *
     * @param {object[]} bars    Bar-like objects: { timestamp, open, high, low, close, volume }
     * @param {string}   symbol
     * @param {number}   tf      Timeframe constant (seconds)
     * @returns {BTResult}
     */
    run(bars, symbol, tf) {
        const cfg      = this._cfg;
        const algo     = this._algo;
        const n        = bars.length;
        const startMs  = Date.now();
        const slippage = cfg.slippagePts * 0.00001;  // points → price units

        // Pre-compute the full-series ATR for SL/TP sizing
        const highs  = bars.map(b => b.high);
        const lows   = bars.map(b => b.low);
        const closes = bars.map(b => b.close);
        const atrArr = _atrFn(highs, lows, closes, 14);

        const trades      = [];
        const equityCurve = [];

        let capital   = cfg.initialCapital;
        let openTrade = null;   // active BTTrade or null

        for (let i = cfg.warmupBars; i < n; i++) {
            const bar  = bars[i];
            const next = (i + 1 < n) ? bars[i + 1] : null;

            // ── Step 1: Check SL/TP on current bar's high/low ─────────────────
            if (openTrade !== null) {
                let closed = false;

                if (openTrade.direction === 1 /* LONG */) {
                    if (bar.low <= openTrade.sl) {
                        // Stop-loss hit
                        openTrade.exitPrice   = openTrade.sl;
                        openTrade.exitTime    = bar.timestamp;
                        openTrade.barsHeld    = i - openTrade._entryBar;
                        openTrade.grossPnl    = (openTrade.sl - openTrade.entryPrice)
                                                * openTrade.lots * 100000;
                        openTrade.commission  = cfg.commissionUsd * openTrade.lots;
                        openTrade.closeReason = 'SL';
                        closed = true;
                    } else if (bar.high >= openTrade.tp) {
                        // Take-profit hit
                        openTrade.exitPrice   = openTrade.tp;
                        openTrade.exitTime    = bar.timestamp;
                        openTrade.barsHeld    = i - openTrade._entryBar;
                        openTrade.grossPnl    = (openTrade.tp - openTrade.entryPrice)
                                                * openTrade.lots * 100000;
                        openTrade.commission  = cfg.commissionUsd * openTrade.lots;
                        openTrade.closeReason = 'TP';
                        closed = true;
                    }
                } else /* SHORT */ {
                    if (bar.high >= openTrade.sl) {
                        // Stop-loss hit
                        openTrade.exitPrice   = openTrade.sl;
                        openTrade.exitTime    = bar.timestamp;
                        openTrade.barsHeld    = i - openTrade._entryBar;
                        openTrade.grossPnl    = (openTrade.entryPrice - openTrade.sl)
                                                * openTrade.lots * 100000;
                        openTrade.commission  = cfg.commissionUsd * openTrade.lots;
                        openTrade.closeReason = 'SL';
                        closed = true;
                    } else if (bar.low <= openTrade.tp) {
                        // Take-profit hit
                        openTrade.exitPrice   = openTrade.tp;
                        openTrade.exitTime    = bar.timestamp;
                        openTrade.barsHeld    = i - openTrade._entryBar;
                        openTrade.grossPnl    = (openTrade.entryPrice - openTrade.tp)
                                                * openTrade.lots * 100000;
                        openTrade.commission  = cfg.commissionUsd * openTrade.lots;
                        openTrade.closeReason = 'TP';
                        closed = true;
                    }
                }

                if (closed) {
                    capital += openTrade.netPnl();
                    trades.push(openTrade);
                    openTrade = null;
                }
            }

            // ── Step 2: No open trade + next bar exists → ask algo ────────────
            if (openTrade === null && next !== null) {
                const ind      = _buildIndicators(bars.slice(0, i + 1));
                const pat      = _buildPatterns(bars.slice(0, i + 1));
                const decision = algo.safeEvaluate(symbol, tf, bars, i + 1, ind, pat);

                if (decision.isActionable()) {
                    const atrVal = atrArr[i];
                    if (atrVal !== null && atrVal > 0) {
                        const isLong = decision.signal === AlgoSignal.BUY;

                        // Fill at next bar's open ± slippage
                        const fillPx = isLong
                            ? next.open + slippage
                            : next.open - slippage;

                        // ATR-based stop / target
                        const stopDist = atrVal * cfg.atrSlMult;
                        const sl = isLong
                            ? fillPx - stopDist
                            : fillPx + stopDist;
                        const tp = isLong
                            ? fillPx + stopDist * cfg.rrRatio
                            : fillPx - stopDist * cfg.rrRatio;

                        // Position sizing: risk fixed % of capital over stop distance
                        const _sizeMult = (algo && algo.metadata && algo.metadata.sizeMult != null)
                            ? algo.metadata.sizeMult : 1.0;
                        let lots = (capital * cfg.riskPct) / (stopDist * 100000) * _sizeMult;
                        lots = Math.round(lots * 100) / 100;           // round to 2 dp
                        lots = Math.max(0.01, Math.min(10.0, lots));   // clamp [0.01, 10]

                        openTrade = makeBTTrade({
                            entryTime : next.timestamp,
                            symbol,
                            direction : isLong ? 1 : -1,
                            lots,
                            entryPrice: fillPx,
                            sl,
                            tp,
                            algoName  : algo.name(),
                        });
                        // Internal marker for barsHeld computation
                        openTrade._entryBar = i + 1;
                    }
                }
            }

            // ── Step 3: Record equity curve ───────────────────────────────────
            let floatingPnl = 0;
            if (openTrade !== null) {
                floatingPnl = openTrade.direction === 1
                    ? (bar.close - openTrade.entryPrice) * openTrade.lots * 100000
                    : (openTrade.entryPrice - bar.close) * openTrade.lots * 100000;
            }
            equityCurve.push(capital + floatingPnl);
        }

        // ── Step 4: Force-close any open trade at last bar's close ────────────
        if (openTrade !== null && n > 0) {
            const lastBar           = bars[n - 1];
            openTrade.exitPrice     = lastBar.close;
            openTrade.exitTime      = lastBar.timestamp;
            openTrade.barsHeld      = (n - 1) - openTrade._entryBar;
            openTrade.grossPnl      = openTrade.direction === 1
                ? (lastBar.close - openTrade.entryPrice) * openTrade.lots * 100000
                : (openTrade.entryPrice - lastBar.close) * openTrade.lots * 100000;
            openTrade.commission    = cfg.commissionUsd * openTrade.lots;
            openTrade.closeReason   = 'END';
            capital += openTrade.netPnl();
            trades.push(openTrade);
        }

        const durationSec = (Date.now() - startMs) / 1000;
        return new BTResult(symbol, tf, cfg.initialCapital, trades, equityCurve, durationSec);
    }
}
