# AlgoForge — C

Pure C17 implementation of the AlgoForge blueprint.

**Status:** Rounds 1–6 complete. Indicators, pattern engine (candlestick +
chart + harmonic), broker abstraction, and paper broker all implemented and
matching the C++ reference.

## Build

```bash
make            # builds build/algoforge
make test       # builds build/af_tests and runs them
make bench      # builds build/af_bench
./build/af_bench [bars] [iterations]   # defaults: 100000 10
make clean
```

## Layout

```
c/
├── Makefile
├── include/
│   ├── af_types.h       — core types: bars, ticks, orders, positions, accounts
│   ├── af_indicators.h  — 38 indicator function declarations
│   ├── af_patterns.h    — candlestick, chart, harmonic pattern declarations
│   └── af_broker.h      — af_broker_t vtable interface + make_paper_broker()
├── src/
│   ├── indicators.c     — 38 indicators (SMA, EMA, RSI, ATR, Bollinger, …)
│   ├── patterns.c       — 20 patterns + af_scan_patterns()
│   ├── paper_broker.c   — PaperBroker: LCG price sim, 10 FX pairs, $7/lot comm
│   ├── bench.c          — cross-language TSV benchmark harness
│   └── main.c           — bot entry point (scaffold)
├── tests/               — test_smoke.c zero-dep test runner
└── build/               — generated (gitignored)
```

## Completed modules

| Round | Module | Files |
|---|---|---|
| R1–R4 | Indicators (38) | `af_indicators.h`, `indicators.c` |
| R5 | Pattern engine (7 candle + 8 chart + 5 harmonic) | `af_patterns.h`, `patterns.c` |
| R6 | Broker vtable + paper broker | `af_broker.h`, `paper_broker.c` |

## Broker quick-start

```c
#include "af_broker.h"

af_broker_t *broker = af_make_paper_broker(10000.0);
broker->connect(broker);

af_tick_t tick;
broker->get_tick(broker, "EURUSD", &tick);

af_order_t order;
broker->place_order(broker, "EURUSD", AF_ORDER_BUY,
                    0.10, 0.0, 0.0, 0.0, 0, NULL, &order);

broker->poll_sl_tp(broker);
broker->destroy(broker);   /* frees impl + broker struct */
```

## Roadmap

| Round | Feature | Status |
|---|---|---|
| R7 | Backtest engine (bar-by-bar, ATR SL/TP, Sharpe/DD) | Next |
| R8 | Algorithm registry (trend, reversion, breakout, scalper, swing) | Planned |
| R9 | Multi-TF confluence + weighted vote | Planned |
| R10 | Risk & execution (sizer, SL/TP, drawdown guard) | Planned |
| R11 | 24/7 ops (Docker, structured logs, Telegram) | Planned |
| R12 | Live MT5 via DLL bridge (`MT5APIClient64.dll`) | Planned |
