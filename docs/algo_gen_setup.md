# Algo Generator Setup (S1)

AlgoForge's S1 layer adds an AI algorithm generator that turns
natural-language briefs into validated trading algorithms. The
generator runs on the same local Ollama stack used by S2, with an
optional remote reasoning backend for `max` effort mode.

For the spec (manifest schema, validation pipeline, tier system), see
**[algo_gen_spec.md](algo_gen_spec.md)**.

---

## 1. Prerequisites

S1 builds on S2. Complete **[llm_setup.md](llm_setup.md)** first —
Ollama must be running with the default chat model pulled.

## 2. Effort modes & recommended models

| Effort | Use case | Recommended model | Notes |
|---|---|---|---|
| `fast` | Quick exploration, drafts | `llama3.1:8b` (default) | Single-pass local Ollama |
| `balanced` | Most production work | `qwen2.5:32b` | Generate + self-critique |
| `max` | Hard problems, novel patterns | Remote reasoning model | N-candidate ensemble via OpenAI-compatible API |

Pull larger Ollama models with:

```bash
ollama pull qwen2.5:32b
```

## 3. Environment variables

S1 inherits all `AF_LLM_*` vars from S2. Additionally:

| Variable | Default | Notes |
|---|---|---|
| `AF_ALGO_GEN_BALANCED_MODEL` | `qwen2.5:32b` | model for `effort=balanced` |
| `AF_ALGO_GEN_REASONING_HOST` | `https://api.openai.com/v1` | OpenAI-compatible base URL for `max` effort |
| `AF_ALGO_GEN_REASONING_MODEL` | `o3-mini` | reasoning model name |
| `AF_ALGO_GEN_REASONING_API_KEY` | _(unset)_ | required for `max` mode |
| `AF_ALGO_GEN_MAX_CANDIDATES` | `5` | ensemble size for `max` mode |
| `AF_ALGO_GEN_SEED` | `42` | RNG seed |

## 4. Validation pipeline

Every generated manifest goes through 5 stages before tier assignment:

1. **Schema** — manifest validates against `algo_gen.schema`
2. **Sandbox backtest** — DSL parsed & executed against seeded bars; rejects unsafe code
3. **Walk-forward** — out-of-sample performance across folds
4. **MC bootstrap** — equity curve resampling for robustness
5. **Parameter robustness** — ±20% perturbation of all numeric params

Tier = `min(walk_forward, mc, robustness)` mapped to:

| Score | Tier | size_mult | Flags | Action |
|---|---|---|---|---|
| <70 | 🔴 Red | — | reject | discarded |
| 70–79 | 🟠 Orange | 0.5 | experimental + paper_only | high-risk, manual review |
| 80–89 | 🟡 Yellow | 0.5 | experimental | caution, half sizing |
| 90–94 | 🟢 Green | 1.0 | — | promotion candidate |
| 95–100 | ⚪ White | 1.2 | — | rare — elevated sizing |

The `size_mult` field rides on `IAlgorithm.metadata` and is honored
by the risk layer in all 4 languages.

## 5. CLI usage

```bash
cd python

# Validate an existing manifest
python -m algoforge.algo_gen validate manifests/eurusd_breakout.json

# Generate a new manifest
python -m algoforge.algo_gen generate \
    --brief "long EURUSD breakouts with ATR stops" \
    --mode balanced --seed 42

# Promote validated manifest
python -m algoforge.algo_gen promote manifests/eurusd_breakout.json

# List registry
python -m algoforge.algo_gen list

# Retire an algorithm
python -m algoforge.algo_gen retire eurusd-breakout-v1
```

## 6. Dashboard usage

Wire the generator into `make_app`:

```python
from algoforge.dashboard.server import make_app
from algoforge.llm.ollama import OllamaProvider

llm = OllamaProvider()
app = make_app(llm=llm, algo_gen=True)
```

Visit `http://localhost:8000/static/algo_lab.html`. The **Algo Lab**
nav link is active when `algo_gen=` is provided; without it all
`/api/algos/*` routes return 503 `algo_gen_disabled`.

## 7. Sandbox security

Manifests use a whitelisted DSL by default. The optional Python
escape-hatch (`code` field, Python-only) runs in a subprocess with:
- AST-checked forbidden imports (`os`, `sys`, `subprocess`, `socket`, network)
- Wall-clock timeout
- Read-only access to seeded bar data only
- No filesystem write access

C/C++/JS ports return a deterministic
`escape_hatch_skip_<lang>` stage record — they never execute foreign
code.

**Never load manifests from untrusted sources** — even sandboxed code
consumes CPU.

## 8. Endpoints (Python dashboard)

| Method | Path | Behavior |
|---|---|---|
| POST | `/api/algos/generate` | non-streaming, returns `{manifest, validation, tier}` |
| POST | `/api/algos/generate/stream` | SSE: stage events → `[DONE]` |
| POST | `/api/algos/{id}/backtest` | paper backtest run |
| POST | `/api/algos/{id}/promote` | promote to registry |
| POST | `/api/algos/{id}/retire` | retire from registry |
| GET | `/api/algos` | list all |
| GET | `/api/algos/{id}/manifest` | full manifest JSON |

Error kind → HTTP: `llm_timeout→504`, `llm_unreachable→502`,
`schema_invalid→422`, `sandbox_failed→422`, `not_found→404`,
generator disabled → 503.

## 9. Cross-language parity

All four implementations validate against the same canonical fixtures
at `tests/fixtures/algo_gen/*.json`:

- Manifest fixtures: `manifest_*.json`
- Tier-report fixtures: `tier_report_*.json`
- LLM response fixtures (Python only): `llm_response_*.json`
- Bar datasets: `bars_*.json`

C/C++/JS test suites exercise the validator + tier + promote + DSL
modules. The LLM-driven generator is Python-only by design — the
ports execute manifests but do not generate them.

## 10. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `503 algo_gen_disabled` | `make_app` built with `algo_gen=None` |
| `schema_invalid` | LLM returned malformed JSON — retry or lower temperature |
| All generations tier Red | Brief too vague — add concrete entry/exit criteria |
| `sandbox_failed: timeout` | DSL evaluation looped — check the manifest's `entries`/`exits` |
| Walk-forward score 0 | Insufficient bars in seeded dataset — switch fixture |
| `AF_ALGO_GEN_REASONING_API_KEY required` | `max` mode without API key — use `balanced` or set the env |
