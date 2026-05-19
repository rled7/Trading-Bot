"""BacktestEngine — bar-by-bar simulation engine.

Mirrors the C++ BacktestEngine blueprint exactly:
  - ATR-based SL/TP
  - Fixed-fractional position sizing (risk_pct of capital per trade)
  - Commission in USD per lot
  - Slippage in points (1 pt = 0.00001)
  - Equity-curve-based metrics (Sharpe, Sortino, Calmar, max-drawdown …)

Loop semantics (matching C++):
  At bar i (i in [warmup_bars, n-1]):
    1. Check SL/TP on open trade using bar[i].high / bar[i].low.
    2. If no open trade, run indicators+patterns on bars[0..i-1] (i.e. the
       *previous* bar), obtain a decision, and fill at bar[i].open ± slippage.
       This ensures no look-ahead: the signal comes from completed bar i-1,
       and execution happens at the open of bar i.

Usage::

    from algoforge.backtest import BacktestEngine, BTConfig
    from algoforge.algorithm import StubAlgo

    engine = BacktestEngine(StubAlgo())
    result = engine.run(bars, symbol="EURUSD", tf=3600)
    print(result.summary())
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass, field
from typing import Optional

from .algorithm import AlgoDecision, AlgoSignal, IAlgorithm
from .indicators import EngineResult, IndicatorEngine
from .patterns import PatternEngine
from .types import Bar, Direction


# ── Configuration ─────────────────────────────────────────────────────────────

@dataclass
class BTConfig:
    """Backtest configuration — all defaults match the C++ BTConfig."""
    initial_capital: float = 10_000.0
    risk_pct:        float = 0.01          # fraction of capital risked per trade
    commission_usd:  float = 7.0           # USD per lot
    slippage_pts:    int   = 2             # points of slippage (1 pt = 0.00001)
    atr_sl_mult:     float = 1.5           # ATR multiplier for stop-loss
    rr_ratio:        float = 2.0           # reward-to-risk ratio (TP = SL * rr_ratio)
    warmup_bars:     int   = 200           # bars skipped before trading begins


# ── Trade record ──────────────────────────────────────────────────────────────

@dataclass
class BTTrade:
    """Record of a single completed trade."""
    entry_time:   int       = 0
    exit_time:    int       = 0
    symbol:       str       = ""
    direction:    Direction = Direction.NONE
    lots:         float     = 0.0
    entry_price:  float     = 0.0
    exit_price:   float     = 0.0
    sl:           float     = 0.0
    tp:           float     = 0.0
    gross_pnl:    float     = 0.0
    commission:   float     = 0.0
    bars_held:    int       = 0
    close_reason: str       = ""   # "SL" | "TP" | "END"
    algo_name:    str       = ""

    @property
    def net_pnl(self) -> float:
        return self.gross_pnl - self.commission

    @property
    def is_winner(self) -> bool:
        return self.net_pnl > 0.0


# ── Result container with computed metrics ────────────────────────────────────

@dataclass
class BTResult:
    """Backtest result — trades, equity curve, and performance metrics."""
    symbol:          str           = ""
    timeframe:       int           = 0
    initial_capital: float         = 10_000.0
    trades:          list[BTTrade] = field(default_factory=list)
    equity_curve:    list[float]   = field(default_factory=list)
    duration_sec:    float         = 0.0

    # ── Basic aggregates ──────────────────────────────────────────────────────

    @property
    def total_trades(self) -> int:
        return len(self.trades)

    @property
    def winners(self) -> list[BTTrade]:
        return [t for t in self.trades if t.is_winner]

    @property
    def losers(self) -> list[BTTrade]:
        return [t for t in self.trades if not t.is_winner]

    @property
    def net_profit(self) -> float:
        return sum(t.net_pnl for t in self.trades)

    @property
    def final_capital(self) -> float:
        return self.initial_capital + self.net_profit

    @property
    def return_pct(self) -> float:
        return self.net_profit / self.initial_capital * 100.0

    @property
    def win_rate(self) -> float:
        if not self.trades:
            return 0.0
        return len(self.winners) / len(self.trades)

    @property
    def avg_win(self) -> float:
        w = self.winners
        if not w:
            return 0.0
        return sum(t.net_pnl for t in w) / len(w)

    @property
    def avg_loss(self) -> float:
        l = self.losers
        if not l:
            return 0.0
        return sum(t.net_pnl for t in l) / len(l)

    @property
    def profit_factor(self) -> float:
        gross_win  = sum(t.net_pnl for t in self.winners)
        gross_loss = sum(abs(t.net_pnl) for t in self.losers)
        if gross_loss < 1e-12:
            return 999.0
        return gross_win / gross_loss

    @property
    def expectancy(self) -> float:
        wr = self.win_rate
        return wr * self.avg_win + (1.0 - wr) * self.avg_loss

    # ── Equity-curve metrics ──────────────────────────────────────────────────

    @property
    def max_drawdown_pct(self) -> float:
        """Peak-to-trough drawdown on equity_curve expressed as a percentage."""
        curve = self.equity_curve
        if len(curve) < 2:
            return 0.0
        peak   = curve[0]
        max_dd = 0.0
        for v in curve:
            if v > peak:
                peak = v
            dd = (peak - v) / peak * 100.0 if peak > 1e-12 else 0.0
            if dd > max_dd:
                max_dd = dd
        return max_dd

    @property
    def sharpe_ratio(self) -> float:
        """Annualised Sharpe ratio (sqrt(252) * mean/std of bar-to-bar returns)."""
        curve = self.equity_curve
        if len(curve) < 2:
            return 0.0
        returns: list[float] = []
        for i in range(1, len(curve)):
            prev = curve[i - 1]
            returns.append((curve[i] - prev) / prev if prev > 1e-12 else 0.0)
        if not returns:
            return 0.0
        mean     = sum(returns) / len(returns)
        variance = sum((r - mean) ** 2 for r in returns) / len(returns)
        std      = math.sqrt(variance)
        if std < 1e-12:
            return 0.0
        return math.sqrt(252.0) * mean / std

    @property
    def sortino_ratio(self) -> float:
        """Annualised Sortino ratio (uses only negative-return std)."""
        curve = self.equity_curve
        if len(curve) < 2:
            return 0.0
        returns: list[float] = []
        for i in range(1, len(curve)):
            prev = curve[i - 1]
            returns.append((curve[i] - prev) / prev if prev > 1e-12 else 0.0)
        if not returns:
            return 0.0
        mean         = sum(returns) / len(returns)
        neg_returns  = [r for r in returns if r < 0.0]
        if not neg_returns:
            return 999.0
        downside_var = sum(r ** 2 for r in neg_returns) / len(neg_returns)
        downside_std = math.sqrt(downside_var)
        if downside_std < 1e-12:
            return 999.0
        return math.sqrt(252.0) * mean / downside_std

    @property
    def calmar_ratio(self) -> float:
        """Return_pct divided by max_drawdown_pct."""
        mdd = self.max_drawdown_pct
        if mdd < 1e-12:
            return 999.0
        return self.return_pct / mdd

    # ── Human-readable summary ────────────────────────────────────────────────

    def summary(self) -> str:
        lines = [
            f"=== Backtest Result: {self.symbol} (tf={self.timeframe}) ===",
            f"  Duration         : {self.duration_sec:.3f}s",
            f"  Initial Capital  : ${self.initial_capital:,.2f}",
            f"  Final Capital    : ${self.final_capital:,.2f}",
            f"  Net Profit       : ${self.net_profit:,.2f}",
            f"  Return           : {self.return_pct:.2f}%",
            f"  Total Trades     : {self.total_trades}",
            f"  Win Rate         : {self.win_rate * 100:.1f}%",
            f"  Profit Factor    : {self.profit_factor:.2f}",
            f"  Avg Win          : ${self.avg_win:.2f}",
            f"  Avg Loss         : ${self.avg_loss:.2f}",
            f"  Expectancy       : ${self.expectancy:.2f}",
            f"  Max Drawdown     : {self.max_drawdown_pct:.2f}%",
            f"  Sharpe Ratio     : {self.sharpe_ratio:.4f}",
            f"  Sortino Ratio    : {self.sortino_ratio:.4f}",
            f"  Calmar Ratio     : {self.calmar_ratio:.4f}",
        ]
        return "\n".join(lines)


# ── Engine ────────────────────────────────────────────────────────────────────

_POINT: float         = 0.00001       # 1 point in price terms
_CONTRACT_SIZE: float = 100_000.0     # standard forex contract size (1 lot)


class BacktestEngine:
    """Bar-by-bar backtest engine.

    Parameters
    ----------
    algo:
        The trading algorithm to evaluate.
    cfg:
        Backtest configuration.  Defaults to :class:`BTConfig` defaults.
    """

    def __init__(self, algo: IAlgorithm, cfg: BTConfig = BTConfig()) -> None:
        self._algo = algo
        self._cfg  = cfg
        self._ind  = IndicatorEngine()
        self._pat  = PatternEngine()

    # ── Public API ────────────────────────────────────────────────────────────

    def run(self, bars: list[Bar], symbol: str, tf: int) -> BTResult:
        """Run the backtest over *bars* and return a :class:`BTResult`."""
        t0  = time.perf_counter()
        cfg = self._cfg

        result = BTResult(
            symbol          = symbol,
            timeframe       = tf,
            initial_capital = cfg.initial_capital,
        )

        n = len(bars)
        # Need at least warmup_bars + 1 so we have a bar to evaluate and a next
        # bar to fill on.
        if n < cfg.warmup_bars + 1:
            result.duration_sec = time.perf_counter() - t0
            return result

        capital:  float = cfg.initial_capital
        slippage: float = cfg.slippage_pts * _POINT

        # Open-trade state (None = flat)
        open_trade:    Optional[BTTrade] = None
        entry_bar_idx: int               = 0

        equity_curve: list[float] = [capital]

        # Loop from warmup_bars to n-1 inclusive.
        # At each bar i:
        #   - Check SL/TP using bar[i].high / bar[i].low.
        #   - If flat, evaluate signal at bar[i-1] and fill at bar[i].open.
        for i in range(cfg.warmup_bars, n):
            bar = bars[i]

            # ── Step 1: Check SL / TP on open trade ───────────────────────
            if open_trade is not None:
                closed = False

                if open_trade.direction == Direction.LONG:
                    if bar.low <= open_trade.sl:
                        # SL hit — exit at SL price
                        exit_px = open_trade.sl
                        gross   = ((exit_px - open_trade.entry_price)
                                   * open_trade.lots * _CONTRACT_SIZE)
                        self._close_trade(open_trade, bar.timestamp, exit_px,
                                          gross, "SL", i - entry_bar_idx)
                        capital += open_trade.net_pnl
                        result.trades.append(open_trade)
                        open_trade = None
                        closed = True
                    elif bar.high >= open_trade.tp:
                        # TP hit — exit at TP price
                        exit_px = open_trade.tp
                        gross   = ((exit_px - open_trade.entry_price)
                                   * open_trade.lots * _CONTRACT_SIZE)
                        self._close_trade(open_trade, bar.timestamp, exit_px,
                                          gross, "TP", i - entry_bar_idx)
                        capital += open_trade.net_pnl
                        result.trades.append(open_trade)
                        open_trade = None
                        closed = True

                elif open_trade.direction == Direction.SHORT:
                    if bar.high >= open_trade.sl:
                        # SL hit
                        exit_px = open_trade.sl
                        gross   = ((open_trade.entry_price - exit_px)
                                   * open_trade.lots * _CONTRACT_SIZE)
                        self._close_trade(open_trade, bar.timestamp, exit_px,
                                          gross, "SL", i - entry_bar_idx)
                        capital += open_trade.net_pnl
                        result.trades.append(open_trade)
                        open_trade = None
                        closed = True
                    elif bar.low <= open_trade.tp:
                        # TP hit
                        exit_px = open_trade.tp
                        gross   = ((open_trade.entry_price - exit_px)
                                   * open_trade.lots * _CONTRACT_SIZE)
                        self._close_trade(open_trade, bar.timestamp, exit_px,
                                          gross, "TP", i - entry_bar_idx)
                        capital += open_trade.net_pnl
                        result.trades.append(open_trade)
                        open_trade = None
                        closed = True

                equity_curve.append(capital)
                if closed:
                    continue

            # ── Step 2: If flat, evaluate signal on prev bar → fill this bar ─
            if open_trade is None:
                # Evaluate indicator + pattern engines on bar[i-1]
                prev_idx: int              = i - 1
                ind_result: EngineResult   = self._ind.compute(bars, prev_idx)
                decision:   AlgoDecision   = self._algo.safe_evaluate(
                    symbol, tf, bars, prev_idx, ind_result, self._pat
                )

                if decision.is_actionable() and ind_result.atr_value is not None:
                    atr_val = ind_result.atr_value
                    if atr_val > 0.0:
                        signal    = decision.signal
                        direction = (Direction.LONG
                                     if signal == AlgoSignal.BUY
                                     else Direction.SHORT)

                        if direction == Direction.LONG:
                            entry_price = bar.open + slippage
                            sl = entry_price - atr_val * cfg.atr_sl_mult
                            tp = entry_price + atr_val * cfg.atr_sl_mult * cfg.rr_ratio
                        else:  # SHORT
                            entry_price = bar.open - slippage
                            sl = entry_price + atr_val * cfg.atr_sl_mult
                            tp = entry_price - atr_val * cfg.atr_sl_mult * cfg.rr_ratio

                        stop_dist = abs(entry_price - sl)
                        if stop_dist > 1e-12:
                            # Position sizing: lots = capital * risk_pct / (stop_dist * 100000)
                            lots_raw = capital * cfg.risk_pct / (stop_dist * _CONTRACT_SIZE)
                            lots     = max(0.01, min(10.0, round(lots_raw, 2)))
                            commission = cfg.commission_usd * lots

                            open_trade = BTTrade(
                                entry_time  = bar.timestamp,
                                symbol      = symbol,
                                direction   = direction,
                                lots        = lots,
                                entry_price = entry_price,
                                sl          = sl,
                                tp          = tp,
                                commission  = commission,
                                algo_name   = self._algo.name(),
                            )
                            entry_bar_idx = i

                equity_curve.append(capital)

        # ── Step 3: Close any remaining open trade at last bar's close ────────
        if open_trade is not None:
            last_bar   = bars[-1]
            exit_price = last_bar.close
            if open_trade.direction == Direction.LONG:
                gross = ((exit_price - open_trade.entry_price)
                         * open_trade.lots * _CONTRACT_SIZE)
            else:
                gross = ((open_trade.entry_price - exit_price)
                         * open_trade.lots * _CONTRACT_SIZE)
            bars_held = n - 1 - entry_bar_idx
            self._close_trade(open_trade, last_bar.timestamp, exit_price,
                              gross, "END", bars_held)
            capital += open_trade.net_pnl
            result.trades.append(open_trade)
            # Patch the last equity point to reflect the closed trade
            if equity_curve:
                equity_curve[-1] = capital
            else:
                equity_curve.append(capital)

        result.equity_curve = equity_curve
        result.duration_sec = time.perf_counter() - t0
        return result

    # ── Internal helpers ──────────────────────────────────────────────────────

    @staticmethod
    def _close_trade(
        trade:        BTTrade,
        exit_time:    int,
        exit_price:   float,
        gross_pnl:    float,
        reason:       str,
        bars_held:    int,
    ) -> None:
        """Mutate *trade* in-place to record exit details."""
        trade.exit_time    = exit_time
        trade.exit_price   = exit_price
        trade.gross_pnl    = gross_pnl
        trade.bars_held    = bars_held
        trade.close_reason = reason
