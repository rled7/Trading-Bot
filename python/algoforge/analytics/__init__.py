"""AlgoForge analytics package (S4).

Re-exports the public API of each submodule.
"""

from __future__ import annotations

# Types
from algoforge.analytics.types import (  # noqa: F401
    CorrelationMatrix,
    DrawdownEpisode,
    DistributionStats,
    EquityPoint,
    MetricsResult,
    TradeReturn,
)

# Metrics
from algoforge.analytics.metrics import (  # noqa: F401
    avg_loss,
    avg_win,
    calmar,
    compute_metrics,
    expectancy,
    longest_loss_streak,
    longest_win_streak,
    max_consec_losses,
    omega,
    payoff_ratio,
    profit_factor,
    recovery_factor,
    sharpe,
    sortino,
    time_in_market,
    win_rate,
)

# Drawdown
from algoforge.analytics.drawdown import (  # noqa: F401
    drawdown_episodes,
    drawdown_series,
    max_drawdown_abs,
    max_drawdown_pct,
    top5_drawdowns,
)

# Distributions
from algoforge.analytics.distributions import (  # noqa: F401
    distribution_stats,
    excess_kurtosis,
    quantile,
    skewness,
)

# Correlations
from algoforge.analytics.correlations import (  # noqa: F401
    correlation_matrix,
    pearson,
    rolling_beta,
)

# Streaming
from algoforge.analytics.streaming import OnlineMetrics  # noqa: F401

# HTML report
from algoforge.analytics.html_report import (  # noqa: F401
    render_report,
    REPORT_CSS,
    CHART_INIT_JS_TEMPLATE,
)

__all__ = [
    # types
    "TradeReturn",
    "EquityPoint",
    "DrawdownEpisode",
    "MetricsResult",
    "DistributionStats",
    "CorrelationMatrix",
    # metrics
    "sharpe",
    "sortino",
    "calmar",
    "omega",
    "profit_factor",
    "win_rate",
    "expectancy",
    "avg_win",
    "avg_loss",
    "payoff_ratio",
    "recovery_factor",
    "time_in_market",
    "longest_win_streak",
    "longest_loss_streak",
    "max_consec_losses",
    "compute_metrics",
    # drawdown
    "drawdown_series",
    "max_drawdown_pct",
    "max_drawdown_abs",
    "drawdown_episodes",
    "top5_drawdowns",
    # distributions
    "skewness",
    "excess_kurtosis",
    "quantile",
    "distribution_stats",
    # correlations
    "pearson",
    "correlation_matrix",
    "rolling_beta",
    # streaming
    "OnlineMetrics",
    # html report
    "render_report",
    "REPORT_CSS",
    "CHART_INIT_JS_TEMPLATE",
]
