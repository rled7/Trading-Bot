"""Generate canonical algo_gen fixtures.

Run this script to regenerate all fixtures:
    cd /path/to/Trading-Bot
    python tests/fixtures/algo_gen/generate_fixtures.py

Writes all fixture files to the same directory as this script.
"""
from __future__ import annotations

import json
import math
import os
import sys

# Ensure the python package is importable
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO_ROOT, "python"))

FIXTURE_DIR = os.path.dirname(os.path.abspath(__file__))


# ── LCG (fixed-seed) bar generation ──────────────────────────────────────────

_UINT64_MASK = 0xFFFFFFFFFFFFFFFF
_LCG_MUL = 6364136223846793005
_LCG_ADD = 1442695040888963407


class _LCG:
    def __init__(self, seed: int) -> None:
        self._state = int(seed) & _UINT64_MASK

    def next_uint32(self) -> int:
        self._state = (self._state * _LCG_MUL + _LCG_ADD) & _UINT64_MASK
        return self._state >> 33

    def next_float(self) -> float:
        """Return float in [0, 1)."""
        return self.next_uint32() / (2**31)

    def next_normal(self) -> float:
        """Box-Muller normal."""
        u1 = max(1e-12, self.next_float())
        u2 = self.next_float()
        return math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2)


def _gen_bars(n: int, seed: int, start_price: float = 1.1000,
              start_ts: int = 1_600_000_000, bar_sec: int = 3600) -> list[dict]:
    rng   = _LCG(seed)
    bars  = []
    price = start_price
    ts    = start_ts

    for i in range(n):
        # Random walk
        drift  = rng.next_normal() * 0.0003
        atr_v  = abs(rng.next_normal()) * 0.0010 + 0.0002
        open_p = price
        close  = price + drift
        high   = max(open_p, close) + abs(rng.next_normal()) * atr_v * 0.4
        low    = min(open_p, close) - abs(rng.next_normal()) * atr_v * 0.4
        volume = abs(rng.next_normal()) * 500_000 + 100_000

        bars.append({
            "timestamp": ts,
            "open":      round(open_p, 5),
            "high":      round(high, 5),
            "low":       round(low, 5),
            "close":     round(close, 5),
            "volume":    round(volume, 2),
            "spread":    0.00002,
        })

        price = close
        ts   += bar_sec

    return bars


def _save(fname: str, data: object) -> None:
    path = os.path.join(FIXTURE_DIR, fname)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"  wrote {fname}")


# ── Bar fixtures ──────────────────────────────────────────────────────────────

def gen_bars():
    print("Generating bar fixtures...")
    # EURUSD H1 2y ≈ 8760 bars (365 * 24)
    bars_eu = _gen_bars(8760, seed=1001, start_price=1.1000, bar_sec=3600)
    _save("bars_eurusd_h1_2y.json", bars_eu)

    # GBPJPY M15 6mo ≈ 17280 bars (6*30*24*4 = 17280)
    bars_gj = _gen_bars(17280, seed=2002, start_price=155.000, bar_sec=900)
    _save("bars_gbpjpy_m15_6mo.json", bars_gj)


# ── Manifest fixtures ─────────────────────────────────────────────────────────

def _trend_follow_green_manifest() -> dict:
    """Known-green: robust EMA/RSI trend strategy."""
    return {
        "schema_version": "1.0",
        "name":           "trend-follow-ema-rsi",
        "description":    "EMA crossover with RSI momentum filter — designed for green tier.",
        "rationale":      "Uses EMA crossover (fast 9/slow 21) with RSI > 50 filter to capture medium-term trends on H1.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "ema9",  "kind": "ema", "params": {"period": 9}},
            {"id": "ema21", "kind": "ema", "params": {"period": 21}},
            {"id": "rsi14", "kind": "rsi", "params": {"period": 14}},
            {"id": "atr14", "kind": "atr", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long",  "when": "ema9 > ema21 and rsi14 > 50"},
            {"side": "short", "when": "ema9 < ema21 and rsi14 < 50"},
        ],
        "exits": [
            {"side": "long",  "sl_atr": 1.5, "tp_atr": 3.0},
            {"side": "short", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {
            "size":           "atr",
            "atr_mult":       1.5,
            "fixed_lots":     0.01,
            "max_concurrent": 1,
            "hedge":          False,
            "cool_down_bars": 3,
        },
    }


def _trend_follow_orange_manifest() -> dict:
    """Known-orange: same strategy but with a fragile tight RSI threshold."""
    return {
        "schema_version": "1.0",
        "name":           "trend-follow-orange",
        "description":    "EMA crossover with tight RSI band — fragile entry.",
        "rationale":      "RSI threshold 49.5 is fragile; small perturbations break it.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "ema9",  "kind": "ema", "params": {"period": 9}},
            {"id": "ema21", "kind": "ema", "params": {"period": 21}},
            {"id": "rsi14", "kind": "rsi", "params": {"period": 14}},
            {"id": "atr14", "kind": "atr", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long",  "when": "ema9 > ema21 and rsi14 > 40"},
            {"side": "short", "when": "ema9 < ema21 and rsi14 < 60"},
        ],
        "exits": [
            {"side": "long",  "sl_atr": 2.0, "tp_atr": 2.0},
            {"side": "short", "sl_atr": 2.0, "tp_atr": 2.0},
        ],
        "risk": {
            "size":           "atr",
            "atr_mult":       2.0,
            "fixed_lots":     0.01,
            "max_concurrent": 1,
            "hedge":          False,
            "cool_down_bars": 1,
        },
    }


def _mean_revert_yellow_manifest() -> dict:
    """Known-yellow: RSI oversold/overbought mean-reversion."""
    return {
        "schema_version": "1.0",
        "name":           "mean-revert-rsi-bb",
        "description":    "RSI oversold/overbought with Bollinger Band touch.",
        "rationale":      "Mean-reversion in ranging markets using RSI extremes.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi14", "kind": "rsi",      "params": {"period": 14}},
            {"id": "bb20",  "kind": "bollinger", "params": {"period": 20, "mult": 2.0}},
            {"id": "atr14", "kind": "atr",       "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long",  "when": "rsi14 < 35"},
            {"side": "short", "when": "rsi14 > 65"},
        ],
        "exits": [
            {"side": "long",  "sl_atr": 1.5, "tp_atr": 2.0},
            {"side": "short", "sl_atr": 1.5, "tp_atr": 2.0},
        ],
        "risk": {
            "size":           "atr",
            "atr_mult":       1.5,
            "fixed_lots":     0.01,
            "max_concurrent": 1,
            "hedge":          False,
            "cool_down_bars": 5,
        },
    }


def _breakout_white_manifest() -> dict:
    """Known-white: clean SMA breakout strategy."""
    return {
        "schema_version": "1.0",
        "name":           "breakout-sma-momentum",
        "description":    "SMA momentum breakout — designed for white tier.",
        "rationale":      "Fast SMA cross with wide ATR bracket for strong trend capture.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "sma5",  "kind": "sma", "params": {"period": 5}},
            {"id": "sma20", "kind": "sma", "params": {"period": 20}},
            {"id": "atr14", "kind": "atr", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long",  "when": "sma5 > sma20 and close > sma20"},
            {"side": "short", "when": "sma5 < sma20 and close < sma20"},
        ],
        "exits": [
            {"side": "long",  "sl_atr": 1.0, "tp_atr": 4.0},
            {"side": "short", "sl_atr": 1.0, "tp_atr": 4.0},
        ],
        "risk": {
            "size":           "atr",
            "atr_mult":       1.0,
            "fixed_lots":     0.01,
            "max_concurrent": 1,
            "hedge":          False,
            "cool_down_bars": 2,
        },
    }


def _invalid_schema_manifest() -> dict:
    """Stage 1 rejection: bad indicator kind."""
    return {
        "schema_version": "1.0",
        "name":           "invalid-schema-test",
        "description":    "This manifest has an invalid indicator kind.",
        "rationale":      "Testing schema rejection.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "x1", "kind": "nonexistent_indicator", "params": {"period": 14}},
        ],
        "entries": [
            {"side": "long", "when": "x1 > 50"},
        ],
        "exits": [
            {"side": "long", "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {"size": "atr", "atr_mult": 1.5, "fixed_lots": 0.01,
                 "max_concurrent": 1, "hedge": False, "cool_down_bars": 0},
    }


def _low_trade_count_manifest() -> dict:
    """Stage 2 rejection: conditions so restrictive it produces <20 trades."""
    return {
        "schema_version": "1.0",
        "name":           "low-trade-count-test",
        "description":    "Very restrictive conditions that produce almost no trades.",
        "rationale":      "Testing stage 2 rejection due to insufficient trade count.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi14", "kind": "rsi", "params": {"period": 14}},
            {"id": "atr14", "kind": "atr", "params": {"period": 14}},
        ],
        "entries": [
            # RSI must be exactly between 49.99 and 50.01 — almost never fires
            {"side": "long",  "when": "rsi14 > 49 and rsi14 < 51 and close > 2.0"},
        ],
        "exits": [
            {"side": "long",  "sl_atr": 1.5, "tp_atr": 3.0},
        ],
        "risk": {"size": "atr", "atr_mult": 1.5, "fixed_lots": 0.01,
                 "max_concurrent": 1, "hedge": False, "cool_down_bars": 0},
    }


def _wf_fail_manifest() -> dict:
    """Stage 3 rejection: passes stage 2 but fails walk-forward."""
    return {
        "schema_version": "1.0",
        "name":           "wf-fail-test",
        "description":    "Strategy that overfits to specific bar ranges.",
        "rationale":      "Testing walk-forward failure.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators": [
            {"id": "rsi5",  "kind": "rsi", "params": {"period": 5}},
            {"id": "atr14", "kind": "atr", "params": {"period": 14}},
        ],
        "entries": [
            # Very short RSI — produces many trades but is noisy/unstable
            {"side": "long",  "when": "rsi5 < 25"},
            {"side": "short", "when": "rsi5 > 75"},
        ],
        "exits": [
            # Very tight stops — lots of SL hits
            {"side": "long",  "sl_atr": 0.3, "tp_atr": 0.4},
            {"side": "short", "sl_atr": 0.3, "tp_atr": 0.4},
        ],
        "risk": {"size": "atr", "atr_mult": 0.3, "fixed_lots": 0.01,
                 "max_concurrent": 1, "hedge": False, "cool_down_bars": 0},
    }


def _escape_hatch_green_manifest() -> dict:
    """Python escape-hatch manifest — should pass validation."""
    code = '''
from algoforge.algorithm import IAlgorithm, AlgoDecision, AlgoSignal
from algoforge.types import Direction
from algoforge import indicators as _ind

class GeneratedAlgo(IAlgorithm):
    def name(self):      return "escape-hatch-green"
    def description(self): return "Escape hatch EMA crossover"
    def magic(self):     return 99001

    def evaluate(self, symbol, tf, bars, count, ind, pat):
        closes = [b.close for b in bars[:count+1]]
        if len(closes) < 22:
            return AlgoDecision.none(symbol)
        ema9  = _ind.ema(closes, 9)[-1]
        ema21 = _ind.ema(closes, 21)[-1]
        atr_v = ind.atr_value or 0.0
        if ema9 is None or ema21 is None:
            return AlgoDecision.none(symbol)
        if ema9 > ema21:
            return AlgoDecision(
                signal=AlgoSignal.BUY, symbol=symbol,
                direction=Direction.LONG, confidence=0.72,
                reason="EMA9 > EMA21", atr=atr_v
            )
        if ema9 < ema21:
            return AlgoDecision(
                signal=AlgoSignal.SELL, symbol=symbol,
                direction=Direction.SHORT, confidence=0.72,
                reason="EMA9 < EMA21", atr=atr_v
            )
        return AlgoDecision.none(symbol)
'''
    return {
        "schema_version": "1.0",
        "name":           "escape-hatch-green",
        "description":    "Escape-hatch EMA crossover strategy.",
        "rationale":      "Tests the escape-hatch code path with a working strategy.",
        "timeframes":     ["H1"],
        "symbols":        ["EURUSD"],
        "indicators":     [],
        "entries":        [],
        "exits":          [],
        "risk": {"size": "atr", "atr_mult": 1.5, "fixed_lots": 0.01,
                 "max_concurrent": 1, "hedge": False, "cool_down_bars": 0},
        "code": code,
    }


def gen_manifests():
    print("Generating manifest fixtures...")
    _save("manifest_trend_follow_green.json",   _trend_follow_green_manifest())
    _save("manifest_trend_follow_orange.json",  _trend_follow_orange_manifest())
    _save("manifest_mean_revert_yellow.json",   _mean_revert_yellow_manifest())
    _save("manifest_breakout_white.json",       _breakout_white_manifest())
    _save("manifest_invalid_schema.json",       _invalid_schema_manifest())
    _save("manifest_low_trade_count.json",      _low_trade_count_manifest())
    _save("manifest_wf_fail.json",              _wf_fail_manifest())
    _save("manifest_escape_hatch_green.json",   _escape_hatch_green_manifest())


# ── Tier report fixtures ──────────────────────────────────────────────────────

def gen_tier_reports():
    """Generate tier report fixtures by running the validator on each manifest."""
    print("Generating tier report fixtures (this may take a while)...")

    from algoforge.algo_gen.fixtures import load_bars
    from algoforge.algo_gen.validator import validate
    from algoforge.backtest import BTConfig

    bars = load_bars("bars_eurusd_h1_2y.json")
    cfg  = BTConfig(warmup_bars=200)
    seed = 42

    manifests_to_validate = [
        ("manifest_trend_follow_green.json",  "tier_report_trend_follow_green.json"),
        ("manifest_trend_follow_orange.json", "tier_report_trend_follow_orange.json"),
        ("manifest_mean_revert_yellow.json",  "tier_report_mean_revert_yellow.json"),
        ("manifest_breakout_white.json",      "tier_report_breakout_white.json"),
        ("manifest_invalid_schema.json",      "tier_report_invalid_schema.json"),
        ("manifest_low_trade_count.json",     "tier_report_low_trade_count.json"),
        ("manifest_wf_fail.json",             "tier_report_wf_fail.json"),
    ]

    for manifest_fname, report_fname in manifests_to_validate:
        print(f"  Running validator on {manifest_fname} ...")
        with open(os.path.join(FIXTURE_DIR, manifest_fname)) as f:
            manifest_dict = json.load(f)

        try:
            result = validate(manifest_dict, bars, seed, cfg=cfg)
        except Exception as e:
            print(f"    ERROR: {e}")
            continue

        # Build tier report dict
        report_dict: dict = {
            "manifest_name": manifest_dict.get("name", "?"),
            "passed":        result.passed,
            "stages": [
                {
                    "stage":   s.stage,
                    "name":    s.name,
                    "passed":  s.passed,
                    "reason":  s.reason,
                    "metrics": s.metrics,
                }
                for s in result.stages
            ],
        }

        if result.report is not None:
            r = result.report
            report_dict["tier"] = {
                "tier":         r.tier.value,
                "min_score":    r.min_score,
                "walk_forward": r.walk_forward,
                "mc_bootstrap": r.mc_bootstrap,
                "robustness":   r.robustness,
                "size_mult":    r.size_mult,
                "experimental": r.experimental,
                "paper_only":   r.paper_only,
            }
        else:
            report_dict["tier"] = None

        _save(report_fname, report_dict)
        print(f"    → passed={result.passed}, tier={result.report.tier.value if result.report else 'n/a'}")


if __name__ == "__main__":
    gen_bars()
    gen_manifests()
    gen_tier_reports()
    print("\nAll fixtures generated successfully.")
