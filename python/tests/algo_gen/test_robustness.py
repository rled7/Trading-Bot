"""Tests for algoforge.algo_gen.robustness — parameter_robustness."""
from __future__ import annotations

import pytest

from algoforge.algo_gen.robustness import (
    _apply_param,
    _collect_numeric_params,
    parameter_robustness,
)
from algoforge.algo_gen.schema import validate_manifest
from algoforge.algo_gen.types import AlgoManifest, IndicatorDecl, RiskSpec
from algoforge.backtest import BTConfig
from algoforge.types import Bar


# ── Helpers ────────────────────────────────────────────────────────────────────

def _make_bars(n: int = 50) -> list[Bar]:
    """Generate simple trending bar data."""
    bars = []
    price = 1.1000
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
    """A manifest with a single simple indicator for fast testing."""
    return {
        "schema_version": "1.0",
        "name":           "robustness-test",
        "description":    "Robustness test algo.",
        "rationale":      "Test.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi14",  "kind": "rsi",  "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long", "when": "rsi14 < 70"},
        ],
        "exits": [
            {"side": "long", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {"size": "atr", "atr_mult": 1.5},
    }


# Fast BTConfig that minimizes computation overhead
_FAST_CFG = BTConfig(warmup_bars=10, initial_capital=10_000.0)


# ── _collect_numeric_params ────────────────────────────────────────────────────

class TestCollectNumericParams:
    def test_returns_list(self):
        m = validate_manifest(_minimal_manifest_dict())
        params = _collect_numeric_params(m)
        assert isinstance(params, list)

    def test_includes_indicator_params(self):
        m = validate_manifest(_minimal_manifest_dict())
        params = _collect_numeric_params(m)
        paths = [p for p, _ in params]
        assert "ind.rsi14.period" in paths

    def test_includes_risk_params(self):
        m = validate_manifest(_minimal_manifest_dict())
        params = _collect_numeric_params(m)
        paths = [p for p, _ in params]
        assert "risk.atr_mult" in paths

    def test_values_are_floats(self):
        m = validate_manifest(_minimal_manifest_dict())
        params = _collect_numeric_params(m)
        for path, val in params:
            assert isinstance(val, float)

    def test_no_params_returns_empty_or_risk_only(self):
        d = _minimal_manifest_dict()
        d["indicators"] = []
        d["entries"] = [{"side": "long", "when": "close > 0"}]
        m = validate_manifest(d)
        params = _collect_numeric_params(m)
        # Should at least have risk.atr_mult
        assert len(params) >= 1


# ── _apply_param ───────────────────────────────────────────────────────────────

class TestApplyParam:
    def test_changes_indicator_period(self):
        m = validate_manifest(_minimal_manifest_dict())
        m2 = _apply_param(m, "ind.rsi14.period", 20.0)
        rsi = next(i for i in m2.indicators if i.id == "rsi14")
        assert int(rsi.params["period"]) == 20

    def test_does_not_modify_other_indicators(self):
        # Use a manifest with TWO indicators to test isolation
        d = _minimal_manifest_dict()
        d["indicators"].append({"id": "ema20", "kind": "ema", "params": {"period": 20}})
        d["entries"] = [{"side": "long", "when": "rsi14 < 70"}]
        m = validate_manifest(d)
        m2 = _apply_param(m, "ind.rsi14.period", 25.0)
        # ema20 should be unchanged
        ema = next(i for i in m2.indicators if i.id == "ema20")
        assert ema.params["period"] == 20  # unchanged

    def test_changes_risk_atr_mult(self):
        m = validate_manifest(_minimal_manifest_dict())
        m2 = _apply_param(m, "risk.atr_mult", 2.0)
        assert m2.risk.atr_mult == pytest.approx(2.0)

    def test_original_not_mutated(self):
        m = validate_manifest(_minimal_manifest_dict())
        orig_period = m.indicators[0].params["period"]
        _apply_param(m, "ind.rsi14.period", 999.0)
        assert m.indicators[0].params["period"] == orig_period

    def test_risk_cool_down_bars(self):
        d = _minimal_manifest_dict()
        d["risk"]["cool_down_bars"] = 5
        m = validate_manifest(d)
        m2 = _apply_param(m, "risk.cool_down_bars", 10.0)
        assert m2.risk.cool_down_bars == 10

    def test_atr_mult_floored_at_min(self):
        m = validate_manifest(_minimal_manifest_dict())
        m2 = _apply_param(m, "risk.atr_mult", -1.0)
        assert m2.risk.atr_mult >= 0.0


# ── parameter_robustness ───────────────────────────────────────────────────────

@pytest.mark.slow
class TestParameterRobustness:
    """Integration tests for parameter_robustness.

    Marked @slow because the backtest engine is O(n²) in bar count via
    ManifestAlgo._build_env recomputing indicators on each call.
    Run selectively with: pytest -m slow
    """

    def test_returns_float(self):
        """Quick integration test using minimal bars and fast config."""
        m = validate_manifest(_minimal_manifest_dict())
        bars = _make_bars(50)
        score = parameter_robustness(m, bars, seed=42, cfg=_FAST_CFG)
        assert isinstance(score, float)

    def test_score_in_range(self):
        m = validate_manifest(_minimal_manifest_dict())
        bars = _make_bars(50)
        score = parameter_robustness(m, bars, seed=42, cfg=_FAST_CFG)
        assert 0.0 <= score <= 100.0

    def test_deterministic_with_same_seed(self):
        m = validate_manifest(_minimal_manifest_dict())
        bars = _make_bars(50)
        s1 = parameter_robustness(m, bars, seed=42, cfg=_FAST_CFG)
        s2 = parameter_robustness(m, bars, seed=42, cfg=_FAST_CFG)
        assert s1 == s2
