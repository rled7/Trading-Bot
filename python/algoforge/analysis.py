"""Multi-timeframe analysis layer — Python port of analysis.hpp,
confluence_scorer.cpp, and market_structure.cpp.

Public API
----------
MarketStructure      – swing-based structure detection
TrendClassifier      – EMA/ADX trend classification
ConfluenceScorer     – weighted bull/bear point scoring
eval_session         – session-quality lookup
classify_regime      – volatility/ATR regime detection
MultiTimeframeAnalyzer – MTF confluence aggregation

All result types are frozen dataclasses; enums match the C++ equivalents.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Any

from .indicators import ema as _ema, atr as _atr, adx as _adx, bollinger as _bollinger
from .types import Bar, Timeframe


# ── Enums ─────────────────────────────────────────────────────────────────────

class TrendState(IntEnum):
    """Matches af_trend_state_t in analysis.hpp."""
    STRONG_BULL = 2
    BULL        = 1
    NEUTRAL     = 0
    BEAR        = -1
    STRONG_BEAR = -2


class TrendBias(IntEnum):
    """Matches af_trend_bias_t (market-structure bias)."""
    STRONG_BULL = 2
    BULL        = 1
    RANGING     = 0
    BEAR        = -1
    STRONG_BEAR = -2


class VolRegime(IntEnum):
    """Matches af_vol_regime_t in analysis.hpp."""
    SQUEEZE       = 0
    EXPANDING     = 1
    HIGH_TRENDING = 2
    HIGH_RANGING  = 3
    LOW_TRENDING  = 4
    LOW_RANGING   = 5


class SessionType(IntEnum):
    OVERLAP = 0
    LONDON  = 1
    NY      = 2
    ASIA    = 3
    DEAD    = 4


class PatternCategory(IntEnum):
    """Matches af_pattern_category_t."""
    CANDLESTICK = 0
    CHART       = 1
    HARMONIC    = 2
    ELLIOTT     = 3
    WYCKOFF     = 4
    VOLUME      = 5


# ── Result dataclasses ────────────────────────────────────────────────────────

@dataclass(frozen=True)
class StructureResult:
    """Output of MarketStructure.analyse()."""
    bias:        TrendBias
    confidence:  float
    bos:         bool       # break of structure
    choch:       bool       # change of character
    swing_highs: list[int]  # bar indices of detected swing highs
    swing_lows:  list[int]  # bar indices of detected swing lows


@dataclass(frozen=True)
class TrendClassification:
    """Output of TrendClassifier.classify()."""
    state:      TrendState
    confidence: float
    bull_score: int
    bear_score: int
    trending:   bool        # adx > 20


@dataclass(frozen=True)
class ConfluenceScore:
    """Output of ConfluenceScorer.score_timeframe()."""
    symbol:     str
    timeframe:  int
    bull_pts:   float
    bear_pts:   float
    total_pts:  float
    score:      float       # percentage 0-100
    direction:  int         # 1, -1, 0
    grade:      str         # A/B/C/D/F
    confidence: float
    margin:     float       # abs(bull_pct - bear_pct)

    def is_tradeable(self) -> bool:
        return self.score >= 60.0 and self.direction != 0


@dataclass(frozen=True)
class SessionScore:
    """Output of eval_session()."""
    symbol:    str
    session:   SessionType
    quality:   float
    tradeable: bool         # quality >= 0.45


@dataclass(frozen=True)
class RegimeResult:
    """Output of classify_regime()."""
    regime:       VolRegime
    size_mult:    float
    atr_pct:      float     # percentile (0-100)
    bb_squeeze:   bool
    bb_expanding: bool
    trending:     bool


@dataclass(frozen=True)
class MTFResult:
    """Output of MultiTimeframeAnalyzer.analyze()."""
    symbol:         str
    direction:      int     # 1, -1, 0
    confidence:     float
    weighted_score: float
    tf_scores:      dict[int, ConfluenceScore]
    dominant_tf:    int     # tf enum with highest weighted contribution


# ── MarketStructure ───────────────────────────────────────────────────────────

class MarketStructure:
    """Detect swing highs/lows and derive HH/HL/LH/LL market bias."""

    def __init__(self, swing_strength: int = 3) -> None:
        self.swing_strength = swing_strength

    def analyse(self, bars: list[Bar], n: int) -> StructureResult:
        """Analyse bars[0..n-1] and return a StructureResult.

        Swing detection: position i is a swing high if
          bars[i].high > bars[i±j].high  for j in 1..swing_strength
        (both sides must satisfy the condition).
        """
        ss = self.swing_strength
        swing_highs: list[int] = []
        swing_lows:  list[int] = []

        for i in range(ss, n - ss):
            h = bars[i].high
            l = bars[i].low
            is_sh = all(h > bars[i + j].high and h > bars[i - j].high
                        for j in range(1, ss + 1))
            is_sl = all(l < bars[i + j].low and l < bars[i - j].low
                        for j in range(1, ss + 1))
            if is_sh:
                swing_highs.append(i)
            if is_sl:
                swing_lows.append(i)

        # Determine HH/HL/LH/LL using last 2 swings
        hh = hl = lh = ll = False
        if len(swing_highs) >= 2:
            hh = bars[swing_highs[-1]].high > bars[swing_highs[-2]].high
            lh = not hh
        if len(swing_lows) >= 2:
            hl = bars[swing_lows[-1]].low > bars[swing_lows[-2]].low
            ll = not hl

        # Derive bias and confidence
        if hh and hl:
            bias, conf = TrendBias.STRONG_BULL, 0.90
        elif hh or hl:
            bias, conf = TrendBias.BULL, 0.70
        elif lh and ll:
            bias, conf = TrendBias.STRONG_BEAR, 0.90
        elif lh or ll:
            bias, conf = TrendBias.BEAR, 0.70
        else:
            bias, conf = TrendBias.RANGING, 0.40

        # BOS / ChoCH: in bear bias, if cur_price > last swing high → flip
        bos = choch = False
        cur_price = bars[n - 1].close
        if bias in (TrendBias.BEAR, TrendBias.STRONG_BEAR) and swing_highs:
            last_sh_price = bars[swing_highs[-1]].high
            if cur_price > last_sh_price:
                bos = choch = True

        return StructureResult(
            bias=bias,
            confidence=conf,
            bos=bos,
            choch=choch,
            swing_highs=swing_highs,
            swing_lows=swing_lows,
        )


# ── TrendClassifier ───────────────────────────────────────────────────────────

class TrendClassifier:
    """EMA9/21/50/200 + ADX(14) trend classification."""

    def classify(self, bars: list[Bar], n: int) -> TrendClassification:
        """Classify trend over bars[0..n-1]."""
        sub  = bars[:n]
        closes = [b.close for b in sub]
        highs  = [b.high  for b in sub]
        lows   = [b.low   for b in sub]

        ema9_v   = _ema(closes, 9)
        ema21_v  = _ema(closes, 21)
        ema50_v  = _ema(closes, 50)
        ema200_v = _ema(closes, 200)
        adx_v, _, _ = _adx(highs, lows, closes, 14)

        # Last valid values
        def _last(lst: list) -> float | None:
            return lst[-1] if lst else None

        price   = closes[-1]
        e9      = _last(ema9_v)
        e21     = _last(ema21_v)
        e50     = _last(ema50_v)
        e200    = _last(ema200_v)
        adx_val = _last(adx_v)

        trending = (adx_val is not None) and (adx_val > 20.0)

        bull_score = 0
        bear_score = 0

        # Price vs each EMA (+1 each side)
        for ev in (e9, e21, e50, e200):
            if ev is not None:
                if price > ev:
                    bull_score += 1
                else:
                    bear_score += 1

        # EMA alignment (+1 each)
        if e9 is not None and e21 is not None:
            if e9 > e21:
                bull_score += 1
            else:
                bear_score += 1
        if e21 is not None and e50 is not None:
            if e21 > e50:
                bull_score += 1
            else:
                bear_score += 1
        if e50 is not None and e200 is not None:
            if e50 > e200:
                bull_score += 1
            else:
                bear_score += 1

        net = bull_score - bear_score

        if net >= 6 and trending:
            state = TrendState.STRONG_BULL
        elif net >= 3:
            state = TrendState.BULL
        elif net <= -6 and trending:
            state = TrendState.STRONG_BEAR
        elif net <= -3:
            state = TrendState.BEAR
        else:
            state = TrendState.NEUTRAL

        confidence = min(0.95, 0.40 + abs(net) * 0.07 + (0.10 if trending else 0.0))

        return TrendClassification(
            state=state,
            confidence=confidence,
            bull_score=bull_score,
            bear_score=bear_score,
            trending=trending,
        )


# ── ConfluenceScorer ──────────────────────────────────────────────────────────

_PATTERN_WEIGHTS: dict[PatternCategory, float] = {
    PatternCategory.CANDLESTICK: 0.60,
    PatternCategory.CHART:       0.90,
    PatternCategory.HARMONIC:    0.85,
    PatternCategory.ELLIOTT:     0.80,
    PatternCategory.WYCKOFF:     1.00,
    PatternCategory.VOLUME:      0.75,
}

# Map pattern names from PatternEngine to categories
_PATTERN_CATEGORY_MAP: dict[str, PatternCategory] = {
    "doji":                      PatternCategory.CANDLESTICK,
    "hammer":                    PatternCategory.CANDLESTICK,
    "marubozu":                  PatternCategory.CANDLESTICK,
    "pin_bar":                   PatternCategory.CANDLESTICK,
    "engulfing":                 PatternCategory.CANDLESTICK,
    "morning_star":              PatternCategory.CANDLESTICK,
    "evening_star":              PatternCategory.CANDLESTICK,
    "three_white_soldiers":      PatternCategory.CANDLESTICK,
    "three_black_crows":         PatternCategory.CANDLESTICK,
    "double_top":                PatternCategory.CHART,
    "double_bottom":             PatternCategory.CHART,
    "ascending_triangle":        PatternCategory.CHART,
    "descending_triangle":       PatternCategory.CHART,
    "bullish_flag":              PatternCategory.CHART,
    "bearish_flag":              PatternCategory.CHART,
    "head_and_shoulders":        PatternCategory.CHART,
    "inverse_head_and_shoulders": PatternCategory.CHART,
    "gartley_bull":              PatternCategory.HARMONIC,
    "gartley_bear":              PatternCategory.HARMONIC,
    "bat_bull":                  PatternCategory.HARMONIC,
    "butterfly_bull":            PatternCategory.HARMONIC,
    "crab_bull":                 PatternCategory.HARMONIC,
}


def _pat_weight(category: PatternCategory) -> float:
    return _PATTERN_WEIGHTS.get(category, 0.60)


class ConfluenceScorer:
    """Score bull/bear confluence for a single timeframe."""

    def score_timeframe(
        self,
        symbol:       str,
        timeframe:    int,
        ind_summary:  Any | None   = None,
        pat_result:   Any | None   = None,
        struct_sig:   int          = 0,
        struct_conf:  float        = 0.0,
        trend_sig:    int          = 0,
        trend_conf:   float        = 0.0,
    ) -> ConfluenceScore:
        """Compute a ConfluenceScore for one timeframe.

        Parameters
        ----------
        ind_summary  – object with .bullish and .bearish int counts
                       (e.g. from EngineResult-derived summary), or None.
        pat_result   – list[PatternMatch] from PatternEngine.scan(), or None.
        struct_sig   – 1 (bull), -1 (bear), 0 (neutral)
        struct_conf  – confidence from MarketStructure (0–1)
        trend_sig    – 1 (bull), -1 (bear), 0 (neutral)
        trend_conf   – confidence from TrendClassifier (0–1)
        """
        bull_pts = 0.0
        bear_pts = 0.0

        # Indicator summary contribution
        if ind_summary is not None:
            bull_pts += float(getattr(ind_summary, "bullish", 0))
            bear_pts += float(getattr(ind_summary, "bearish", 0))

        # Pattern contribution
        if pat_result is not None:
            for match in pat_result:
                name     = getattr(match, "name", "")
                signal   = getattr(match, "signal", 0)
                conf     = getattr(match, "confidence", 1.0)
                cat      = _PATTERN_CATEGORY_MAP.get(name, PatternCategory.CANDLESTICK)
                w        = _pat_weight(cat) * float(conf) * 3.0
                if signal > 0:
                    bull_pts += w
                elif signal < 0:
                    bear_pts += w

        # Structure contribution
        if struct_sig == 1:
            bull_pts += struct_conf * 5.0
        elif struct_sig == -1:
            bear_pts += struct_conf * 5.0

        # Trend contribution
        if trend_sig == 1:
            bull_pts += trend_conf * 4.0
        elif trend_sig == -1:
            bear_pts += trend_conf * 4.0

        total_pts = bull_pts + bear_pts
        if total_pts < 1e-9:
            bull_pct = bear_pct = 0.0
        else:
            bull_pct = bull_pts / total_pts * 100.0
            bear_pct = bear_pts / total_pts * 100.0

        margin = abs(bull_pct - bear_pct)
        score  = max(bull_pct, bear_pct)

        if margin < 10.0:
            direction = 0
        elif bull_pct > bear_pct:
            direction = 1
        else:
            direction = -1

        if score >= 80.0:
            grade = "A"
        elif score >= 65.0:
            grade = "B"
        elif score >= 50.0:
            grade = "C"
        elif score >= 35.0:
            grade = "D"
        else:
            grade = "F"

        confidence = min(1.0, score / 100.0 * (1.0 + margin / 200.0))

        return ConfluenceScore(
            symbol=symbol,
            timeframe=timeframe,
            bull_pts=bull_pts,
            bear_pts=bear_pts,
            total_pts=total_pts,
            score=score,
            direction=direction,
            grade=grade,
            confidence=confidence,
            margin=margin,
        )


# ── Session evaluation ────────────────────────────────────────────────────────

# 24-hour quality lookup tables (index = UTC hour)
_DEFAULT_QUALITY = [
    0.30, 0.25, 0.20, 0.20, 0.25, 0.35,
    0.55, 0.80, 0.85, 0.90, 0.90, 0.90,
    1.00, 1.00, 1.00, 0.95, 0.85, 0.70,
    0.55, 0.45, 0.40, 0.30, 0.20, 0.25,
]
_USDJPY_QUALITY = [
    0.80, 0.85, 0.85, 0.80, 0.75, 0.70,
    0.75, 0.90, 0.95, 0.80, 0.70, 0.65,
    0.85, 0.90, 0.85, 0.75, 0.60, 0.50,
    0.40, 0.35, 0.30, 0.40, 0.55, 0.70,
]
_XAUUSD_QUALITY = [
    0.35, 0.30, 0.25, 0.25, 0.30, 0.40,
    0.60, 0.75, 0.85, 0.90, 0.90, 0.90,
    1.00, 1.00, 0.95, 0.85, 0.70, 0.50,
    0.40, 0.35, 0.30, 0.25, 0.20, 0.25,
]


def _session_type(hour_utc: int) -> SessionType:
    if 12 <= hour_utc <= 16:
        return SessionType.OVERLAP
    if 7 <= hour_utc <= 11:
        return SessionType.LONDON
    if 17 <= hour_utc <= 20:
        return SessionType.NY
    return SessionType.ASIA


def eval_session(symbol: str, hour_utc: int, day_of_week: int) -> SessionScore:
    """Evaluate trading session quality.

    Parameters
    ----------
    symbol      – e.g. "USDJPY", "XAUUSD", "EURUSD"
    hour_utc    – current UTC hour (0-23)
    day_of_week – 0=Monday … 5=Saturday, 6=Sunday  (matching C++ dow convention)
    """
    # Sunday → DEAD
    if day_of_week == 6:
        return SessionScore(symbol=symbol, session=SessionType.DEAD, quality=0.0, tradeable=False)

    # Friday after 20:00 → DEAD (weekend gap risk)
    if day_of_week == 5 and hour_utc >= 20:
        return SessionScore(symbol=symbol, session=SessionType.DEAD, quality=0.0, tradeable=False)

    sym_upper = symbol.upper()
    is_jpy = "JPY" in sym_upper

    # JPY dead hours
    if is_jpy and hour_utc in (22, 23):
        return SessionScore(symbol=symbol, session=SessionType.DEAD, quality=0.0, tradeable=False)

    # Non-JPY dead hours
    if not is_jpy and (hour_utc >= 22 or hour_utc <= 3):
        return SessionScore(symbol=symbol, session=SessionType.DEAD, quality=0.0, tradeable=False)

    # Select quality table
    if is_jpy:
        table = _USDJPY_QUALITY
    elif sym_upper == "XAUUSD":
        table = _XAUUSD_QUALITY
    else:
        table = _DEFAULT_QUALITY

    quality  = table[hour_utc]
    session  = _session_type(hour_utc)
    tradeable = quality >= 0.45

    return SessionScore(symbol=symbol, session=session, quality=quality, tradeable=tradeable)


# ── Regime classifier ─────────────────────────────────────────────────────────

class RegimeClassifier:
    """Volatility / regime detection (ATR percentile + Bollinger squeeze)."""

    def classify_regime(self, bars: list[Bar], n: int) -> RegimeResult:
        """Classify the volatility regime over bars[0..n-1]."""
        sub    = bars[:n]
        closes = [b.close for b in sub]
        highs  = [b.high  for b in sub]
        lows   = [b.low   for b in sub]

        # ATR(14)
        atr_vals = _atr(highs, lows, closes, 14)
        cur_atr  = atr_vals[-1]

        # ATR percentile: % of last 100 bars with ATR < current ATR
        lookback = min(100, n)
        atr_window = [v for v in atr_vals[n - lookback:n] if v is not None]
        if cur_atr is not None and atr_window:
            count_below = sum(1 for v in atr_window if v < cur_atr)
            atr_pct = count_below / len(atr_window) * 100.0
        else:
            atr_pct = 50.0

        # Bollinger bandwidth (period=20, mult=2)
        bb_upper, bb_middle, bb_lower = _bollinger(closes, 20, 2.0)

        def _bw(i: int) -> float | None:
            u, m, l = bb_upper[i], bb_middle[i], bb_lower[i]
            if u is None or m is None or l is None or abs(m) < 1e-12:
                return None
            return (u - l) / m

        # Bandwidth for current and last 100 bars
        bw_now = _bw(n - 1)
        bw_series = [_bw(i) for i in range(max(0, n - 100), n) if _bw(i) is not None]

        bb_squeeze = False
        bb_expanding = False

        if bw_now is not None and bw_series:
            bw_min = min(bw_series)
            bb_squeeze = bw_now <= bw_min * 1.05

            # Expanding: bw slope positive (bw[n-1] - bw[n-5]) AND not squeeze
            if n >= 5:
                bw_prev5 = _bw(n - 5)
                if bw_prev5 is not None:
                    bb_expanding = (bw_now - bw_prev5 > 0.0) and not bb_squeeze
            else:
                bb_expanding = False

        # ADX trending
        adx_v, _, _ = _adx(highs, lows, closes, 14)
        adx_val = adx_v[-1]
        trending = (adx_val is not None) and (adx_val > 20.0)

        # Determine regime
        if bb_squeeze:
            regime    = VolRegime.SQUEEZE
            size_mult = 0.0
        elif bb_expanding:
            regime    = VolRegime.EXPANDING
            size_mult = 1.2
        elif atr_pct >= 65.0 and trending:
            regime    = VolRegime.HIGH_TRENDING
            size_mult = 1.0
        elif atr_pct >= 65.0:
            regime    = VolRegime.HIGH_RANGING
            size_mult = 0.3
        elif atr_pct <= 35.0 and trending:
            regime    = VolRegime.LOW_TRENDING
            size_mult = 0.75
        else:
            regime    = VolRegime.LOW_RANGING
            size_mult = 0.80

        return RegimeResult(
            regime=regime,
            size_mult=size_mult,
            atr_pct=atr_pct,
            bb_squeeze=bb_squeeze,
            bb_expanding=bb_expanding,
            trending=trending,
        )


# Module-level convenience function
def classify_regime(bars: list[Bar], n: int) -> RegimeResult:
    """Classify volatility regime over bars[0..n-1]."""
    return RegimeClassifier().classify_regime(bars, n)


# ── MultiTimeframeAnalyzer ────────────────────────────────────────────────────

# Timeframe weights matching C++ TF_WEIGHTS
TF_WEIGHTS: dict[int, float] = {
    int(Timeframe.M15): 1.0,
    int(Timeframe.H1):  2.0,
    int(Timeframe.H4):  3.0,
    int(Timeframe.D1):  4.0,
}


def _tf_weight(tf_enum: int) -> float:
    """Return weight for a timeframe; defaults to 1.0 for unknown TFs."""
    return TF_WEIGHTS.get(tf_enum, 1.0)


class MultiTimeframeAnalyzer:
    """Aggregate confluence scores across multiple timeframes."""

    def __init__(self) -> None:
        self._struct    = MarketStructure()
        self._trend     = TrendClassifier()
        self._confluence = ConfluenceScorer()

    def analyze(
        self,
        symbol:       str,
        bars_per_tf:  dict[int, list[Bar]],
        ind_per_tf:   dict[int, Any] | None = None,
        pat_per_tf:   dict[int, Any] | None = None,
    ) -> MTFResult:
        """Analyze symbol across multiple timeframes and return MTFResult.

        Parameters
        ----------
        symbol       – instrument symbol, e.g. "EURUSD"
        bars_per_tf  – mapping of Timeframe enum value → list[Bar]
        ind_per_tf   – optional mapping of tf_enum → EngineResult (or any object
                       with .bullish / .bearish attributes)
        pat_per_tf   – optional mapping of tf_enum → list[PatternMatch]
        """
        if ind_per_tf is None:
            ind_per_tf = {}
        if pat_per_tf is None:
            pat_per_tf = {}

        tf_scores:           dict[int, ConfluenceScore] = {}
        weighted_bull:       float = 0.0
        weighted_bear:       float = 0.0
        total_weight:        float = 0.0
        dominant_tf:         int   = -1
        dominant_contribution: float = 0.0

        for tf_enum, bars in bars_per_tf.items():
            n = len(bars)
            if n == 0:
                continue

            weight = _tf_weight(tf_enum)

            # Market structure
            struct_res  = self._struct.analyse(bars, n)
            struct_sig  = int(struct_res.bias)   # TrendBias values map to -2..2
            # Normalise to -1/0/1 for scorer
            struct_sig1 = 1 if struct_sig > 0 else (-1 if struct_sig < 0 else 0)

            # Trend
            trend_res  = self._trend.classify(bars, n)
            trend_sig  = int(trend_res.state)
            trend_sig1 = 1 if trend_sig > 0 else (-1 if trend_sig < 0 else 0)

            cs = self._confluence.score_timeframe(
                symbol      = symbol,
                timeframe   = tf_enum,
                ind_summary = ind_per_tf.get(tf_enum),
                pat_result  = pat_per_tf.get(tf_enum),
                struct_sig  = struct_sig1,
                struct_conf = struct_res.confidence,
                trend_sig   = trend_sig1,
                trend_conf  = trend_res.confidence,
            )

            tf_scores[tf_enum] = cs

            # Weighted aggregation
            contribution = cs.score * weight * cs.direction if cs.direction != 0 else 0.0
            if cs.direction == 1:
                weighted_bull += cs.score * weight
            elif cs.direction == -1:
                weighted_bear += cs.score * weight
            total_weight += weight

            # Track dominant timeframe by absolute weighted contribution
            abs_contrib = abs(cs.score * weight)
            if abs_contrib > dominant_contribution:
                dominant_contribution = abs_contrib
                dominant_tf           = tf_enum

        if total_weight < 1e-9:
            return MTFResult(
                symbol=symbol,
                direction=0,
                confidence=0.0,
                weighted_score=0.0,
                tf_scores=tf_scores,
                dominant_tf=dominant_tf if dominant_tf >= 0 else -1,
            )

        net_weighted = (weighted_bull - weighted_bear) / total_weight
        avg_score    = (weighted_bull + weighted_bear) / total_weight

        if net_weighted > 10.0:
            direction = 1
        elif net_weighted < -10.0:
            direction = -1
        else:
            direction = 0

        confidence = min(1.0, avg_score / 100.0)

        return MTFResult(
            symbol=symbol,
            direction=direction,
            confidence=confidence,
            weighted_score=avg_score,
            tf_scores=tf_scores,
            dominant_tf=dominant_tf if dominant_tf >= 0 else -1,
        )
