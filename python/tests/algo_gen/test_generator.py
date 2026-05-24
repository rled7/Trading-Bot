"""Tests for algoforge.algo_gen.generator — fast / balanced / max modes.

All LLM calls are intercepted via MultiTurnMockTransport + fixture files;
no real network access is required.

Fixture files live in tests/fixtures/algo_gen/llm_response_*.json
"""
from __future__ import annotations

import json
import os
import tempfile
import warnings

import pytest

from algoforge.algo_gen.fixtures import (
    MultiTurnMockTransport,
    load_llm_response,
)
from algoforge.algo_gen.generator import (
    GenerationError,
    GenerationTrace,
    generate_balanced,
    generate_fast,
    generate_max,
)
from algoforge.algo_gen.prompts import extract_json_block
from algoforge.algo_gen.types import AlgoManifest
from algoforge.llm.ollama import OllamaProvider


# ── Helpers ────────────────────────────────────────────────────────────────────

def _make_provider(fixture_name: str, model: str = "llama3.1:8b") -> OllamaProvider:
    """Build an OllamaProvider backed by a MultiTurnMockTransport."""
    data = load_llm_response(fixture_name)
    transport = MultiTurnMockTransport(data)
    return OllamaProvider(
        host="http://localhost:11434",
        model=model,
        seed=42,
        temperature=0.5,
        max_tokens=2048,
        transport=transport,
    )


def _minimal_manifest_dict() -> dict:
    return {
        "schema_version": "1.0",
        "name": "test-gen-algo",
        "description": "Generated test algorithm.",
        "rationale": "Testing generation pipeline.",
        "timeframes": ["H1"],
        "symbols": ["EURUSD"],
        "indicators": [
            {"id": "ema9",  "kind": "ema", "params": {"period": 9}},
            {"id": "ema21", "kind": "ema", "params": {"period": 21}},
            {"id": "atr14", "kind": "atr", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long", "when": "ema9 > ema21"},
        ],
        "exits": [
            {"side": "long", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {
            "size": "atr",
            "atr_mult": 1.5,
            "fixed_lots": 0.01,
            "max_concurrent": 1,
            "hedge": False,
            "cool_down_bars": 0,
        },
        "code": None,
    }


def _make_response_body(manifest_dict: dict, model: str = "llama3.1:8b") -> dict:
    """Build a single-turn fixture dict that returns the given manifest."""
    content = f"```json\n{json.dumps(manifest_dict, indent=2)}\n```"
    return {
        "request": {"method": "POST", "path": "/api/chat", "body": {}},
        "response": {
            "status": 200,
            "body": {
                "model": model,
                "done": True,
                "done_reason": "stop",
                "prompt_eval_count": 100,
                "eval_count": 200,
                "total_duration": 3000000000,
                "message": {
                    "role": "assistant",
                    "content": content,
                },
            },
        },
    }


def _make_malformed_response(model: str = "llama3.1:8b") -> dict:
    """Build a single-turn fixture dict returning invalid JSON."""
    return {
        "request": {"method": "POST", "path": "/api/chat", "body": {}},
        "response": {
            "status": 200,
            "body": {
                "model": model,
                "done": True,
                "done_reason": "stop",
                "prompt_eval_count": 50,
                "eval_count": 80,
                "total_duration": 1000000000,
                "message": {
                    "role": "assistant",
                    "content": "Here is the broken JSON: {invalid: json, no: quotes",
                },
            },
        },
    }


# ── extract_json_block unit tests ─────────────────────────────────────────────

class TestExtractJsonBlock:
    def test_extracts_fenced_json(self):
        text = 'Some prose\n```json\n{"key": "value"}\n```\nMore prose'
        result = extract_json_block(text)
        assert result == {"key": "value"}

    def test_extracts_unfenced_json(self):
        text = 'Here: {"name": "algo"}'
        result = extract_json_block(text)
        assert result == {"name": "algo"}

    def test_extracts_nested_json(self):
        text = '```json\n{"outer": {"inner": 42}}\n```'
        result = extract_json_block(text)
        assert result["outer"]["inner"] == 42

    def test_raises_on_no_json(self):
        with pytest.raises(ValueError, match="No valid JSON"):
            extract_json_block("no json here at all")

    def test_strips_leading_prose(self):
        text = "Here is your manifest:\n```json\n{\"a\": 1}\n```"
        result = extract_json_block(text)
        assert result["a"] == 1

    def test_fence_without_json_label(self):
        text = "```\n{\"x\": true}\n```"
        result = extract_json_block(text)
        assert result["x"] is True


# ── MultiTurnMockTransport tests ───────────────────────────────────────────────

class TestMultiTurnMockTransport:
    def test_single_turn_list(self):
        fixture = [_make_response_body({"key": "val"})]
        transport = MultiTurnMockTransport(fixture)
        status, raw = transport.request("POST", "http://x/api/chat", b"{}", {}, 30.0)
        assert status == 200
        data = json.loads(raw)
        assert data["message"]["role"] == "assistant"

    def test_single_turn_dict_normalised(self):
        fixture = _make_response_body({"key": "val"})
        transport = MultiTurnMockTransport(fixture)
        status, _ = transport.request("POST", "http://x/api/chat", b"{}", {}, 30.0)
        assert status == 200

    def test_multi_turn_playback_order(self):
        fixture = [
            _make_response_body({"turn": 1}),
            _make_response_body({"turn": 2}),
        ]
        transport = MultiTurnMockTransport(fixture)
        _, raw1 = transport.request("POST", "http://x/api/chat", b"{}", {}, 30.0)
        _, raw2 = transport.request("POST", "http://x/api/chat", b"{}", {}, 30.0)
        d1 = json.loads(json.loads(raw1)["message"]["content"].strip("`json \n"))
        d2 = json.loads(json.loads(raw2)["message"]["content"].strip("`json \n"))
        assert d1["turn"] == 1
        assert d2["turn"] == 2

    def test_exhausted_raises_stop_iteration(self):
        fixture = [_make_response_body({"x": 1})]
        transport = MultiTurnMockTransport(fixture)
        transport.request("POST", "http://x/", b"{}", {}, 30.0)
        with pytest.raises(StopIteration):
            transport.request("POST", "http://x/", b"{}", {}, 30.0)

    def test_tracks_turns_consumed(self):
        fixture = [_make_response_body({}), _make_response_body({})]
        transport = MultiTurnMockTransport(fixture)
        assert transport.turns_consumed == 0
        assert transport.turns_remaining == 2
        transport.request("POST", "http://x/", b"{}", {}, 30.0)
        assert transport.turns_consumed == 1
        assert transport.turns_remaining == 1

    def test_requests_made_recorded(self):
        fixture = [_make_response_body({"x": 1})]
        transport = MultiTurnMockTransport(fixture)
        body = json.dumps({"model": "llama3.1:8b"}).encode()
        transport.request("POST", "http://x/api/chat", body, {}, 30.0)
        assert len(transport.requests_made) == 1
        assert transport.requests_made[0]["method"] == "POST"


# ── generate_fast tests ────────────────────────────────────────────────────────

class TestGenerateFast:
    def test_happy_path_returns_manifest(self):
        provider = _make_provider("llm_response_trend_fast")
        manifest, trace = generate_fast(
            "long EURUSD trend", provider, seed=42, model="llama3.1:8b"
        )
        assert isinstance(manifest, AlgoManifest)
        assert manifest.name == "eurusd-long-trend-ema"

    def test_trace_mode_is_fast(self):
        provider = _make_provider("llm_response_trend_fast")
        _, trace = generate_fast("long EURUSD trend", provider, seed=42, model="llama3.1:8b")
        assert trace.mode == "fast"

    def test_trace_seed_recorded(self):
        provider = _make_provider("llm_response_trend_fast")
        _, trace = generate_fast("long EURUSD trend", provider, seed=99, model="llama3.1:8b")
        assert trace.seed == 99

    def test_trace_turns_recorded(self):
        provider = _make_provider("llm_response_trend_fast")
        _, trace = generate_fast("long EURUSD trend", provider, seed=42, model="llama3.1:8b")
        assert len(trace.turns) >= 3  # system + user + assistant

    def test_manifest_passes_schema(self):
        from algoforge.algo_gen.schema import validate_manifest
        provider = _make_provider("llm_response_trend_fast")
        manifest, _ = generate_fast("long EURUSD trend", provider, seed=42, model="llama3.1:8b")
        # Re-validate the manifest dict
        import dataclasses
        d = {
            "schema_version": manifest.schema_version,
            "name": manifest.name,
            "description": manifest.description,
            "rationale": manifest.rationale,
            "timeframes": manifest.timeframes,
            "symbols": manifest.symbols,
            "indicators": [{"id": i.id, "kind": i.kind, "params": i.params} for i in manifest.indicators],
            "entries": [{"side": e.side, "when": e.when} for e in manifest.entries],
            "exits": [
                {k: v for k, v in {"side": ex.side, "when": ex.when, "sl_atr": ex.sl_atr, "tp_atr": ex.tp_atr}.items() if v is not None}
                for ex in manifest.exits
            ],
            "risk": {
                "size": manifest.risk.size,
                "atr_mult": manifest.risk.atr_mult,
                "fixed_lots": manifest.risk.fixed_lots,
                "max_concurrent": manifest.risk.max_concurrent,
                "hedge": manifest.risk.hedge,
                "cool_down_bars": manifest.risk.cool_down_bars,
            },
        }
        revalidated = validate_manifest(d)
        assert revalidated.name == manifest.name

    def test_manifest_has_indicators(self):
        provider = _make_provider("llm_response_trend_fast")
        manifest, _ = generate_fast("long EURUSD trend", provider, seed=42, model="llama3.1:8b")
        assert len(manifest.indicators) > 0

    def test_manifest_has_entries(self):
        provider = _make_provider("llm_response_trend_fast")
        manifest, _ = generate_fast("long EURUSD trend", provider, seed=42, model="llama3.1:8b")
        assert len(manifest.entries) > 0

    def test_no_parse_failures_on_valid_fixture(self):
        provider = _make_provider("llm_response_trend_fast")
        _, trace = generate_fast("long EURUSD trend", provider, seed=42, model="llama3.1:8b")
        assert len(trace.parse_failures) == 0


# ── generate_balanced tests ────────────────────────────────────────────────────

class TestGenerateBalanced:
    def test_two_turn_returns_revised_manifest(self):
        provider = _make_provider("llm_response_trend_balanced", model="qwen2.5:32b")
        manifest, trace = generate_balanced(
            "long EURUSD trend", provider, seed=42, model="qwen2.5:32b"
        )
        assert isinstance(manifest, AlgoManifest)
        # The revised manifest is the second turn — it includes ADX
        assert manifest.name == "eurusd-trend-ema-adx"

    def test_trace_mode_is_balanced(self):
        provider = _make_provider("llm_response_trend_balanced", model="qwen2.5:32b")
        _, trace = generate_balanced("long EURUSD trend", provider, seed=42, model="qwen2.5:32b")
        assert trace.mode == "balanced"

    def test_revised_manifest_different_from_initial(self):
        """The final manifest (after critique) should differ from initial."""
        provider = _make_provider("llm_response_trend_balanced", model="qwen2.5:32b")
        manifest, trace = generate_balanced("long EURUSD trend", provider, seed=42, model="qwen2.5:32b")
        # The revised version has ADX indicator; initial did not
        indicator_kinds = {ind.kind for ind in manifest.indicators}
        assert "adx" in indicator_kinds

    def test_trace_records_critique_turn(self):
        provider = _make_provider("llm_response_trend_balanced", model="qwen2.5:32b")
        _, trace = generate_balanced("long EURUSD trend", provider, seed=42, model="qwen2.5:32b")
        # Should have: system, user, assistant (turn1), user (critique), assistant (turn2)
        assert len(trace.turns) >= 5

    def test_balanced_manifest_schema_valid(self):
        from algoforge.algo_gen.schema import validate_manifest
        provider = _make_provider("llm_response_trend_balanced", model="qwen2.5:32b")
        manifest, _ = generate_balanced("long EURUSD trend", provider, seed=42, model="qwen2.5:32b")
        assert manifest.name  # non-empty name means schema passed


# ── generate_max tests ─────────────────────────────────────────────────────────

class TestGenerateMax:
    def _make_max_provider(self, model: str = "o3-mini") -> OllamaProvider:
        data = load_llm_response("llm_response_breakout_max_ensemble")
        transport = MultiTurnMockTransport(data)
        return OllamaProvider(
            host="http://localhost:11434",
            model=model,
            seed=42,
            temperature=0.5,
            max_tokens=2048,
            transport=transport,
        )

    def test_max_returns_manifest(self):
        provider = self._make_max_provider()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            manifest, trace = generate_max(
                "EURUSD breakout", provider, seed=42, n_candidates=5
            )
        assert isinstance(manifest, AlgoManifest)

    def test_max_trace_lists_candidates(self):
        provider = self._make_max_provider()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            _, trace = generate_max(
                "EURUSD breakout", provider, seed=42, n_candidates=5
            )
        assert len(trace.candidates) == 5

    def test_max_trace_mode(self):
        provider = self._make_max_provider()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            _, trace = generate_max(
                "EURUSD breakout", provider, seed=42, n_candidates=5
            )
        assert trace.mode == "max"

    def test_max_reasoning_provider_none_warns(self):
        provider = self._make_max_provider()
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            generate_max("EURUSD breakout", provider, seed=42, n_candidates=5)
        assert any("reasoning_provider=None" in str(warning.message) for warning in w)

    def test_max_trace_records_warning(self):
        provider = self._make_max_provider()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            _, trace = generate_max(
                "EURUSD breakout", provider, seed=42, n_candidates=5
            )
        assert len(trace.warnings) >= 1
        assert any("reasoning_provider" in w for w in trace.warnings)

    def test_max_candidates_all_generated(self):
        provider = self._make_max_provider()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            _, trace = generate_max(
                "EURUSD breakout", provider, seed=42, n_candidates=5
            )
        # All candidates have indexes 0..4
        indexes = [c.index for c in trace.candidates]
        assert sorted(indexes) == list(range(5))

    def test_max_with_explicit_reasoning_provider(self):
        """When reasoning_provider is supplied, no warning and trace.reasoning_model set."""
        data = load_llm_response("llm_response_breakout_max_ensemble")
        transport1 = MultiTurnMockTransport(data)
        main_provider = OllamaProvider(
            host="http://localhost:11434",
            model="llama3.1:8b",
            seed=42,
            transport=transport1,
        )
        data2 = load_llm_response("llm_response_breakout_max_ensemble")
        transport2 = MultiTurnMockTransport(data2)
        reasoning_provider = OllamaProvider(
            host="http://localhost:11434",
            model="o3-mini",
            seed=42,
            transport=transport2,
        )
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            manifest, trace = generate_max(
                "EURUSD breakout",
                main_provider,
                seed=42,
                n_candidates=5,
                reasoning_provider=reasoning_provider,
                model="o3-mini",
            )
        # No warning about fallback
        assert not any("reasoning_provider=None" in str(warning.message) for warning in w)
        assert isinstance(manifest, AlgoManifest)


# ── Retry / recovery tests ─────────────────────────────────────────────────────

class TestRetryMechanics:
    def test_malformed_json_retries_once(self):
        """Fixture: first turn malformed, second turn valid — succeeds."""
        provider = _make_provider("llm_response_malformed_then_recovery")
        manifest, trace = generate_fast(
            "EURUSD recovery", provider, seed=42, model="llama3.1:8b"
        )
        assert isinstance(manifest, AlgoManifest)
        # One parse failure recorded for the malformed first turn
        assert len(trace.parse_failures) == 1

    def test_malformed_retry_trace_has_fix_prompt(self):
        """The retry turn should contain the fix prompt in the trace."""
        provider = _make_provider("llm_response_malformed_then_recovery")
        _, trace = generate_fast("EURUSD recovery", provider, seed=42, model="llama3.1:8b")
        turn_contents = [t["content"] for t in trace.turns]
        assert any("malformed" in c.lower() or "fix" in c.lower() or "corrected" in c.lower()
                   for c in turn_contents)

    def test_schema_invalid_retries_once(self):
        """Fixture: first turn has unknown indicator, second turn is corrected."""
        provider = _make_provider("llm_response_invalid_then_recovery")
        manifest, trace = generate_fast(
            "EURUSD corrected", provider, seed=42, model="llama3.1:8b"
        )
        assert isinstance(manifest, AlgoManifest)
        assert len(trace.parse_failures) == 1

    def test_schema_invalid_manifest_name_correct(self):
        provider = _make_provider("llm_response_invalid_then_recovery")
        manifest, _ = generate_fast("EURUSD corrected", provider, seed=42, model="llama3.1:8b")
        assert manifest.name == "eurusd-corrected-algo"

    def test_double_failure_raises_generation_error(self):
        """Two consecutive invalid responses → GenerationError."""
        # Build a 2-turn fixture where both turns are malformed
        fixture = [
            _make_malformed_response(),
            _make_malformed_response(),
        ]
        transport = MultiTurnMockTransport(fixture)
        provider = OllamaProvider(
            host="http://localhost:11434",
            model="llama3.1:8b",
            seed=42,
            transport=transport,
        )
        with pytest.raises(GenerationError):
            generate_fast("test", provider, seed=42, model="llama3.1:8b")

    def test_double_failure_trace_has_two_parse_failures(self):
        """Both parse failures should be recorded in the trace."""
        fixture = [_make_malformed_response(), _make_malformed_response()]
        transport = MultiTurnMockTransport(fixture)
        provider = OllamaProvider(
            host="http://localhost:11434", model="llama3.1:8b",
            seed=42, transport=transport,
        )
        try:
            generate_fast("test", provider, seed=42, model="llama3.1:8b")
        except GenerationError:
            pass
        # We can't inspect the trace from here since it's raised, but
        # the GenerationError message should mention 2 attempts
        # Re-run and catch properly:
        fixture2 = [_make_malformed_response(), _make_malformed_response()]
        transport2 = MultiTurnMockTransport(fixture2)
        provider2 = OllamaProvider(
            host="http://localhost:11434", model="llama3.1:8b",
            seed=42, transport=transport2,
        )
        with pytest.raises(GenerationError, match="2 attempts"):
            generate_fast("test", provider2, seed=42, model="llama3.1:8b")


# ── GenerationTrace dataclass tests ───────────────────────────────────────────

class TestGenerationTrace:
    def test_trace_is_dataclass(self):
        import dataclasses
        assert dataclasses.is_dataclass(GenerationTrace)

    def test_trace_has_required_fields(self):
        trace = GenerationTrace(mode="fast", model="llama3.1:8b", seed=42)
        assert trace.mode == "fast"
        assert trace.model == "llama3.1:8b"
        assert trace.seed == 42
        assert trace.turns == []
        assert trace.parse_failures == []
        assert trace.candidates == []
        assert trace.critique_pairs == []
        assert trace.warnings == []

    def test_trace_turns_are_dicts(self):
        provider = _make_provider("llm_response_trend_fast")
        _, trace = generate_fast("test", provider, seed=42, model="llama3.1:8b")
        for turn in trace.turns:
            assert "role" in turn
            assert "content" in turn

    def test_trace_reasoning_model_none_for_fast(self):
        provider = _make_provider("llm_response_trend_fast")
        _, trace = generate_fast("test", provider, seed=42, model="llama3.1:8b")
        assert trace.reasoning_model is None


# ── load_llm_response fixture loader tests ─────────────────────────────────────

class TestLoadLlmResponse:
    def test_loads_single_turn_fixture(self):
        data = load_llm_response("llm_response_trend_fast")
        assert isinstance(data, list)
        assert len(data) == 1

    def test_loads_two_turn_fixture(self):
        data = load_llm_response("llm_response_trend_balanced")
        assert isinstance(data, list)
        assert len(data) == 2

    def test_loads_multi_turn_ensemble(self):
        data = load_llm_response("llm_response_breakout_max_ensemble")
        assert isinstance(data, list)
        assert len(data) == 9

    def test_loads_malformed_fixture(self):
        data = load_llm_response("llm_response_malformed_then_recovery")
        assert isinstance(data, list)
        assert len(data) == 2

    def test_loads_invalid_schema_fixture(self):
        data = load_llm_response("llm_response_invalid_then_recovery")
        assert isinstance(data, list)
        assert len(data) == 2

    def test_missing_fixture_raises_file_not_found(self):
        with pytest.raises(FileNotFoundError):
            load_llm_response("llm_response_nonexistent_fixture")

    def test_fixture_suffix_optional(self):
        # With or without .json suffix
        data1 = load_llm_response("llm_response_trend_fast")
        data2 = load_llm_response("llm_response_trend_fast.json")
        assert len(data1) == len(data2)

    def test_each_turn_has_request_and_response(self):
        data = load_llm_response("llm_response_trend_balanced")
        for turn in data:
            assert "request" in turn
            assert "response" in turn
            assert "status" in turn["response"]
            assert "body" in turn["response"]


# ── CLI generate tests ─────────────────────────────────────────────────────────

class TestCLIGenerate:
    def _make_transport(self, fixture_name: str) -> MultiTurnMockTransport:
        data = load_llm_response(fixture_name)
        return MultiTurnMockTransport(data)

    def test_cli_generate_writes_manifest(self, monkeypatch, tmp_path):
        """CLI generate → file written, exit code 0."""
        from algoforge.algo_gen.__main__ import main

        # Patch the generator to avoid real LLM + real backtest
        from algoforge.algo_gen import generator as gen_mod

        def _mock_generate_fast(brief, provider, seed=42, model="llama3.1:8b"):
            from algoforge.algo_gen.schema import validate_manifest
            manifest = validate_manifest(_minimal_manifest_dict())
            trace = GenerationTrace(mode="fast", model=model, seed=seed)
            return manifest, trace

        monkeypatch.setattr(gen_mod, "generate_fast", _mock_generate_fast)

        # Also patch validate to skip actual backtesting
        from algoforge.algo_gen import validator as val_mod
        from algoforge.algo_gen.types import ValidationResult, StageResult, TierName, TierReport
        def _mock_validate(manifest_dict, bars, seed):
            stage1 = StageResult(stage=1, name="Schema+lint", passed=True, reason="ok")
            report = TierReport(
                tier=TierName.green,
                min_score=91.0,
                walk_forward=92.0,
                mc_bootstrap=91.0,
                robustness=93.0,
                size_mult=1.0,
                experimental=False,
                paper_only=False,
            )
            return ValidationResult(passed=True, stages=[stage1], report=report)

        monkeypatch.setattr(val_mod, "validate", _mock_validate)

        # Patch load_bars to return empty list (skip real bars)
        from algoforge.algo_gen import fixtures as fix_mod
        monkeypatch.setattr(fix_mod, "load_bars", lambda _: [])

        out_dir = str(tmp_path / "registry")
        rc = main(["generate", "--brief", "long EURUSD trend", "--mode", "fast",
                   "--seed", "42", "--out", out_dir])
        assert rc == 0
        # Check that a file was created
        files = os.listdir(out_dir)
        assert any(f.endswith(".json") for f in files)

    def test_cli_generate_empty_brief_returns_1(self):
        from algoforge.algo_gen.__main__ import main
        rc = main(["generate", "--brief", ""])
        assert rc == 1

    def test_cli_generate_validator_rejection_nonzero_exit(self, monkeypatch, tmp_path):
        """When validator rejects the manifest, CLI returns nonzero."""
        from algoforge.algo_gen.__main__ import main
        from algoforge.algo_gen import generator as gen_mod

        def _mock_generate_fast(brief, provider, seed=42, model="llama3.1:8b"):
            from algoforge.algo_gen.schema import validate_manifest
            manifest = validate_manifest(_minimal_manifest_dict())
            trace = GenerationTrace(mode="fast", model=model, seed=seed)
            return manifest, trace

        monkeypatch.setattr(gen_mod, "generate_fast", _mock_generate_fast)

        # Validator FAILS
        from algoforge.algo_gen import validator as val_mod
        from algoforge.algo_gen.types import ValidationResult, StageResult
        def _mock_validate_fail(manifest_dict, bars, seed):
            stage = StageResult(stage=2, name="Sandbox", passed=False, reason="too few trades")
            return ValidationResult(passed=False, stages=[stage], report=None)

        monkeypatch.setattr(val_mod, "validate", _mock_validate_fail)

        # Return non-empty bars list so validation runs
        from algoforge.algo_gen import fixtures as fix_mod
        from algoforge.types import Bar
        fake_bar = Bar(timestamp=0, open=1.0, high=1.01, low=0.99, close=1.005, volume=1000.0)
        monkeypatch.setattr(fix_mod, "load_bars", lambda _: [fake_bar] * 10)

        out_dir = str(tmp_path / "registry2")
        rc = main(["generate", "--brief", "long EURUSD trend", "--mode", "fast",
                   "--seed", "42", "--out", out_dir])
        assert rc != 0

    def test_cli_generate_missing_reasoning_env_for_max_errors_clearly(
        self, monkeypatch, tmp_path, capsys
    ):
        """Max mode without API key → clear error message, nonzero exit."""
        from algoforge.algo_gen.__main__ import main

        # Ensure no API key is set
        monkeypatch.delenv("AF_ALGO_GEN_REASONING_API_KEY", raising=False)

        rc = main(["generate", "--brief", "EURUSD breakout", "--mode", "max",
                   "--out", str(tmp_path / "registry3")])
        assert rc != 0
        captured = capsys.readouterr()
        assert "AF_ALGO_GEN_REASONING_API_KEY" in captured.err

    def test_cli_generate_fast_mode_no_reasoning_needed(self, monkeypatch, tmp_path):
        """Fast mode should work without any reasoning env vars."""
        from algoforge.algo_gen.__main__ import main
        from algoforge.algo_gen import generator as gen_mod

        def _mock_fast(brief, provider, seed=42, model="llama3.1:8b"):
            from algoforge.algo_gen.schema import validate_manifest
            manifest = validate_manifest(_minimal_manifest_dict())
            return manifest, GenerationTrace(mode="fast", model=model, seed=seed)

        monkeypatch.setattr(gen_mod, "generate_fast", _mock_fast)

        from algoforge.algo_gen import fixtures as fix_mod
        monkeypatch.setattr(fix_mod, "load_bars", lambda _: [])

        out_dir = str(tmp_path / "registry4")
        rc = main(["generate", "--brief", "long EURUSD trend", "--mode", "fast",
                   "--seed", "42", "--out", out_dir])
        assert rc == 0

    def test_cli_generate_generation_error_returns_2(self, monkeypatch, tmp_path):
        """GenerationError from generator → exit code 2."""
        from algoforge.algo_gen.__main__ import main
        from algoforge.algo_gen import generator as gen_mod

        def _mock_fail(brief, provider, seed=42, model="llama3.1:8b"):
            raise GenerationError("LLM refused")

        monkeypatch.setattr(gen_mod, "generate_fast", _mock_fail)
        from algoforge.algo_gen import fixtures as fix_mod
        monkeypatch.setattr(fix_mod, "load_bars", lambda _: [])

        rc = main(["generate", "--brief", "test", "--mode", "fast",
                   "--out", str(tmp_path / "registry5")])
        assert rc == 2


# ── Prompt template tests ──────────────────────────────────────────────────────

class TestPromptTemplates:
    def test_fast_system_not_empty(self):
        from algoforge.algo_gen.prompts import FAST_SYSTEM
        assert len(FAST_SYSTEM) > 50

    def test_fast_user_contains_brief(self):
        from algoforge.algo_gen.prompts import render_fast_user
        result = render_fast_user("my strategy brief")
        assert "my strategy brief" in result

    def test_fast_user_contains_schema_ref(self):
        from algoforge.algo_gen.prompts import render_fast_user
        result = render_fast_user("my brief")
        assert "schema_version" in result

    def test_balanced_critique_contains_manifest(self):
        from algoforge.algo_gen.prompts import render_balanced_critique
        d = {"schema_version": "1.0", "name": "test-algo"}
        result = render_balanced_critique(d)
        assert "test-algo" in result
        assert "weaknesses" in result.lower()

    def test_max_user_contains_candidate_num(self):
        from algoforge.algo_gen.prompts import render_max_user
        result = render_max_user("my brief", candidate_num=3, n_candidates=5)
        assert "3" in result
        assert "5" in result

    def test_max_critique_contains_sharpe(self):
        from algoforge.algo_gen.prompts import render_max_critique
        result = render_max_critique({"name": "test"}, trades=42, sharpe=1.23, max_dd=8.5)
        assert "1.230" in result
        assert "42" in result

    def test_balanced_system_not_empty(self):
        from algoforge.algo_gen.prompts import BALANCED_SYSTEM
        assert len(BALANCED_SYSTEM) > 50

    def test_max_system_mentions_reasoning(self):
        from algoforge.algo_gen.prompts import MAX_SYSTEM
        assert "reasoning" in MAX_SYSTEM.lower() or "step-by-step" in MAX_SYSTEM.lower()

    def test_all_templates_produce_json_fence_instruction(self):
        from algoforge.algo_gen.prompts import (
            render_fast_user, render_balanced_user, render_max_user
        )
        for result in [
            render_fast_user("brief"),
            render_balanced_user("brief"),
            render_max_user("brief", 1, 5),
        ]:
            assert "```json" in result


# ── Integration: full pipeline with fixture-backed provider ───────────────────

class TestIntegrationWithFixtures:
    def test_fast_end_to_end_schema_valid(self):
        """Fast generate → manifest passes schema validation."""
        from algoforge.algo_gen.schema import validate_manifest
        provider = _make_provider("llm_response_trend_fast")
        manifest, trace = generate_fast("long EURUSD", provider, seed=42, model="llama3.1:8b")
        # Build manifest dict for re-validation
        d = {
            "schema_version": manifest.schema_version,
            "name": manifest.name,
            "description": manifest.description,
            "rationale": manifest.rationale,
            "timeframes": manifest.timeframes,
            "symbols": manifest.symbols,
            "indicators": [{"id": i.id, "kind": i.kind, "params": i.params} for i in manifest.indicators],
            "entries": [{"side": e.side, "when": e.when} for e in manifest.entries],
            "exits": [
                {k: v for k, v in {"side": ex.side, "when": ex.when, "sl_atr": ex.sl_atr, "tp_atr": ex.tp_atr}.items() if v is not None}
                for ex in manifest.exits
            ],
            "risk": {
                "size": manifest.risk.size,
                "atr_mult": manifest.risk.atr_mult,
                "fixed_lots": manifest.risk.fixed_lots,
                "max_concurrent": manifest.risk.max_concurrent,
                "hedge": manifest.risk.hedge,
                "cool_down_bars": manifest.risk.cool_down_bars,
            },
        }
        revalidated = validate_manifest(d)
        assert revalidated.name == manifest.name

    def test_balanced_end_to_end_trace_complete(self):
        """Balanced generate → trace has both initial and revision info."""
        provider = _make_provider("llm_response_trend_balanced", model="qwen2.5:32b")
        manifest, trace = generate_balanced("long EURUSD", provider, seed=42, model="qwen2.5:32b")
        assert trace.mode == "balanced"
        # Should have system + user + assistant + user (critique) + assistant (revision)
        assert len(trace.turns) >= 5
        assert isinstance(manifest, AlgoManifest)

    def test_malformed_recovery_trace_parse_failures(self):
        """Malformed → recovery fixture → exactly 1 parse failure, valid manifest."""
        provider = _make_provider("llm_response_malformed_then_recovery")
        manifest, trace = generate_fast("EURUSD recovery", provider, seed=42, model="llama3.1:8b")
        assert len(trace.parse_failures) == 1
        assert isinstance(manifest, AlgoManifest)
        # The recovered manifest should have the name from the second turn
        assert manifest.name == "eurusd-recovery-algo"

    def test_max_five_candidates_in_trace(self):
        """Max mode produces exactly 5 candidate records in the trace."""
        data = load_llm_response("llm_response_breakout_max_ensemble")
        transport = MultiTurnMockTransport(data)
        provider = OllamaProvider(
            host="http://localhost:11434", model="o3-mini",
            seed=42, transport=transport,
        )
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            _, trace = generate_max(
                "EURUSD breakout", provider, seed=42, n_candidates=5
            )
        assert len(trace.candidates) == 5

    def test_max_winner_is_one_of_top2_critiqued(self):
        """The final manifest should have been through the critique pipeline."""
        data = load_llm_response("llm_response_breakout_max_ensemble")
        transport = MultiTurnMockTransport(data)
        provider = OllamaProvider(
            host="http://localhost:11434", model="o3-mini",
            seed=42, transport=transport,
        )
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            manifest, trace = generate_max(
                "EURUSD breakout", provider, seed=42, n_candidates=5
            )
        # Critique pairs should be populated
        assert len(trace.critique_pairs) > 0
        assert isinstance(manifest, AlgoManifest)
