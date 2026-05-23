"""Distribution statistics: skewness, excess kurtosis, quantiles.

Formulas follow spec §4 exactly:

* Skewness: Fisher-Pearson adjusted G1.
* Excess kurtosis: Fisher's definition, adjusted G2.
* Quantiles: linear interpolation (R-7 / numpy ``method='linear'``).

All functions are pure — no I/O, no state.
"""

from __future__ import annotations

import math

from .types import DistributionStats


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _mean(xs: list[float]) -> float:
    if not xs:
        return 0.0
    return sum(xs) / len(xs)


def _biased_std(xs: list[float], mean: float) -> float:
    """Population (biased) standard deviation."""
    n = len(xs)
    if n == 0:
        return 0.0
    variance = sum((x - mean) ** 2 for x in xs) / n
    return math.sqrt(variance)


def _sample_std(xs: list[float], mean: float) -> float:
    """Sample (unbiased) standard deviation (N-1 denominator)."""
    n = len(xs)
    if n < 2:
        return 0.0
    variance = sum((x - mean) ** 2 for x in xs) / (n - 1)
    return math.sqrt(variance)


# ---------------------------------------------------------------------------
# Public functions
# ---------------------------------------------------------------------------

def skewness(xs: list[float]) -> float:
    """Fisher-Pearson adjusted skewness (G1).

    .. code-block::

        g1 = (1/n) * sum((x-mean)^3) / std_biased^3
        G1 = sqrt(n*(n-1)) / (n-2) * g1

    Returns ``0.0`` for ``n < 4``.
    """
    n = len(xs)
    if n < 4:
        return 0.0
    m = _mean(xs)
    s_b = _biased_std(xs, m)
    if s_b == 0.0:
        return 0.0
    g1 = (sum((x - m) ** 3 for x in xs) / n) / (s_b ** 3)
    return math.sqrt(n * (n - 1)) / (n - 2) * g1


def excess_kurtosis(xs: list[float]) -> float:
    """Fisher's excess kurtosis, adjusted for sample size (G2).

    .. code-block::

        g2 = (1/n) * sum((x-mean)^4) / std_biased^4 - 3
        G2 = ((n-1) / ((n-2)*(n-3))) * ((n+1)*g2 + 6)

    Returns ``0.0`` for ``n < 4``.
    """
    n = len(xs)
    if n < 4:
        return 0.0
    m = _mean(xs)
    s_b = _biased_std(xs, m)
    if s_b == 0.0:
        return 0.0
    g2 = (sum((x - m) ** 4 for x in xs) / n) / (s_b ** 4) - 3.0
    return ((n - 1) / ((n - 2) * (n - 3))) * ((n + 1) * g2 + 6.0)


def quantile(xs: list[float], p: float) -> float:
    """Quantile via linear interpolation (R-7 / numpy ``method='linear'``).

    Parameters
    ----------
    xs:
        Input data (need not be sorted).
    p:
        Probability in [0, 1].

    Returns
    -------
    float
        The *p*-th quantile.  Returns ``0.0`` for an empty sequence.
    """
    if not xs:
        return 0.0
    if len(xs) == 1:
        return xs[0]
    p = max(0.0, min(1.0, p))
    sorted_xs = sorted(xs)
    n = len(sorted_xs)
    # R-7 formula: h = (n-1)*p
    h = (n - 1) * p
    lo = int(math.floor(h))
    hi = int(math.ceil(h))
    if lo == hi:
        return sorted_xs[lo]
    frac = h - lo
    return sorted_xs[lo] + frac * (sorted_xs[hi] - sorted_xs[lo])


def distribution_stats(xs: list[float]) -> DistributionStats:
    """Compute all distribution statistics for *xs*.

    Returns
    -------
    DistributionStats
        Contains: n, mean, std (sample), min, max, skewness (G1),
        excess_kurtosis (G2), p25, p50, p75.
    """
    n = len(xs)
    if n == 0:
        return DistributionStats()
    m = _mean(xs)
    std = _sample_std(xs, m)
    return DistributionStats(
        n=n,
        mean=m,
        std=std,
        min=min(xs),
        max=max(xs),
        skewness=skewness(xs),
        excess_kurtosis=excess_kurtosis(xs),
        p25=quantile(xs, 0.25),
        p50=quantile(xs, 0.50),
        p75=quantile(xs, 0.75),
    )
