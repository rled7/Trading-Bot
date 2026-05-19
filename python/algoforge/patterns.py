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


def _max_range(bars: Sequence[Bar], from_idx: int, to_idx: int) -> float:
    """Return highest high in bars[from_idx..to_idx] inclusive."""
    mx = bars[from_idx].high
    for i in range(from_idx + 1, to_idx + 1):
        if bars[i].high > mx:
            mx = bars[i].high
    return mx


def _min_range(bars: Sequence[Bar], from_idx: int, to_idx: int) -> float:
    """Return lowest low in bars[from_idx..to_idx] inclusive."""
    mn = bars[from_idx].low
    for i in range(from_idx + 1, to_idx + 1):
        if bars[i].low < mn:
            mn = bars[i].low
    return mn


def is_double_top(bars: Sequence[Bar]) -> int:
    """Return -1 if the latest window contains a Double Top pattern, else 0.

    Requires len(bars) >= 30.
    Matches cpp/ chart_patterns.cpp DoubleTop rule:
      - hi1 = max high in first half [0, mid-1]
      - hi2 = max high in second half [mid, n-1]
      - lo  = min low over entire window
      - |hi1 - hi2| <= tol (tol = (hi1+hi2)*0.005)
      - lo < hi1 * 0.99
      - last bar close < (hi1 + lo) / 2
    """
    n = len(bars)
    if n < 30:
        return 0
    mid = n // 2
    hi1 = _max_range(bars, 0, mid - 1)
    lo  = _min_range(bars, 0, n - 1)
    hi2 = _max_range(bars, mid, n - 1)
    tol = (hi1 + hi2) * 0.005
    if (abs(hi1 - hi2) <= tol and lo < hi1 * 0.99
            and bars[n - 1].close < (hi1 + lo) / 2.0):
        return -1
    return 0


def is_double_bottom(bars: Sequence[Bar]) -> int:
    """Return +1 if the latest window contains a Double Bottom pattern, else 0.

    Requires len(bars) >= 30.
    Matches cpp/ chart_patterns.cpp DoubleBottom rule:
      - lo1 = min low in first half [0, mid-1]
      - hi  = max high over entire window
      - lo2 = min low in second half [mid, n-1]
      - |lo1 - lo2| <= tol (tol = (lo1+lo2)*0.005)
      - hi > lo1 * 1.01
      - last bar close > (lo1 + hi) / 2
    """
    n = len(bars)
    if n < 30:
        return 0
    mid = n // 2
    lo1 = _min_range(bars, 0, mid - 1)
    hi  = _max_range(bars, 0, n - 1)
    lo2 = _min_range(bars, mid, n - 1)
    tol = (lo1 + lo2) * 0.005
    if (abs(lo1 - lo2) <= tol and hi > lo1 * 1.01
            and bars[n - 1].close > (lo1 + hi) / 2.0):
        return 1
    return 0


def is_ascending_triangle(bars: Sequence[Bar]) -> int:
    """Return +1 if the latest 20-bar window shows an Ascending Triangle, else 0.

    Requires len(bars) >= 20.
    Looks at the last 20 bars:
      - resistance = max high over [n-20, n-1]
      - lo_first   = min low over [n-20, n-11] (first half)
      - lo_last    = min low over [n-10, n-1]  (second half)
      - hi_var     = max high of first half - max high of second half
      - lo_last > lo_first * 1.002  (rising lows)
      - |hi_var| < resistance * 0.005  (flat resistance)
    Matches cpp/ chart_patterns.cpp AscendingTriangle rule.
    """
    n = len(bars)
    if n < 20:
        return 0
    resistance = _max_range(bars, n - 20, n - 1)
    lo_first   = _min_range(bars, n - 20, n - 11)
    lo_last    = _min_range(bars, n - 10, n - 1)
    hi_var     = _max_range(bars, n - 20, n - 11) - _max_range(bars, n - 10, n - 1)
    if lo_last > lo_first * 1.002 and abs(hi_var) < resistance * 0.005:
        return 1
    return 0


def is_descending_triangle(bars: Sequence[Bar]) -> int:
    """Return -1 if the latest 20-bar window shows a Descending Triangle, else 0.

    Requires len(bars) >= 20.
    Looks at the last 20 bars:
      - support   = min low over [n-20, n-1]
      - hi_first  = max high over [n-20, n-11] (first half)
      - hi_last   = max high over [n-10, n-1]  (second half)
      - lo_var    = min low of first half - min low of second half
      - hi_last < hi_first * 0.998  (falling highs)
      - |lo_var| < support * 0.005  (flat support)
    Matches cpp/ chart_patterns.cpp DescendingTriangle rule.
    """
    n = len(bars)
    if n < 20:
        return 0
    support  = _min_range(bars, n - 20, n - 1)
    hi_first = _max_range(bars, n - 20, n - 11)
    hi_last  = _max_range(bars, n - 10, n - 1)
    lo_var   = _min_range(bars, n - 20, n - 11) - _min_range(bars, n - 10, n - 1)
    if hi_last < hi_first * 0.998 and abs(lo_var) < support * 0.005:
        return -1
    return 0


def is_bullish_flag(bars: Sequence[Bar]) -> int:
    """Return +1 if the latest 20-bar window shows a Bullish Flag, else 0.

    Requires len(bars) >= 20.
    Looks at the last 20 bars:
      - pole: first half [n-20, n-11]
      - flag: second half [n-10, n-1]
      - pole_bull  = b[n-11].close > b[n-20].close  (upward pole)
      - tight_flag = flag_rng < pole_rng * 0.45
      - pole_rng   > pole_lo * 0.01
    Matches cpp/ chart_patterns.cpp BullishFlag rule.
    """
    n = len(bars)
    if n < 20:
        return 0
    pole_lo  = _min_range(bars, n - 20, n - 11)
    pole_hi  = _max_range(bars, n - 20, n - 11)
    pole_rng = pole_hi - pole_lo
    flag_hi  = _max_range(bars, n - 10, n - 1)
    flag_lo  = _min_range(bars, n - 10, n - 1)
    flag_rng = flag_hi - flag_lo
    pole_bull  = bars[n - 11].close > bars[n - 20].close
    tight_flag = flag_rng < pole_rng * 0.45
    if pole_bull and tight_flag and pole_rng > pole_lo * 0.01:
        return 1
    return 0


def is_bearish_flag(bars: Sequence[Bar]) -> int:
    """Return -1 if the latest 20-bar window shows a Bearish Flag, else 0.

    Requires len(bars) >= 20.
    Looks at the last 20 bars:
      - pole: first half [n-20, n-11]
      - flag: second half [n-10, n-1]
      - pole_bear  = b[n-11].close < b[n-20].close  (downward pole)
      - tight_flag = flag_rng < pole_rng * 0.45
      - pole_rng   > pole_lo * 0.01
    Matches cpp/ chart_patterns.cpp BearishFlag rule.
    """
    n = len(bars)
    if n < 20:
        return 0
    pole_lo  = _min_range(bars, n - 20, n - 11)
    pole_hi  = _max_range(bars, n - 20, n - 11)
    pole_rng = pole_hi - pole_lo
    flag_rng = _max_range(bars, n - 10, n - 1) - _min_range(bars, n - 10, n - 1)
    pole_bear  = bars[n - 11].close < bars[n - 20].close
    tight_flag = flag_rng < pole_rng * 0.45
    if pole_bear and tight_flag and pole_rng > pole_lo * 0.01:
        return -1
    return 0


def is_head_and_shoulders(bars: Sequence[Bar]) -> int:
    """Return -1 if the latest 40-bar window shows a Head and Shoulders, else 0.

    Requires len(bars) >= 40.
    Uses the last 40 bars; let s = n - 40:
      - ls       = max high in [s, s+12]      (left shoulder)
      - h        = max high in [s+13, s+26]   (head)
      - rs       = max high in [s+27, n-1]    (right shoulder)
      - neckline = min(min_low[s..s+12], min_low[s+27..n-1])
      - h > ls*1.01 and h > rs*1.01
      - |ls - rs| / ((ls + rs) / 2) < 0.07   (shoulders nearly equal)
      - bars[n-1].close < neckline
    Matches cpp/ chart_patterns.cpp HeadAndShoulders rule.
    """
    n = len(bars)
    if n < 40:
        return 0
    s = n - 40
    ls = _max_range(bars, s,      s + 12)
    h  = _max_range(bars, s + 13, s + 26)
    rs = _max_range(bars, s + 27, n - 1)
    neckline = min(_min_range(bars, s, s + 12), _min_range(bars, s + 27, n - 1))
    if (h > ls * 1.01 and h > rs * 1.01
            and abs(ls - rs) / (ls + rs) * 2 < 0.07
            and bars[n - 1].close < neckline):
        return -1
    return 0


def is_inverse_head_and_shoulders(bars: Sequence[Bar]) -> int:
    """Return +1 if the latest 40-bar window shows an Inverse Head and Shoulders, else 0.

    Requires len(bars) >= 40.
    Uses the last 40 bars; let s = n - 40:
      - ls       = min low in [s, s+12]       (left shoulder)
      - h        = min low in [s+13, s+26]    (head — lowest trough)
      - rs       = min low in [s+27, n-1]     (right shoulder)
      - neckline = max(max_high[s..s+12], max_high[s+27..n-1])
      - h < ls*0.99 and h < rs*0.99
      - |ls - rs| / ((ls + rs) / 2) < 0.07   (shoulders nearly equal)
      - bars[n-1].close > neckline
    Matches cpp/ chart_patterns.cpp InverseHeadAndShoulders rule.
    """
    n = len(bars)
    if n < 40:
        return 0
    s = n - 40
    ls = _min_range(bars, s,      s + 12)
    h  = _min_range(bars, s + 13, s + 26)
    rs = _min_range(bars, s + 27, n - 1)
    neckline = max(_max_range(bars, s, s + 12), _max_range(bars, s + 27, n - 1))
    if (h < ls * 0.99 and h < rs * 0.99
            and abs(ls - rs) / (ls + rs) * 2 < 0.07
            and bars[n - 1].close > neckline):
        return 1
    return 0


def _fib_ratio(a: float, b: float, target: float, tol: float) -> bool:
    if abs(a) < 1e-12:
        return False
    return abs(b / a - target) <= tol


def is_gartley_bull(bars: Sequence[Bar]) -> int:
    n = len(bars)
    if n < 50:
        return 0
    x  = _min_range(bars, n - 50, n - 40)
    a  = _max_range(bars, n - 40, n - 30)
    bb = _min_range(bars, n - 30, n - 20)
    cc = _max_range(bars, n - 20, n - 10)
    d  = _min_range(bars, n - 10, n - 1)
    xa = a - x;  ab = a - bb;  bc = cc - bb;  cd = cc - d
    if (xa > 1e-6 and bc > 1e-6
            and _fib_ratio(xa, ab, 0.618, 0.05)
            and (_fib_ratio(bc, cd, 1.272, 0.07) or _fib_ratio(bc, cd, 1.618, 0.07))):
        return 1
    return 0


def is_gartley_bear(bars: Sequence[Bar]) -> int:
    n = len(bars)
    if n < 50:
        return 0
    x  = _max_range(bars, n - 50, n - 40)
    a  = _min_range(bars, n - 40, n - 30)
    bb = _max_range(bars, n - 30, n - 20)
    cc = _min_range(bars, n - 20, n - 10)
    d  = _max_range(bars, n - 10, n - 1)
    xa = x - a;  ab = bb - a;  bc = bb - cc;  cd = d - cc
    if (xa > 1e-6 and bc > 1e-6
            and _fib_ratio(xa, ab, 0.618, 0.05)
            and (_fib_ratio(bc, cd, 1.272, 0.07) or _fib_ratio(bc, cd, 1.618, 0.07))):
        return -1
    return 0


def is_bat_bull(bars: Sequence[Bar]) -> int:
    n = len(bars)
    if n < 50:
        return 0
    x  = _min_range(bars, n - 50, n - 40)
    a  = _max_range(bars, n - 40, n - 30)
    d  = _min_range(bars, n - 10, n - 1)
    xa = a - x;  xd = a - d
    if xa > 1e-6 and _fib_ratio(xa, xd, 0.886, 0.05):
        return 1
    return 0


def is_butterfly_bull(bars: Sequence[Bar]) -> int:
    n = len(bars)
    if n < 50:
        return 0
    x  = _min_range(bars, n - 50, n - 40)
    a  = _max_range(bars, n - 40, n - 30)
    d  = _min_range(bars, n - 10, n - 1)
    xa = a - x;  xd = a - d
    if xa > 1e-6 and _fib_ratio(xa, xd, 1.272, 0.07):
        return 1
    return 0


def is_crab_bull(bars: Sequence[Bar]) -> int:
    n = len(bars)
    if n < 50:
        return 0
    x  = _min_range(bars, n - 50, n - 40)
    a  = _max_range(bars, n - 40, n - 30)
    d  = _min_range(bars, n - 10, n - 1)
    xa = a - x;  xd = a - d
    if xa > 1e-6 and _fib_ratio(xa, xd, 1.618, 0.07):
        return 1
    return 0


@dataclass
class PatternMatch:
    bar_index: int
    name:      str
    signal:    int   # +1 / -1


def scan_patterns(bars: Sequence[Bar]) -> list[PatternMatch]:
    out: list[PatternMatch] = []
    n = len(bars)
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
    # Chart pattern detectors look at the full bar sequence up to each point.
    # They are only emitted at bar_index = len(bars) - 1 (latest bar).
    last = n - 1
    if n >= 20:
        if (s := is_ascending_triangle(bars)):
            out.append(PatternMatch(last, "ascending_triangle", s))
        if (s := is_descending_triangle(bars)):
            out.append(PatternMatch(last, "descending_triangle", s))
    if n >= 30:
        if (s := is_double_top(bars)):
            out.append(PatternMatch(last, "double_top", s))
        if (s := is_double_bottom(bars)):
            out.append(PatternMatch(last, "double_bottom", s))
    if n >= 20:
        if (s := is_bullish_flag(bars)):
            out.append(PatternMatch(last, "bullish_flag", s))
        if (s := is_bearish_flag(bars)):
            out.append(PatternMatch(last, "bearish_flag", s))
    if n >= 40:
        if (s := is_head_and_shoulders(bars)):
            out.append(PatternMatch(last, "head_and_shoulders", s))
        if (s := is_inverse_head_and_shoulders(bars)):
            out.append(PatternMatch(last, "inverse_head_and_shoulders", s))
    if n >= 50:
        if (s := is_gartley_bull(bars)):
            out.append(PatternMatch(last, "gartley_bull", s))
        if (s := is_gartley_bear(bars)):
            out.append(PatternMatch(last, "gartley_bear", s))
        if (s := is_bat_bull(bars)):
            out.append(PatternMatch(last, "bat_bull", s))
        if (s := is_butterfly_bull(bars)):
            out.append(PatternMatch(last, "butterfly_bull", s))
        if (s := is_crab_bull(bars)):
            out.append(PatternMatch(last, "crab_bull", s))
    return out
