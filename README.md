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

| Project   | Build entry            | Tests passing |
|-----------|------------------------|---------------|
| c/        | `cd c && make test`    | 1 / 1         |
| cpp/      | `cd cpp && cmake -B build/Release && cmake --build build/Release && ./build/Release/af_tests` | 102 / 102 |
| python/   | `cd python && python -m unittest discover tests` | 1 / 1 |
| js/       | `cd js && npm test`    | 1 / 1         |

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
| 3. Indicators           | —   | ⚠️  | —      | —   |
| 4. Pattern Engine       | —   | ⚠️  | —      | —   |
| 5. Multi-TF Confluence  | —   | ❌  | —      | —   |
| 6. Risk & Execution     | —   | ⚠️  | —      | —   |
| 7. Built-in Algorithms  | —   | ⚠️  | —      | —   |
| 8. Backtesting          | —   | ✅  | —      | —   |
| 9. 24/7 Ops             | —   | ⚠️  | —      | —   |
| S1. AI Algo Generator   | —   | ❌  | —      | —   |
| S2. Local LLM           | —   | ❌  | —      | —   |
| S3. Web Dashboard       | —   | ❌  | —      | —   |

Legend: ✅ done · ⚠️ partial · ❌ not started · — scaffold only
