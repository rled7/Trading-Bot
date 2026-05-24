"""LLM-driven algorithm generators for S1 Phase 1.

Three public generators:
  generate_fast(brief, provider, seed=42)
  generate_balanced(brief, provider, seed=42)
  generate_max(brief, provider, seed=42, n_candidates=5, reasoning_provider=None)

Each returns (AlgoManifest, GenerationTrace).

Robust extraction: one retry on parse or schema failure with a "please fix JSON"
follow-up message. Two consecutive failures raise GenerationError.
"""
from __future__ import annotations

import json
import logging
import warnings
from dataclasses import dataclass, field
from typing import Any, Optional

from ..types import Bar
from ..backtest import BTConfig, BTResult, BacktestEngine
from ..llm.provider import LLMProvider
from ..llm.types import ChatMessage, ChatRequest
from .prompts import (
    FAST_SYSTEM,
    BALANCED_SYSTEM,
    MAX_SYSTEM,
    extract_json_block,
    render_balanced_critique,
    render_balanced_user,
    render_fast_user,
    render_max_critique,
    render_max_user,
)
from .schema import validate_manifest
from .runtime import ManifestAlgo
from .types import AlgoManifest

logger = logging.getLogger(__name__)

# ── Models / defaults ─────────────────────────────────────────────────────────

_DEFAULT_MODEL_FAST     = "llama3.1:8b"
_DEFAULT_MODEL_BALANCED = "qwen2.5:32b"
_DEFAULT_MODEL_MAX      = "o3-mini"
_DEFAULT_TEMPERATURE    = 0.5
_DEFAULT_MAX_TOKENS     = 2048
_MAX_CANDIDATES_DEFAULT = 5
_QUICK_BACKTEST_BARS    = 200


# ── GenerationError ───────────────────────────────────────────────────────────

class GenerationError(Exception):
    """Raised when the LLM fails to produce a valid manifest after retries."""


# ── GenerationTrace ───────────────────────────────────────────────────────────

@dataclass
class CandidateSummary:
    """Record of one candidate in max mode."""
    index:       int
    manifest:    Optional[dict]
    sharpe:      Optional[float]
    trades:      Optional[int]
    max_dd:      Optional[float]
    error:       Optional[str]       = None
    critiqued:   bool                = False
    final:       bool                = False


@dataclass
class CritiquePair:
    """One critique+revision pair."""
    candidate_index: int
    critique_prompt: str
    revised_manifest: Optional[dict]
    error: Optional[str] = None


@dataclass
class GenerationTrace:
    """Captures every LLM turn for auditability."""
    mode:              str
    model:             str
    reasoning_model:   Optional[str]         = None
    seed:              int                    = 42
    # Ordered list of {role, content} dicts covering every turn
    turns:             list[dict[str, str]]   = field(default_factory=list)
    # Parse failures with raw text
    parse_failures:    list[dict[str, str]]   = field(default_factory=list)
    # Max mode only
    candidates:        list[CandidateSummary] = field(default_factory=list)
    critique_pairs:    list[CritiquePair]     = field(default_factory=list)
    warnings:          list[str]              = field(default_factory=list)


# ── Internal helpers ──────────────────────────────────────────────────────────

_FIX_JSON_PROMPT = (
    "The JSON you produced is malformed or schema-invalid. "
    "Please output ONLY the corrected manifest JSON in a ```json ... ``` fence. "
    "No prose, no explanation — just the fixed JSON."
)


def _chat(
    provider: LLMProvider,
    messages: list[ChatMessage],
    model: str,
    seed: int,
    temperature: float = _DEFAULT_TEMPERATURE,
    max_tokens: int = _DEFAULT_MAX_TOKENS,
) -> str:
    """Call provider.chat and return the assistant message content."""
    req = ChatRequest(
        model=model,
        messages=messages,
        temperature=temperature,
        max_tokens=max_tokens,
        seed=seed,
    )
    resp = provider.chat(req)
    return resp.message.content


def _try_extract_and_validate(raw: str) -> tuple[dict, AlgoManifest]:
    """Extract JSON from raw text and schema-validate it.

    Returns (raw_dict, AlgoManifest) or raises ValueError.
    """
    d = extract_json_block(raw)
    manifest = validate_manifest(d)
    return d, manifest


def _record_turn(trace: GenerationTrace, role: str, content: str) -> None:
    trace.turns.append({"role": role, "content": content})


def _generate_with_retry(
    provider: LLMProvider,
    messages: list[ChatMessage],
    model: str,
    seed: int,
    trace: GenerationTrace,
    temperature: float = _DEFAULT_TEMPERATURE,
    max_tokens: int = _DEFAULT_MAX_TOKENS,
) -> tuple[dict, AlgoManifest]:
    """Call the LLM, attempt JSON extraction and schema validation.

    On first failure: send a follow-up "fix JSON" message and try once more.
    On second failure: raise GenerationError.
    """
    # First attempt
    raw = _chat(provider, messages, model, seed, temperature, max_tokens)
    _record_turn(trace, "assistant", raw)

    try:
        return _try_extract_and_validate(raw)
    except (ValueError, TypeError, KeyError) as exc:
        trace.parse_failures.append({"raw": raw, "error": str(exc)})
        logger.debug("First attempt failed (%s); retrying with fix prompt", exc)

    # Build retry conversation
    retry_messages = list(messages) + [
        ChatMessage(role="assistant", content=raw),
        ChatMessage(role="user", content=_FIX_JSON_PROMPT),
    ]
    _record_turn(trace, "user", _FIX_JSON_PROMPT)

    raw2 = _chat(provider, retry_messages, model, seed, temperature, max_tokens)
    _record_turn(trace, "assistant", raw2)

    try:
        return _try_extract_and_validate(raw2)
    except (ValueError, TypeError, KeyError) as exc2:
        trace.parse_failures.append({"raw": raw2, "error": str(exc2)})
        raise GenerationError(
            f"Failed to produce a valid manifest after 2 attempts. "
            f"Last error: {exc2}"
        ) from exc2


# ── Quick backtest for max mode ───────────────────────────────────────────────

def _quick_backtest(manifest: AlgoManifest, bars: list[Bar]) -> BTResult:
    """Run a minimal backtest (≤_QUICK_BACKTEST_BARS bars) for scoring."""
    short_bars = bars[:_QUICK_BACKTEST_BARS]
    algo = ManifestAlgo(manifest)
    cfg = BTConfig(initial_balance=10_000.0)
    engine = BacktestEngine(algo, cfg)
    return engine.run(short_bars, symbol="EURUSD", tf=3600)


def _load_canonical_bars() -> list[Bar]:
    """Load the canonical bar fixture for quick-backtest scoring."""
    from .fixtures import load_bars
    try:
        return load_bars("bars_eurusd_h1_2y.json")
    except FileNotFoundError:
        # Return a minimal stub if fixture not available (graceful degradation)
        logger.warning("Canonical bar fixture not found; quick-backtest will score 0")
        return []


# ── Public generators ─────────────────────────────────────────────────────────

def generate_fast(
    brief: str,
    provider: LLMProvider,
    seed: int = 42,
    model: str = _DEFAULT_MODEL_FAST,
) -> tuple[AlgoManifest, GenerationTrace]:
    """Single-pass fast generation.

    Parameters
    ----------
    brief:
        Natural-language strategy brief.
    provider:
        LLM provider (local Ollama).
    seed:
        Determinism seed passed to LLM.
    model:
        Override model name; defaults to llama3.1:8b.
    """
    trace = GenerationTrace(mode="fast", model=model, seed=seed)

    system_content = FAST_SYSTEM
    user_content = render_fast_user(brief)

    messages = [
        ChatMessage(role="system", content=system_content),
        ChatMessage(role="user",   content=user_content),
    ]
    _record_turn(trace, "system", system_content)
    _record_turn(trace, "user",   user_content)

    manifest_dict, manifest = _generate_with_retry(
        provider, messages, model, seed, trace
    )

    return manifest, trace


def generate_balanced(
    brief: str,
    provider: LLMProvider,
    seed: int = 42,
    model: str = _DEFAULT_MODEL_BALANCED,
) -> tuple[AlgoManifest, GenerationTrace]:
    """Two-pass balanced generation: initial manifest + critique + revision.

    Parameters
    ----------
    brief:
        Natural-language strategy brief.
    provider:
        LLM provider.
    seed:
        Determinism seed.
    model:
        Override model name; defaults to qwen2.5:32b.
    """
    trace = GenerationTrace(mode="balanced", model=model, seed=seed)

    system_content = BALANCED_SYSTEM
    user_content = render_balanced_user(brief)

    messages: list[ChatMessage] = [
        ChatMessage(role="system", content=system_content),
        ChatMessage(role="user",   content=user_content),
    ]
    _record_turn(trace, "system", system_content)
    _record_turn(trace, "user",   user_content)

    # Turn 1: initial generation
    initial_dict, _initial_manifest = _generate_with_retry(
        provider, messages, model, seed, trace
    )

    # Build critique conversation
    critique_content = render_balanced_critique(initial_dict)
    messages = messages + [
        ChatMessage(role="assistant", content=json.dumps(initial_dict, indent=2)),
        ChatMessage(role="user",      content=critique_content),
    ]
    _record_turn(trace, "user", critique_content)

    # Turn 2: critique + revision
    revised_dict, revised_manifest = _generate_with_retry(
        provider, messages, model, seed, trace
    )

    return revised_manifest, trace


def generate_max(
    brief: str,
    provider: LLMProvider,
    seed: int = 42,
    n_candidates: int = _MAX_CANDIDATES_DEFAULT,
    reasoning_provider: Optional[LLMProvider] = None,
    model: str = _DEFAULT_MODEL_MAX,
) -> tuple[AlgoManifest, GenerationTrace]:
    """N-candidate ensemble max generation.

    Pipeline:
      1. Generate N candidates (via reasoning_provider or main provider).
      2. Quick-backtest each to score by Sharpe.
      3. Take top 2 by Sharpe.
      4. Critique each top-2 candidate, get revised manifest.
      5. Quick-backtest the 2 revised manifests.
      6. Return the best revised manifest.

    Parameters
    ----------
    brief:
        Natural-language strategy brief.
    provider:
        Main LLM provider (used for generation if reasoning_provider=None).
    seed:
        Determinism seed.
    n_candidates:
        Number of initial candidates to generate (default 5).
    reasoning_provider:
        Optional separate reasoning-model provider. If None, falls back to
        main provider with a warning recorded in the trace.
    model:
        Model name for the reasoning provider.
    """
    # Decide which provider to use for generation
    gen_provider: LLMProvider
    if reasoning_provider is not None:
        gen_provider = reasoning_provider
        gen_model = model
    else:
        gen_provider = provider
        gen_model = model
        warnings.warn(
            "generate_max: reasoning_provider=None, falling back to main provider. "
            "Set AF_ALGO_GEN_REASONING_* env vars for true max mode.",
            stacklevel=2,
        )

    trace = GenerationTrace(
        mode="max",
        model=gen_model,
        reasoning_model=gen_model if reasoning_provider else None,
        seed=seed,
    )
    if reasoning_provider is None:
        trace.warnings.append(
            "reasoning_provider=None: fell back to main provider for generation"
        )

    bars = _load_canonical_bars()

    # Context hints to diversify candidates
    _context_hints = [
        "momentum breakout above resistance using ATR bands",
        "mean reversion to moving average after oversold RSI signal",
        "volatility contraction then expansion with Bollinger bands",
        "trend following with ADX filter for strong directional markets",
        "multi-timeframe confluence using EMA slopes and RSI divergence",
        "stochastic oscillator crossover with volume confirmation",
        "keltner channel breakout with OBV divergence filter",
    ]

    # ── Phase 1: Generate N candidates ────────────────────────────────────────
    system_content = MAX_SYSTEM
    _record_turn(trace, "system", system_content)

    candidates: list[CandidateSummary] = []

    for i in range(n_candidates):
        hint = _context_hints[i % len(_context_hints)]
        user_content = render_max_user(
            brief=brief,
            candidate_num=i + 1,
            n_candidates=n_candidates,
            context_hint=hint,
        )
        messages = [
            ChatMessage(role="system", content=system_content),
            ChatMessage(role="user",   content=user_content),
        ]
        _record_turn(trace, "user", user_content)

        summary = CandidateSummary(index=i, manifest=None, sharpe=None, trades=None, max_dd=None)

        try:
            manifest_dict, manifest = _generate_with_retry(
                gen_provider, messages, gen_model, seed + i, trace
            )
            summary.manifest = manifest_dict

            # Quick backtest
            if bars:
                try:
                    result = _quick_backtest(manifest, bars)
                    summary.sharpe = result.sharpe
                    summary.trades = result.total_trades
                    summary.max_dd = result.max_drawdown
                except Exception as bt_err:
                    summary.error  = f"backtest: {bt_err}"
                    summary.sharpe = -999.0
                    summary.trades = 0
                    summary.max_dd = 100.0
            else:
                summary.sharpe = 0.0
                summary.trades = 0
                summary.max_dd = 100.0

        except GenerationError as gen_err:
            summary.error  = str(gen_err)
            summary.sharpe = -999.0

        candidates.append(summary)

    trace.candidates = candidates

    # ── Phase 2: Pick top 2 by Sharpe ─────────────────────────────────────────
    valid = [c for c in candidates if c.manifest is not None]
    if not valid:
        raise GenerationError("All candidates failed to generate valid manifests")

    valid.sort(key=lambda c: (c.sharpe or -999.0), reverse=True)
    top2 = valid[:2]

    # ── Phase 3: Critique and revise top 2 ────────────────────────────────────
    refined: list[tuple[AlgoManifest, float]] = []

    for cand in top2:
        cand.critiqued = True
        critique_content = render_max_critique(
            manifest_dict=cand.manifest or {},
            trades=cand.trades or 0,
            sharpe=cand.sharpe or 0.0,
            max_dd=cand.max_dd or 100.0,
        )

        messages = [
            ChatMessage(role="system", content=MAX_SYSTEM),
            ChatMessage(role="user",   content=critique_content),
        ]
        _record_turn(trace, "user", critique_content)

        pair = CritiquePair(
            candidate_index=cand.index,
            critique_prompt=critique_content,
            revised_manifest=None,
        )

        try:
            rev_dict, rev_manifest = _generate_with_retry(
                gen_provider, messages, gen_model, seed + 100 + cand.index, trace
            )
            pair.revised_manifest = rev_dict

            # Score revised
            if bars:
                try:
                    rev_result = _quick_backtest(rev_manifest, bars)
                    rev_sharpe = rev_result.sharpe
                except Exception:
                    rev_sharpe = cand.sharpe or 0.0
            else:
                rev_sharpe = cand.sharpe or 0.0

            refined.append((rev_manifest, rev_sharpe))

        except GenerationError as err:
            pair.error = str(err)
            # Fall back to original candidate
            try:
                original_manifest = validate_manifest(cand.manifest or {})
                refined.append((original_manifest, cand.sharpe or 0.0))
            except Exception:
                pass

        trace.critique_pairs.append(pair)

    if not refined:
        # Final fallback: use best original candidate
        best_cand = valid[0]
        best_manifest = validate_manifest(best_cand.manifest or {})
        best_cand.final = True
        return best_manifest, trace

    # Pick winner by Sharpe
    refined.sort(key=lambda x: x[1], reverse=True)
    winner_manifest, _ = refined[0]

    # Mark the winning candidate
    for cand in candidates:
        if (
            cand.manifest is not None
            and cand.manifest.get("name") == winner_manifest.name
        ):
            cand.final = True
            break

    return winner_manifest, trace
