"""Correlation functions: Pearson, correlation matrix, rolling beta.

All functions are pure — no I/O, no state.
"""

from __future__ import annotations

import math

from .types import CorrelationMatrix


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _mean(xs: list[float]) -> float:
    if not xs:
        return 0.0
    return sum(xs) / len(xs)


# ---------------------------------------------------------------------------
# Public functions
# ---------------------------------------------------------------------------

def pearson(x: list[float], y: list[float]) -> float:
    """Pearson correlation coefficient between two equal-length series.

    .. code-block::

        r = sum((x-mx)*(y-my)) / sqrt(sum((x-mx)^2) * sum((y-my)^2))

    Returns ``0.0`` if either series has zero variance, or if the series are
    empty / of different lengths.
    """
    n = len(x)
    if n == 0 or n != len(y):
        return 0.0
    mx = _mean(x)
    my = _mean(y)
    cov = sum((xi - mx) * (yi - my) for xi, yi in zip(x, y))
    var_x = sum((xi - mx) ** 2 for xi in x)
    var_y = sum((yi - my) ** 2 for yi in y)
    denom = var_x * var_y
    if denom <= 0.0:
        return 0.0
    return cov / math.sqrt(denom)


def correlation_matrix(
    returns_per_symbol: dict[str, list[float]],
) -> CorrelationMatrix:
    """Build a symmetric Pearson correlation matrix.

    Series are truncated to the length of the shortest series before
    computing correlations.

    Parameters
    ----------
    returns_per_symbol:
        Mapping of symbol name to its return series.

    Returns
    -------
    CorrelationMatrix
        ``symbols`` lists symbol names in insertion order; ``matrix[i][j]``
        is the Pearson correlation between ``symbols[i]`` and ``symbols[j]``.
    """
    symbols = list(returns_per_symbol.keys())
    k = len(symbols)
    if k == 0:
        return CorrelationMatrix(symbols=[], matrix=[])

    # Truncate to shortest series length
    min_len = min(len(v) for v in returns_per_symbol.values())
    series = [returns_per_symbol[s][:min_len] for s in symbols]

    matrix: list[list[float]] = []
    for i in range(k):
        row: list[float] = []
        for j in range(k):
            if i == j:
                row.append(1.0)
            elif j < i:
                # reuse already-computed value for symmetry
                row.append(matrix[j][i])
            else:
                row.append(pearson(series[i], series[j]))
        matrix.append(row)

    return CorrelationMatrix(symbols=symbols, matrix=matrix)


def rolling_beta(
    target: list[float],
    baseline: list[float],
    window: int,
) -> list[float]:
    """Rolling beta of *target* vs *baseline*.

    Beta is defined as ``cov(target, baseline) / var(baseline)`` over a
    sliding window of size *window*.

    Parameters
    ----------
    target:
        Return series for the strategy / asset being evaluated.
    baseline:
        Return series for the benchmark.
    window:
        Look-back window in bars.  Must be ``>= 2``.

    Returns
    -------
    list[float]
        Beta values; one per bar starting from index ``window - 1``.
        Returns an empty list if inputs are too short or *window* < 2.
    """
    n = min(len(target), len(baseline))
    if n < window or window < 2:
        return []

    result: list[float] = []
    for end in range(window, n + 1):
        start = end - window
        t_window = target[start:end]
        b_window = baseline[start:end]
        mt = _mean(t_window)
        mb = _mean(b_window)
        cov_tb = sum((t_window[i] - mt) * (b_window[i] - mb) for i in range(window))
        var_b = sum((b_window[i] - mb) ** 2 for i in range(window))
        if var_b == 0.0:
            result.append(0.0)
        else:
            result.append(cov_tb / var_b)

    return result
