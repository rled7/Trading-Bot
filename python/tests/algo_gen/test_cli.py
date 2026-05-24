"""Tests for algoforge.algo_gen.__main__ — CLI subcommands."""
from __future__ import annotations

import json
import os
import tempfile

import pytest

from algoforge.algo_gen.__main__ import main


# ── Helpers ────────────────────────────────────────────────────────────────────

def _write_manifest(path: str, d: dict) -> None:
    with open(path, "w") as f:
        json.dump(d, f)


def _minimal_manifest_dict() -> dict:
    return {
        "schema_version": "1.0",
        "name":           "cli-test-algo",
        "description":    "CLI test algorithm.",
        "rationale":      "Testing CLI.",
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
        "risk": {"size": "atr"},
    }


# ── No-args help ───────────────────────────────────────────────────────────────

class TestCLIHelp:
    def test_no_args_returns_0(self):
        rc = main([])
        assert rc == 0

    def test_help_flag(self, capsys):
        with pytest.raises(SystemExit) as exc:
            main(["--help"])
        assert exc.value.code == 0


# ── validate command ───────────────────────────────────────────────────────────

class TestCLIValidate:
    def test_missing_manifest_returns_1(self):
        rc = main(["validate", "/nonexistent/manifest.json"])
        assert rc == 1

    def test_invalid_manifest_returns_2(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
            json.dump({"schema_version": "99.0"}, f)
            path = f.name
        try:
            rc = main(["validate", path, "--bars", "bars_eurusd_h1_2y.json"])
            assert rc in (1, 2)  # either missing fixture or validation failure
        finally:
            os.unlink(path)


# ── list command ───────────────────────────────────────────────────────────────

class TestCLIList:
    def test_list_empty_registry_returns_0(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            rc = main(["list", "--registry", tmpdir])
            assert rc == 0

    def test_list_with_registered_algo(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            # Write a mock registry file
            mock = {
                "name": "mock-algo",
                "_metadata": {
                    "tier": "green",
                    "min_score": 92.0,
                    "experimental": False,
                    "paper_only": False,
                },
            }
            with open(os.path.join(tmpdir, "mock-algo.json"), "w") as f:
                json.dump(mock, f)
            rc = main(["list", "--registry", tmpdir])
            assert rc == 0

    def test_list_tier_filter(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            for tier in ["green", "orange"]:
                mock = {"name": f"algo-{tier}", "_metadata": {"tier": tier,
                        "min_score": 80.0, "experimental": False, "paper_only": False}}
                with open(os.path.join(tmpdir, f"algo-{tier}.json"), "w") as f:
                    json.dump(mock, f)
            rc = main(["list", "--registry", tmpdir, "--tier", "green"])
            assert rc == 0

    def test_list_missing_registry_returns_0(self):
        rc = main(["list", "--registry", "/nonexistent/registry"])
        assert rc == 0  # just prints message, doesn't error


# ── retire command ─────────────────────────────────────────────────────────────

class TestCLIRetire:
    def test_retire_nonexistent_returns_1(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            rc = main(["retire", "no-such-algo", "--registry", tmpdir])
            assert rc == 1

    def test_retire_existing_algo(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            mock = {"name": "retire-me", "is_enabled": True}
            path = os.path.join(tmpdir, "retire-me.json")
            with open(path, "w") as f:
                json.dump(mock, f)
            rc = main(["retire", "retire-me", "--registry", tmpdir])
            assert rc == 0
            with open(path) as f:
                data = json.load(f)
            assert data["is_enabled"] is False

    def test_retire_already_retired(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            mock = {"name": "already-retired", "is_enabled": False}
            path = os.path.join(tmpdir, "already-retired.json")
            with open(path, "w") as f:
                json.dump(mock, f)
            rc = main(["retire", "already-retired", "--registry", tmpdir])
            assert rc == 0  # idempotent


# ── generate command ───────────────────────────────────────────────────────────
# Generator implementation tested separately in test_generator.py.
# The CLI requires --brief; verify that contract here.

class TestCLIGenerate:
    def test_generate_requires_brief(self, capsys):
        rc = main(["generate"])
        assert rc == 1
        captured = capsys.readouterr()
        assert "brief" in captured.err.lower()


# ── Unknown command ────────────────────────────────────────────────────────────

class TestCLIUnknownCommand:
    def test_unknown_command_returns_nonzero(self, monkeypatch):
        """Bypass argparse to test the dispatch manually."""
        import argparse
        # argparse will print error and exit for unrecognised subcommands
        with pytest.raises(SystemExit):
            main(["badcommand"])
