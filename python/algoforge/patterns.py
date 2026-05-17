"""Candlestick pattern detectors — same math as the cpp/ reference.

Each `is_*` function returns +1 (bullish), -1 (bearish), or 0 (no match).
Bars with range == 0 never match.

Defaults match the cpp/ reference:
    doji                doji_pct      = 0.10
    marubozu            body_min_pct  = 0.90
    pin_bar             min_wick_mult = 2.0
    morning_star        body > range*0.60; mid body < first_body*0.30
    evening_star        mirror of morning_star
    three_white_soldiers body_pct >= 0.60; upper_shadow < body*0.30
    three_black_crows   body_pct >= 0.60; lower_shadow < body*0.30
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


def is_morning_star(b0: Bar, b1: Bar, b2: Bar) -> int:
    """Return +1 if bars form a Morning Star pattern, else 0.

    b0: bearish with large body (body > range*0.60).
    b1: small body relative to b0 (body < b0.body*0.30).
    b2: bullish closing above midpoint of b0's body.
    Matches cpp/ candlestick_patterns.cpp MorningStar rule.
    """
    bear_first = (not b0.is_bullish) and b0.body > b0.range * 0.60
    small_mid  = b1.body < b0.body * 0.30
    bull_last  = b2.is_bullish and b2.close > (b0.open + b0.close) / 2.0
    return 1 if (bear_first and small_mid and bull_last) else 0


def is_evening_star(b0: Bar, b1: Bar, b2: Bar) -> int:
    """Return -1 if bars form an Evening Star pattern, else 0.

    b0: bullish with large body (body > range*0.60).
    b1: small body relative to b0 (body < b0.body*0.30).
    b2: bearish closing below midpoint of b0's body.
    Matches cpp/ candlestick_patterns.cpp EveningStar rule.
    """
    bull_first = b0.is_bullish and b0.body > b0.range * 0.60
    small_mid  = b1.body < b0.body * 0.30
    bear_last  = (not b2.is_bullish) and b2.close < (b0.open + b0.close) / 2.0
    return -1 if (bull_first and small_mid and bear_last) else 0


def is_three_white_soldiers(b0: Bar, b1: Bar, b2: Bar) -> int:
    """Return +1 if bars form Three White Soldiers, else 0.

    All three bars bullish with body_pct >= 0.60 and upper_shadow < body*0.30;
    each close higher than the previous close.
    Matches cpp/ candlestick_patterns.cpp ThreeWhiteSoldiers rule.
    """
    def _ok(bar: Bar) -> bool:
        if bar.range < EPSILON:
            return False
        body_pct   = bar.body / bar.range
        top        = bar.close if bar.close > bar.open else bar.open
        upper_shad = bar.high - top
        return bar.is_bullish and body_pct >= 0.60 and upper_shad < bar.body * 0.30

    higher = b2.close > b1.close and b1.close > b0.close
    return 1 if (_ok(b0) and _ok(b1) and _ok(b2) and higher) else 0


def is_three_black_crows(b0: Bar, b1: Bar, b2: Bar) -> int:
    """Return -1 if bars form Three Black Crows, else 0.

    All three bars bearish with body_pct >= 0.60 and lower_shadow < body*0.30;
    each close lower than the previous close.
    Matches cpp/ candlestick_patterns.cpp ThreeBlackCrows rule.
    """
    def _ok(bar: Bar) -> bool:
        if bar.range < EPSILON:
            return False
        body_pct   = bar.body / bar.range
        bot        = bar.close if bar.close < bar.open else bar.open
        lower_shad = bot - bar.low
        return (not bar.is_bullish) and body_pct >= 0.60 and lower_shad < bar.body * 0.30

    lower = b2.close < b1.close and b1.close < b0.close
    return -1 if (_ok(b0) and _ok(b1) and _ok(b2) and lower) else 0


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
        if i >= 2:
            b0, b1 = bars[i - 2], bars[i - 1]
            if (s := is_morning_star(b0, b1, b)):
                out.append(PatternMatch(i, "morning_star", s))
            if (s := is_evening_star(b0, b1, b)):
                out.append(PatternMatch(i, "evening_star", s))
            if (s := is_three_white_soldiers(b0, b1, b)):
                out.append(PatternMatch(i, "three_white_soldiers", s))
            if (s := is_three_black_crows(b0, b1, b)):
                out.append(PatternMatch(i, "three_black_crows", s))
    return out
