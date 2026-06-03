# AlgoForge — Full C++ Production Port Plan

*Created 2026-06-01. Decision-locked. Supersedes the language-coverage question in NEXT-STEPS.md.*

## Decision (user, 2026-06-01)
**C++ is the production language. Port EVERYTHING to C++** — including the pieces that are
Python-only today (web dashboard, LLM algo-generator orchestration, live MT5 bridge). Rationale:
C++ is the fastest tier and already the most complete/engineered build. The cost (multi-week,
and C++ is a hostile language for the web UI + LLM orchestration) was presented and accepted.

## Governing rules (do not violate)
1. **Python is the reference oracle.** Every phase ports a *tested* Python module. C++ output must
   match Python output on identical inputs — port the Python tests (incl. mocked API responses),
   don't just re-green a fresh suite. Differential parity is the acceptance gate.
2. **Do NOT decommission Python until C++ reaches parity, phase by phase.** Python (1269 tests,
   runs live) stays as reference + fallback the entire way. No worker deletes or breaks a Python
   module as it ports. **The production switch flips LAST**, only after C++ is verified at parity.
3. **No guessing on unspecified behavior.** Where C++ scope has no tested Python reference (S6),
   it is BLOCKED on user decisions — see §S6. Reference-backed work goes first.

## What C++ already has (do not re-port)
core engine, algorithms, backtesting, indicators, patterns, learning, analytics, llm + algo_gen
scaffolding, the **R7–R12 ladder**, and `broker.hpp` + `paper_broker.cpp` + partial `mt5_broker.cpp`.

## Phase order (reference-backed first; worst rewrite last)
- **Phase 1 — S5 broker adapters in C++** *(IN PROGRESS)*. Port the 5 Python adapters
  (OANDA, Alpaca, IBKR, Binance, Coinbase) + `config` + `registry` behind the existing
  `broker.hpp`; finish `mt5_broker.cpp`. Port the 185 Python broker tests (mock responses) to C++
  and assert parity. Locked sub-decisions (mirror Python S5): all 5 brokers; per-adapter symbol
  mapping; paper/live = same code path + mode flag; env-var creds; **C++ is the target.**
- **Phase 2 — algo_gen parity in C++** *(IN PROGRESS)*. RECON 2026-06-01: C++ already has parity
  for dsl, promote, robustness, runtime, schema, tier, validator, json/serialise. **Only gaps**
  (the header's deferred "LLM generation + escape-hatch sandbox"): `generator.py` (LLM
  orchestration), `prompts.py` (templates), `sandbox.py` (escape-hatch code exec). Port these 3 to
  C++ behind the existing `algo_gen` namespace, reusing `cpp/include/core/llm.hpp` + the
  `cpp/tests/llm_mock.*` harness. Parity vs `python/tests/algo_gen/test_generator.py` +
  `test_sandbox.py`. ⚠️ The escape-hatch sandbox (executes generated strategy code) is the one
  genuinely-hostile piece — worker flags if intractable rather than faking it.
- **Phase 3 — live MT5 bridge finish + analytics.** Complete `mt5_broker.cpp`; port
  `analytics/dashboard.py` compute (not the web layer).
- **Phase 4 — S6 C++ discovery daemon.** ⛔ BLOCKED — see §S6. Do not build until decided.
- **Phase 5 — Dashboard in C++ (LAST).** HTTP server + algo_gen routes + Algo Lab panel +
  log buffer. Highest effort / lowest payoff / no perf benefit — deliberately last so the user
  can revise scope at this checkpoint before days are sunk into the least-defensible piece.
- **Phase 6 — Flip production to C++.** Only after all above verified at parity. Decide Python's
  fate (retire from production vs keep as reference).

## §S6 — BLOCKED on 6 user decisions (proposed defaults; confirm before any code)
S6 exists in no language and has no tested reference — highest-throwaway risk. Proposed defaults:
1. **Hypothesis logic:** hybrid — regime detection + statistical drift + pattern co-occurrence,
   feeding the LLM on candidate windows.
2. **Data window:** multiple horizons in parallel (1h / 24h / 7d).
3. **Output cadence:** emit candidate manifests on threshold-crossing, rate-limited (max N/hour).
4. **Tier targeting:** target the existing S1 mid tier first.
5. **Validator integration:** route generated manifests through the existing algo_gen validator
   before promote.
6. **Resource budget:** single low-priority background thread, bounded memory.

## Progress log
- 2026-06-01: Plan created, decision locked. Phase 1 (S5 C++ adapters) launched.
