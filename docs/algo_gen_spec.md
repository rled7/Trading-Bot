# Stretch S1 — AI Algorithm Generator (Spec)

**Status:** v0.1 (draft, supersedes earlier informal notes)
**Owners:** Supervisor (Opus) + per-language sub-agents (Sonnet)
**Depends on:** R8 (algorithm registry), R7 (backtest engine), R10 (risk), S2 (LLM provider), S4 (walk-forward + Monte Carlo).

This is the single source of truth for **S1 — AI Algorithm Generator**: an
LLM-driven pipeline that proposes new trading algorithms, validates them
against historical data, scores their robustness across multiple
dimensions, and (only if they earn a tier) promotes them into the
algorithm registry the live engine consumes.

---

## 1. Scope

S1 adds a new subsystem with four responsibilities:

1. **Generate** candidate algorithms from a natural-language brief plus
   live market context (LLM-driven, via the S2 provider abstraction).
2. **Validate** candidates against a deterministic five-stage pipeline.
3. **Score** validated candidates on three independent robustness axes
   and assign a tier (color-coded).
4. **Promote** tier-passing candidates into the existing R8 algorithm
   registry, with metadata carrying the tier, scores, and sizing policy
   the R10 risk layer must honor.

Out of scope: model training, online learning, parameter tuning beyond
the robustness perturbations, ensembling at runtime.

## 2. Module layout

```
python/algoforge/algo_gen/        # reference implementation (Python only for generation)
    __init__.py
    types.py                       # AlgoManifest, AlgoCandidate, TierReport, ConfidenceScore
    schema.py                      # JSON schema + Python validator
    sandbox.py                     # subprocess-isolated Python escape-hatch execution
    prompts.py                     # prompt templates per effort mode
    generator.py                   # fast / balanced / max generators
    validator.py                   # 5-stage pipeline orchestrator
    robustness.py                  # parameter-perturbation sweep
    tier.py                        # composite scoring + tier assignment
    promote.py                     # registry promotion + metadata write
    fixtures.py                    # canonical-fixture loader for tests
    __main__.py                    # CLI

python/tests/algo_gen/             # unit + integration tests

# validator + tier scorer ported (deterministic given canned LLM responses):
c/include/af_algo_gen.h            c/src/algo_gen.c            c/tests/test_algo_gen.c
cpp/include/core/algo_gen.hpp      cpp/src/algo_gen/*.cpp      cpp/tests/test_algo_gen.cpp
js/src/algo_gen/                   js/tests/algo_gen/

tests/fixtures/algo_gen/           # cross-language canonical fixtures
```

Generation (LLM orchestration, sandbox, prompts) is **Python-only**.
Validation and tier scoring are **fully cross-language** — given the
same manifest + canned market data + fixed seed, all four languages
produce byte-identical tier reports.

## 3. Manifest schema

The generated artifact is a JSON document conforming to this schema. The
LLM's job is to produce a valid manifest; everything downstream (sandbox
backtest, walk-forward, MC, promotion) operates on the manifest.

### 3.1 Top-level

```json
{
  "schema_version": "1.0",
  "name": "string (kebab-case, ≤48 chars)",
  "description": "string (≤280 chars, plain prose)",
  "rationale": "string (≤1024 chars, LLM's reasoning trace)",
  "timeframes": ["M5", "M15", "H1", ...],
  "symbols": ["EURUSD", "GBPJPY", ...] | "any",
  "indicators": [ /* §3.2 */ ],
  "entries":    [ /* §3.3 */ ],
  "exits":      [ /* §3.4 */ ],
  "risk":       { /* §3.5 */ },
  "code": "string | null"     /* §3.6 escape-hatch */
}
```

### 3.2 Indicators

A list of indicator declarations. Each is a name + parameters drawn
from `algoforge.indicators`:

```json
{ "id": "rsi14", "kind": "rsi", "params": { "period": 14 } }
{ "id": "ema50", "kind": "ema", "params": { "period": 50 } }
```

Allowed `kind` values are exactly the function names exported by the
shared indicators module (sma, ema, rsi, atr, macd, bollinger,
stochastic, obv, wma, cci, williams_r, roc, mfi, vwap, keltner, adx,
hma, dema, tema). `id` is a local label used in expressions.

### 3.3 Entries

A list of entry rules, each a Boolean expression DSL referencing
indicators by `id`, bar fields (`close`, `open`, `high`, `low`,
`volume`), and pattern names:

```json
{
  "side": "long",
  "when": "rsi14 < 30 and close > ema50 and pattern.hammer"
}
```

Multiple entries for the same side are OR'd. Long + short entries can
coexist on the same algo (hedged unless `risk.hedge=false`).

### 3.4 Exits

Same DSL surface plus stop-loss / take-profit shorthands:

```json
{ "side": "long", "when": "rsi14 > 70" }
{ "side": "long", "sl_atr": 1.5, "tp_atr": 3.0 }
```

`sl_atr` / `tp_atr` are unconditional bracket exits sized in ATR
multiples (uses the first declared ATR indicator, or default `atr14`).

### 3.5 Risk

```json
{
  "size":       "atr" | "fixed",
  "atr_mult":   1.5,                    /* when size=atr */
  "fixed_lots": 0.01,                   /* when size=fixed */
  "max_concurrent": 1,                  /* max open positions */
  "hedge":      false,
  "cool_down_bars": 0                   /* min bars between exits and re-entries */
}
```

### 3.6 Escape hatch (`code` field)

If the LLM cannot express its intent in the DSL, it may emit a `code`
string instead — a Python module body containing a single class
`GeneratedAlgo(IAlgorithm)` (signature defined in
`python/algoforge/algorithm.py`). When `code` is non-null:

- All other fields except `name`, `description`, `rationale`, `risk`,
  `timeframes`, `symbols` are ignored.
- Validation runs the code in a **subprocess-isolated** Python
  interpreter (see §5).
- The escape-hatch path is **Python-only**. The C/C++/JS validators
  return a deterministic "skip — escape hatch" record for these
  manifests; their CI suites simply don't include escape-hatch fixtures.

## 4. Effort modes

The generator exposes three modes, selected per request:

| Mode       | Backend                                                                                                          | Pipeline                                                                                                                                                                  |
|------------|------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `fast`     | local Ollama (`AF_LLM_MODEL`, default `llama3.1:8b`)                                                              | single pass: brief + context → manifest                                                                                                                                   |
| `balanced` | local Ollama larger model (env `AF_ALGO_GEN_BALANCED_MODEL`, default `qwen2.5:32b`; falls back to default if absent) | single pass + structured self-critique step ("identify three weaknesses; revise to address them") → manifest                                                              |
| `max`      | reasoning model via OpenAI-compatible API (env `AF_ALGO_GEN_REASONING_HOST`, `AF_ALGO_GEN_REASONING_MODEL`)        | N-candidate ensemble (default N=5): generate 5 → quick-backtest each → self-critique top 2 → refine → pick winner. All sub-generations and critiques streamed to the caller. |

Environment defaults:

| Variable                          | Default                          | Notes                                  |
|-----------------------------------|----------------------------------|----------------------------------------|
| `AF_ALGO_GEN_DEFAULT_MODE`        | `balanced`                       | mode used when none specified          |
| `AF_ALGO_GEN_BALANCED_MODEL`      | `qwen2.5:32b`                    | local                                  |
| `AF_ALGO_GEN_REASONING_HOST`      | `https://api.openai.com/v1`      | OpenAI-compatible                      |
| `AF_ALGO_GEN_REASONING_MODEL`     | `o3-mini`                        | adjustable                             |
| `AF_ALGO_GEN_REASONING_API_KEY`   | —                                | required for `max` mode                |
| `AF_ALGO_GEN_MAX_CANDIDATES`      | `5`                              | ensemble size in `max` mode            |
| `AF_ALGO_GEN_SEED`                | `42`                             | for the LCG used in MC + robustness    |

`fast` and `balanced` work fully offline. `max` is opt-in.

## 5. Validation pipeline

Five sequential stages. **Any stage failure rejects the candidate**;
later stages do not run.

```
┌────────┐  ┌────────┐  ┌──────────┐  ┌─────────────┐  ┌──────────┐  ┌────────┐
│ LLM    │→ │ Schema │→ │ Sandbox  │→ │ Walk-       │→ │ MC       │→ │ Tier   │→ promote
│ output │  │  +lint │  │ backtest │  │ forward     │  │ bootstrap│  │ score  │   or
└────────┘  └────────┘  └──────────┘  └─────────────┘  └──────────┘  └────────┘   reject
              (1)            (2)            (3)             (4)         (5)
```

### Stage 1 — Schema + lint

- Validate against the JSON schema in §3.
- Reject if any referenced indicator `id` is undeclared.
- Reject if DSL expression fails to parse (whitelist parser; no `eval`).
- Reject if escape-hatch `code` contains forbidden imports (`os`, `sys`,
  `subprocess`, `socket`, `urllib`, `requests`, `open`, `exec`, `eval`,
  `__import__`).

### Stage 2 — Sandbox backtest

- DSL path: compile the manifest into a runtime `IAlgorithm`
  implementation (`algo_gen.runtime.ManifestAlgo`) and run the existing
  R7 backtest engine on a canonical bar dataset
  (`tests/fixtures/algo_gen/bars_*.json`).
- Escape-hatch path: spawn a Python subprocess with restricted
  `sys.path`, no env inheritance, write-only pipe back. Subprocess
  imports `IAlgorithm`, instantiates `GeneratedAlgo`, runs the backtest,
  serializes a `BTResult`, exits.
- Hard gate: must produce ≥20 trades AND no NaN/inf in the equity curve
  AND not raise.

### Stage 3 — Walk-forward

- Anchored walk-forward via `analytics.walkforward_anchored` with
  `n_windows=5` (default; configurable per fixture).
- Each window must independently pass the gate (Sharpe ≥0.5, max DD
  ≤25%, ≥20 trades).
- Component score = `windows_passing / n_windows` (integer percentages:
  20, 40, 60, 80, 100).

### Stage 4 — Monte Carlo

- `analytics.mc_bootstrap` resamples the trade-by-trade returns 1000×
  with the seeded LCG (seed = `AF_ALGO_GEN_SEED`).
- Component score = % of resampled equity curves where the final
  Sharpe ≥0.5 AND max DD ≤25%.

### Stage 5 — Parameter robustness

- For every numeric parameter in the manifest (indicator `params.*`,
  `risk.atr_mult`, `risk.cool_down_bars`, any number in DSL constants),
  perturb ±20% (3 settings each: -20%, 0, +20%).
- Cartesian product capped at `AF_ALGO_GEN_ROBUSTNESS_MAX_RUNS`
  (default 27 = 3^3, so we perturb at most the top-3 most-impactful
  numeric params; selection is by absolute gradient on the in-sample
  Sharpe, computed via finite differences on a quick 50-bar replay).
- Component score = % of perturbed runs that pass the same gate.

## 6. Tier system

Three component scores (walk-forward, MC, parameter robustness), each
in `[0, 100]`. The tier is the **minimum of the three** — an algo that
is brittle on any single axis cannot earn a higher tier.

| Tier       | Color   | Min score | Live-engine treatment                                                     |
|------------|---------|-----------|---------------------------------------------------------------------------|
| 🔴 Reject  | red     | `< 70`    | not promoted; report only                                                 |
| 🟠 Orange  | orange  | `70–79`   | `experimental=true`, ¼ normal sizing, manual review, **paper-trade only** |
| 🟡 Caution | yellow  | `80–89`   | `experimental=true`, ½ normal sizing, manual review before live           |
| 🟢 Green   | green   | `90–94`   | full sizing, paper + live                                                 |
| ⚪ White   | white   | `95–100`  | elevated sizing (multiplier from `[algo_gen]` config, default `1.5×`)     |

Tier metadata is written into the registry entry (§7).

Configurable in `algoforge.ini`:

```ini
[algo_gen]
orange_size_mult = 0.25
yellow_size_mult = 0.50
green_size_mult  = 1.00
white_size_mult  = 1.50
require_manual_review_below = green     ; orange & yellow gated behind /api/algos/{id}/promote with confirm=true
paper_only_below             = yellow    ; orange forbidden from live regardless of confirmation
```

## 7. Registry promotion

A passing candidate is registered via `AlgorithmRegistry.register(...)`
with an extended metadata dict:

```python
algo.metadata = {
    "source": "algo_gen",
    "schema_version": "1.0",
    "tier": "green",                       # red is never promoted
    "tier_color": "#00ff88",
    "scores": {"walk_forward": 100.0, "mc_bootstrap": 92.4, "robustness": 88.9},
    "min_score": 88.9,
    "size_mult": 1.0,                      # already resolved against [algo_gen]
    "experimental": False,
    "paper_only": False,
    "created_at": "2026-05-23T18:42:11Z",
    "generator": {"mode": "balanced", "model": "qwen2.5:32b", "seed": 42},
    "brief": "long-side EURUSD H1 trend continuation",
    "manifest_path": "registry/<name>.json",
}
```

`IAlgorithm` is extended with an optional `metadata` attribute (default
`None`) — backward compatible with the existing four hand-written
algos.

The R10 risk layer reads `metadata["size_mult"]` when sizing positions
for any algo that has it; algos without metadata keep their current
behavior.

## 8. CLI surface

```
python -m algoforge.algo_gen generate \
    --brief "long-side EURUSD H1 trend continuation"  \
    --mode balanced                                    \
    --seed 42                                          \
    --out registry/

python -m algoforge.algo_gen validate registry/<name>.json
python -m algoforge.algo_gen promote  registry/<name>.json --confirm
python -m algoforge.algo_gen list     [--tier green]
python -m algoforge.algo_gen retire   <algo-name>
```

## 9. Dashboard surface (Python only)

| Method | Path                              | Behavior                                                                                                       |
|--------|-----------------------------------|----------------------------------------------------------------------------------------------------------------|
| POST   | `/api/algos/generate`             | Body `{brief, mode?, seed?}`. Returns SSE: `event: thinking`, `event: candidate`, `event: validation`, `event: tier`, `event: done`. |
| POST   | `/api/algos/{id}/backtest`        | Re-runs stages 2–5 on demand (e.g., after a registry edit).                                                    |
| POST   | `/api/algos/{id}/promote`         | Body `{confirm: true}`. Refused for tier=red. Required for orange/yellow.                                      |
| POST   | `/api/algos/{id}/retire`          | Disables the algo (sets `is_enabled=false`) without deleting.                                                  |
| GET    | `/api/algos`                      | Lists registry entries with tier badges + scores.                                                              |
| GET    | `/api/algos/{id}/manifest`        | Returns the raw manifest JSON.                                                                                 |

If the dashboard is built with `llm=None`, **all generation endpoints
return 503** `error: "llm_disabled"`. List/manifest/backtest/promote/
retire continue to work — they don't need the LLM.

Frontend Algo Lab panel:

- Brief textarea + effort dropdown (`fast`/`balanced`/`max`) + Generate
  button (streams `event: thinking` tokens live).
- Tier badge appears on `event: tier` — colored per §6.
- Three score bars (WF / MC / Robustness).
- "Save to registry" button (disabled for red; opens confirm dialog for
  orange / yellow).
- Registry table at the bottom, filterable by tier, with retire button.

## 10. Cross-language parity scope

| Capability            | Python | C   | C++ | JS  |
|-----------------------|--------|-----|-----|-----|
| Manifest schema       | ✅     | ✅  | ✅  | ✅  |
| Schema validator      | ✅     | ✅  | ✅  | ✅  |
| Sandbox backtest      | ✅     | ✅¹ | ✅¹ | ✅¹ |
| Walk-forward scoring  | ✅     | ✅  | ✅  | ✅  |
| MC bootstrap scoring  | ✅     | ✅  | ✅  | ✅  |
| Robustness scoring    | ✅     | ✅  | ✅  | ✅  |
| Tier assignment       | ✅     | ✅  | ✅  | ✅  |
| LLM generation        | ✅     | ❌  | ❌  | ❌  |
| Escape-hatch sandbox  | ✅     | ❌  | ❌  | ❌  |
| Registry promotion    | ✅     | ✅² | ✅² | ✅² |

¹ DSL-path only. Escape-hatch manifests return a deterministic "skip"
   record in C/C++/JS.
² Each language has its own registry surface; promotion writes manifest
   metadata that all four can read.

Given the same manifest + canned bar data + fixed seed, all four
languages produce byte-identical `TierReport` JSON.

## 11. Canonical fixtures

`tests/fixtures/algo_gen/`:

| File                                       | Purpose                                              |
|--------------------------------------------|------------------------------------------------------|
| `bars_eurusd_h1_2y.json`                   | 8760 H1 bars, deterministic synthetic data           |
| `bars_gbpjpy_m15_6mo.json`                 | 17280 M15 bars                                       |
| `manifest_trend_follow_green.json`         | known-green manifest (full scores)                   |
| `manifest_trend_follow_orange.json`        | known-orange manifest (fragile entry threshold)      |
| `manifest_mean_revert_yellow.json`         | known-yellow                                         |
| `manifest_breakout_white.json`             | known-white                                          |
| `manifest_invalid_schema.json`             | stage 1 rejection                                    |
| `manifest_low_trade_count.json`            | stage 2 rejection (<20 trades)                       |
| `manifest_wf_fail.json`                    | stage 3 rejection                                    |
| `manifest_escape_hatch_green.json`         | Python-only (others skip)                            |
| `llm_response_trend_balanced.json`         | canned generation response (balanced mode)           |
| `llm_response_breakout_max_ensemble.json`  | canned 5-candidate + critique + refinement transcript|
| `tier_report_trend_follow_green.json`      | expected tier output for the green manifest          |
| `tier_report_breakout_white.json`          | expected tier output for the white manifest          |
| ... (≥18 fixtures total)                   |                                                      |

All four languages validate against the same `tier_report_*.json`
files. Component scores must match to **±0.01** (the tolerance for
floating-point variation across language stdlibs).

## 12. Determinism

- All randomness flows through `analytics.SeededLCG(seed=AF_ALGO_GEN_SEED)`.
- LLM responses are canned in tests via the same fixture protocol as S2
  (`provider.with_transport(MockTransport(fixture_name))`).
- Subprocess sandbox: seeded `random.seed(seed); numpy.random.seed(seed)`
  in a `sitecustomize`-equivalent shim before `GeneratedAlgo` imports.
- Tier reports compare byte-for-byte after canonical key ordering and
  fixed-precision float formatting (15 sig figs, shortest round-trip
  decimal — same convention as S2 §3.6).

## 13. Testing

Per-language test counts (targets; final numbers locked in commit
messages):

| Language | Tests (target) | Notes                                              |
|----------|----------------|----------------------------------------------------|
| Python   | ≥150           | schema, sandbox, generator, validator, tier, CLI   |
| JS       | ≥40            | validator + tier scorer + fixture round-trip       |
| C        | ≥60            | validator + tier scorer                            |
| C++      | ≥30            | validator + tier scorer                            |

Dashboard adds: ≥18 endpoint tests (mirrors S2 phase 2A); ≥5 static
asset tests for the Algo Lab panel.

## 14. Security posture

- DSL parser is a whitelist (no `eval`, no attribute access beyond a
  fixed set of bar/indicator names).
- Escape-hatch subprocess: dropped privileges (no env, no network, no
  filesystem writes outside a single temp dir, hard wall-clock cap of
  30s).
- Forbidden imports list enforced via AST inspection **before** the
  subprocess even spawns. Manifests with forbidden imports never reach
  stage 2.
- API keys for `max` mode are read from env only — never echoed in
  prompts, never logged, never serialized into manifests or tier
  reports.
- Generated code is **never** executed in the supervisor's process.

## 15. Versioning / compatibility

- `schema_version` is required; the validator refuses unknown versions
  with `error: "unsupported_schema_version"`.
- A manifest produced by `algoforge` v0.X with schema 1.0 must remain
  validatable by any later v0.Y as long as both declare schema 1.0.
- Tier thresholds and sizing multipliers are read from
  `algoforge.ini` at runtime — changing them does **not** retro-tier
  previously promoted algos (they keep the tier they earned at
  promotion time, but `size_mult` is re-resolved on every live load).
