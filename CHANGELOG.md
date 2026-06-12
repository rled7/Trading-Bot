# AlgoForge — Changelog

The canonical, top-level changelog for the whole project (all four language
implementations). For bug-level detail on the original C/C++ port see
[`cpp/CHANGELOG_CPP.md`](./cpp/CHANGELOG_CPP.md); for the live C++ port progress log see
[`docs/CPP-PORT-PLAN.md`](./docs/CPP-PORT-PLAN.md).

---

## 2026-06 — C++ port to production parity (Phases 1–5)

Branch `cpp-port-phase1` → merged to `main`. The C++ implementation (`cpp/`) was brought
from the reference core up to feature parity with the Python bot. Full **ctest suite:
14/14 green** (cmake Release build verified).

### Phase 1 — REST broker layer
Ported every REST broker adapter from its Python oracle, each with a C++ parity test:
- **IBKR** 65/65 · **OANDA** 74/74 · **Alpaca** 33/33 · **Binance** 28/28 · **Coinbase** 24/24
- Added `core/hmac_sha256` (SHA-256 + HMAC), verified byte-for-byte against openssl
  (Binance/Coinbase request signing).
- `RestBroker` foundation + `BrokerConfig::from_env()` + `make_broker()` registry.

### Phase 2 — Algorithm generation (`algo_gen`)
- Prompts module + `extract_json_block` (10/10).
- `generate_fast` + retry/trace (7/7), `generate_balanced` (11/11),
  `generate_max` ensemble — **generator 17/17**.
- Fixed the `AlgoGenTests` CMake fixture path (pointed at the canonical repo-root
  fixtures, same as the Python oracle) → suite went **12/12 green**.
- Note: the Python `sandbox.py` subprocess executor is intentionally **not** ported
  (no honest C++ analog); C++ uses a deterministic skip hatch.

### Phase 3 — Analytics
Mirrors the full Python analytics package (already ported; `dashboard.py` is pure web →
belongs to Phase 5).

### Phase 4 — S6 discovery daemon
- Implementation spec (`docs/s6_spec.md`) + `s6/discovery_daemon` (lib `af_s6`).
- Orchestration-only over classify_regime / generate_balanced / validate / promote, with
  injected generate/validate fns for determinism. 7-scenario golden oracle — **13/13**.

### Phase 5 — C++ web dashboard
- Vendored header-only **cpp-httplib**. `af_dashboard` = parity-tested pure handlers
  ((inputs) → JSON string) + a thin, untested-by-design httplib binding (`server.cpp`)
  that only wires sockets to those handlers. **SSE proven** (chunked content provider).
- Routes ported to parity with `python/algoforge/dashboard/server.py`:
  - Slice 1 — `/` static, `/api/health`, `/api/symbols`, `/api/logs` (+ `LogRingBuffer`).
  - Slice 2 — broker-backed `/api/account`, `/api/positions`, `/api/orders`, `/api/bars/{symbol}`.
  - Slice 3 — `/api/llm/health`, `/api/llm/models`, `/api/llm/chat`, `/api/llm/chat/stream`.
  - Slice 4 — `/api/stream/ticks` (broker tick SSE; `tick_json` + interval clamp + symbol parse).
  - Slice 5 — the full `/api/algos` group: GET list, POST `/generate`, POST `/generate/stream`
    (SSE staged prompt→manifest→tier→[DONE]), POST `/{id}/backtest`, GET `/{id}/manifest`,
    POST `/{id}/promote`, POST `/{id}/retire`. Backed by the new public `AlgoGenService`
    facade + `manifest_to_json` serialiser + a thread-safe in-memory `AlgoStore` (mirrors the
    Python `_STORE`). 503 `algo_gen_disabled` on all routes when the service is unconfigured.
  - Slice 6 — `af_dashboard_server` binary: constructs `ServerDeps` (PaperBroker + optional
    OllamaProvider/AlgoGenService) and runs `httplib::Server`. The routes now actually serve.
- **Full suite green: `ctest` 14/14, `af_tests` 102/102.** (The SQLite-journal /
  LearnedBlockStore / CSV-fixture cases write DB + temp files, so they only "fail" when run
  inside a sandbox that blocks those writes — not a code defect; they pass with write access.)

> **All `server.py` routes are now ported AND served.** Slice 6 adds the
> **`af_dashboard_server`** binary (the C++ analogue of `uvicorn make_app`): it wires a live
> in-process `PaperBroker` into the routes and runs `httplib::Server`. Smoke-tested over a
> socket — `/api/health`, `/api/account`, `/api/bars/{sym}`, `/api/symbols` serve live broker
> data; `/api/llm/*` and `/api/algos/*` return 503 (`llm_disabled` / `algo_gen_disabled`)
> until enabled with `--llm-host URL`, which constructs an `OllamaProvider` + `AlgoGenService`
> over canonical EURUSD/H1 bars. The httplib binding itself remains untested-by-design; the
> serialisers, parsers, and error mapping carry the parity tests.
>
> Documented behavioural divergences from Python: the C++ `/generate` response folds Python's
> separate `{validation, tier}` into the unified `TierReport`; `/generate/stream` emits no
> separate `validation` stage; absent manifest Optionals are omitted, not `null`.

### Still open (decisions / hard blocks only)
- **MT5 connector** — hard-blocked: needs the proprietary Windows MetaTrader5 DLL.
  Stays a WIN32-only stub on macOS/Linux.
- **Phase 6 (decision)** — whether to flip production over to the C++ build and retire Python
  (or keep Python as the reference oracle). The C++ port is now feature-complete to parity
  (engine, brokers, algo_gen, S6, and a served dashboard), so this is purely a go/no-go call.
- **Live LLM/algo_gen demo (optional)** — running `af_dashboard_server --llm-host …` against a
  real Ollama instance to exercise `/api/algos` end-to-end is environment-gated (needs Ollama),
  not a code task.

---

## 2026-03 — Initial multi-language port (C/C++ reference)

Complete reimplementation of the Python AlgoForge in C17 + C++20 (the `cpp/` reference):
38 indicators, 25+ candlestick + chart/harmonic patterns, risk/sizing, backtest engine,
paper broker, learning error-registry. Self-contained test runner, **94/94 tests**, with
~40–60× speedups over Python on the hot paths. Full file-by-file detail and the eight
build-time bug fixes are in [`cpp/CHANGELOG_CPP.md`](./cpp/CHANGELOG_CPP.md).
