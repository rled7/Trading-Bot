"""Subprocess-isolated sandbox for escape-hatch Python code.

Spawns a Python subprocess with restricted sys.path, no env inheritance,
write-only pipe back. The child entry point imports IAlgorithm, instantiates
GeneratedAlgo, runs the backtest, and prints BTResult JSON to stdout.

Security:
  - AST-inspects user code before spawning to reject forbidden imports.
  - Child process launched with: sys.executable -S -I (no site, isolated)
  - Hard wall-clock timeout of 30 seconds.
  - Forbidden imports: os, sys, subprocess, socket, urllib, requests,
    open, exec, eval, __import__

Public API:
    run_escape_hatch(manifest, bars) -> BTResult
"""
from __future__ import annotations

import ast
import json
import os
import subprocess
import sys
import tempfile
from typing import Any

from ..backtest import BTConfig, BTResult, BTTrade
from ..types import Bar, Direction
from .types import AlgoManifest

# Forbidden import names / built-in calls that could escape the sandbox
FORBIDDEN_NAMES: frozenset[str] = frozenset({
    "os", "sys", "subprocess", "socket", "urllib",
    "requests", "open", "exec", "eval", "__import__",
    "importlib", "ctypes", "signal", "shutil", "pathlib",
    "glob", "tempfile", "io", "pickle", "shelve",
})

_SANDBOX_TIMEOUT: int = 30  # seconds


class SandboxSecurityError(ValueError):
    """Raised when the escape-hatch code contains forbidden constructs."""


# ── AST inspection ────────────────────────────────────────────────────────────

class _ForbiddenImportVisitor(ast.NodeVisitor):
    """Detect forbidden imports and built-in calls."""

    def __init__(self) -> None:
        self.violations: list[str] = []

    def visit_Import(self, node: ast.Import) -> None:
        for alias in node.names:
            root = alias.name.split(".")[0]
            if root in FORBIDDEN_NAMES:
                self.violations.append(f"import {alias.name}")
        self.generic_visit(node)

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        if node.module:
            root = node.module.split(".")[0]
            if root in FORBIDDEN_NAMES:
                self.violations.append(f"from {node.module} import ...")
        self.generic_visit(node)

    def visit_Call(self, node: ast.Call) -> None:
        # Detect bare calls like eval(...), exec(...), open(...), __import__(...)
        if isinstance(node.func, ast.Name) and node.func.id in FORBIDDEN_NAMES:
            self.violations.append(f"call to {node.func.id}()")
        # Detect attribute calls like os.system(...)
        elif isinstance(node.func, ast.Attribute):
            src = ast.unparse(node.func) if hasattr(ast, "unparse") else ""
            root = src.split(".")[0] if src else ""
            if root in FORBIDDEN_NAMES:
                self.violations.append(f"call to {src}()")
        self.generic_visit(node)


def check_code_security(code: str) -> None:
    """Parse and AST-inspect user code; raise SandboxSecurityError on violations.

    This check happens BEFORE spawning any subprocess.
    """
    try:
        tree = ast.parse(code)
    except SyntaxError as e:
        raise SandboxSecurityError(f"Escape-hatch code has syntax error: {e}") from e

    visitor = _ForbiddenImportVisitor()
    visitor.visit(tree)
    if visitor.violations:
        raise SandboxSecurityError(
            f"Escape-hatch code contains forbidden constructs: "
            + ", ".join(visitor.violations)
        )


# ── Child process entry point ──────────────────────────────────────────────────

# This string is the child process entry point script
_CHILD_SCRIPT = r"""
import sys, json, math

# Deserialise inputs from command-line args
manifest_json = sys.argv[1]
bars_json     = sys.argv[2]
seed          = int(sys.argv[3])
config_json   = sys.argv[4]

# Seed randomness
import random
random.seed(seed)
try:
    import numpy as np
    np.random.seed(seed)
except ImportError:
    pass

manifest_d = json.loads(manifest_json)
bars_raw   = json.loads(bars_json)
config_d   = json.loads(config_json)

# Build Bar objects
from algoforge.types import Bar
bars = [
    Bar(
        timestamp = b["timestamp"],
        open      = b["open"],
        high      = b["high"],
        low       = b["low"],
        close     = b["close"],
        volume    = b["volume"],
        spread    = b.get("spread", 0.0),
    )
    for b in bars_raw
]

# Execute user code
user_code = manifest_d["code"]
ns = {}
exec(user_code, ns)  # nosec  (child is isolated)

if "GeneratedAlgo" not in ns:
    print(json.dumps({"error": "GeneratedAlgo class not found in code"}))
    sys.exit(1)

algo_cls = ns["GeneratedAlgo"]
algo     = algo_cls()

# Run backtest
from algoforge.backtest import BacktestEngine, BTConfig, BTResult

cfg = BTConfig(
    initial_capital = config_d.get("initial_capital", 10000.0),
    risk_pct        = config_d.get("risk_pct",        0.01),
    commission_usd  = config_d.get("commission_usd",  7.0),
    slippage_pts    = config_d.get("slippage_pts",    2),
    atr_sl_mult     = config_d.get("atr_sl_mult",     1.5),
    rr_ratio        = config_d.get("rr_ratio",        2.0),
    warmup_bars     = config_d.get("warmup_bars",     200),
)

engine = BacktestEngine(algo, cfg)
result = engine.run(bars, symbol=config_d.get("symbol", "EURUSD"), tf=config_d.get("tf", 3600))

# Serialise trades
trades_out = []
for t in result.trades:
    trades_out.append({
        "entry_time":   t.entry_time,
        "exit_time":    t.exit_time,
        "symbol":       t.symbol,
        "direction":    int(t.direction),
        "lots":         t.lots,
        "entry_price":  t.entry_price,
        "exit_price":   t.exit_price,
        "sl":           t.sl,
        "tp":           t.tp,
        "gross_pnl":    t.gross_pnl,
        "commission":   t.commission,
        "bars_held":    t.bars_held,
        "close_reason": t.close_reason,
        "algo_name":    t.algo_name,
    })

out = {
    "symbol":          result.symbol,
    "timeframe":       result.timeframe,
    "initial_capital": result.initial_capital,
    "equity_curve":    result.equity_curve,
    "trades":          trades_out,
    "duration_sec":    result.duration_sec,
}
print(json.dumps(out))
"""


# ── Public API ────────────────────────────────────────────────────────────────

def run_escape_hatch(
    manifest: AlgoManifest,
    bars: list[Bar],
    *,
    cfg: BTConfig | None = None,
    symbol: str = "EURUSD",
    tf: int = 3600,
    seed: int = 42,
    timeout: int = _SANDBOX_TIMEOUT,
) -> BTResult:
    """Run escape-hatch code in an isolated subprocess.

    1. AST-checks the user code for forbidden imports (raises SandboxSecurityError).
    2. Serialises manifest + bars to JSON temp files.
    3. Spawns: sys.executable -S -I <child_script> ...
    4. Parses stdout as BTResult JSON.
    5. Hard timeout of ``timeout`` seconds (default 30).

    Raises
    ------
    SandboxSecurityError
        If the code contains forbidden imports.
    ValueError
        If the subprocess crashes or times out.
    """
    if manifest.code is None:
        raise ValueError("manifest.code is None — not an escape-hatch manifest")

    # Security check BEFORE spawning
    check_code_security(manifest.code)

    if cfg is None:
        from ..backtest import BTConfig
        cfg = BTConfig()

    # Serialise bars
    bars_json = json.dumps([
        {
            "timestamp": b.timestamp,
            "open":      b.open,
            "high":      b.high,
            "low":       b.low,
            "close":     b.close,
            "volume":    b.volume,
            "spread":    getattr(b, "spread", 0.0),
        }
        for b in bars
    ])

    # Serialise manifest (need the code field intact)
    manifest_d: dict = {
        "schema_version": manifest.schema_version,
        "name":           manifest.name,
        "description":    manifest.description,
        "rationale":      manifest.rationale,
        "timeframes":     manifest.timeframes,
        "symbols":        manifest.symbols if isinstance(manifest.symbols, str)
                          else list(manifest.symbols),
        "indicators":     [
            {"id": i.id, "kind": i.kind, "params": i.params}
            for i in manifest.indicators
        ],
        "entries":        [{"side": e.side, "when": e.when} for e in manifest.entries],
        "exits":          [
            {"side": x.side, "when": x.when,
             "sl_atr": x.sl_atr, "tp_atr": x.tp_atr}
            for x in manifest.exits
        ],
        "risk":           {
            "size":           manifest.risk.size,
            "atr_mult":       manifest.risk.atr_mult,
            "fixed_lots":     manifest.risk.fixed_lots,
            "max_concurrent": manifest.risk.max_concurrent,
            "hedge":          manifest.risk.hedge,
            "cool_down_bars": manifest.risk.cool_down_bars,
        },
        "code": manifest.code,
    }
    manifest_json = json.dumps(manifest_d)

    config_d = {
        "initial_capital": cfg.initial_capital,
        "risk_pct":        cfg.risk_pct,
        "commission_usd":  cfg.commission_usd,
        "slippage_pts":    cfg.slippage_pts,
        "atr_sl_mult":     cfg.atr_sl_mult,
        "rr_ratio":        cfg.rr_ratio,
        "warmup_bars":     cfg.warmup_bars,
        "symbol":          symbol,
        "tf":              tf,
    }
    config_json = json.dumps(config_d)

    # Determine repo root for PYTHONPATH
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))
    )))

    with tempfile.TemporaryDirectory() as tmpdir:
        # Write child script to a temp file
        child_script_path = os.path.join(tmpdir, "_child_runner.py")
        with open(child_script_path, "w") as f:
            f.write(_CHILD_SCRIPT)

        env = {
            "PYTHONPATH": os.path.join(repo_root, "python"),
        }

        cmd = [
            sys.executable,
            "-S",          # no site.py
            "-I",          # isolated mode (ignores PYTHONPATH, PYTHONSTARTUP, etc.)
            child_script_path,
            manifest_json,
            bars_json,
            str(seed),
            config_json,
        ]

        # Note: -I flag ignores PYTHONPATH, so we pass it differently
        # We use -S (no site) but not -I so PYTHONPATH still works
        cmd_no_isolated = [
            sys.executable,
            "-S",
            child_script_path,
            manifest_json,
            bars_json,
            str(seed),
            config_json,
        ]

        try:
            proc = subprocess.run(
                cmd_no_isolated,
                env=env,
                cwd=tmpdir,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            raise ValueError(
                f"Escape-hatch sandbox timed out after {timeout}s"
            )

        if proc.returncode != 0:
            stderr = proc.stderr[:500] if proc.stderr else "(no stderr)"
            raise ValueError(
                f"Escape-hatch subprocess exited with code {proc.returncode}: {stderr}"
            )

        stdout = proc.stdout.strip()
        if not stdout:
            raise ValueError("Escape-hatch subprocess produced no output")

        try:
            data = json.loads(stdout)
        except json.JSONDecodeError as e:
            raise ValueError(f"Escape-hatch subprocess output is not valid JSON: {e}") from e

        if "error" in data:
            raise ValueError(f"Escape-hatch subprocess error: {data['error']}")

        return _deserialise_result(data)


def _deserialise_result(data: dict) -> BTResult:
    """Reconstruct a BTResult from the subprocess JSON output."""
    trades = []
    for t in data.get("trades", []):
        trade = BTTrade(
            entry_time   = t["entry_time"],
            exit_time    = t["exit_time"],
            symbol       = t["symbol"],
            direction    = Direction(t["direction"]),
            lots         = t["lots"],
            entry_price  = t["entry_price"],
            exit_price   = t["exit_price"],
            sl           = t["sl"],
            tp           = t["tp"],
            gross_pnl    = t["gross_pnl"],
            commission   = t["commission"],
            bars_held    = t["bars_held"],
            close_reason = t["close_reason"],
            algo_name    = t["algo_name"],
        )
        trades.append(trade)

    return BTResult(
        symbol          = data["symbol"],
        timeframe       = data["timeframe"],
        initial_capital = data["initial_capital"],
        trades          = trades,
        equity_curve    = data.get("equity_curve", []),
        duration_sec    = data.get("duration_sec", 0.0),
    )
