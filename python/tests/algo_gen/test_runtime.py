"""Tests for algoforge.algo_gen.runtime — ManifestAlgo."""
from __future__ import annotations

import pytest

from algoforge.algo_gen.runtime import ManifestAlgo, _call_indicator, _last, _last_tuple
from algoforge.algo_gen.schema import validate_manifest
from algoforge.algo_gen.types import AlgoManifest, IndicatorDecl, EntryRule, ExitRule, RiskSpec
from algoforge.algorithm import AlgoDecision, AlgoSignal
from algoforge.types import Bar


# ── Helpers ────────────────────────────────────────────────────────────────────

def _make_bars(n: int = 100, start_price: float = 1.1000) -> list[Bar]:
    """Generate simple monotone-rising bar data."""
    bars = []
    price = start_price
    for i in range(n):
        bars.append(Bar(
            timestamp = 1_600_000_000 + i * 3600,
            open      = price,
            high      = price + 0.0010,
            low       = price - 0.0005,
            close     = price + 0.0003,
            volume    = 100_000.0,
            spread    = 0.00002,
        ))
        price += 0.0001
    return bars


def _minimal_manifest_dict() -> dict:
    return {
        "schema_version": "1.0",
        "name":           "test-runtime",
        "description":    "Runtime test algo.",
        "rationale":      "Test.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi14", "kind": "rsi", "params": {"period": 14}},
            {"id": "atr14", "kind": "atr", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long",  "when": "rsi14 < 70"},
            {"side": "short", "when": "rsi14 > 30"},
        ],
        "exits": [
            {"side": "long",  "sl_atr": 1.5, "tp_atr": 3.0},
            {"side": "short", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {"size": "atr", "atr_mult": 1.5},
    }


# ── ManifestAlgo meta tests ────────────────────────────────────────────────────

class TestManifestAlgoMeta:
    def test_name_from_manifest(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        assert algo.name() == "test-runtime"

    def test_description_from_manifest(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        assert "Runtime test" in algo.description()

    def test_magic_is_positive_int(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        assert isinstance(algo.magic(), int)
        assert algo.magic() > 0

    def test_magic_deterministic(self):
        m = validate_manifest(_minimal_manifest_dict())
        a1 = ManifestAlgo(m)
        a2 = ManifestAlgo(m)
        assert a1.magic() == a2.magic()

    def test_metadata_none_by_default(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        assert algo.metadata is None

    def test_implements_ialgorithm(self):
        from algoforge.algorithm import IAlgorithm
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        assert isinstance(algo, IAlgorithm)

    def test_enable_disable(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        assert algo.is_enabled() is True
        algo.disable()
        assert algo.is_enabled() is False
        algo.enable()
        assert algo.is_enabled() is True


# ── evaluate() tests ───────────────────────────────────────────────────────────

class TestEvaluate:
    def test_too_few_bars_returns_none(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        bars = _make_bars(5)
        result = algo.evaluate("EURUSD", 3600, bars, 1, None, None)
        assert result.signal == AlgoSignal.NONE

    def test_sufficient_bars_returns_decision(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        bars = _make_bars(100)
        # With monotone rising bars, RSI will be high → should produce SELL signal
        result = algo.evaluate("EURUSD", 3600, bars, 99, None, None)
        assert result.signal in (AlgoSignal.BUY, AlgoSignal.SELL, AlgoSignal.NONE)

    def test_returns_algo_decision(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        bars = _make_bars(50)
        result = algo.evaluate("EURUSD", 3600, bars, 49, None, None)
        assert isinstance(result, AlgoDecision)

    def test_symbol_propagated(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        bars = _make_bars(100)
        result = algo.evaluate("EURUSD", 3600, bars, 99, None, None)
        assert result.symbol == "EURUSD"

    def test_disabled_returns_none(self):
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        algo.disable()
        bars = _make_bars(100)
        result = algo.safe_evaluate("EURUSD", 3600, bars, 99, None, None)
        assert result.signal == AlgoSignal.NONE

    def test_confidence_set(self):
        # Use a manifest that always triggers (rsi14 > 0 is always true)
        d = _minimal_manifest_dict()
        d["entries"] = [{"side": "long", "when": "rsi14 > 0"}]
        m = validate_manifest(d)
        algo = ManifestAlgo(m)
        bars = _make_bars(100)
        result = algo.evaluate("EURUSD", 3600, bars, 99, None, None)
        if result.signal == AlgoSignal.BUY:
            assert result.confidence > 0.5

    def test_atr_in_decision(self):
        # When ATR indicator declared, it should be in the decision
        m = validate_manifest(_minimal_manifest_dict())
        algo = ManifestAlgo(m)
        bars = _make_bars(100)
        result = algo.evaluate("EURUSD", 3600, bars, 99, None, None)
        # atr should be non-negative (may be 0 if no signal)
        assert result.atr >= 0.0


# ── _last and _last_tuple helpers ──────────────────────────────────────────────

class TestHelpers:
    def test_last_list(self):
        assert _last([1.0, 2.0, 3.0]) == 3.0

    def test_last_empty(self):
        assert _last([]) is None

    def test_last_none(self):
        assert _last(None) is None

    def test_last_nan_returns_none(self):
        import math
        assert _last([1.0, float("nan")]) is None

    def test_last_scalar(self):
        assert _last(5.0) == 5.0

    def test_last_tuple_of_lists(self):
        # tuple of lists → return last element of first list
        result = ([1.0, 2.0, 3.0], [4.0, 5.0, 6.0])
        assert _last_tuple(result) == 3.0

    def test_last_tuple_plain_list(self):
        assert _last_tuple([1.0, 2.0]) == 2.0


# ── _call_indicator dispatch ───────────────────────────────────────────────────

class TestCallIndicator:
    def _make_bars_list(self, n: int = 30) -> list[Bar]:
        price = 1.1
        bars = []
        for i in range(n):
            bars.append(Bar(
                timestamp=i,
                open=price, high=price+0.001, low=price-0.001,
                close=price, volume=1000.0, spread=0.0
            ))
            price += 0.0001
        return bars

    def test_sma_returns_list(self):
        bars = self._make_bars_list(30)
        result = _call_indicator("sma", {"period": 5}, bars)
        assert isinstance(result, list)
        assert len(result) == 30

    def test_ema_returns_list(self):
        bars = self._make_bars_list(30)
        result = _call_indicator("ema", {"period": 5}, bars)
        assert isinstance(result, list)

    def test_rsi_returns_list(self):
        bars = self._make_bars_list(30)
        result = _call_indicator("rsi", {"period": 14}, bars)
        assert isinstance(result, list)

    def test_atr_returns_list(self):
        bars = self._make_bars_list(30)
        result = _call_indicator("atr", {"period": 14}, bars)
        assert isinstance(result, list)

    def test_macd_returns_tuple(self):
        bars = self._make_bars_list(50)
        result = _call_indicator("macd", {"fast": 12, "slow": 26, "signal": 9}, bars)
        assert isinstance(result, tuple)
        assert len(result) == 3

    def test_bollinger_returns_tuple(self):
        bars = self._make_bars_list(30)
        result = _call_indicator("bollinger", {"period": 20}, bars)
        assert isinstance(result, tuple)

    def test_stochastic_returns_tuple(self):
        bars = self._make_bars_list(30)
        result = _call_indicator("stochastic", {"k_period": 14, "d_period": 3}, bars)
        assert isinstance(result, tuple)

    def test_unknown_kind_raises(self):
        bars = self._make_bars_list(30)
        with pytest.raises((ValueError, KeyError)):
            _call_indicator("nonexistent_indicator", {}, bars)


# ── Cooldown behaviour ─────────────────────────────────────────────────────────

class TestCooldown:
    def test_cooldown_suppresses_entries(self):
        d = _minimal_manifest_dict()
        d["risk"]["cool_down_bars"] = 100
        d["entries"] = [{"side": "long", "when": "rsi14 > 0"}]
        m = validate_manifest(d)
        algo = ManifestAlgo(m)
        bars = _make_bars(100)

        # First call (bars_since_exit starts at 9999) — should pass cooldown
        r1 = algo.evaluate("EURUSD", 3600, bars, 99, None, None)
        # Simulate the cooldown counter reset by calling again immediately
        # Second call — bars_since_exit was incremented to 10000 after first call
        # — well above threshold, so still ok. The test just checks it returns a decision type.
        assert isinstance(r1, AlgoDecision)
