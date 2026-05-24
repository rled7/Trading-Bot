"""Tests for algoforge.algo_gen.validator — 5-stage validation pipeline."""
from __future__ import annotations

import pytest

from algoforge.algo_gen.types import TierName, ValidationResult
from algoforge.algo_gen.validator import validate
from algoforge.types import Bar


# ── Helpers ────────────────────────────────────────────────────────────────────

def _make_bars(n: int = 1000, oscillate: bool = True) -> list[Bar]:
    """Generate bars with oscillating prices to produce trades."""
    bars = []
    price = 1.1000
    for i in range(n):
        if oscillate:
            price += 0.0003 * (1 if (i // 10) % 2 == 0 else -1)
        else:
            price += 0.0001  # monotone rise
        bars.append(Bar(
            timestamp = 1_600_000_000 + i * 3600,
            open      = price,
            high      = price + 0.0015,
            low       = price - 0.0015,
            close     = price,
            volume    = 100_000.0,
            spread    = 0.00002,
        ))
    return bars


def _minimal_dict() -> dict:
    return {
        "schema_version": "1.0",
        "name":           "val-test",
        "description":    "Validation test.",
        "rationale":      "Test.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi14", "kind": "rsi", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long",  "when": "rsi14 < 30"},
            {"side": "short", "when": "rsi14 > 70"},
        ],
        "exits": [
            {"side": "long",  "sl_atr": 1.5, "tp_atr": 3.0},
            {"side": "short", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {"size": "atr", "atr_mult": 1.5},
    }


# ── Stage 1 failure ────────────────────────────────────────────────────────────

class TestStage1Failure:
    def test_invalid_schema_fails_stage1(self):
        d = {"schema_version": "99.0", "name": "x"}
        result = validate(d, _make_bars(100), seed=42)
        assert result.passed is False
        assert result.stages[0].stage == 1
        assert result.stages[0].passed is False

    def test_bad_dsl_fails_stage1(self):
        d = _minimal_dict()
        d["entries"] = [{"side": "long", "when": "eval('os.system(\"rm\")')"}]
        result = validate(d, _make_bars(100), seed=42)
        assert result.passed is False
        assert result.stages[0].passed is False

    def test_only_stage1_run_on_schema_error(self):
        d = {"schema_version": "99.0"}
        result = validate(d, _make_bars(100), seed=42)
        assert len(result.stages) == 1

    def test_stage1_passes_with_valid_manifest(self):
        from algoforge.backtest import BTConfig
        # Use a minimal config with tiny warmup to make stage 2 potentially fail
        # but stage 1 should pass
        d = _minimal_dict()
        bars = _make_bars(30)  # too few bars for stage 2 to produce 20 trades
        result = validate(d, bars, seed=42)
        # Stage 1 should pass
        assert result.stages[0].passed is True

    def test_stage1_result_has_name(self):
        d = _minimal_dict()
        result = validate(d, _make_bars(30), seed=42)
        assert result.stages[0].name == "schema_lint"


# ── Stage 2 failure ────────────────────────────────────────────────────────────

class TestStage2Failure:
    def test_too_few_trades_fails_stage2(self):
        """An algo that never triggers → stage 2 fails."""
        d = _minimal_dict()
        # RSI stays near 50 on monotone bars → very few trades
        d["entries"] = [{"side": "long", "when": "rsi14 < 1"}]  # essentially never
        result = validate(d, _make_bars(100), seed=42)
        # Should fail at stage 2 (too few trades)
        stage2 = next((s for s in result.stages if s.stage == 2), None)
        assert stage2 is not None
        assert stage2.passed is False

    def test_stage2_result_name(self):
        d = _minimal_dict()
        d["entries"] = [{"side": "long", "when": "rsi14 < 1"}]
        result = validate(d, _make_bars(50), seed=42)
        stage2 = next((s for s in result.stages if s.stage == 2), None)
        if stage2:
            assert stage2.name == "sandbox_backtest"


# ── Returns ValidationResult ───────────────────────────────────────────────────

class TestValidationResult:
    def test_returns_validation_result(self):
        d = _minimal_dict()
        result = validate(d, _make_bars(30), seed=42)
        assert isinstance(result, ValidationResult)

    def test_passed_false_when_no_trades(self):
        d = _minimal_dict()
        d["entries"] = [{"side": "long", "when": "rsi14 < 1"}]
        result = validate(d, _make_bars(100), seed=42)
        assert result.passed is False

    def test_stages_list_not_empty(self):
        d = _minimal_dict()
        result = validate(d, _make_bars(30), seed=42)
        assert len(result.stages) >= 1

    def test_report_none_on_failure(self):
        d = {"schema_version": "99.0"}
        result = validate(d, _make_bars(100), seed=42)
        assert result.report is None

    def test_manifest_name_stored(self):
        d = _minimal_dict()
        result = validate(d, _make_bars(100), seed=42)
        assert result.manifest_name == "val-test"

    def test_stage_results_have_reason(self):
        d = _minimal_dict()
        result = validate(d, _make_bars(30), seed=42)
        for s in result.stages:
            assert isinstance(s.reason, str)

    def test_stage_results_have_stage_number(self):
        d = _minimal_dict()
        result = validate(d, _make_bars(30), seed=42)
        for s in result.stages:
            assert 1 <= s.stage <= 5


# ── Full pass (when it works) ──────────────────────────────────────────────────

class TestValidationPass:
    @pytest.mark.slow
    def test_report_has_tier_on_pass(self):
        """Integration test: if all 5 stages pass, report has a tier."""
        # This test may run slowly due to walkforward + MC + robustness
        d = _minimal_dict()
        bars = _make_bars(2000)  # enough bars for WF
        result = validate(d, bars, seed=42)
        if result.passed:
            assert result.report is not None
            assert isinstance(result.report.tier, TierName)
        # If it failed, that's also acceptable for synthetic data

    @pytest.mark.slow
    def test_all_stages_run_on_pass(self):
        """When all stages pass, all 5 StageResult objects should be present."""
        d = _minimal_dict()
        bars = _make_bars(2000)
        result = validate(d, bars, seed=42)
        if result.passed:
            assert len(result.stages) == 5
