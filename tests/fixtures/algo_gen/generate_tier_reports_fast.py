"""Fast tier report fixture generator.

Creates tier report JSON fixtures using hardcoded representative scores,
bypassing the full validator (which is too slow for CI).

Usage:
    cd Trading-Bot
    PYTHONPATH=python python3 tests/fixtures/algo_gen/generate_tier_reports_fast.py
"""
from __future__ import annotations

import json
import os
import sys

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(_REPO_ROOT, "python"))

FIXTURE_DIR = os.path.dirname(os.path.abspath(__file__))


def _save(fname: str, data: object) -> None:
    path = os.path.join(FIXTURE_DIR, fname)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"  wrote {fname}")


def gen_tier_reports():
    """Generate tier report fixtures using score_to_tier directly."""
    from algoforge.algo_gen.tier import score_to_tier
    from algoforge.algo_gen.types import StageResult

    print("Generating tier report fixtures (fast path)...")

    # --- trend-follow-green: all scores >= 90 → green ---
    r = score_to_tier(91.0, 90.5, 90.2)
    _save("tier_report_trend_follow_green.json", {
        "manifest_name": "trend-follow-ema-rsi",
        "passed":        True,
        "tier":          r.tier.value,
        "min_score":     r.min_score,
        "walk_forward":  r.walk_forward,
        "mc_bootstrap":  r.mc_bootstrap,
        "robustness":    r.robustness,
        "size_mult":     r.size_mult,
        "experimental":  r.experimental,
        "paper_only":    r.paper_only,
        "stages": [
            {"stage": 1, "name": "schema_lint",         "passed": True, "reason": "OK", "metrics": {}},
            {"stage": 2, "name": "sandbox_backtest",    "passed": True, "reason": "OK", "metrics": {"trades": 80}},
            {"stage": 3, "name": "walk_forward",        "passed": True, "reason": "5/5 windows passed", "metrics": {"score": 91.0}},
            {"stage": 4, "name": "mc_bootstrap",        "passed": True, "reason": "MC score=90.5%", "metrics": {"score": 90.5}},
            {"stage": 5, "name": "parameter_robustness","passed": True, "reason": "Robustness score=90.2%", "metrics": {"score": 90.2}},
        ],
    })

    # --- trend-follow-orange: min score 72-79 → orange ---
    r = score_to_tier(79.0, 74.0, 72.0)
    _save("tier_report_trend_follow_orange.json", {
        "manifest_name": "trend-follow-orange",
        "passed":        True,
        "tier":          r.tier.value,
        "min_score":     r.min_score,
        "walk_forward":  r.walk_forward,
        "mc_bootstrap":  r.mc_bootstrap,
        "robustness":    r.robustness,
        "size_mult":     r.size_mult,
        "experimental":  r.experimental,
        "paper_only":    r.paper_only,
        "stages": [
            {"stage": 1, "name": "schema_lint",         "passed": True, "reason": "OK", "metrics": {}},
            {"stage": 2, "name": "sandbox_backtest",    "passed": True, "reason": "OK", "metrics": {"trades": 60}},
            {"stage": 3, "name": "walk_forward",        "passed": True, "reason": "5/5 windows passed", "metrics": {"score": 79.0}},
            {"stage": 4, "name": "mc_bootstrap",        "passed": True, "reason": "MC score=74.0%", "metrics": {"score": 74.0}},
            {"stage": 5, "name": "parameter_robustness","passed": True, "reason": "Robustness score=72.0%", "metrics": {"score": 72.0}},
        ],
    })

    # --- mean-revert-yellow: min score 80-89 → yellow ---
    r = score_to_tier(85.0, 83.0, 80.5)
    _save("tier_report_mean_revert_yellow.json", {
        "manifest_name": "mean-revert-rsi-bb",
        "passed":        True,
        "tier":          r.tier.value,
        "min_score":     r.min_score,
        "walk_forward":  r.walk_forward,
        "mc_bootstrap":  r.mc_bootstrap,
        "robustness":    r.robustness,
        "size_mult":     r.size_mult,
        "experimental":  r.experimental,
        "paper_only":    r.paper_only,
        "stages": [
            {"stage": 1, "name": "schema_lint",         "passed": True, "reason": "OK", "metrics": {}},
            {"stage": 2, "name": "sandbox_backtest",    "passed": True, "reason": "OK", "metrics": {"trades": 70}},
            {"stage": 3, "name": "walk_forward",        "passed": True, "reason": "5/5 windows passed", "metrics": {"score": 85.0}},
            {"stage": 4, "name": "mc_bootstrap",        "passed": True, "reason": "MC score=83.0%", "metrics": {"score": 83.0}},
            {"stage": 5, "name": "parameter_robustness","passed": True, "reason": "Robustness score=80.5%", "metrics": {"score": 80.5}},
        ],
    })

    # --- breakout-white: min score >= 95 → white ---
    r = score_to_tier(96.0, 95.5, 95.0)
    _save("tier_report_breakout_white.json", {
        "manifest_name": "breakout-sma-momentum",
        "passed":        True,
        "tier":          r.tier.value,
        "min_score":     r.min_score,
        "walk_forward":  r.walk_forward,
        "mc_bootstrap":  r.mc_bootstrap,
        "robustness":    r.robustness,
        "size_mult":     r.size_mult,
        "experimental":  r.experimental,
        "paper_only":    r.paper_only,
        "stages": [
            {"stage": 1, "name": "schema_lint",         "passed": True, "reason": "OK", "metrics": {}},
            {"stage": 2, "name": "sandbox_backtest",    "passed": True, "reason": "OK", "metrics": {"trades": 100}},
            {"stage": 3, "name": "walk_forward",        "passed": True, "reason": "5/5 windows passed", "metrics": {"score": 96.0}},
            {"stage": 4, "name": "mc_bootstrap",        "passed": True, "reason": "MC score=95.5%", "metrics": {"score": 95.5}},
            {"stage": 5, "name": "parameter_robustness","passed": True, "reason": "Robustness score=95.0%", "metrics": {"score": 95.0}},
        ],
    })

    # --- invalid-schema: fails at stage 1 → no tier ---
    _save("tier_report_invalid_schema.json", {
        "manifest_name": "invalid-schema-test",
        "passed":        False,
        "tier":          None,
        "stages": [
            {"stage": 1, "name": "schema_lint", "passed": False, "reason": "unknown indicator kind 'nonexistent_indicator'", "metrics": {}},
        ],
    })

    # --- low-trade-count: fails at stage 2 → no tier ---
    _save("tier_report_low_trade_count.json", {
        "manifest_name": "low-trade-count-test",
        "passed":        False,
        "tier":          None,
        "stages": [
            {"stage": 1, "name": "schema_lint",      "passed": True,  "reason": "OK", "metrics": {}},
            {"stage": 2, "name": "sandbox_backtest", "passed": False, "reason": "too few trades: 0 < 20", "metrics": {"trades": 0}},
        ],
    })

    # --- wf-fail: fails at stage 3 → no tier ---
    _save("tier_report_wf_fail.json", {
        "manifest_name": "wf-fail-test",
        "passed":        False,
        "tier":          None,
        "stages": [
            {"stage": 1, "name": "schema_lint",      "passed": True,  "reason": "OK", "metrics": {}},
            {"stage": 2, "name": "sandbox_backtest", "passed": True,  "reason": "OK", "metrics": {"trades": 30}},
            {"stage": 3, "name": "walk_forward",     "passed": False, "reason": "2/5 windows passed (stage failed)", "metrics": {"score": 40.0}},
        ],
    })

    print("Done.")


if __name__ == "__main__":
    gen_tier_reports()
