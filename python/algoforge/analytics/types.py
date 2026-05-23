"""Shared dataclasses for the analytics layer.

All types are plain dataclasses; no I/O, no state.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class TradeReturn:
    """A single trade result with net PnL and metadata."""

    net_pnl: float
    bars_held: int = 0


@dataclass
class EquityPoint:
    """A single point on an equity curve."""

    timestamp: int   # unix seconds
    equity: float


@dataclass
class DrawdownEpisode:
    """Contiguous drawdown below a prior peak.

    Attributes
    ----------
    start_idx:
        Index of the first bar below the prior peak.
    trough_idx:
        Index of the bar with the deepest drawdown within this episode.
    recover_idx:
        Index of the first bar where equity recovers to at least the
        pre-episode peak.  ``None`` if the series ends before recovery.
    depth_pct:
        Drawdown depth as a percentage (negative value).
    depth_abs:
        Drawdown depth in absolute equity units (negative value).
    duration_bars:
        Number of bars from ``start_idx`` to ``recover_idx - 1``, or to
        the end of the series if not recovered.
    recovery_bars:
        Number of bars from ``trough_idx`` to ``recover_idx - 1``.
        ``None`` if not recovered.
    """

    start_idx: int
    trough_idx: int
    recover_idx: Optional[int]
    depth_pct: float
    depth_abs: float
    duration_bars: int
    recovery_bars: Optional[int]


@dataclass
class MetricsResult:
    """All 14 performance metrics computed over a set of trades / equity curve."""

    sharpe: float = 0.0
    sortino: float = 0.0
    calmar: float = 0.0
    omega: float = 0.0
    profit_factor: float = 0.0
    win_rate: float = 0.0
    expectancy: float = 0.0
    avg_win: float = 0.0
    avg_loss: float = 0.0
    payoff_ratio: float = 0.0
    recovery_factor: float = 0.0
    time_in_market: float = 0.0
    longest_win_streak: int = 0
    longest_loss_streak: int = 0

    @property
    def max_consec_losses(self) -> int:
        """Alias for ``longest_loss_streak``."""
        return self.longest_loss_streak


@dataclass
class DistributionStats:
    """Descriptive statistics for a numeric series."""

    n: int = 0
    mean: float = 0.0
    std: float = 0.0
    min: float = 0.0
    max: float = 0.0
    skewness: float = 0.0        # Fisher-Pearson adjusted G1
    excess_kurtosis: float = 0.0  # Fisher adjusted G2
    p25: float = 0.0
    p50: float = 0.0
    p75: float = 0.0


@dataclass
class CorrelationMatrix:
    """Symmetric Pearson correlation matrix for a set of return series."""

    symbols: list[str] = field(default_factory=list)
    matrix: list[list[float]] = field(default_factory=list)
