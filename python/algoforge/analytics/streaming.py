from __future__ import annotations

"""Online (streaming) metrics for AlgoForge analytics.

OnlineMetrics ingests trades one at a time and maintains running statistics
using Welford's online algorithm for mean/variance.  No batch passes; all
metrics are O(1) per update.

Spec reference: analytics_spec.md §4 "Streaming".
"""

import math
from typing import Any


class OnlineMetrics:
    """Incremental trade-by-trade metric accumulator.

    Parameters
    ----------
    initial_capital:
        Starting equity value.  Used as the basis for drawdown percentages
        and for ``time_in_market`` denominator once ``total_bars`` is known.
    """

    def __init__(self, initial_capital: float = 10_000.0) -> None:
        self._initial_capital: float = initial_capital
        self._reset_state()

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    def update(self, trade_dict: dict[str, Any]) -> None:
        """Ingest a single completed trade.

        Parameters
        ----------
        trade_dict:
            Must contain:
            - ``net_pnl``   (float) – trade profit / loss in currency units.
            - ``bars_held`` (int)   – number of bars the trade was open.
            - ``timestamp`` (int)   – Unix seconds at trade close (unused for
              metrics but available for future time-series use).
        """
        pnl: float = float(trade_dict["net_pnl"])
        bars_held: int = int(trade_dict.get("bars_held", 0))

        # ---- trade count -------------------------------------------------
        self._n_trades += 1
        n = self._n_trades

        # ---- Welford online mean / M2 ------------------------------------
        delta = pnl - self._welford_mean
        self._welford_mean += delta / n
        delta2 = pnl - self._welford_mean
        self._welford_m2 += delta * delta2

        # ---- running sums ------------------------------------------------
        self._sum_pnl += pnl
        self._sum_bars += bars_held

        if pnl > 0.0:
            self._n_wins += 1
            self._sum_winning_pnl += pnl
        elif pnl < 0.0:
            self._n_losses += 1
            self._sum_losing_pnl_abs += abs(pnl)

        # ---- streak tracking -------------------------------------------
        # _current_streak is signed: positive = consecutive wins,
        # negative = consecutive losses.
        if pnl > 0.0:
            self._current_streak = max(0, self._current_streak) + 1
        else:
            # Spec: longest_loss_streak includes breakeven (net_pnl <= 0)
            self._current_streak = min(0, self._current_streak) - 1

        if self._current_streak > 0:
            if self._current_streak > self._longest_win_streak:
                self._longest_win_streak = self._current_streak
        else:
            loss_run = abs(self._current_streak)
            if loss_run > self._longest_loss_streak:
                self._longest_loss_streak = loss_run

        # ---- equity & drawdown -----------------------------------------
        self._running_equity += pnl
        if self._running_equity > self._peak_equity:
            self._peak_equity = self._running_equity

        dd_abs = self._running_equity - self._peak_equity
        if self._peak_equity != 0.0:
            dd_pct = dd_abs / self._peak_equity * 100.0
        else:
            dd_pct = 0.0

        if dd_abs < self._max_dd_abs:
            self._max_dd_abs = dd_abs
        if dd_pct < self._max_dd_pct:
            self._max_dd_pct = dd_pct

        self._current_dd_pct = dd_pct

    def snapshot(self) -> dict[str, Any]:
        """Return a dict with all 14 spec metrics plus drawdown stats.

        Returns
        -------
        dict with keys:
            n_trades, win_rate, expectancy, avg_win, avg_loss, payoff_ratio,
            profit_factor, longest_win_streak, longest_loss_streak,
            max_consec_losses, time_in_market, recovery_factor,
            sharpe, sortino, calmar, omega,
            max_dd_abs, max_dd_pct, current_dd_pct,
            running_equity, peak_equity.

        Notes
        -----
        * ``sharpe``, ``sortino``, ``calmar``, ``omega`` require bar_returns
          series which are not available in the online setting.  They are set
          to ``0.0`` and must be computed by the batch ``metrics.py`` module
          from the full equity curve.
        * ``time_in_market`` is also ``0.0`` until the caller records
          ``total_bars`` via an attribute or future API.
        """
        n = self._n_trades
        n_wins = self._n_wins
        n_losses = self._n_losses

        # ---- win_rate ----
        win_rate = n_wins / n if n > 0 else 0.0

        # ---- expectancy (mean net_pnl) ----
        # Welford mean equals mean(pnl) by construction.
        expectancy = self._welford_mean if n > 0 else 0.0

        # ---- avg_win / avg_loss ----
        avg_win = self._sum_winning_pnl / n_wins if n_wins > 0 else 0.0
        avg_loss = -self._sum_losing_pnl_abs / n_losses if n_losses > 0 else 0.0

        # ---- payoff_ratio ----
        if n_losses == 0:
            payoff_ratio = math.inf if n_wins > 0 else 0.0
        else:
            payoff_ratio = avg_win / abs(avg_loss)

        # ---- profit_factor ----
        if self._sum_losing_pnl_abs == 0.0:
            profit_factor = math.inf if self._sum_winning_pnl > 0.0 else 0.0
        else:
            profit_factor = self._sum_winning_pnl / self._sum_losing_pnl_abs

        # ---- recovery_factor ----
        max_dd_abs = self._max_dd_abs  # negative or 0
        if max_dd_abs == 0.0:
            recovery_factor = 0.0
        else:
            recovery_factor = self._sum_pnl / abs(max_dd_abs)

        # ---- streak aliases ----
        longest_win_streak = self._longest_win_streak
        longest_loss_streak = self._longest_loss_streak
        max_consec_losses = longest_loss_streak

        # ---- time_in_market ----
        # Not computable online without total_bars knowledge.
        time_in_market = 0.0

        # ---- bar-return-dependent metrics (batch only) ----
        sharpe = 0.0
        sortino = 0.0
        calmar = 0.0
        omega = 0.0

        return {
            # Core trade metrics
            "n_trades": n,
            "win_rate": win_rate,
            "expectancy": expectancy,
            "avg_win": avg_win,
            "avg_loss": avg_loss,
            "payoff_ratio": payoff_ratio,
            "profit_factor": profit_factor,
            "longest_win_streak": longest_win_streak,
            "longest_loss_streak": longest_loss_streak,
            "max_consec_losses": max_consec_losses,
            "time_in_market": time_in_market,
            "recovery_factor": recovery_factor,
            # Bar-return-dependent (0.0 in online mode)
            "sharpe": sharpe,
            "sortino": sortino,
            "calmar": calmar,
            "omega": omega,
            # Drawdown stats
            "max_dd_abs": max_dd_abs,
            "max_dd_pct": self._max_dd_pct,
            "current_dd_pct": self._current_dd_pct,
            # Equity state
            "running_equity": self._running_equity,
            "peak_equity": self._peak_equity,
        }

    def reset(self) -> None:
        """Reset all accumulated state back to initial values."""
        self._reset_state()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _reset_state(self) -> None:
        """Initialise (or reinitialise) all mutable tracking variables."""
        # Welford online algorithm variables
        self._welford_mean: float = 0.0
        self._welford_m2: float = 0.0   # sum of squared deviations

        # Trade counts
        self._n_trades: int = 0
        self._n_wins: int = 0
        self._n_losses: int = 0

        # Running sums
        self._sum_pnl: float = 0.0
        self._sum_bars: int = 0
        self._sum_winning_pnl: float = 0.0
        self._sum_losing_pnl_abs: float = 0.0

        # Streak state
        # Positive integer = current win streak length.
        # Negative integer = current loss streak length (absolute value).
        self._current_streak: int = 0
        self._longest_win_streak: int = 0
        self._longest_loss_streak: int = 0

        # Equity / drawdown
        self._running_equity: float = self._initial_capital
        self._peak_equity: float = self._initial_capital
        self._max_dd_abs: float = 0.0
        self._max_dd_pct: float = 0.0
        self._current_dd_pct: float = 0.0
