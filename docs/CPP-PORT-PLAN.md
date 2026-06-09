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
- 2026-06-08: **Phase 1 S5 COMPLETE** — all 5 REST broker adapters ported + parity-green +
  pushed (IBKR 65, OANDA 74, Alpaca 33, Binance 28, Coinbase 24; rest_broker 41; af_tests 102).
  Added `core/hmac_sha256.hpp` (verified vs openssl). MT5 left as the intended WIN32-only stub
  (Phase 3; proprietary DLL not in repo — not portable on macOS/Linux).
- 2026-06-08: **Phase 2 algo_gen — prompts + generator DONE** (branch cpp-port-phase1):
  `prompts.cpp` (extract_json_block + renderers, 10/10) + `generator.cpp` (generate_fast/
  balanced/max against a mock LLMProvider, 17/17). Commits 81d4b74 → 0f07bdd.
  ⛔ **sandbox.py is a HARD BLOCKER — NOT ported (flag, don't fake).** It is a Python
  *subprocess* executor for user-generated *Python* escape-hatch code (`sys.executable -S -I`);
  there is no honest C++ analog. The C++ engine already treats escape-hatch manifests as a
  deterministic skip in `validate()` ("escape_hatch_skip_cpp"), which is the correct C++ stance.
  Remaining Phase 2 = wire generator into a CLI/entry if/when needed (not required for parity).
- 2026-06-08: **§S6 DECISIONS LOCKED** — user accepted all 6 proposed defaults verbatim
  (2026-06-08). S6 is no longer user-blocked; it is future work downstream of Phases 2–3.
- 2026-06-09: **C++ test suite 12/12 GREEN** — fixed the last failing suite (`AlgoGenTests`).
  Root cause was a CMake bug, not a logic gap: `FIXTURE_DIR` pointed at the non-existent
  `cpp/tests/fixtures` (CMAKE_SOURCE_DIR is the cpp/ subdir). Repointed to the canonical
  repo-root `../tests/fixtures` shared with the Python oracle (truest parity, no drift).
  No regressions (GeneratorTests/PromptsTests still green under the now-valid path).
- 2026-06-09: **Phase 1 verified fully closed** — config + registry are ported AND tested
  (consolidated into `rest_broker.hpp`: `BrokerConfig::from_env()/require()` mirrors config.py;
  `make_broker()/available_brokers()` mirrors registry.py; covered by `test_rest_broker.cpp`
  TestRegistry/TestConfigFromEnv, RestBrokerTests 41/41).
- 2026-06-09: **Phase 3 analytics compute = already DONE** — `cpp/src/analytics/` already
  mirrors the Python analytics package (correlations, distributions, drawdown, factors,
  html_report, metrics, ml_attribution, montecarlo, streaming, walkforward) + `test_analytics`.
  The only unmirrored file, `analytics/dashboard.py`, is **pure web** (its own docstring:
  "Dashboard SSE (Python only)" — a FastAPI router; compute lives in the already-ported
  `streaming.py`/`streaming.cpp`). It therefore belongs to **Phase 5 (web, LAST)**, not Phase 3.

- 2026-06-09: **Phase 4 (S6 discovery daemon) BUILT** — user authorized "spec-first then build".
  Spec `docs/s6_spec.md` (6 locked decisions + synthetic oracle). Implementation
  `cpp/include/s6/discovery_daemon.hpp` + `cpp/src/s6/discovery_daemon.cpp` (lib `af_s6`):
  orchestration only — reuses `af::classify_regime`, `generate_balanced`, `validate`, `promote`.
  Determinism via injected `generate_fn`/`validate_fn` (default to the real pipeline; tests
  override). `test_s6` golden oracle 7/7 (no-false-positive, single-crossing→single-emit,
  rate-limit cap, validator-reject-writes-nothing, tier-floor, bounded-memory, pause-during-live).
  **Full ctest 13/13.** af_s6 links af_algo_gen + af_engine cleanly (lazy static linking resolves
  the shared indicators.c; no duplicate-symbol clash). NOTE: this lifts Phase 4 off the wall;
  remaining items below are still the user's calls.

## ⏸️ DECISION WALL (2026-06-09) — reference-backed work is exhausted
Everything still buildable is now either a user decision or a platform impossibility:
- **MT5 (Phase 3 remainder)** — hard-blocked: proprietary MetaTrader5 DLL + Windows host,
  not portable on macOS/Linux. Intentional WIN32-only stub. *Not a decision — an impossibility.*
- **Phase 4 (S6 daemon)** — decisions are locked, but S6 *exists in no language and has no
  tested oracle*. Building it = guessing on unspecified behavior with nothing to assert parity
  against (violates governing rules 1 & 3). **Should be user-steered, not built autonomously.**
- **Phase 5 (dashboard web)** — explicitly LAST; the user reserved a scope-revision checkpoint
  here before sinking days into the least-defensible piece. *User decision.*
- **Phase 6 (flip production to C++)** — only after the above; decide Python's fate. *User decision.*
