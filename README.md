# AlgoForge — Multi-Language Trading Bot

Four independent implementations of the same blueprint, each in its own
language, sharing one repo.

```
.
├── c/        — C17, manual memory, MT5 DLL bridge for live trading
├── cpp/      — C++17/20, the most complete implementation today
├── python/   — Python 3.11+, native MetaTrader5 pkg for live trading
└── js/       — Node.js 18+, MT5 via ffi for live trading
```

Each project is fully self-contained: its own build entry point, its own
tests, its own README. They share the blueprint (architecture, pattern
registry, risk rules) but no code.

## Project status

| Project   | Test entry                                    | Bench entry                         | Tests |
|-----------|-----------------------------------------------|-------------------------------------|-------|
| c/        | `make -C c test`                              | `make -C c bench && c/build/af_bench`           | 41    |
| cpp/      | `cpp/build/Release/af_tests`                  | `cpp/build/Release/af_bench`                    | 102   |
| python/   | `(cd python && PYTHONPATH=. python3 -m unittest discover tests)` | `(cd python && PYTHONPATH=. python3 -m algoforge.bench)` | 47    |
| js/       | `npm --prefix js test`                        | `npm --prefix js run bench`                     | 47    |

Each language's bench accepts `[bars] [iterations]` positional args (defaults
100000 / 10) and prints a TSV with `# language` / `# bars` / `# iterations`
headers followed by `indicator\ttotal_ns\tns_per_iter` rows — same format
across all four projects.

## Cross-language benchmark

```bash
benchmarks/run_all.sh [bars] [iterations]
```

Runs the same SMA(20), EMA(50), RSI(14), ATR(14) workload on a deterministic
100k-bar walk in every project and prints a comparison table.

Run all four:

```bash
make -C c test \
  && (cd cpp && cmake --build build/Release && ./build/Release/af_tests) \
  && (cd python && python3 -m unittest discover tests) \
  && (cd js && npm test)
```

## Blueprint phases

| Phase                   | c   | cpp | python | js  |
|-------------------------|-----|-----|--------|-----|
| 1. Foundation           | —   | ⚠️  | —      | —   |
| 2. Data Pipeline        | —   | ⚠️  | —      | —   |
| 3. Indicators           | ⚠️  | ⚠️  | ⚠️     | ⚠️  |
| 4. Pattern Engine       | ⚠️  | ⚠️  | ⚠️     | ⚠️  |
| 5. Multi-TF Confluence  | —   | ❌  | —      | —   |
| 6. Risk & Execution     | —   | ⚠️  | —      | —   |
| 7. Built-in Algorithms  | —   | ⚠️  | —      | —   |
| 8. Backtesting          | —   | ✅  | —      | —   |
| 9. 24/7 Ops             | —   | ⚠️  | —      | —   |
| S1. AI Algo Generator   | —   | ❌  | —      | —   |
| S2. Local LLM           | —   | ❌  | —      | —   |
| S3. Web Dashboard       | —   | ❌  | —      | —   |

Legend: ✅ done · ⚠️ partial · ❌ not started · — scaffold only
