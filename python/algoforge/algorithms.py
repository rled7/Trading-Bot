"""Concrete algorithm implementations + AlgorithmRegistry.

Mirrors the C++ concrete algorithms (TrendFollower, MeanReversion,
BreakoutTrader, SwingTrader) and their registry.

All algorithms receive:
    symbol  : str
    tf      : int          (timeframe in seconds — use Timeframe enum values)
    bars    : list[Bar]    (full history up to and including current bar)
    count   : int          (index of the current bar; bars[count] is current)
    ind     : EngineResult (pre-computed standard indicators at bars[count])
    pat     : PatternEngine

Each algorithm computes any additional indicators it needs directly from
bars[0..count] using the indicator functions in this package.
"""

from __future__ import annotations

from statistics import mean
from typing import Optional

from . import indicators as _ind_mod
from .algorithm import AlgoDecision, AlgoSignal, IAlgorithm
from .indicators import EngineResult
from .patterns import PatternEngine
from .types import Bar, Direction, Timeframe

# ── Timeframe constants (seconds) used by SwingTrader ────────────────────────
_TF_H4: int = int(Timeframe.H4)   # 14400
_TF_D1: int = int(Timeframe.D1)   # 86400


# ── Helpers ───────────────────────────────────────────────────────────────────

def _closes(bars: list[Bar], count: int) -> list[float]:
    return [b.close for b in bars[: count + 1]]


def _highs(bars: list[Bar], count: int) -> list[float]:
    return [b.high for b in bars[: count + 1]]


def _lows(bars: list[Bar], count: int) -> list[float]:
    return [b.low for b in bars[: count + 1]]


def _last(lst: list) -> Optional[float]:
    """Return last element or None if list is empty or last element is None."""
    if not lst:
        return None
    v = lst[-1]
    return v  # may already be None


# ══════════════════════════════════════════════════════════════════════════════
# 1. TrendFollower — EMA crossover with ADX filter
# ══════════════════════════════════════════════════════════════════════════════

class TrendFollower(IAlgorithm):
    """EMA crossover strategy filtered by ADX trend strength.

    Parameters
    ----------
    fast_ema : int
        Period of the fast EMA (default 21).
    slow_ema : int
        Period of the slow EMA (default 50).
    adx_min : float
        Minimum ADX required to trade (default 22.0).
    magic_number : int
        Unique identifier for this instance (default 1001).
    """

    def __init__(
        self,
        fast_ema: int = 21,
        slow_ema: int = 50,
        adx_min: float = 22.0,
        magic_number: int = 1001,
    ) -> None:
        self._fast_ema    = fast_ema
        self._slow_ema    = slow_ema
        self._adx_min     = adx_min
        self._magic       = magic_number

    # ── IAlgorithm interface ──────────────────────────────────────────────────

    def name(self) -> str:
        return f"TrendFollower_EMA{self._fast_ema}/{self._slow_ema}"

    def description(self) -> str:
        return (
            f"EMA crossover ({self._fast_ema}/{self._slow_ema}) "
            f"with ADX>{self._adx_min} filter and EMA200 trend confirmation."
        )

    def magic(self) -> int:
        return self._magic

    def evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: EngineResult,
        pat: PatternEngine,
    ) -> AlgoDecision:
        closes = _closes(bars, count)
        highs  = _highs(bars, count)
        lows   = _lows(bars, count)

        price = closes[-1]

        # Compute required EMAs
        ema_fast_vals = _ind_mod.ema(closes, self._fast_ema)
        ema_slow_vals = _ind_mod.ema(closes, self._slow_ema)
        ema200_vals   = _ind_mod.ema(closes, 200)

        ema_fast = _last(ema_fast_vals)
        ema_slow = _last(ema_slow_vals)
        ema200   = _last(ema200_vals)

        if ema_fast is None or ema_slow is None or ema200 is None:
            return AlgoDecision.none(symbol)

        # ADX (period=14): value, +DI, -DI
        adx_vals, pdi_vals, mdi_vals = _ind_mod.adx(highs, lows, closes, 14)
        adx_v = _last(adx_vals)
        pdi   = _last(pdi_vals)
        mdi   = _last(mdi_vals)

        if adx_v is None or pdi is None or mdi is None:
            return AlgoDecision.none(symbol)

        # RSI (period=14)
        rsi_vals = _ind_mod.rsi(closes, 14)
        rsi_v    = _last(rsi_vals)
        if rsi_v is None:
            return AlgoDecision.none(symbol)

        # ADX filter
        if adx_v < self._adx_min:
            return AlgoDecision.none(symbol)

        confidence = min(0.90, 0.60 + adx_v / 100.0 * 0.30)
        atr_v      = ind.atr_value or 0.0

        # Bullish: fast > slow AND price > EMA200 AND +DI > -DI
        if ema_fast > ema_slow and price > ema200 and pdi > mdi:
            if rsi_v < 75.0:
                return AlgoDecision(
                    signal     = AlgoSignal.BUY,
                    symbol     = symbol,
                    direction  = Direction.LONG,
                    confidence = confidence,
                    reason     = f"EMA cross up ADX={int(adx_v)}",
                    atr        = atr_v,
                )
            return AlgoDecision.none(symbol)

        # Bearish: fast < slow AND price < EMA200 AND -DI > +DI
        if ema_fast < ema_slow and price < ema200 and mdi > pdi:
            if rsi_v > 25.0:
                return AlgoDecision(
                    signal     = AlgoSignal.SELL,
                    symbol     = symbol,
                    direction  = Direction.SHORT,
                    confidence = confidence,
                    reason     = f"EMA cross down ADX={int(adx_v)}",
                    atr        = atr_v,
                )
            return AlgoDecision.none(symbol)

        return AlgoDecision.none(symbol)


# ══════════════════════════════════════════════════════════════════════════════
# 2. MeanReversion — RSI extremes + Bollinger Band touch
# ══════════════════════════════════════════════════════════════════════════════

class MeanReversion(IAlgorithm):
    """Mean-reversion strategy using RSI and Bollinger Bands.

    Parameters
    ----------
    rsi_os : float
        RSI oversold threshold (default 30.0).
    rsi_ob : float
        RSI overbought threshold (default 70.0).
    adx_max : float
        Maximum ADX allowed (trending market filter; default 25.0).
    """

    def __init__(
        self,
        rsi_os: float = 30.0,
        rsi_ob: float = 70.0,
        adx_max: float = 25.0,
    ) -> None:
        self._rsi_os  = rsi_os
        self._rsi_ob  = rsi_ob
        self._adx_max = adx_max

    # ── IAlgorithm interface ──────────────────────────────────────────────────

    def name(self) -> str:
        return "MeanReversion_RSI_BB"

    def description(self) -> str:
        return (
            f"RSI oversold/overbought ({self._rsi_os}/{self._rsi_ob}) "
            f"confirmed by Bollinger Band touch, filtered when ADX>{self._adx_max}."
        )

    def magic(self) -> int:
        return 1002

    def evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: EngineResult,
        pat: PatternEngine,
    ) -> AlgoDecision:
        closes = _closes(bars, count)
        highs  = _highs(bars, count)
        lows   = _lows(bars, count)

        price = closes[-1]

        # RSI (period=14)
        rsi_vals = _ind_mod.rsi(closes, 14)
        rsi_v    = _last(rsi_vals)
        if rsi_v is None:
            return AlgoDecision.none(symbol)

        # Bollinger Bands (period=20, mult=2)
        bb_upper_vals, _, bb_lower_vals = _ind_mod.bollinger(closes, 20, 2.0)
        bb_upper = _last(bb_upper_vals)
        bb_lower = _last(bb_lower_vals)
        if bb_upper is None or bb_lower is None:
            return AlgoDecision.none(symbol)

        # ADX (period=14)
        adx_vals, _, _ = _ind_mod.adx(highs, lows, closes, 14)
        adx_v = _last(adx_vals)
        if adx_v is None:
            return AlgoDecision.none(symbol)

        # Squeeze detection: BB width as a fraction of price (proxy)
        # "squeeze" extra field — we check BB width relative to price; treat
        # squeeze as true when (upper - lower) / price < 0.5% (tight bands).
        bb_width = bb_upper - bb_lower
        squeeze  = (bb_width / price) < 0.005 if price > 1e-10 else False

        # ADX filter: trending market or squeeze → skip
        if adx_v > self._adx_max or squeeze:
            return AlgoDecision.none(symbol)

        atr_v = ind.atr_value or 0.0

        # Oversold: RSI at or below oversold level AND price at or below lower BB
        if rsi_v <= self._rsi_os and price <= bb_lower * 1.002:
            conf = min(0.85, 0.55 + (self._rsi_os - rsi_v) / 30.0 * 0.30)
            return AlgoDecision(
                signal     = AlgoSignal.BUY,
                symbol     = symbol,
                direction  = Direction.LONG,
                confidence = conf,
                reason     = f"Oversold RSI={int(rsi_v)} at lower BB",
                atr        = atr_v,
            )

        # Overbought: RSI at or above overbought level AND price at or above upper BB
        if rsi_v >= self._rsi_ob and price >= bb_upper * 0.998:
            conf = min(0.85, 0.55 + (rsi_v - self._rsi_ob) / 30.0 * 0.30)
            return AlgoDecision(
                signal     = AlgoSignal.SELL,
                symbol     = symbol,
                direction  = Direction.SHORT,
                confidence = conf,
                reason     = f"Overbought RSI={int(rsi_v)} at upper BB",
                atr        = atr_v,
            )

        return AlgoDecision.none(symbol)


# ══════════════════════════════════════════════════════════════════════════════
# 3. BreakoutTrader — Donchian Channel breakout with volume + ATR confirmation
# ══════════════════════════════════════════════════════════════════════════════

class BreakoutTrader(IAlgorithm):
    """Donchian Channel breakout with volume surge and ATR expansion filters.

    Parameters
    ----------
    dc_period : int
        Donchian Channel lookback period (default 20).
    vol_mult : float
        Volume surge multiplier vs. 20-bar average (default 1.5).
    """

    def __init__(
        self,
        dc_period: int = 20,
        vol_mult: float = 1.5,
    ) -> None:
        self._dc_period = dc_period
        self._vol_mult  = vol_mult

    # ── IAlgorithm interface ──────────────────────────────────────────────────

    def name(self) -> str:
        return "Breakout_Donchian"

    def description(self) -> str:
        return (
            f"Donchian Channel ({self._dc_period}) breakout confirmed by "
            f"volume surge (>{self._vol_mult}x avg) and ATR expansion."
        )

    def magic(self) -> int:
        return 1003

    def evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: EngineResult,
        pat: PatternEngine,
    ) -> AlgoDecision:
        sub    = bars[: count + 1]
        closes = [b.close  for b in sub]
        highs  = [b.high   for b in sub]
        lows   = [b.low    for b in sub]

        price = closes[-1]
        n     = len(sub)

        # Need at least dc_period bars
        if n < self._dc_period:
            return AlgoDecision.none(symbol)

        # Donchian Channels (period=20): upper, _, lower
        dc_upper_vals, _, dc_lower_vals = _ind_mod.donchian(highs, lows, self._dc_period)
        dc_upper = _last(dc_upper_vals)
        dc_lower = _last(dc_lower_vals)
        if dc_upper is None or dc_lower is None:
            return AlgoDecision.none(symbol)

        # MACD (12, 26, 9): histogram is value3
        _, _, macd_hist_vals = _ind_mod.macd(closes, 12, 26, 9)
        macd_hist = _last(macd_hist_vals)
        if macd_hist is None:
            return AlgoDecision.none(symbol)

        # Volume surge: compare last bar vs. 20-bar average
        window = self._dc_period
        if n < window:
            return AlgoDecision.none(symbol)

        recent_bars = sub[-window:]
        avg_vol = mean(b.volume for b in recent_bars)
        cur_vol = sub[-1].volume
        vol_surge = cur_vol > avg_vol * self._vol_mult

        # ATR expansion: current range vs. 20-bar average range
        range_avg = mean(b.high - b.low for b in recent_bars)
        cur_range = sub[-1].high - sub[-1].low
        atr_expand = cur_range > range_avg * 1.1

        # Confidence formula
        conf = min(
            0.88,
            0.60
            + (0.15 if vol_surge  else 0.0)
            + (0.10 if atr_expand else 0.0),
        )

        atr_v = ind.atr_value or 0.0

        # BUY: price at or above upper channel
        if price >= dc_upper and vol_surge and atr_expand and macd_hist > 0:
            vol_pct = int(cur_vol / avg_vol * 100) if avg_vol > 1e-10 else 0
            return AlgoDecision(
                signal     = AlgoSignal.BUY,
                symbol     = symbol,
                direction  = Direction.LONG,
                confidence = conf,
                reason     = f"DC breakout up vol={vol_pct}%",
                atr        = atr_v,
            )

        # SELL: price at or below lower channel
        if price <= dc_lower and vol_surge and atr_expand and macd_hist < 0:
            vol_pct = int(cur_vol / avg_vol * 100) if avg_vol > 1e-10 else 0
            return AlgoDecision(
                signal     = AlgoSignal.SELL,
                symbol     = symbol,
                direction  = Direction.SHORT,
                confidence = conf,
                reason     = f"DC breakout down vol={vol_pct}%",
                atr        = atr_v,
            )

        return AlgoDecision.none(symbol)


# ══════════════════════════════════════════════════════════════════════════════
# 4. SwingTrader — Pullback to EMA21 in H4 / D1 timeframes
# ══════════════════════════════════════════════════════════════════════════════

class SwingTrader(IAlgorithm):
    """Pullback-to-EMA21 swing trade strategy; only fires on H4 or D1 charts."""

    def name(self) -> str:
        return "SwingTrader_PullbackEMA"

    def description(self) -> str:
        return (
            "Pullback-to-EMA21 swing entry on H4/D1 timeframes, confirmed by "
            "EMA trend alignment (21>50>200 or 21<50<200) and a candlestick pattern."
        )

    def magic(self) -> int:
        return 1004

    def evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: EngineResult,
        pat: PatternEngine,
    ) -> AlgoDecision:
        # Only fire on H4 or D1
        if tf not in (_TF_H4, _TF_D1):
            return AlgoDecision.none(symbol)

        closes = _closes(bars, count)
        price  = closes[-1]

        # Compute required EMAs
        ema21_vals  = _ind_mod.ema(closes, 21)
        ema50_vals  = _ind_mod.ema(closes, 50)
        ema200_vals = _ind_mod.ema(closes, 200)

        ema21  = _last(ema21_vals)
        ema50  = _last(ema50_vals)
        ema200 = _last(ema200_vals)

        if ema21 is None or ema50 is None or ema200 is None:
            return AlgoDecision.none(symbol)

        # RSI (period=14)
        rsi_vals = _ind_mod.rsi(closes, 14)
        rsi_v    = _last(rsi_vals)
        if rsi_v is None:
            return AlgoDecision.none(symbol)

        # Price proximity to EMA21: within 0.3%
        at_ema21 = abs(price - ema21) / ema21 < 0.003 if ema21 > 1e-10 else False

        # Trend alignment
        bull_trend = ema21 > ema50 > ema200
        bear_trend = ema21 < ema50 < ema200

        atr_v = ind.atr_value or 0.0

        # BUY: bullish trend, pullback to EMA21, RSI in neutral zone, bullish pattern
        if (
            bull_trend
            and at_ema21
            and 38.0 < rsi_v < 58.0
            and pat.has_bullish(bars, count)
        ):
            return AlgoDecision(
                signal     = AlgoSignal.BUY,
                symbol     = symbol,
                direction  = Direction.LONG,
                confidence = 0.75,
                reason     = f"Pullback to EMA21 in bull trend RSI={int(rsi_v)}",
                atr        = atr_v,
            )

        # SELL: bearish trend, pullback to EMA21, RSI in neutral zone, bearish pattern
        if (
            bear_trend
            and at_ema21
            and 42.0 < rsi_v < 62.0
            and pat.has_bearish(bars, count)
        ):
            return AlgoDecision(
                signal     = AlgoSignal.SELL,
                symbol     = symbol,
                direction  = Direction.SHORT,
                confidence = 0.75,
                reason     = f"Pullback to EMA21 in bear trend RSI={int(rsi_v)}",
                atr        = atr_v,
            )

        return AlgoDecision.none(symbol)


# ══════════════════════════════════════════════════════════════════════════════
# AlgorithmRegistry
# ══════════════════════════════════════════════════════════════════════════════

class AlgorithmRegistry:
    """Registry for all registered :class:`IAlgorithm` instances.

    Pre-registers the standard suite on construction:
        - TrendFollower(fast=21, slow=50, adx_min=22, magic=1001)
        - TrendFollower(fast=9,  slow=21, adx_min=20, magic=1005)
        - MeanReversion()
        - BreakoutTrader()
        - SwingTrader()

    All algorithms are enabled by default.
    """

    def __init__(self) -> None:
        # Ordered dict preserves registration order; keys are algo.name()
        self._algos: dict[str, IAlgorithm] = {}

        # Register default suite
        self.register(TrendFollower(fast_ema=21, slow_ema=50, adx_min=22.0, magic_number=1001))
        self.register(TrendFollower(fast_ema=9,  slow_ema=21, adx_min=20.0, magic_number=1005))
        self.register(MeanReversion())
        self.register(BreakoutTrader())
        self.register(SwingTrader())

    # ── Registry management ───────────────────────────────────────────────────

    def register(self, algo: IAlgorithm) -> None:
        """Add *algo* to the registry (replaces any existing algo with the same name)."""
        self._algos[algo.name()] = algo

    def get(self, name: str) -> Optional[IAlgorithm]:
        """Return the algorithm with the given name, or None if not found."""
        return self._algos.get(name)

    def enable(self, name: str) -> None:
        """Enable the named algorithm. No-op if name is not registered."""
        algo = self._algos.get(name)
        if algo is not None:
            algo.enable()

    def disable(self, name: str) -> None:
        """Disable the named algorithm. No-op if name is not registered."""
        algo = self._algos.get(name)
        if algo is not None:
            algo.disable()

    def evaluate_all(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: EngineResult,
        pat: PatternEngine,
    ) -> list[AlgoDecision]:
        """Run all enabled algorithms and return only actionable decisions.

        Parameters
        ----------
        symbol : str
            Instrument symbol.
        tf : int
            Timeframe in seconds.
        bars : list[Bar]
            Full bar history.
        count : int
            Index of the current (latest) bar.
        ind : EngineResult
            Pre-computed standard indicator snapshot.
        pat : PatternEngine
            Pattern engine for candlestick / chart-pattern detection.

        Returns
        -------
        list[AlgoDecision]
            Only decisions where :meth:`AlgoDecision.is_actionable` is True.
        """
        results: list[AlgoDecision] = []
        for algo in self._algos.values():
            decision = algo.safe_evaluate(symbol, tf, bars, count, ind, pat)
            if decision.is_actionable():
                results.append(decision)
        return results
