"""Tests for algoforge.algo_gen.schema — validate_manifest and related helpers."""
from __future__ import annotations

import pytest

from algoforge.algo_gen.schema import (
    INDICATOR_KINDS,
    VALID_TIMEFRAMES,
    validate_manifest,
)
from algoforge.algo_gen.types import AlgoManifest


# ── Helpers ────────────────────────────────────────────────────────────────────

def _minimal() -> dict:
    """Return a minimal valid manifest dict."""
    return {
        "schema_version": "1.0",
        "name":           "test-algo",
        "description":    "A test algorithm.",
        "rationale":      "Testing purposes only.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi14", "kind": "rsi", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long", "when": "rsi14 < 30"},
        ],
        "exits": [
            {"side": "long", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {
            "size": "atr",
            "atr_mult": 1.5,
        },
    }


# ── Happy-path tests ───────────────────────────────────────────────────────────

class TestValidManifest:
    def test_returns_algo_manifest(self):
        m = validate_manifest(_minimal())
        assert isinstance(m, AlgoManifest)

    def test_name_preserved(self):
        m = validate_manifest(_minimal())
        assert m.name == "test-algo"

    def test_description_preserved(self):
        m = validate_manifest(_minimal())
        assert m.description == "A test algorithm."

    def test_indicators_parsed(self):
        m = validate_manifest(_minimal())
        assert len(m.indicators) == 1
        assert m.indicators[0].id   == "rsi14"
        assert m.indicators[0].kind == "rsi"
        assert m.indicators[0].params == {"period": 14}

    def test_entries_parsed(self):
        m = validate_manifest(_minimal())
        assert len(m.entries) == 1
        assert m.entries[0].side == "long"
        assert m.entries[0].when == "rsi14 < 30"

    def test_exits_bracket(self):
        m = validate_manifest(_minimal())
        assert m.exits[0].sl_atr == 1.5
        assert m.exits[0].tp_atr == 3.0

    def test_symbols_list(self):
        m = validate_manifest(_minimal())
        assert m.symbols == ["EURUSD"]

    def test_symbols_any(self):
        d = _minimal()
        d["symbols"] = "any"
        m = validate_manifest(d)
        assert m.symbols == "any"

    def test_multiple_timeframes(self):
        d = _minimal()
        d["timeframes"] = ["H1", "H4", "D1"]
        m = validate_manifest(d)
        assert m.timeframes == ["H1", "H4", "D1"]

    def test_all_valid_timeframes(self):
        for tf in VALID_TIMEFRAMES:
            d = _minimal()
            d["timeframes"] = [tf]
            m = validate_manifest(d)
            assert tf in m.timeframes

    def test_risk_defaults(self):
        d = _minimal()
        d["risk"] = {}
        m = validate_manifest(d)
        assert m.risk.size == "atr"
        assert m.risk.atr_mult == 1.5
        assert m.risk.max_concurrent == 1
        assert m.risk.hedge is False
        assert m.risk.cool_down_bars == 0

    def test_risk_fixed_size(self):
        d = _minimal()
        d["risk"] = {"size": "fixed", "fixed_lots": 0.05}
        m = validate_manifest(d)
        assert m.risk.size == "fixed"
        assert m.risk.fixed_lots == 0.05

    def test_escape_hatch_code(self):
        d = _minimal()
        d["code"] = "class GeneratedAlgo: pass"
        m = validate_manifest(d)
        assert m.code == "class GeneratedAlgo: pass"

    def test_cool_down_bars(self):
        d = _minimal()
        d["risk"]["cool_down_bars"] = 5
        m = validate_manifest(d)
        assert m.risk.cool_down_bars == 5

    def test_all_known_indicator_kinds(self):
        for kind in sorted(INDICATOR_KINDS)[:5]:  # spot-check 5
            d = _minimal()
            d["indicators"] = [{"id": "ind1", "kind": kind, "params": {}}]
            d["entries"] = [{"side": "long", "when": "close > 0"}]
            m = validate_manifest(d)
            assert m.indicators[0].kind == kind

    def test_multiple_indicators(self):
        d = _minimal()
        d["indicators"] = [
            {"id": "fast", "kind": "ema", "params": {"period": 10}},
            {"id": "slow", "kind": "ema", "params": {"period": 50}},
        ]
        d["entries"] = [{"side": "long", "when": "fast > slow"}]
        m = validate_manifest(d)
        assert len(m.indicators) == 2

    def test_short_entry(self):
        d = _minimal()
        d["entries"].append({"side": "short", "when": "rsi14 > 70"})
        m = validate_manifest(d)
        assert any(e.side == "short" for e in m.entries)

    def test_schema_version_stored(self):
        m = validate_manifest(_minimal())
        assert m.schema_version == "1.0"

    def test_exit_when_only(self):
        d = _minimal()
        d["exits"] = [{"side": "long", "when": "rsi14 > 70"}]
        m = validate_manifest(d)
        assert m.exits[0].when == "rsi14 > 70"

    def test_hedge_flag(self):
        d = _minimal()
        d["risk"]["hedge"] = True
        m = validate_manifest(d)
        assert m.risk.hedge is True


# ── Invalid schema tests ───────────────────────────────────────────────────────

class TestInvalidSchema:
    def test_not_dict_raises_type_error(self):
        with pytest.raises(TypeError):
            validate_manifest("not a dict")

    def test_missing_schema_version(self):
        d = _minimal()
        del d["schema_version"]
        with pytest.raises(ValueError, match="schema_version"):
            validate_manifest(d)

    def test_bad_schema_version(self):
        d = _minimal()
        d["schema_version"] = "99.0"
        with pytest.raises(ValueError, match="unsupported_schema_version"):
            validate_manifest(d)

    def test_missing_name(self):
        d = _minimal()
        del d["name"]
        with pytest.raises(ValueError, match="name"):
            validate_manifest(d)

    def test_name_too_long(self):
        d = _minimal()
        d["name"] = "a" * 49
        with pytest.raises(ValueError):
            validate_manifest(d)

    def test_name_not_kebab_case(self):
        d = _minimal()
        d["name"] = "MyAlgo"
        with pytest.raises(ValueError, match="kebab"):
            validate_manifest(d)

    def test_name_starts_with_digit(self):
        d = _minimal()
        d["name"] = "1-algo"
        with pytest.raises(ValueError):
            validate_manifest(d)

    def test_missing_description(self):
        d = _minimal()
        del d["description"]
        with pytest.raises(ValueError, match="description"):
            validate_manifest(d)

    def test_missing_rationale(self):
        d = _minimal()
        del d["rationale"]
        with pytest.raises(ValueError, match="rationale"):
            validate_manifest(d)

    def test_missing_timeframes(self):
        d = _minimal()
        del d["timeframes"]
        with pytest.raises(ValueError, match="timeframes"):
            validate_manifest(d)

    def test_empty_timeframes(self):
        d = _minimal()
        d["timeframes"] = []
        with pytest.raises(ValueError):
            validate_manifest(d)

    def test_invalid_timeframe(self):
        d = _minimal()
        d["timeframes"] = ["X99"]
        with pytest.raises(ValueError, match="timeframe"):
            validate_manifest(d)

    def test_missing_symbols(self):
        d = _minimal()
        del d["symbols"]
        with pytest.raises(ValueError, match="symbols"):
            validate_manifest(d)

    def test_symbols_invalid_string(self):
        d = _minimal()
        d["symbols"] = "all"
        with pytest.raises(ValueError):
            validate_manifest(d)

    def test_symbols_wrong_type(self):
        d = _minimal()
        d["symbols"] = 42
        with pytest.raises(ValueError):
            validate_manifest(d)

    def test_unknown_indicator_kind(self):
        d = _minimal()
        d["indicators"][0]["kind"] = "unknown_indicator"
        with pytest.raises(ValueError, match="unknown indicator kind"):
            validate_manifest(d)

    def test_indicator_missing_id(self):
        d = _minimal()
        d["indicators"][0].pop("id")
        with pytest.raises(ValueError, match="id"):
            validate_manifest(d)

    def test_indicator_invalid_id(self):
        d = _minimal()
        d["indicators"][0]["id"] = "not-an-identifier"
        with pytest.raises(ValueError):
            validate_manifest(d)

    def test_duplicate_indicator_ids(self):
        d = _minimal()
        d["indicators"] = [
            {"id": "same", "kind": "rsi", "params": {}},
            {"id": "same", "kind": "ema", "params": {}},
        ]
        with pytest.raises(ValueError, match="duplicate"):
            validate_manifest(d)

    def test_entry_missing_side(self):
        d = _minimal()
        d["entries"] = [{"when": "rsi14 < 30"}]
        with pytest.raises(ValueError, match="side"):
            validate_manifest(d)

    def test_entry_invalid_side(self):
        d = _minimal()
        d["entries"] = [{"side": "up", "when": "rsi14 < 30"}]
        with pytest.raises(ValueError, match="side"):
            validate_manifest(d)

    def test_exit_no_condition(self):
        d = _minimal()
        d["exits"] = [{"side": "long"}]
        with pytest.raises(ValueError, match="when"):
            validate_manifest(d)

    def test_risk_invalid_size_method(self):
        d = _minimal()
        d["risk"]["size"] = "kelly"
        with pytest.raises(ValueError, match="size"):
            validate_manifest(d)

    def test_risk_negative_cool_down(self):
        d = _minimal()
        d["risk"]["cool_down_bars"] = -1
        with pytest.raises(ValueError, match="cool_down_bars"):
            validate_manifest(d)

    def test_indicator_params_wrong_type(self):
        d = _minimal()
        d["indicators"][0]["params"] = {"period": "fourteen"}
        with pytest.raises(ValueError):
            validate_manifest(d)
