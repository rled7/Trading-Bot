"""Performance metrics — all 14 metrics from spec §4.

All functions are pure — no I/O, no state.
Annualisation factor A defaults to ``sqrt(252)`` (daily bars).
"""

from __future__ import annotations

import math

from .drawdown import max_drawdown_abs, max_drawdown_pct
from .types import MetricsResult, TradeReturn


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _mean(xs: list[float]) -> float:
    """Arithmetic mean; returns 0.0 for empty sequences."""
    if not xs:
        return 0.0
    return sum(xs) / len(xs)


def _sample_std(xs: list[float]) -> float:
    """Sample standard deviation (N-1 denominator); returns 0.0 for n < 2."""
    n = len(xs)
    if n < 2:
        return 0.0
    m = sum(xs) / n
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1))


def _bar_returns(equity: list[float]) -> list[float]:
    """Fractional bar-to-bar returns from an equity curve."""
    result: list[float] = []
    for i in range(1, len(equity)):
        prev = equity[i - 1]
        if prev != 0.0:
            result.append((equity[i] - prev) / prev)
        else:
            result.append(0.0)
    return result


# ---------------------------------------------------------------------------
# Equity-curve metrics
# ---------------------------------------------------------------------------

def sharpe(
    equity: list[float],
    annualisation_factor: float = math.sqrt(252.0),
) -> float:
    """Annualised Sharpe ratio.

    ``A * mean(bar_returns) / std(bar_returns)``

    Returns ``0.0`` if ``std == 0`` or ``len(equity) < 2``.
    """
    if len(equity) < 2:
        return 0.0
    rets = _bar_returns(equity)
    if not rets:
        return 0.0
    std = _sample_std(rets)
    if std == 0.0:
        return 0.0
    return annualisation_factor * _mean(rets) / std


def sortino(
    equity: list[float],
    annualisation_factor: float = math.sqrt(252.0),
) -> float:
    """Annualised Sortino ratio.

    Downside deviation uses the full sample size ``n`` for the denominator
    and is demeaned at 0 (not at the mean):

        ``downside_std = sqrt(sum(min(r, 0)^2) / (n-1))``

    Returns ``0.0`` if the downside std is 0.
    """
    if len(equity) < 2:
        return 0.0
    rets = _bar_returns(equity)
    n = len(rets)
    if n < 2:
        return 0.0
    neg_sq_sum = sum(min(r, 0.0) ** 2 for r in rets)
    if neg_sq_sum == 0.0:
        return 0.0
    downside_std = math.sqrt(neg_sq_sum / (n - 1))
    if downside_std == 0.0:
        return 0.0
    return annualisation_factor * _mean(rets) / downside_std


def calmar(
    equity: list[float],
    total_return_pct: float | None = None,
) -> float:
    """Calmar ratio.

    ``total_return_pct / max_drawdown_pct``

    If *total_return_pct* is ``None`` it is computed from the equity curve as
    ``(equity[-1] - equity[0]) / equity[0] * 100``.

    Returns ``0.0`` if ``max_drawdown_pct == 0``.
    """
    if not equity:
        return 0.0
    mdd = max_drawdown_pct(equity)
    if mdd == 0.0:
        return 0.0
    if total_return_pct is None:
        if equity[0] == 0.0:
            return 0.0
        total_return_pct = (equity[-1] - equity[0]) / equity[0] * 100.0
    return total_return_pct / abs(mdd)


def omega(equity: list[float], threshold: float = 0.0) -> float:
    """Omega ratio.

    ``sum(max(r - threshold, 0)) / sum(max(threshold - r, 0))``

    Returns ``0.0`` if the denominator is 0.
    """
    if len(equity) < 2:
        return 0.0
    rets = _bar_returns(equity)
    gains = sum(max(r - threshold, 0.0) for r in rets)
    losses = sum(max(threshold - r, 0.0) for r in rets)
    if losses == 0.0:
        return 0.0
    return gains / losses


# ---------------------------------------------------------------------------
# Trade-based metrics
# ---------------------------------------------------------------------------

def profit_factor(trades: list[TradeReturn]) -> float:
    """Profit factor.

    ``sum(winning pnls) / abs(sum(losing pnls))``

    Returns ``math.inf`` if there are no losing trades, ``0.0`` if no winners.
    """
    if not trades:
        return 0.0
    gross_win = sum(t.net_pnl for t in trades if t.net_pnl > 0.0)
    gross_loss = sum(t.net_pnl for t in trades if t.net_pnl < 0.0)
    if gross_win == 0.0:
        return 0.0
    if gross_loss == 0.0:
        return math.inf
    return gross_win / abs(gross_loss)


def win_rate(trades: list[TradeReturn]) -> float:
    """Fraction of trades with ``net_pnl > 0``."""
    if not trades:
        return 0.0
    return sum(1 for t in trades if t.net_pnl > 0.0) / len(trades)


def expectancy(trades: list[TradeReturn]) -> float:
    """Mean net PnL per trade."""
    if not trades:
        return 0.0
    return sum(t.net_pnl for t in trades) / len(trades)


def avg_win(trades: list[TradeReturn]) -> float:
    """Average net PnL of winning trades; ``0.0`` if no winners."""
    winners = [t.net_pnl for t in trades if t.net_pnl > 0.0]
    if not winners:
        return 0.0
    return sum(winners) / len(winners)


def avg_loss(trades: list[TradeReturn]) -> float:
    """Average net PnL of losing trades (returned as negative); ``0.0`` if no losers."""
    losers = [t.net_pnl for t in trades if t.net_pnl < 0.0]
    if not losers:
        return 0.0
    return sum(losers) / len(losers)


def payoff_ratio(trades: list[TradeReturn]) -> float:
    """Ratio of average win to absolute average loss.

    ``avg_win / abs(avg_loss)``

    Returns ``math.inf`` if there are no losing trades, ``0.0`` if no winners.
    """
    aw = avg_win(trades)
    al = avg_loss(trades)
    if aw == 0.0:
        return 0.0
    if al == 0.0:
        return math.inf
    return aw / abs(al)


def recovery_factor(
    trades: list[TradeReturn],
    equity: list[float],
) -> float:
    """Recovery factor.

    ``total_pnl / max_drawdown_abs``

    Returns ``0.0`` if ``max_drawdown_abs == 0``.
    """
    if not trades or not equity:
        return 0.0
    total_pnl = sum(t.net_pnl for t in trades)
    mdd_abs = max_drawdown_abs(equity)
    if mdd_abs == 0.0:
        return 0.0
    return total_pnl / abs(mdd_abs)


def time_in_market(
    trades: list[TradeReturn],
    total_bars: int,
) -> float:
    """Fraction of backtest bars spent in a position.

    ``sum(bars_held) / total_bars``
    """
    if total_bars <= 0 or not trades:
        return 0.0
    return sum(t.bars_held for t in trades) / total_bars


def longest_win_streak(trades: list[TradeReturn]) -> int:
    """Longest consecutive run of trades with ``net_pnl > 0``."""
    if not trades:
        return 0
    best = 0
    current = 0
    for t in trades:
        if t.net_pnl > 0.0:
            current += 1
            if current > best:
                best = current
        else:
            current = 0
    return best


def longest_loss_streak(trades: list[TradeReturn]) -> int:
    """Longest consecutive run of trades with ``net_pnl <= 0``."""
    if not trades:
        return 0
    best = 0
    current = 0
    for t in trades:
        if t.net_pnl <= 0.0:
            current += 1
            if current > best:
                best = current
        else:
            current = 0
    return best


# Alias per spec
max_consec_losses = longest_loss_streak


# ---------------------------------------------------------------------------
# Composite result builder
# ---------------------------------------------------------------------------

def compute_metrics(
    trades: list[TradeReturn],
    equity: list[float],
    total_bars: int | None = None,
    annualisation_factor: float = math.sqrt(252.0),
) -> MetricsResult:
    """Compute all 14 metrics and return a :class:`MetricsResult`.

    Parameters
    ----------
    trades:
        List of completed trades.
    equity:
        Equity curve (one point per bar, or one point per trade close —
        caller's choice; used for drawdown, Sharpe, Sortino, Calmar, Omega).
    total_bars:
        Total number of bars in the backtest window; used for
        ``time_in_market``.  If ``None``, ``len(equity) - 1`` is used.
    annualisation_factor:
        ``sqrt(252)`` by default.
    """
    if total_bars is None:
        total_bars = max(len(equity) - 1, 0)

    return MetricsResult(
        sharpe=sharpe(equity, annualisation_factor),
        sortino=sortino(equity, annualisation_factor),
        calmar=calmar(equity),
        omega=omega(equity),
        profit_factor=profit_factor(trades),
        win_rate=win_rate(trades),
        expectancy=expectancy(trades),
        avg_win=avg_win(trades),
        avg_loss=avg_loss(trades),
        payoff_ratio=payoff_ratio(trades),
        recovery_factor=recovery_factor(trades, equity),
        time_in_market=time_in_market(trades, total_bars),
        longest_win_streak=longest_win_streak(trades),
        longest_loss_streak=longest_loss_streak(trades),
    )
