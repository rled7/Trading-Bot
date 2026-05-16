"""Candlestick pattern detectors — same math as the cpp/ reference.

Each `is_*` function returns +1 (bullish), -1 (bearish), or 0 (no match).
Bars with range == 0 never match.

Defaults match the cpp/ reference:
    doji      doji_pct      = 0.10
    marubozu  body_min_pct  = 0.90
    pin_bar   min_wick_mult = 2.0
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence

from .types import Bar

EPSILON = 1e-12


def _geom(o: float, h: float, l: float, c: float):
    body  = abs(c - o)
    rng   = h - l
    top   = c if c > o else o
    bot   = c if c < o else o
    upper = h - top
    lower = bot - l
    return body, rng, upper, lower, c > o


def is_doji(bar: Bar, doji_pct: float = 0.10) -> int:
    body, rng, *_ = _geom(bar.open, bar.high, bar.low, bar.close)
    if rng < EPSILON:
        return 0
    return 1 if body / rng <= doji_pct else 0


def is_hammer(bar: Bar) -> int:
    body, rng, upper, lower, _ = _geom(bar.open, bar.high, bar.low, bar.close)
    if rng < EPSILON:
        return 0
    lo_ratio = lower / rng
    hi_ratio = upper / rng
    if lo_ratio >= 0.60 and hi_ratio <= 0.20:
        return 1
    if hi_ratio >= 0.60 and lo_ratio <= 0.20:
        return -1
    return 0


def is_engulfing(prev: Bar, curr: Bar) -> int:
    prev_bull = prev.close > prev.open
    curr_bull = curr.close > curr.open
    if curr_bull and not prev_bull and curr.open < prev.close and curr.close > prev.open:
        return 1
    if not curr_bull and prev_bull and curr.open > prev.close and curr.close < prev.open:
        return -1
    return 0


def is_marubozu(bar: Bar, body_min_pct: float = 0.90) -> int:
    body, rng, upper, lower, bull = _geom(bar.open, bar.high, bar.low, bar.close)
    if rng < EPSILON:
        return 0
    if body / rng < body_min_pct:
        return 0
    shadow_max = rng * 0.05
    if upper < shadow_max and lower < shadow_max:
        return 1 if bull else -1
    return 0


def is_pin_bar(bar: Bar, min_wick_mult: float = 2.0) -> int:
    body, rng, upper, lower, _ = _geom(bar.open, bar.high, bar.low, bar.close)
    if rng < EPSILON:
        return 0
    # Tiny-body fix from cpp/: treat very small bodies as 3% of range.
    effective_body = body if body > rng * 0.03 else rng * 0.03
    lwm = lower / effective_body
    uwm = upper / effective_body
    opp_max = rng * 0.25
    if lwm >= min_wick_mult and upper <= opp_max:
        return 1
    if uwm >= min_wick_mult and lower <= opp_max:
        return -1
    return 0


@dataclass
class PatternMatch:
    bar_index: int
    name:      str
    signal:    int   # +1 / -1


def scan_patterns(bars: Sequence[Bar]) -> list[PatternMatch]:
    out: list[PatternMatch] = []
    for i, b in enumerate(bars):
        if (s := is_doji(b)):     out.append(PatternMatch(i, "doji",     s))
        if (s := is_hammer(b)):   out.append(PatternMatch(i, "hammer",   s))
        if (s := is_marubozu(b)): out.append(PatternMatch(i, "marubozu", s))
        if (s := is_pin_bar(b)):  out.append(PatternMatch(i, "pin_bar",  s))
        if i > 0:
            if (s := is_engulfing(bars[i - 1], b)):
                out.append(PatternMatch(i, "engulfing", s))
    return out
