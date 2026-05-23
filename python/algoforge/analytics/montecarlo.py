"""
Canonical LCG(seed=42) first 10 outputs (uint32):
  [1220265334, 484179026, 886563538, 1353769503, 1460606294, 56326156, 46730969, 327394710, 1017823166, 53256125]

Monte Carlo bootstrap resampling of trade returns using a seeded LCG RNG.

This module is the canonical authority for the LCG sequence.  All other
language implementations (C, C++, JS) must produce the identical uint32
sequence when given the same seed.

LCG specification (must match across all languages):
    state = seed  (uint64, treat as unsigned)
    next():
        state = (state * 6364136223846793005 + 1442695040888963407) mod 2^64
        return state >> 33                          (uint32 output)
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

__all__ = [
    "SeededLCG",
    "pointwise_percentile",
    "mc_bootstrap",
]

# ---------------------------------------------------------------------------
# RNG
# ---------------------------------------------------------------------------

_UINT64_MASK = 0xFFFFFFFFFFFFFFFF
_LCG_MUL = 6364136223846793005
_LCG_ADD = 1442695040888963407


class SeededLCG:
    """Seeded 64-bit LCG whose output is state >> 33 (uint32)."""

    __slots__ = ("_state",)

    def __init__(self, seed: int) -> None:
        # Mask to uint64 so the behaviour is identical across platforms and
        # matches C/C++/JS unsigned 64-bit arithmetic.
        self._state: int = int(seed) & _UINT64_MASK

    def next(self) -> int:
        """Advance the RNG and return the next uint32 output."""
        self._state = (self._state * _LCG_MUL + _LCG_ADD) & _UINT64_MASK
        return self._state >> 33


# ---------------------------------------------------------------------------
# Percentile helpers
# ---------------------------------------------------------------------------

def _percentile_1d(values: list[float], pct: float) -> float:
    """Linear-interpolation percentile (numpy 'linear' / R-7 method).

    pct is in [0, 100].
    """
    n = len(values)
    if n == 0:
        raise ValueError("Cannot compute percentile of an empty sequence")
    if n == 1:
        return float(values[0])
    sorted_vals = sorted(values)
    # Map pct in [0,100] to index in [0, n-1]
    index = (pct / 100.0) * (n - 1)
    lo = int(index)
    hi = lo + 1
    if hi >= n:
        return float(sorted_vals[-1])
    frac = index - lo
    return float(sorted_vals[lo]) + frac * float(sorted_vals[hi] - sorted_vals[lo])


def pointwise_percentile(paths: list[list[float]], pct: float) -> list[float]:
    """Return the pct-th percentile at each time step across all paths.

    Args:
        paths: List of equity paths, each of the same length.
        pct:   Percentile in [0, 100].

    Returns:
        List of length equal to path length; element i is the pct-th
        percentile of paths[0][i], paths[1][i], ..., paths[n-1][i].
    """
    if not paths:
        return []
    path_len = len(paths[0])
    result: list[float] = []
    for step in range(path_len):
        column = [paths[run][step] for run in range(len(paths))]
        result.append(_percentile_1d(column, pct))
    return result


# ---------------------------------------------------------------------------
# Monte Carlo result dataclass
# ---------------------------------------------------------------------------

@dataclass
class MCResult:
    """Result returned by mc_bootstrap."""
    p5_final: float
    p50_final: float
    p95_final: float
    p5_path: list[float] = field(default_factory=list)
    p50_path: list[float] = field(default_factory=list)
    p95_path: list[float] = field(default_factory=list)
    prob_of_ruin: float = 0.0


# ---------------------------------------------------------------------------
# Bootstrap
# ---------------------------------------------------------------------------

def mc_bootstrap(
    trades: list[Any],
    n_runs: int,
    seed: int,
    initial_capital: float = 10_000.0,
) -> dict[str, Any]:
    """Bootstrap Monte Carlo simulation over trade PnL returns.

    Args:
        trades:          List of trade objects.  Each object must expose a
                         ``net_pnl`` attribute (float), or be a plain float/int
                         (interpreted directly as net PnL).
        n_runs:          Number of bootstrap runs.
        seed:            Integer seed for the SeededLCG.
        initial_capital: Starting equity for each simulation path.

    Returns:
        Dictionary with keys:
            p5_final, p50_final, p95_final   -- scalar percentiles of final equity
            p5_path, p50_path, p95_path      -- per-step path percentile lists
            prob_of_ruin                     -- fraction of runs whose equity
                                               touched <= 0 at any point
    """
    if not trades:
        empty_path = [float(initial_capital)]
        return {
            "p5_final": float(initial_capital),
            "p50_final": float(initial_capital),
            "p95_final": float(initial_capital),
            "p5_path": empty_path,
            "p50_path": empty_path,
            "p95_path": empty_path,
            "prob_of_ruin": 0.0,
        }

    # Extract net PnL values once
    pnl_values: list[float] = []
    for t in trades:
        if hasattr(t, "net_pnl"):
            pnl_values.append(float(t.net_pnl))
        else:
            pnl_values.append(float(t))

    n_trades = len(pnl_values)
    rng = SeededLCG(seed)
    final_eq: list[float] = []
    paths: list[list[float]] = []
    ruin_count = 0

    for _ in range(n_runs):
        # Resample with replacement using the LCG
        resample = [pnl_values[rng.next() % n_trades] for _ in range(n_trades)]
        eq = float(initial_capital)
        path: list[float] = [eq]
        ruined = eq <= 0.0
        for pnl in resample:
            eq += pnl
            path.append(eq)
            if eq <= 0.0:
                ruined = True
        final_eq.append(eq)
        paths.append(path)
        if ruined:
            ruin_count += 1

    return {
        "p5_final": _percentile_1d(final_eq, 5.0),
        "p50_final": _percentile_1d(final_eq, 50.0),
        "p95_final": _percentile_1d(final_eq, 95.0),
        "p5_path": pointwise_percentile(paths, 5.0),
        "p50_path": pointwise_percentile(paths, 50.0),
        "p95_path": pointwise_percentile(paths, 95.0),
        "prob_of_ruin": ruin_count / n_runs,
    }
