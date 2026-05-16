"""SMA / EMA / RSI / ATR — same math as the cpp/ reference.

API:
    sma(values, period) -> list[float | None]
    ema(values, period) -> list[float | None]
    rsi(values, period) -> list[float | None]
    atr(highs, lows, closes, period) -> list[float | None]

Indices where the indicator is not yet defined come back as None.
Raises ValueError for invalid args (period <= 0, mismatched lengths, etc.).
"""

from __future__ import annotations

from typing import Sequence


def _check_period(period: int) -> None:
    if not isinstance(period, int) or period <= 0:
        raise ValueError(f"period must be a positive integer, got {period!r}")


def sma(values: Sequence[float], period: int) -> list[float | None]:
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    if n < period:
        return out
    window = sum(values[:period])
    out[period - 1] = window / period
    for i in range(period, n):
        window += values[i] - values[i - period]
        out[i] = window / period
    return out


def ema(values: Sequence[float], period: int) -> list[float | None]:
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    if n < period:
        return out
    alpha = 2.0 / (period + 1)
    seed = sum(values[:period]) / period
    out[period - 1] = seed
    prev = seed
    for i in range(period, n):
        prev = values[i] * alpha + prev * (1.0 - alpha)
        out[i] = prev
    return out


def rsi(values: Sequence[float], period: int) -> list[float | None]:
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    if n <= period:
        return out

    alpha = 1.0 / period
    ag = al = 0.0
    for i in range(1, period + 1):
        d = values[i] - values[i - 1]
        if d > 0:
            ag += d
        else:
            al += -d
    ag /= period
    al /= period

    for i in range(period, n):
        if i > period:
            d = values[i] - values[i - 1]
            u = d if d > 0 else 0.0
            v = -d if d < 0 else 0.0
            ag = ag * (1.0 - alpha) + u * alpha
            al = al * (1.0 - alpha) + v * alpha
        rs = ag / al if al > 1e-12 else 100.0
        out[i] = 100.0 - 100.0 / (1.0 + rs)
    return out


def atr(highs: Sequence[float], lows: Sequence[float],
        closes: Sequence[float], period: int) -> list[float | None]:
    _check_period(period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    out: list[float | None] = [None] * n
    if n <= period:
        return out

    tr = [0.0] * n
    tr[0] = highs[0] - lows[0]
    for i in range(1, n):
        prev_close = closes[i - 1]
        tr[i] = max(
            highs[i] - lows[i],
            abs(highs[i] - prev_close),
            abs(lows[i]  - prev_close),
        )

    alpha = 1.0 / period
    seed = sum(tr[:period]) / period
    out[period - 1] = seed
    prev = seed
    for i in range(period, n):
        prev = tr[i] * alpha + prev * (1.0 - alpha)
        out[i] = prev
    return out
