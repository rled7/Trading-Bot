"""ManifestAlgo — compiles an AlgoManifest into a runtime IAlgorithm.

Computes declared indicators per-bar, evaluates DSL entry/exit expressions,
and emits AlgoDecision objects. Supports:
  - sl_atr / tp_atr bracket exits (size in ATR multiples)
  - risk.cool_down_bars cooldown between exits and re-entries
"""
from __future__ import annotations

import math
from typing import Any, Callable, Optional

from ..algorithm import AlgoDecision, AlgoSignal, IAlgorithm
from ..indicators import atr as _atr_fn
from ..indicators import (
    ema, sma, rsi, macd, bollinger, stochastic, obv, wma, cci,
    williams_r, roc, mfi, vwap, keltner, adx, hma, dema, tema,
    trix, momentum, true_range, wilder_ema, vwma, hist_vol,
    cmf, acc_dist, force_index, vol_osc, donchian,
)
from ..types import Bar, Direction
from .dsl import compile_expr, PATTERN_NAMES
from .types import AlgoManifest, EntryRule, ExitRule

# Map indicator kind → callable
_IND_DISPATCH: dict[str, Callable] = {
    "sma":        sma,
    "ema":        ema,
    "rsi":        rsi,
    "macd":       macd,
    "bollinger":  bollinger,
    "stochastic": stochastic,
    "obv":        obv,
    "wma":        wma,
    "cci":        cci,
    "williams_r": williams_r,
    "roc":        roc,
    "mfi":        mfi,
    "vwap":       vwap,
    "keltner":    keltner,
    "adx":        adx,
    "hma":        hma,
    "dema":       dema,
    "tema":       tema,
    "trix":       trix,
    "momentum":   momentum,
    "true_range": true_range,
    "wilder_ema": wilder_ema,
    "vwma":       vwma,
    "hist_vol":   hist_vol,
    "cmf":        cmf,
    "acc_dist":   acc_dist,
    "force_index": force_index,
    "vol_osc":    vol_osc,
    "donchian":   donchian,
    "atr":        _atr_fn,
}


def _call_indicator(kind: str, params: dict, bars_sub: list[Bar]) -> Any:
    """Call the indicator function with the right arguments based on kind."""
    fn = _IND_DISPATCH[kind]
    closes = [b.close for b in bars_sub]
    highs  = [b.high  for b in bars_sub]
    lows   = [b.low   for b in bars_sub]
    vols   = [b.volume for b in bars_sub]

    p = params

    # Dispatch based on indicator kind and its expected arguments
    if kind in ("sma", "ema", "rsi", "wma", "hma", "dema", "tema",
                "trix", "momentum", "wilder_ema"):
        period = int(p.get("period", 14))
        return fn(closes, period)

    elif kind == "roc":
        period = int(p.get("period", 14))
        return fn(closes, period)

    elif kind == "atr":
        period = int(p.get("period", 14))
        return fn(highs, lows, closes, period)

    elif kind == "macd":
        fast   = int(p.get("fast", 12))
        slow   = int(p.get("slow", 26))
        signal = int(p.get("signal", 9))
        return fn(closes, fast, slow, signal)

    elif kind == "bollinger":
        period = int(p.get("period", 20))
        mult   = float(p.get("mult", 2.0))
        return fn(closes, period, mult)

    elif kind == "stochastic":
        k_period = int(p.get("k_period", 14))
        d_period = int(p.get("d_period", 3))
        return fn(highs, lows, closes, k_period, d_period)

    elif kind == "obv":
        return fn(closes, vols)

    elif kind == "cci":
        period   = int(p.get("period", 20))
        constant = float(p.get("constant", 0.015))
        return fn(highs, lows, closes, period, constant)

    elif kind == "williams_r":
        period = int(p.get("period", 14))
        return fn(highs, lows, closes, period)

    elif kind == "mfi":
        period = int(p.get("period", 14))
        return fn(highs, lows, closes, vols, period)

    elif kind == "vwap":
        return fn(highs, lows, closes, vols)

    elif kind == "keltner":
        ema_period = int(p.get("ema_period", 20))
        atr_period = int(p.get("atr_period", 10))
        mult       = float(p.get("mult", 2.0))
        return fn(highs, lows, closes, ema_period, atr_period, mult)

    elif kind == "adx":
        period = int(p.get("period", 14))
        return fn(highs, lows, closes, period)

    elif kind == "vwma":
        period = int(p.get("period", 14))
        return fn(closes, vols, period)

    elif kind == "hist_vol":
        period = int(p.get("period", 20))
        tdays  = int(p.get("tdays", 252))
        return fn(closes, period, tdays)

    elif kind == "cmf":
        period = int(p.get("period", 20))
        return fn(highs, lows, closes, vols, period)

    elif kind == "acc_dist":
        return fn(highs, lows, closes, vols)

    elif kind == "force_index":
        period = int(p.get("period", 13))
        return fn(closes, vols, period)

    elif kind == "vol_osc":
        fast = int(p.get("fast", 14))
        slow = int(p.get("slow", 28))
        return fn(vols, fast, slow)

    elif kind == "donchian":
        period = int(p.get("period", 20))
        return fn(highs, lows, period)

    elif kind == "true_range":
        return fn(highs, lows, closes)

    else:
        raise ValueError(f"Unknown indicator kind: {kind!r}")


def _last(values: Any) -> Optional[float]:
    """Return the last element of a list-like, or None."""
    if values is None:
        return None
    if isinstance(values, (list, tuple)):
        if not values:
            return None
        v = values[-1]
        if v is None or (isinstance(v, float) and math.isnan(v)):
            return None
        return float(v)
    return float(values)


def _last_tuple(result: Any) -> Optional[float]:
    """For multi-output indicators (tuples of lists), return last of first list."""
    if isinstance(result, tuple):
        return _last(result[0])
    return _last(result)


class _PatternProxy:
    """Proxy object for pattern.X attribute access in DSL expressions."""

    def __init__(self, pattern_values: dict[str, bool]) -> None:
        self._vals = pattern_values

    def __getattr__(self, name: str) -> bool:
        return self._vals.get(name, False)


class ManifestAlgo(IAlgorithm):
    """Runtime algorithm compiled from an AlgoManifest.

    Computes all declared indicators on each bar slice, evaluates DSL
    entry/exit expressions, and emits AlgoDecision objects.
    """

    metadata: Optional[dict] = None

    def __init__(self, manifest: AlgoManifest) -> None:
        self._manifest   = manifest
        self._ind_ids    = {ind.id for ind in manifest.indicators}
        self._cool_down  = manifest.risk.cool_down_bars
        self._bars_since_exit: int = 9999  # starts "ready"

        # Compile all entry DSL expressions
        self._entry_exprs: list[tuple[str, Callable]] = []
        for rule in manifest.entries:
            if rule.when:
                fn = compile_expr(rule.when, self._ind_ids)
                self._entry_exprs.append((rule.side, fn))

        # Compile all exit DSL expressions
        self._exit_exprs: list[tuple[str, Callable]] = []
        for rule in manifest.exits:
            if rule.when:
                fn = compile_expr(rule.when, self._ind_ids)
                self._exit_exprs.append((rule.side, fn))

        # Find ATR indicator id (first declared, or fallback computation)
        self._atr_ind_id: Optional[str] = None
        for ind in manifest.indicators:
            if ind.kind == "atr":
                self._atr_ind_id = ind.id
                break

        # Extract bracket specs from exits
        self._sl_atr: dict[str, float] = {}  # side -> sl_atr
        self._tp_atr: dict[str, float] = {}  # side -> tp_atr
        for rule in manifest.exits:
            if rule.sl_atr is not None:
                self._sl_atr[rule.side] = rule.sl_atr
            if rule.tp_atr is not None:
                self._tp_atr[rule.side] = rule.tp_atr

        # Current open bracket state (side -> (entry_price, sl, tp))
        self._open_bracket: Optional[tuple[str, float, float, float]] = None

        # Set metadata from constructor if manifest has generation info
        self.metadata = None

    def name(self) -> str:
        return self._manifest.name

    def description(self) -> str:
        return self._manifest.description

    def magic(self) -> int:
        # Deterministic magic number from name hash
        return abs(hash(self._manifest.name)) % 100000 + 10000

    def evaluate(
        self,
        symbol: str,
        tf: int,
        bars: list[Bar],
        count: int,
        ind: object,
        pat: object,
    ) -> AlgoDecision:
        """Evaluate the manifest algorithm at the current bar."""
        sub = bars[: count + 1]
        if len(sub) < 2:
            return AlgoDecision.none(symbol)

        # Compute all declared indicators
        env = self._build_env(sub, pat)

        if env is None:
            return AlgoDecision.none(symbol)

        # Get current ATR value (for bracket sizing)
        atr_val = self._get_atr(sub, env)

        # Cooldown check
        self._bars_since_exit += 1
        if self._bars_since_exit < self._cool_down:
            return AlgoDecision.none(symbol)

        # Check entry rules
        for side, expr_fn in self._entry_exprs:
            try:
                if expr_fn(env):
                    direction = Direction.LONG if side == "long" else Direction.SHORT
                    signal    = AlgoSignal.BUY if side == "long" else AlgoSignal.SELL
                    return AlgoDecision(
                        signal     = signal,
                        symbol     = symbol,
                        direction  = direction,
                        confidence = 0.70,
                        reason     = f"manifest-entry:{side}",
                        atr        = atr_val or 0.0,
                    )
            except Exception:
                continue

        return AlgoDecision.none(symbol)

    def _build_env(self, sub: list[Bar], pat: object) -> Optional[dict]:
        """Build the evaluation environment dict for DSL expressions."""
        env: dict = {}

        # Bar fields from the most recent bar
        last_bar = sub[-1]
        env["close"]  = last_bar.close
        env["open"]   = last_bar.open
        env["high"]   = last_bar.high
        env["low"]    = last_bar.low
        env["volume"] = last_bar.volume
        env["spread"] = getattr(last_bar, "spread", 0.0)

        # Compute all declared indicators
        for ind_decl in self._manifest.indicators:
            try:
                result = _call_indicator(ind_decl.kind, ind_decl.params, sub)
                # For multi-output indicators, store the last value of the first output
                if isinstance(result, tuple):
                    val = _last(result[0])
                else:
                    val = _last(result)
                env[ind_decl.id] = val
            except Exception:
                env[ind_decl.id] = None

        # Pattern proxy
        pattern_vals: dict[str, bool] = {}
        if pat is not None:
            try:
                for pname in PATTERN_NAMES:
                    has_it = False
                    if hasattr(pat, "scan"):
                        matches = pat.scan(sub, len(sub) - 1)
                        for m in matches:
                            if m.name == pname and m.signal > 0:
                                has_it = True
                                break
                    pattern_vals[pname] = has_it
            except Exception:
                pass
        env["pattern"] = _PatternProxy(pattern_vals)

        return env

    def _get_atr(self, sub: list[Bar], env: dict) -> float:
        """Return ATR value from environment or compute a fallback."""
        if self._atr_ind_id and self._atr_ind_id in env and env[self._atr_ind_id] is not None:
            return float(env[self._atr_ind_id])
        # Fallback: compute ATR(14) directly
        try:
            highs  = [b.high  for b in sub]
            lows   = [b.low   for b in sub]
            closes = [b.close for b in sub]
            result = _atr_fn(highs, lows, closes, 14)
            val = _last(result)
            return val or 0.0
        except Exception:
            return 0.0
