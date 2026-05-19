/**
 * Risk management — Round 10 (Risk + Execution).
 * Mirrors C++ PositionSizer, DrawdownGuard, SignalProcessor.
 *
 * Broker note: broker.js / paper_broker.js are CJS modules loaded via
 * createRequire when needed. All public methods in this file are synchronous.
 */

// ── Enums ─────────────────────────────────────────────────────────────────────

export const SizingMethod = Object.freeze({
    FIXED_RISK: 'fixed_risk',
    ATR_RISK:   'atr_risk',
    KELLY:      'kelly',
});

export const GuardState = Object.freeze({
    GREEN:  'green',
    YELLOW: 'yellow',
    RED:    'red',
});

// ── PositionSizer ─────────────────────────────────────────────────────────────

export class PositionSizer {

    /**
     * Round lots to the nearest step and clamp to [minLots, maxLots].
     * @param {number} lots
     * @param {number} step    volume step (e.g. 0.01)
     * @param {number} minLots minimum volume
     * @param {number} maxLots maximum volume
     * @returns {number}
     */
    static normalizeLots(lots, step, minLots, maxLots) {
        if (step <= 0) step = 0.01;
        // Round to nearest step, then clamp
        const n = Math.round(lots / step) * step;
        // Fix floating-point drift (e.g. 1.5700000000000001)
        const decimals = Math.max(0, -Math.floor(Math.log10(step)));
        const rounded  = parseFloat(n.toFixed(decimals));
        if (rounded < minLots) return minLots;
        if (rounded > maxLots) return maxLots;
        return rounded;
    }

    /**
     * Half-Kelly fraction, clamped to [0.005, maxRisk].
     * @param {number} winRate  probability of winning (0–1)
     * @param {number} rr       reward-to-risk ratio
     * @param {number} maxRisk  hard cap on fraction
     * @returns {number}
     */
    static kellyFraction(winRate, rr, maxRisk) {
        const edge  = winRate * rr - (1 - winRate);
        const kelly = rr > 1e-10 ? edge / rr : 0;
        let   half  = kelly * 0.5;
        half = Math.max(0.005, half);
        half = Math.min(maxRisk, half);
        return half;
    }

    /**
     * Compute position size.
     *
     * @param {string}     method      SizingMethod value
     * @param {number}     balance     account balance
     * @param {number}     entryPrice  intended entry price
     * @param {number}     stopLoss    stop-loss price (0 → use ATR)
     * @param {object}     sym         SymbolInfo-like { point, contractSize, volumeStep, volumeMin, volumeMax }
     * @param {number}     atr         ATR value (used when stopLoss <= 0)
     * @param {number}     riskPct     risk fraction of balance (e.g. 0.01 = 1%)
     * @param {number}     maxLots     hard lot cap
     * @param {number}     minLots     hard lot floor
     * @param {number}    [winRate=0.5]
     * @param {number}    [rrRatio=2.0]
     * @returns {{ lots: number, riskAmount: number, riskPct: number, stopDistance: number, capped: boolean }}
     */
    static sizePosition(
        method, balance, entryPrice, stopLoss, sym, atr,
        riskPct, maxLots, minLots, winRate = 0.5, rrRatio = 2.0,
    ) {
        // Stop distance
        let stopDist;
        if (stopLoss <= 0 && atr > 0) {
            stopDist = atr * 1.5;
        } else {
            stopDist = Math.abs(entryPrice - stopLoss);
        }
        if (stopDist < 1e-10) stopDist = entryPrice * 0.005;

        // Effective risk fraction
        const effRisk = (method === SizingMethod.KELLY)
            ? PositionSizer.kellyFraction(winRate, rrRatio, riskPct)
            : riskPct;

        const riskAmount = balance * effRisk;

        // Symbol properties
        const point        = sym.point        > 0 ? sym.point        : 0.00001;
        const contractSize = sym.contractSize > 0 ? sym.contractSize : 100_000;
        const valPerPoint  = contractSize * point;

        const stopInPts = stopDist / point;

        // Raw lot size
        let rawLots;
        if (stopInPts > 1e-10 && valPerPoint > 1e-10) {
            rawLots = riskAmount / (stopInPts * valPerPoint);
        } else {
            rawLots = minLots;
        }

        // Normalize
        const step = sym.volumeStep > 0 ? sym.volumeStep : 0.01;
        const lots = PositionSizer.normalizeLots(rawLots, step, minLots, maxLots);
        const capped = lots < rawLots - 1e-9;  // true if we hit the ceiling

        // Actual risk
        const actualRisk = lots * stopInPts * valPerPoint;
        const actualPct  = balance > 0 ? actualRisk / balance : 0;

        return {
            lots,
            riskAmount: actualRisk,
            riskPct:    actualPct,
            stopDistance: stopDist,
            capped,
        };
    }
}

// ── DrawdownGuard ─────────────────────────────────────────────────────────────

export class DrawdownGuard {

    /**
     * @param {number} [dailyLimit=0.05]  max allowed daily drawdown fraction
     * @param {number} [totalLimit=0.15]  max allowed total drawdown from peak
     */
    constructor(dailyLimit = 0.05, totalLimit = 0.15) {
        this._dailyLimit  = dailyLimit;
        this._totalLimit  = totalLimit;

        this._peakEquity       = 0;
        this._dailyStartEquity = 0;
        this._initialBalance   = 0;
        this._state            = GuardState.GREEN;
        this._resetDay         = -1;
        this._haltCount        = 0;
        this._reason           = '';
    }

    /** UTC year-day integer — rolls over at midnight UTC. */
    static _currentDay() {
        const d   = new Date();
        const jan = new Date(d.getUTCFullYear(), 0, 0);
        const yday = Math.floor((d - jan) / (1_000 * 60 * 60 * 24));
        return d.getUTCFullYear() * 1_000 + yday;
    }

    /**
     * Call once at startup to seed peak and daily equity.
     * @param {number} balance
     * @param {number} equity
     */
    initialise(balance, equity) {
        this._initialBalance   = balance;
        this._peakEquity       = equity;
        this._dailyStartEquity = equity;
        this._state            = GuardState.GREEN;
        this._resetDay         = DrawdownGuard._currentDay();
        this._haltCount        = 0;
        this._reason           = '';
    }

    /**
     * Evaluate current equity / balance and update state.
     *
     * @param {number} equity
     * @param {number} balance
     * @returns {{ state: string, dailyPnl: number, dailyPct: number, totalDd: number,
     *             peakEquity: number, currentEquity: number,
     *             tradingAllowed: boolean, reason: string }}
     */
    check(equity, balance) {   // eslint-disable-line no-unused-vars
        const today = DrawdownGuard._currentDay();

        // Daily reset
        if (today !== this._resetDay) {
            this._dailyStartEquity = equity;
            this._resetDay         = today;
            // Lift a daily-only halt (green ← red only on day reset)
            if (this._state === GuardState.RED) {
                this._state  = GuardState.GREEN;
                this._reason = '';
            }
        }

        // Update peak
        if (equity > this._peakEquity) this._peakEquity = equity;

        const dailyPnl = equity - this._dailyStartEquity;
        const dailyPct = dailyPnl / (this._dailyStartEquity + 1e-10);
        const totalDd  = (this._peakEquity - equity) / (this._peakEquity + 1e-10);

        const warnThresh = 0.75;

        if (dailyPct <= -this._dailyLimit || totalDd >= this._totalLimit) {
            // Breach → halt
            this._state = GuardState.RED;
            this._haltCount++;
            this._reason = dailyPct <= -this._dailyLimit
                ? `Daily loss limit breached: ${(dailyPct * 100).toFixed(2)}%`
                : `Total drawdown limit breached: ${(totalDd * 100).toFixed(2)}%`;
        } else if (
            dailyPct <= -(this._dailyLimit * warnThresh) ||
            totalDd  >=   this._totalLimit * warnThresh
        ) {
            // Warning zone
            if (this._state === GuardState.GREEN) {
                this._state  = GuardState.YELLOW;
                this._reason = `Approaching limit — daily=${(dailyPct * 100).toFixed(2)}% total_dd=${(totalDd * 100).toFixed(2)}%`;
            }
        } else {
            // Recovery
            if (this._state === GuardState.YELLOW) {
                this._state  = GuardState.GREEN;
                this._reason = '';
            }
        }

        return {
            state:          this._state,
            dailyPnl,
            dailyPct,
            totalDd,
            peakEquity:     this._peakEquity,
            currentEquity:  equity,
            tradingAllowed: this._state !== GuardState.RED,
            reason:         this._reason,
        };
    }

    get state()          { return this._state; }
    get tradingAllowed() { return this._state !== GuardState.RED; }
    get haltCount()      { return this._haltCount; }
}

// ── SignalProcessor ───────────────────────────────────────────────────────────

export class SignalProcessor {

    /**
     * @param {object} broker          IBroker-compatible instance
     * @param {object} [opts]
     * @param {number} [opts.maxRisk=0.01]      risk fraction per trade
     * @param {number} [opts.cooldownSec=300]   minimum seconds between signals per key
     */
    constructor(broker, { maxRisk = 0.01, cooldownSec = 300 } = {}) {
        this._broker      = broker;
        this._maxRisk     = maxRisk;
        this._cooldownSec = cooldownSec;
        this._lastSignal  = {};   // key → unix-seconds timestamp
        this._processed   = 0;
        this._rejected    = 0;
    }

    /**
     * Evaluate an AlgoDecision and place an order if actionable and not in cooldown.
     *
     * This method is fully synchronous — it relies on a synchronous broker (e.g. PaperBroker).
     *
     * @param {import('./algorithm.js').AlgoDecision} decision
     * @returns {boolean} true if an order was placed
     */
    process(decision) {
        if (!decision.isActionable()) {
            this._rejected++;
            return false;
        }

        const symbol = decision.symbol;

        // Direction: Direction.LONG = 1, Direction.SHORT = -1
        const isLong = decision.direction === 1;
        const dirChar = isLong ? 'L' : 'S';
        const key = `${symbol}_${dirChar}`;

        // Cooldown check
        const nowSec = Date.now() / 1_000;
        const last   = this._lastSignal[key];
        if (last !== undefined && (nowSec - last) < this._cooldownSec) {
            this._rejected++;
            return false;
        }

        // Market data (synchronous broker)
        const tick = this._broker.getTick(symbol);
        const sym  = this._broker.getSymbolInfo(symbol);
        const acct = this._broker.getAccount();

        // ATR / stop distance
        const atr = decision.atr > 0 ? decision.atr : tick.ask * 0.001;
        const stopDist = atr * 1.5;

        // Lot calculation (simplified — mirrors C++ SignalProcessor)
        const riskAmt  = acct.balance * this._maxRisk;
        const step     = sym.volumeStep > 0 ? sym.volumeStep : 0.01;
        const minLots  = sym.volumeMin  > 0 ? sym.volumeMin  : 0.01;
        const maxLots  = sym.volumeMax  > 0 ? sym.volumeMax  : 100;
        const cs       = sym.contractSize > 0 ? sym.contractSize : 100_000;
        const rawLots  = stopDist > 1e-10 ? riskAmt / (stopDist * cs) : minLots;
        const lots     = PositionSizer.normalizeLots(rawLots, step, minLots, maxLots);

        // Entry / SL / TP
        const entry = isLong ? tick.ask : tick.bid;
        const sl    = isLong ? entry - atr * 1.5 : entry + atr * 1.5;
        const tp    = isLong ? entry + atr * 3.0 : entry - atr * 3.0;

        // Import OrderType from CJS broker at call time to avoid top-level require in ESM
        // We resolve the enum value directly to avoid a CJS/ESM cross-loading issue.
        // OrderType.BUY = 0, OrderType.SELL = 1 (from broker.js)
        const orderType = isLong ? 0 : 1;  // OrderType.BUY : OrderType.SELL

        let result;
        try {
            result = this._broker.placeOrder(
                symbol, orderType, lots,
                /*price=*/ 0, sl, tp,
                /*magic=*/ 1000,
                /*comment=*/ decision.reason || '',
            );
        } catch (err) {
            console.error(`[SignalProcessor] placeOrder failed for ${symbol}: ${err.message}`);
            this._rejected++;
            return false;
        }

        if (result) {
            this._lastSignal[key] = nowSec;
            this._processed++;
            console.log(
                `[SignalProcessor] Placed ${isLong ? 'BUY' : 'SELL'} ${lots.toFixed(2)} ` +
                `${symbol} | SL=${sl.toFixed(5)} TP=${tp.toFixed(5)} | ${decision.reason}`
            );
            return true;
        }

        this._rejected++;
        return false;
    }

    /** Number of orders successfully placed. */
    get processed() { return this._processed; }

    /** Number of decisions rejected (not actionable, cooldown, or order failure). */
    get rejected()  { return this._rejected; }
}
