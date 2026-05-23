/**
 * streaming.js — OnlineMetrics class with Welford's algorithm.
 * Spec §4 (Streaming section).
 */

/**
 * OnlineMetrics tracks all 14 metrics incrementally.
 * Call update(trade) for each new trade, snapshot() for current metrics.
 */
export class OnlineMetrics {
    constructor(initialCapital = 10000) {
        this._initialCapital = initialCapital;
        this.reset();
    }

    reset() {
        // Trade counts
        this._nTrades   = 0;
        this._nWins     = 0;
        this._nLosses   = 0;
        // Welford state for trade PnL
        this._wMean     = 0;
        this._wM2       = 0;
        // Win/loss sums
        this._sumWinPnl  = 0;
        this._sumLossPnlAbs = 0;
        // Streaks
        this._currentStreak = 0;   // positive=winning, negative=losing
        this._longestWin    = 0;
        this._longestLoss   = 0;
        // Equity and drawdown
        this._runningEquity = this._initialCapital;
        this._peakEquity    = this._initialCapital;
        this._maxDdAbs      = 0;
        this._maxDdPct      = 0;
        // For bar-return-based metrics (stored cumulative)
        this._barRetMean    = 0;
        this._barRetM2      = 0;
        this._barRetN       = 0;
        this._barRetDownSqSum = 0;  // sum of min(r,0)^2
        // For omega / sortino
        this._barRetSumPos  = 0;
        this._barRetSumNeg  = 0;
        // For payoff
        this._barsHeldSum   = 0;
    }

    /**
     * Update with a new trade.
     * @param {{netPnl: number|function, barsHeld?: number}} trade
     *   netPnl may be a number or a method returning a number.
     */
    update(trade) {
        const pnl = typeof trade.netPnl === 'function' ? trade.netPnl() : trade.netPnl;

        // Welford online mean/variance for PnL
        this._nTrades++;
        const delta = pnl - this._wMean;
        this._wMean += delta / this._nTrades;
        const delta2 = pnl - this._wMean;
        this._wM2 += delta * delta2;

        // Win/loss accounting
        if (pnl > 0) {
            this._nWins++;
            this._sumWinPnl += pnl;
            // Streaks
            if (this._currentStreak < 0) this._currentStreak = 0;
            this._currentStreak++;
            if (this._currentStreak > this._longestWin) {
                this._longestWin = this._currentStreak;
            }
        } else {
            this._nLosses++;
            this._sumLossPnlAbs += Math.abs(pnl);
            // Streaks
            if (this._currentStreak > 0) this._currentStreak = 0;
            this._currentStreak--;
            const lossRun = -this._currentStreak;
            if (lossRun > this._longestLoss) this._longestLoss = lossRun;
        }

        // Running equity + drawdown
        this._runningEquity += pnl;
        if (this._runningEquity > this._peakEquity) {
            this._peakEquity = this._runningEquity;
        }
        const ddAbs = this._runningEquity - this._peakEquity;
        const ddPct = this._peakEquity > 0
            ? (this._runningEquity - this._peakEquity) / this._peakEquity * 100
            : 0;
        if (ddAbs < this._maxDdAbs) this._maxDdAbs = ddAbs;
        if (ddPct < this._maxDdPct) this._maxDdPct = ddPct;

        // Bar return for bar-level metrics (approximate: treat each trade as one bar)
        const barRet = this._peakEquity > 0 ? pnl / (this._runningEquity - pnl || 1) : 0;
        this._barRetN++;
        const bDelta = barRet - this._barRetMean;
        this._barRetMean += bDelta / this._barRetN;
        const bDelta2 = barRet - this._barRetMean;
        this._barRetM2 += bDelta * bDelta2;
        const minR = Math.min(barRet, 0);
        this._barRetDownSqSum += minR * minR;
        if (barRet > 0) this._barRetSumPos += barRet;
        else this._barRetSumNeg += Math.abs(barRet);

        // BarsHeld
        if (trade.barsHeld !== undefined) this._barsHeldSum += trade.barsHeld;
    }

    /**
     * Current snapshot of all metrics.
     * @returns {object}
     */
    snapshot() {
        const n = this._nTrades;
        const EPS = 1e-14;

        // Sharpe / Sortino approximation from bar-return Welford state
        const bn = this._barRetN;
        let sharpeVal = 0, sortinoVal = 0;
        if (bn >= 2) {
            const barStd = Math.sqrt(this._barRetM2 / (bn - 1));
            const sqrtA  = Math.sqrt(252);
            if (barStd > EPS) sharpeVal = sqrtA * this._barRetMean / barStd;
            const downStd = Math.sqrt(this._barRetDownSqSum / (bn - 1));
            if (downStd > EPS) sortinoVal = sqrtA * this._barRetMean / downStd;
        }

        // Omega
        const omegaVal = this._barRetSumNeg < EPS
            ? (this._barRetSumPos < EPS ? 0 : Infinity)
            : this._barRetSumPos / this._barRetSumNeg;

        const aw = this._nWins > 0 ? this._sumWinPnl / this._nWins : 0;
        const al = this._nLosses > 0
            ? -(this._sumLossPnlAbs / this._nLosses)
            : 0;

        // Profit factor
        const pf = this._sumLossPnlAbs < EPS
            ? (this._sumWinPnl < EPS ? 0 : Infinity)
            : this._sumWinPnl / this._sumLossPnlAbs;

        // Payoff ratio
        const payoff = Math.abs(al) < EPS ? Infinity : aw / Math.abs(al);

        const totalPnl = this._runningEquity - this._initialCapital;
        const totalReturnPct = this._initialCapital > 0
            ? totalPnl / this._initialCapital * 100
            : 0;

        const calmarVal = Math.abs(this._maxDdPct) < EPS ? 0
            : totalReturnPct / Math.abs(this._maxDdPct);

        const recovFactor = Math.abs(this._maxDdAbs) < EPS ? 0
            : totalPnl / Math.abs(this._maxDdAbs);

        return {
            nTrades:           n,
            nWins:             this._nWins,
            nLosses:           this._nLosses,
            winRate:           n > 0 ? this._nWins / n : 0,
            expectancy:        this._wMean,
            avgWin:            aw,
            avgLoss:           al,
            payoffRatio:       payoff,
            profitFactor:      pf,
            sharpe:            sharpeVal,
            sortino:           sortinoVal,
            omega:             omegaVal,
            calmar:            calmarVal,
            recoveryFactor:    recovFactor,
            longestWinStreak:  this._longestWin,
            longestLossStreak: this._longestLoss,
            maxConsecLosses:   this._longestLoss,
            totalPnl,
            totalReturnPct,
            runningEquity:     this._runningEquity,
            peakEquity:        this._peakEquity,
            maxDdAbs:          this._maxDdAbs,
            maxDdPct:          this._maxDdPct,
        };
    }
}
