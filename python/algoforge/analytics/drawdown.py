"""Drawdown analysis functions.

All functions are pure — no I/O, no state.
"""

from __future__ import annotations

from typing import Optional

from .types import DrawdownEpisode


def drawdown_series(
    equity: list[float],
) -> tuple[list[float], list[float]]:
    """Return ``(dd_pct, dd_abs)`` series for an equity curve.

    Parameters
    ----------
    equity:
        Equity values, strictly ordered in time.

    Returns
    -------
    dd_pct:
        Percentage drawdown at each index: ``(eq[i] - peak[i]) / peak[i] * 100``.
        Values are ``<= 0``.
    dd_abs:
        Absolute drawdown at each index: ``eq[i] - peak[i]``.  Values are ``<= 0``.
    """
    if not equity:
        return [], []

    dd_pct: list[float] = []
    dd_abs: list[float] = []
    peak = equity[0]

    for eq in equity:
        if eq > peak:
            peak = eq
        if peak == 0.0:
            dd_pct.append(0.0)
        else:
            dd_pct.append((eq - peak) / peak * 100.0)
        dd_abs.append(eq - peak)

    return dd_pct, dd_abs


def max_drawdown_pct(equity: list[float]) -> float:
    """Maximum percentage drawdown (most negative value, ``<= 0``)."""
    if len(equity) < 2:
        return 0.0
    dd_pct, _ = drawdown_series(equity)
    return min(dd_pct)


def max_drawdown_abs(equity: list[float]) -> float:
    """Maximum absolute drawdown (most negative value, ``<= 0``)."""
    if len(equity) < 2:
        return 0.0
    _, dd_abs = drawdown_series(equity)
    return min(dd_abs)


def drawdown_episodes(equity: list[float]) -> list[DrawdownEpisode]:
    """Identify contiguous drawdown episodes in an equity curve.

    An episode begins at the first bar where ``dd_pct < 0`` and ends at the
    first bar where ``dd_pct == 0`` (equity has recovered to or above the
    prior peak).  If the series ends while still in drawdown the episode
    has ``recover_idx = None`` and ``recovery_bars = None``.

    Parameters
    ----------
    equity:
        Equity curve values, ordered in time.

    Returns
    -------
    list[DrawdownEpisode]:
        One entry per contiguous drawdown run, ordered by ``start_idx``.
    """
    if len(equity) < 2:
        return []

    dd_pct, dd_abs = drawdown_series(equity)
    n = len(equity)
    episodes: list[DrawdownEpisode] = []

    in_dd = False
    ep_start = -1
    ep_trough = -1
    ep_trough_depth_pct = 0.0
    ep_trough_depth_abs = 0.0

    for i in range(n):
        if dd_pct[i] < 0.0:
            if not in_dd:
                in_dd = True
                ep_start = i
                ep_trough = i
                ep_trough_depth_pct = dd_pct[i]
                ep_trough_depth_abs = dd_abs[i]
            else:
                if dd_pct[i] < ep_trough_depth_pct:
                    ep_trough = i
                    ep_trough_depth_pct = dd_pct[i]
                    ep_trough_depth_abs = dd_abs[i]
        else:
            if in_dd:
                recover_idx: Optional[int] = i
                recovery_bars: Optional[int] = i - ep_trough
                duration_bars = i - ep_start
                episodes.append(DrawdownEpisode(
                    start_idx=ep_start,
                    trough_idx=ep_trough,
                    recover_idx=recover_idx,
                    depth_pct=ep_trough_depth_pct,
                    depth_abs=ep_trough_depth_abs,
                    duration_bars=duration_bars,
                    recovery_bars=recovery_bars,
                ))
                in_dd = False

    # Episode running to end of series without recovery
    if in_dd:
        duration_bars = n - 1 - ep_start
        episodes.append(DrawdownEpisode(
            start_idx=ep_start,
            trough_idx=ep_trough,
            recover_idx=None,
            depth_pct=ep_trough_depth_pct,
            depth_abs=ep_trough_depth_abs,
            duration_bars=duration_bars,
            recovery_bars=None,
        ))

    return episodes


def top5_drawdowns(equity: list[float]) -> list[DrawdownEpisode]:
    """Return up to 5 drawdown episodes sorted by depth (deepest first).

    Parameters
    ----------
    equity:
        Equity curve values, ordered in time.

    Returns
    -------
    list[DrawdownEpisode]:
        Up to 5 episodes, sorted by ``depth_pct`` ascending (most negative
        first).
    """
    episodes = drawdown_episodes(equity)
    sorted_eps = sorted(episodes, key=lambda ep: ep.depth_pct)
    return sorted_eps[:5]
