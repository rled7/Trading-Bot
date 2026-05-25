# AlgoForge — Multi-Language Trading Bot

Four independent implementations of the same blueprint, each in its own
language, sharing one repo.

```
.
├── c/        — C17, vtable broker, manual memory, MT5 DLL bridge for live trading
├── cpp/      — C++20, reference implementation (most complete)
├── python/   — Python 3.11+, native MetaTrader5 pkg for live trading
└── js/       — Node.js 18+, MT5 via ffi for live trading
```

Each project is fully self-contained: its own build entry point, its own
tests, its own README. They share the blueprint (architecture, pattern
registry, risk rules) but no code.

## Build & test quick-reference

| Project   | Test command                                                        | Bench command                          |
|-----------|---------------------------------------------------------------------|----------------------------------------|
| `c/`      | `make -C c test`                                                    | `make -C c bench && c/build/af_bench`  |
| `cpp/`    | `cpp/build/Release/af_tests`                                        | `cpp/build/Release/af_bench`           |
| `python/` | `cd python && PYTHONPATH=. python3 -m unittest discover tests`      | `cd python && PYTHONPATH=. python3 -m algoforge.bench` |
| `js/`     | `npm --prefix js test`                                              | `npm --prefix js run bench`            |

Each bench accepts `[bars] [iterations]` (defaults 100 000 / 10) and prints
a TSV with `# language` / `# bars` / `# iterations` headers, then
`indicator\ttotal_ns\tns_per_iter` rows — same format across all four
projects.

## Cross-language benchmark

```bash
benchmarks/run_all.sh [bars] [iterations]
```

Runs SMA(20), EMA(50), RSI(14), ATR(14) on a deterministic 100k-bar walk in
every project and prints a comparison table.

Run all four test suites:

```bash
make -C c test \
  && (cd cpp && cmake --build build/Release && ./build/Release/af_tests) \
  && (cd python && python3 -m unittest discover tests) \
  && (cd js && npm test)
```

---

## Implementation progress

Progress is tracked by **round** — each round ports one feature layer from
the C++ reference to the three remaining languages (C, Python, JS).

### Module inventory

| Module | C | C++ | Python | JS |
|---|---|---|---|---|
| Core types | ✅ `af_types.h` | ✅ `core/types.h` | ✅ `types.py` | ✅ `types.js` |
| Indicators (38) | ✅ `af_indicators.h` | ✅ `indicators.h` | ✅ `indicators.py` | ✅ `indicators.js` |
| Candlestick patterns (7) | ✅ `af_patterns.h` | ✅ pattern engine | ✅ `patterns.py` | ✅ `patterns.js` |
| Chart patterns (8) | ✅ `af_patterns.h` | ✅ pattern engine | ✅ `patterns.py` | ✅ `patterns.js` |
| Harmonic patterns (5) | ✅ `af_patterns.h` | ✅ pattern engine | ✅ `patterns.py` | ✅ `patterns.js` |
| Broker interface | ✅ `af_broker.h` | ✅ `broker.hpp` | ✅ `broker.py` | ✅ `broker.js` |
| Paper broker | ✅ `paper_broker.c` | ✅ `paper_broker.cpp` | ✅ `paper_broker.py` | ✅ `paper_broker.js` |
| Backtest engine | ❌ | ✅ `backtest_engine` | ❌ | ❌ |
| Algorithm registry | ❌ | ✅ 5 algos | ❌ | ❌ |
| Multi-TF confluence | ❌ | ⚠️ partial | ❌ | ❌ |
| Risk & execution | ❌ | ⚠️ partial | ❌ | ❌ |
| 24/7 ops (Docker/logs) | ❌ | ❌ | ❌ | ❌ |
| Live MT5 connectivity | ❌ | ❌ | ❌ | ❌ |

### Round tracker

| Round | Feature | C | C++ | Python | JS | Status |
|---|---|---|---|---|---|---|
| R1–R4 | Indicators (38 total) | ✅ | ✅ | ✅ | ✅ | **Complete** |
| R5 | Pattern engine — candlestick + chart + harmonic | ✅ | ✅ | ✅ | ✅ | **Complete** |
| R6 | Broker abstraction + paper broker | ✅ | ✅ | ✅ | ✅ | **Complete** |
| R7 | Backtest engine (bar-by-bar, ATR SL/TP, Sharpe/DD) | ❌ | ✅ | ❌ | ❌ | Next |
| R8 | Algorithm registry (5 algos) | ❌ | ✅ | ❌ | ❌ | Planned |
| R9 | Multi-TF confluence + weighted vote | ❌ | ⚠️ | ❌ | ❌ | Planned |
| R10 | Risk & execution (sizer, SL/TP, drawdown guard) | ❌ | ⚠️ | ❌ | ❌ | Planned |
| R11 | 24/7 ops (Docker, structured logs, Telegram alerts) | ❌ | ❌ | ❌ | ❌ | Planned |
| R12 | Live MT5 connectivity | ❌ | ❌ | ✅¹ | ❌ | Planned |
| S2 | Local LLM via Ollama (trade rationale, backtest commentary, news sentiment, strategy describe) + dashboard chat panel | ✅⁴ | ✅⁴ | ✅⁴ | ✅⁴ | **Complete** |
| S3 | Web dashboard (FastAPI + static UI, Python-hosted) | — | — | ✅² | — | **Complete** |
| S4 | Advanced analytics (14 metrics, drawdown episodes, distributions, correlations, Monte Carlo, walk-forward, OLS attribution, factor regression, online streaming, HTML report) | ✅³ | ✅³ | ✅³ | ✅³ | **Complete** |
| S1 | AI algorithm generator (manifest schema, 5-stage validator, tier system, sandboxed DSL, LLM-driven generation, Algo Lab dashboard panel) | ✅⁵ | ✅⁵ | ✅⁵ | ✅⁵ | **Complete** |
| S5 | Multi-broker support | — | — | — | — | Stretch |
| S6 | Background C++ algo discovery daemon (always-on observation, hypothesis former, S1-compatible manifests) | — | — | — | — | Planned |

¹ Python has the `MetaTrader5` package wired — live test requires Windows + MT5 terminal.
See **[docs/mt5_setup.md](docs/mt5_setup.md)** for prerequisites, environment variables (`MT5_PATH`, `MT5_SERVER`, `MT5_LOGIN`, `MT5_PASSWORD`), a usage snippet, and troubleshooting tips.

³ Full cross-language analytics layer with numerical parity (1e-9 tolerance) across all four
implementations. Shared canonical test vectors live in **[docs/analytics_spec.md](docs/analytics_spec.md)**;
each language's tests verify against the same vectors and the same seeded LCG sequence. Python
reference: `python/algoforge/analytics/` — run `python -m algoforge.analytics --backtest results.json
--out report.html` to render the report. The Python build also exposes streaming metrics over the
S3 dashboard via `algoforge.analytics.dashboard.make_router`.

² Read-only monitoring dashboard hosted by the Python implementation. FastAPI backend +
static HTML/JS frontend (no build step). Install with `pip install -e ".[dashboard]"` from
`python/`, then run `python -m algoforge.dashboard --port 8000`. Set `AF_DASHBOARD_TOKEN`
in env to enable bearer-token auth. UI shows account state, positions, orders, live ticks
(SSE), recent bars (Chart.js), structured log tail, and an optional LLM chat panel (S2).

⁴ Full cross-language LLM layer with byte-identical prompts and shared canonical fixtures
across all four implementations. Shared spec and fixtures live in
**[docs/llm_spec.md](docs/llm_spec.md)**; setup instructions in
**[docs/llm_setup.md](docs/llm_setup.md)**. Default model: `llama3.1:8b` via local Ollama
(`AF_LLM_HOST`, `AF_LLM_MODEL`). Use cases: `trade_rationale`, `backtest_commentary`,
`news_sentiment`, `strategy_describe`. The Python dashboard exposes a collapsible chat
panel via `/api/llm/*` (health, models, chat, chat/stream SSE).

⁵ Full cross-language algorithm generator layer. Manifest schema, whitelisted DSL,
5-stage validation pipeline (schema → sandbox-backtest → walk-forward → MC bootstrap →
parameter robustness), and tier system (🔴 Red <70, 🟠 Orange 70–79, 🟡 Yellow 80–89,
🟢 Green 90–94, ⚪ White 95–100) shared by all four implementations against canonical
fixtures at `tests/fixtures/algo_gen/*.json`. Spec at **[docs/algo_gen_spec.md](docs/algo_gen_spec.md)**;
setup at **[docs/algo_gen_setup.md](docs/algo_gen_setup.md)**. The LLM-driven generator
(fast / balanced / max effort modes) is Python-only by design (C/C++/JS return a
deterministic `escape_hatch_skip_<lang>` stage for the optional Python `code` field).
Python dashboard exposes the Algo Lab panel via `/api/algos/*` (`generate`,
`generate/stream` SSE, `backtest`, `promote`, `retire`, `list`, `{id}/manifest`).

Legend: ✅ complete · ⚠️ partial · ❌ not started · — not planned for this round

---

## Broker interface (Round 6)

All four languages implement the same `IBroker` contract:

| Method group | Methods |
|---|---|
| Connection | `connect`, `disconnect`, `is_connected`, `broker_name` |
| Account | `get_account` → balance / equity / margin / leverage |
| Market data | `get_tick`, `get_symbol_info`, `get_bars` |
| Trading | `place_order`, `close_position`, `modify_position`, `cancel_order` |
| Queries | `get_positions`, `get_orders`, `get_position` |
| Simulation | `poll_sl_tp` (paper mode SL/TP loop) |

`PaperBroker` supports 10 FX pairs (EURUSD, GBPUSD, USDJPY, XAUUSD, EURJPY,
GBPJPY, AUDUSD, USDCAD, USDCHF, EURGBP) with seeded random-walk price
simulation, realistic spreads, and $7/lot commission.

## Patterns (Round 5)

20 patterns detected across all four languages:

| Category | Patterns |
|---|---|
| Candlestick (7) | Doji, Hammer, Engulfing, Marubozu, PinBar, MorningStar, EveningStar, ThreeWhiteSoldiers, ThreeBlackCrows |
| Chart (8) | DoubleTop, DoubleBottom, AscendingTriangle, DescendingTriangle, H&S, InvH&S, BullishFlag, BearishFlag |
| Harmonic (5) | GartleyBull, GartleyBear, BatBull, ButterflyBull, CrabBull |

## Indicators (Rounds 1–4)

38 indicators across all four languages:

| Category | Indicators |
|---|---|
| Trend | SMA, EMA, WMA, HMA, DEMA, TEMA, VWMA, MACD, ADX, SAR, Ichimoku |
| Momentum | RSI, Stochastic, CCI, Williams%R, ROC, MFI, TRIX, Momentum |
| Volatility | TR, ATR, Bollinger Bands, Keltner, Donchian, HistVol |
| Volume | OBV, VWAP, CMF, A/D Line, Force Index, Volume Oscillator |
| S/R | Pivot (Classic/Fib/Camarilla), Fibonacci Retracements |
