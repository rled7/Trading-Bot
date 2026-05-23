from __future__ import annotations

"""Fama-French-style multi-factor OLS regression.

Reuses the OLS solver from ml_attribution.py — that is the only
intra-package import permitted here.
"""

from dataclasses import dataclass

from algoforge.analytics.ml_attribution import OLSResult, ols


@dataclass
class FactorResult:
    """Result of a multi-factor OLS regression.

    Attributes
    ----------
    alpha:
        Annualised intercept (constant return not explained by factors).
        Corresponds to beta[0] from the underlying OLS (the intercept column).
    betas:
        Mapping of factor name -> loading (slope coefficient).
    alpha_t_stat:
        t-statistic for alpha (tests whether alpha is significantly != 0).
    beta_t_stats:
        Mapping of factor name -> t-statistic for that factor's loading.
    r2:
        Coefficient of determination of the factor model.
    adj_r2:
        Adjusted R².
    """

    alpha: float
    betas: dict[str, float]
    alpha_t_stat: float
    beta_t_stats: dict[str, float]
    r2: float
    adj_r2: float


def factor_regression(
    strategy_returns: list[float],
    factor_returns_by_name: dict[str, list[float]],
) -> FactorResult:
    """Fit the multi-factor model via OLS.

    The model is::

        strategy_returns[t] = alpha
                              + sum_f(beta_f * factor_returns_f[t])
                              + epsilon[t]

    An intercept column is always prepended by the underlying OLS call
    (add_intercept=True), so ``alpha = beta[0]``.

    Parameters
    ----------
    strategy_returns:
        Length-n series of strategy bar returns.
    factor_returns_by_name:
        Mapping of factor name -> length-n factor return series.  All series
        must have the same length as strategy_returns.

    Returns
    -------
    FactorResult

    Raises
    ------
    ValueError
        If factor matrix is singular (factors are collinear or n < k).
    """
    factor_names = list(factor_returns_by_name.keys())
    n = len(strategy_returns)

    # Build X as n×p matrix (one column per factor, column order = insertion order).
    X: list[list[float]] = [
        [factor_returns_by_name[name][i] for name in factor_names]
        for i in range(n)
    ]

    result: OLSResult = ols(X, strategy_returns, add_intercept=True)

    # beta[0] = alpha (intercept); beta[1..] = factor loadings.
    alpha: float = result.beta[0]
    betas: dict[str, float] = {
        name: result.beta[j + 1] for j, name in enumerate(factor_names)
    }

    alpha_t_stat: float = result.t_stats[0]
    beta_t_stats: dict[str, float] = {
        name: result.t_stats[j + 1] for j, name in enumerate(factor_names)
    }

    return FactorResult(
        alpha=alpha,
        betas=betas,
        alpha_t_stat=alpha_t_stat,
        beta_t_stats=beta_t_stats,
        r2=result.r2,
        adj_r2=result.adj_r2,
    )
