"""Prompt templates for the S1 algorithm generator.

Three effort modes:
  fast     — single-pass, brief → manifest
  balanced — single-pass + structured self-critique + revision
  max      — reasoning model, N-candidate ensemble + critique

All templates produce JSON-only output wrapped in ```json ... ``` fences.
Use extract_json_block(text) to robustly pull the manifest dict out.
"""
from __future__ import annotations

import json
import re


# ── Shared schema guidance ────────────────────────────────────────────────────

_SCHEMA_SUMMARY = """
The manifest must be a JSON object with these exact fields:
{
  "schema_version": "1.0",
  "name": "<kebab-case, ≤48 chars, starts with a letter>",
  "description": "<≤280 chars plain prose>",
  "rationale": "<≤1024 chars LLM reasoning trace>",
  "timeframes": ["H1"],
  "symbols": ["EURUSD"] or "any",
  "indicators": [
    {"id": "<valid_python_identifier>", "kind": "<indicator_kind>", "params": {"period": <int>}}
  ],
  "entries": [
    {"side": "long"|"short", "when": "<DSL expression>"}
  ],
  "exits": [
    {"side": "long"|"short", "sl_atr": <float>, "tp_atr": <float>}
  ],
  "risk": {
    "size": "atr"|"fixed",
    "atr_mult": <float>,
    "fixed_lots": <float>,
    "max_concurrent": <int>,
    "hedge": false,
    "cool_down_bars": <int>=0
  },
  "code": null
}

Allowed indicator kinds: sma, ema, rsi, atr, macd, bollinger, stochastic, obv, wma, cci,
williams_r, roc, mfi, vwap, keltner, adx, hma, dema, tema.

DSL expressions use indicator ids, bar fields (close, open, high, low, volume), and
pattern names (pattern.hammer, pattern.doji, pattern.engulfing, pattern.pin_bar,
pattern.morning_star, pattern.evening_star).
Operators: <, >, <=, >=, ==, !=, and, or, not, +, -, *, /.
""".strip()

_INDICATOR_KINDS = (
    "sma, ema, rsi, atr, macd, bollinger, stochastic, obv, wma, cci, "
    "williams_r, roc, mfi, vwap, keltner, adx, hma, dema, tema"
)


# ── FAST mode ─────────────────────────────────────────────────────────────────

FAST_SYSTEM = (
    "You are an expert algorithmic trading strategy designer. "
    "Given a brief description, produce a complete, valid AlgoForge manifest JSON. "
    "Respond with ONLY a JSON code block — no prose before or after. "
    "The manifest must be schema-valid and use only permitted indicator kinds: "
    + _INDICATOR_KINDS + "."
)

FAST_USER_TMPL = (
    "Brief: {brief}\n\n"
    "Schema reference:\n{schema_summary}\n\n"
    "Generate a complete manifest for this strategy. "
    "Respond with exactly:\n```json\n<manifest JSON>\n```"
)


# ── BALANCED mode ─────────────────────────────────────────────────────────────

BALANCED_SYSTEM = (
    "You are an expert algorithmic trading strategy designer and self-critic. "
    "You generate manifests and then rigorously critique them to identify weaknesses. "
    "All output is JSON-only in ```json ... ``` fences — no prose outside fences."
)

BALANCED_USER_TMPL = (
    "Brief: {brief}\n\n"
    "Schema reference:\n{schema_summary}\n\n"
    "Step 1: Generate a complete manifest for this strategy.\n"
    "Respond with exactly:\n```json\n<manifest JSON>\n```"
)

BALANCED_CRITIQUE_TMPL = (
    "You produced this candidate manifest:\n```json\n{manifest_json}\n```\n\n"
    "Step 2: Identify exactly 3 weaknesses (e.g., overfitting risk, fragile thresholds, "
    "missing filters, poor risk/reward, too-few indicators). "
    "Then emit a revised manifest that addresses all three weaknesses.\n\n"
    "Respond with exactly:\n```json\n<revised manifest JSON>\n```\n\n"
    "The revised manifest must be schema-valid and represent a meaningfully improved strategy."
)


# ── MAX mode ──────────────────────────────────────────────────────────────────

MAX_SYSTEM = (
    "You are an expert algorithmic trading strategy designer using step-by-step reasoning. "
    "Given a brief and context, generate a single complete, novel, schema-valid AlgoForge manifest. "
    "Think carefully about market regime, indicator synergy, and risk management before committing. "
    "Respond with ONLY a JSON code block — no prose before or after. "
    "Permitted indicator kinds: " + _INDICATOR_KINDS + "."
)

MAX_USER_TMPL = (
    "Brief: {brief}\n\n"
    "Context: candidate {candidate_num} of {n_candidates}. "
    "Make this candidate distinct from typical EMA-crossover approaches. "
    "Consider: {context_hint}\n\n"
    "Schema reference:\n{schema_summary}\n\n"
    "Generate a complete, distinct manifest. "
    "Respond with exactly:\n```json\n<manifest JSON>\n```"
)

MAX_CRITIQUE_TMPL = (
    "Candidate manifest:\n```json\n{manifest_json}\n```\n\n"
    "Quick backtest summary:\n"
    "  trades={trades}  sharpe={sharpe:.3f}  max_dd={max_dd:.1f}%\n\n"
    "Identify 3 specific weaknesses in this strategy. "
    "Then emit a revised manifest that addresses all three weaknesses and is "
    "likely to score higher on out-of-sample Sharpe and lower drawdown.\n\n"
    "Respond with exactly:\n```json\n<revised manifest JSON>\n```"
)


# ── JSON extraction ───────────────────────────────────────────────────────────

def extract_json_block(text: str) -> dict:
    """Extract the first JSON object from text, handling code fences and prose.

    Strategy:
    1. Look for ```json ... ``` fence (preferred, spec-mandated).
    2. Fall back to scanning for the first balanced { ... } object.

    Raises ValueError if no valid JSON is found.
    """
    # Strategy 1: ```json fence
    fence_pattern = re.compile(r"```(?:json)?\s*(\{.*?\})\s*```", re.DOTALL)
    m = fence_pattern.search(text)
    if m:
        candidate = m.group(1).strip()
        try:
            return json.loads(candidate)
        except json.JSONDecodeError:
            pass  # fall through to strategy 2

    # Strategy 2: scan for balanced { ... }
    depth = 0
    in_string = False
    escape = False
    start = None
    for i, ch in enumerate(text):
        if escape:
            escape = False
            continue
        if ch == "\\" and in_string:
            escape = True
            continue
        if ch == '"':
            in_string = not in_string
            continue
        if in_string:
            continue
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                candidate = text[start : i + 1]
                try:
                    return json.loads(candidate)
                except json.JSONDecodeError:
                    pass
                start = None

    raise ValueError(f"No valid JSON object found in LLM response (length={len(text)})")


def render_fast_user(brief: str) -> str:
    return FAST_USER_TMPL.format(brief=brief, schema_summary=_SCHEMA_SUMMARY)


def render_balanced_user(brief: str) -> str:
    return BALANCED_USER_TMPL.format(brief=brief, schema_summary=_SCHEMA_SUMMARY)


def render_balanced_critique(manifest_dict: dict) -> str:
    return BALANCED_CRITIQUE_TMPL.format(
        manifest_json=json.dumps(manifest_dict, indent=2)
    )


def render_max_user(
    brief: str,
    candidate_num: int,
    n_candidates: int,
    context_hint: str = "momentum, mean-reversion, or volatility breakout",
) -> str:
    return MAX_USER_TMPL.format(
        brief=brief,
        candidate_num=candidate_num,
        n_candidates=n_candidates,
        context_hint=context_hint,
        schema_summary=_SCHEMA_SUMMARY,
    )


def render_max_critique(manifest_dict: dict, trades: int, sharpe: float, max_dd: float) -> str:
    return MAX_CRITIQUE_TMPL.format(
        manifest_json=json.dumps(manifest_dict, indent=2),
        trades=trades,
        sharpe=sharpe,
        max_dd=max_dd,
    )
