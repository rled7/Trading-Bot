"""Tests for algoforge.algo_gen.sandbox — escape-hatch code security and execution."""
from __future__ import annotations

import pytest

from algoforge.algo_gen.sandbox import (
    FORBIDDEN_NAMES,
    SandboxSecurityError,
    check_code_security,
    run_escape_hatch,
)
from algoforge.algo_gen.schema import validate_manifest
from algoforge.types import Bar


# ── Helpers ────────────────────────────────────────────────────────────────────

def _make_bars(n: int = 500, start: float = 1.1000) -> list[Bar]:
    bars = []
    price = start
    for i in range(n):
        bars.append(Bar(
            timestamp = 1_600_000_000 + i * 3600,
            open      = price,
            high      = price + 0.0015,
            low       = price - 0.0010,
            close     = price + 0.0005 * (1 if i % 2 == 0 else -1),
            volume    = 100_000.0,
            spread    = 0.00002,
        ))
        price += 0.00005
    return bars


_SAFE_CODE = '''
from algoforge.algorithm import IAlgorithm, AlgoDecision, AlgoSignal

class GeneratedAlgo(IAlgorithm):
    """Simple RSI-based algo."""
    def name(self):
        return "test-escape"

    def description(self):
        return "Escape hatch test."

    def magic(self):
        return 99999

    def evaluate(self, symbol, tf, bars, count, ind, pat):
        return AlgoDecision()
'''

_ESCAPE_MANIFEST = {
    "schema_version": "1.0",
    "name":           "escape-test",
    "description":    "Escape hatch test algo.",
    "rationale":      "Test purposes.",
    "timeframes":     ["H1"],
    "symbols":        ["EURUSD"],
    "indicators":     [],
    "entries":        [],
    "exits":          [{"side": "long", "sl_atr": 1.5, "tp_atr": 3.0}],
    "risk":           {"size": "atr"},
    "code":           _SAFE_CODE,
}


# ── check_code_security ────────────────────────────────────────────────────────

class TestCheckCodeSecurity:
    def test_clean_code_passes(self):
        # Should not raise
        check_code_security("x = 1 + 2")

    def test_clean_class_passes(self):
        check_code_security("class Foo:\n    def bar(self):\n        return 42")

    def test_import_os_raises(self):
        with pytest.raises(SandboxSecurityError, match="os"):
            check_code_security("import os")

    def test_import_sys_raises(self):
        with pytest.raises(SandboxSecurityError, match="sys"):
            check_code_security("import sys")

    def test_import_subprocess_raises(self):
        with pytest.raises(SandboxSecurityError, match="subprocess"):
            check_code_security("import subprocess")

    def test_import_socket_raises(self):
        with pytest.raises(SandboxSecurityError, match="socket"):
            check_code_security("import socket")

    def test_import_requests_raises(self):
        with pytest.raises(SandboxSecurityError, match="requests"):
            check_code_security("import requests")

    def test_from_os_import_raises(self):
        with pytest.raises(SandboxSecurityError):
            check_code_security("from os import path")

    def test_from_sys_import_raises(self):
        with pytest.raises(SandboxSecurityError):
            check_code_security("from sys import argv")

    def test_eval_call_raises(self):
        with pytest.raises(SandboxSecurityError, match="eval"):
            check_code_security("result = eval('1+1')")

    def test_exec_call_raises(self):
        with pytest.raises(SandboxSecurityError, match="exec"):
            check_code_security("exec('x = 1')")

    def test_open_call_raises(self):
        with pytest.raises(SandboxSecurityError, match="open"):
            check_code_security("f = open('/etc/passwd')")

    def test_syntax_error_raises(self):
        with pytest.raises(SandboxSecurityError, match="syntax"):
            check_code_security("def f(: pass")

    def test_forbidden_names_non_empty(self):
        assert len(FORBIDDEN_NAMES) > 0
        assert "os" in FORBIDDEN_NAMES
        assert "sys" in FORBIDDEN_NAMES


# ── run_escape_hatch ───────────────────────────────────────────────────────────

class TestRunEscapeHatch:
    def test_none_code_raises(self):
        d = dict(_ESCAPE_MANIFEST)
        d["code"] = None
        m = validate_manifest(d)
        # Manually set code to None after validation (validate_manifest accepts code=None)
        m.code = None
        with pytest.raises(ValueError, match="code is None"):
            run_escape_hatch(m, _make_bars(50))

    def test_security_violation_raises_before_spawn(self):
        d = dict(_ESCAPE_MANIFEST)
        d["code"] = "import os\nclass GeneratedAlgo: pass"
        # validate_manifest accepts any string for code — security check is in sandbox
        m = validate_manifest(d)
        with pytest.raises(SandboxSecurityError):
            run_escape_hatch(m, _make_bars(50))

    def test_valid_escape_hatch_returns_bt_result(self):
        """Run safe escape hatch code that produces a BTResult."""
        # This code does nothing (no trades) but should run cleanly
        code = '''
from algoforge.algorithm import IAlgorithm, AlgoDecision

class GeneratedAlgo(IAlgorithm):
    def name(self): return "escape-noop"
    def description(self): return "Noop"
    def magic(self): return 1
    def evaluate(self, symbol, tf, bars, count, ind, pat):
        return AlgoDecision()
'''
        d = dict(_ESCAPE_MANIFEST)
        d["code"] = code
        m = validate_manifest(d)
        from algoforge.backtest import BTResult
        result = run_escape_hatch(m, _make_bars(100), seed=42)
        assert isinstance(result, BTResult)
        # Noop algo produces 0 trades
        assert result.total_trades == 0

    def test_missing_generated_algo_class_raises(self):
        """If the code doesn't define GeneratedAlgo, subprocess should error."""
        code = "x = 1  # no GeneratedAlgo defined"
        d = dict(_ESCAPE_MANIFEST)
        d["code"] = code
        m = validate_manifest(d)
        with pytest.raises(ValueError):
            run_escape_hatch(m, _make_bars(50))

    def test_timeout_respected(self):
        """Code that loops forever should time out."""
        # Very short timeout to avoid hanging tests
        # Use 300 bars so warmup (200) is satisfied and evaluate() is actually called
        code = '''
from algoforge.algorithm import IAlgorithm, AlgoDecision

class GeneratedAlgo(IAlgorithm):
    def name(self): return "slow"
    def description(self): return "Infinite loop"
    def magic(self): return 2
    def evaluate(self, symbol, tf, bars, count, ind, pat):
        while True:
            pass
'''
        d = dict(_ESCAPE_MANIFEST)
        d["code"] = code
        m = validate_manifest(d)
        with pytest.raises(ValueError, match="[Tt]imeout|timed out"):
            run_escape_hatch(m, _make_bars(300), timeout=3)
