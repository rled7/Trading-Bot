from __future__ import annotations

"""OLS linear regression and ML attribution for the analytics layer.

No external dependencies; matrix algebra is delegated to linalg.py.
"""

import math
from dataclasses import dataclass, field

from algoforge.analytics.linalg import (
    inverse,
    matmul,
    transpose,
)


@dataclass
class OLSResult:
    """Result of an OLS regression.

    Attributes
    ----------
    beta:
        Coefficient vector, length k (includes intercept at index 0 when
        add_intercept=True).
    residuals:
        y - y_hat, length n.
    r2:
        Coefficient of determination.
    adj_r2:
        Adjusted R².
    t_stats:
        Per-coefficient t-statistics, length k.
    std_errors:
        Per-coefficient standard errors, length k.
    n:
        Number of observations.
    k:
        Number of parameters (columns in X after optional intercept column
        is prepended).
    """

    beta: list[float]
    residuals: list[float]
    r2: float
    adj_r2: float
    t_stats: list[float]
    std_errors: list[float]
    n: int
    k: int


def ols(
    X: list[list[float]],
    y: list[float],
    *,
    add_intercept: bool = True,
) -> OLSResult:
    """Fit an OLS regression y ~ X (optionally prepending a column of ones).

    Parameters
    ----------
    X:
        n×p feature matrix (rows = observations, cols = features).
        If add_intercept is True a column of 1s is prepended, making it n×(p+1).
    y:
        Length-n response vector.
    add_intercept:
        When True (default) a constant column is prepended to X.

    Returns
    -------
    OLSResult

    Raises
    ------
    ValueError
        If X^T X is singular (multicollinearity or too few observations).
    """
    n = len(y)
    if n == 0:
        raise ValueError("y must be non-empty.")

    # Prepend intercept column of 1s if requested.
    if add_intercept:
        X_mat: list[list[float]] = [[1.0] + list(row) for row in X]
    else:
        X_mat = [list(row) for row in X]

    k = len(X_mat[0])  # number of parameters

    # Column vector y as n×1 matrix.
    y_col: list[list[float]] = [[yi] for yi in y]

    # beta = (X^T X)^{-1} X^T y
    Xt = transpose(X_mat)           # k×n
    XtX = matmul(Xt, X_mat)         # k×k
    XtX_inv = inverse(XtX)          # k×k  (raises ValueError if singular)
    Xty = matmul(Xt, y_col)         # k×1
    beta_col = matmul(XtX_inv, Xty) # k×1
    beta: list[float] = [row[0] for row in beta_col]

    # Fitted values and residuals.
    y_hat: list[float] = [
        sum(X_mat[i][j] * beta[j] for j in range(k)) for i in range(n)
    ]
    residuals: list[float] = [y[i] - y_hat[i] for i in range(n)]

    # R² statistics.
    mean_y = sum(y) / n
    ss_res = sum(r ** 2 for r in residuals)
    ss_tot = sum((yi - mean_y) ** 2 for yi in y)
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0.0 else 0.0
    adj_r2 = (
        1.0 - (1.0 - r2) * (n - 1) / (n - k)
        if n > k
        else float("-inf")
    )

    # Variance of the residuals (unbiased: divide by n-k).
    sigma_sq = ss_res / (n - k) if n > k else 0.0

    # Variance of beta coefficients: var_beta = sigma_sq * (X^T X)^{-1}
    std_errors: list[float] = [
        math.sqrt(sigma_sq * XtX_inv[j][j]) if sigma_sq * XtX_inv[j][j] >= 0.0 else 0.0
        for j in range(k)
    ]
    t_stats: list[float] = [
        beta[j] / std_errors[j] if std_errors[j] > 0.0 else float("inf")
        for j in range(k)
    ]

    return OLSResult(
        beta=beta,
        residuals=residuals,
        r2=r2,
        adj_r2=adj_r2,
        t_stats=t_stats,
        std_errors=std_errors,
        n=n,
        k=k,
    )


def attribute(
    features_by_name: dict[str, list[float]],
    trade_pnls: list[float],
) -> dict[str, object]:
    """Attribute trade PnL to named features via OLS.

    Parameters
    ----------
    features_by_name:
        Mapping of feature name -> length-n list of feature values at each
        trade entry.
    trade_pnls:
        Length-n list of trade net-PnL values (the dependent variable y).

    Returns
    -------
    dict with keys:

    * ``"ols_result"`` – the full :class:`OLSResult`
    * ``"feature_importance"`` – list of ``(feature_name, t_stat)`` tuples
      sorted by ``abs(t_stat)`` descending (intercept excluded).
    * ``"r2"`` – R² from the regression.
    * ``"adj_r2"`` – adjusted R².
    """
    feature_names = list(features_by_name.keys())
    n = len(trade_pnls)

    # Build X as n×p matrix (one column per feature, ordered by name insertion).
    X: list[list[float]] = [[features_by_name[name][i] for name in feature_names] for i in range(n)]

    result = ols(X, trade_pnls, add_intercept=True)

    # beta[0] is the intercept; beta[1..] correspond to feature_names.
    feature_t_stats: list[tuple[str, float]] = [
        (name, result.t_stats[j + 1]) for j, name in enumerate(feature_names)
    ]
    feature_t_stats.sort(key=lambda pair: abs(pair[1]), reverse=True)

    return {
        "ols_result": result,
        "feature_importance": feature_t_stats,
        "r2": result.r2,
        "adj_r2": result.adj_r2,
    }
