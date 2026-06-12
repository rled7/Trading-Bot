# AlgoForge — JavaScript

Node.js 18+ implementation of the AlgoForge blueprint. CommonJS modules,
zero runtime dependencies for core logic.

> 📋 Project history: see the **[top-level CHANGELOG](../CHANGELOG.md)**.

**Status:** Rounds 1–6 complete. Indicators, pattern engine (candlestick +
chart + harmonic), broker interface, and paper broker all implemented and
matching the C++ reference.

## Run

```bash
cd js
npm start                            # runs src/index.js
npm test                             # runs all tests
npm run bench -- 100000 10           # or: node src/bench.js 100000 10
```

## Layout

```
js/
├── package.json
├── src/
│   ├── types.js          — Bar, Direction, Timeframe constants
│   ├── indicators.js     — 38 indicators
│   ├── patterns.js       — 20 patterns + scanPatterns()
│   ├── broker.js         — IBroker base class + OrderType/Direction enums +
│   │                       Tick/Order/Position/AccountInfo/SymbolInfo classes
│   ├── paper_broker.js   — PaperBroker: seeded LCG price sim, 10 FX pairs
│   ├── bench.js          — cross-language TSV benchmark harness
│   └── index.js          — bot entry point (scaffold)
└── test/
    └── smoke.test.js
```

## Completed modules

| Round | Module | Files |
|---|---|---|
| R1–R4 | Indicators (38) | `indicators.js` |
| R5 | Pattern engine (7 candle + 8 chart + 5 harmonic) | `patterns.js` |
| R6 | IBroker interface + PaperBroker | `broker.js`, `paper_broker.js` |

## Broker quick-start

```js
const { makePaperBroker } = require('./src/paper_broker');
const { OrderType } = require('./src/broker');

const broker = makePaperBroker(10_000);
broker.connect();

const tick  = broker.getTick('EURUSD');
const order = broker.placeOrder('EURUSD', OrderType.BUY, 0.10,
                                0, 1.08, 1.10, 0, '');

broker.pollSlTp();   // simulates SL/TP hits
```

## Roadmap

| Round | Feature | Status |
|---|---|---|
| R7 | Backtest engine (bar-by-bar, ATR SL/TP, Sharpe/DD) | Next |
| R8 | Algorithm registry (trend, reversion, breakout, scalper, swing) | Planned |
| R9 | Multi-TF confluence + weighted vote | Planned |
| R10 | Risk & execution (sizer, SL/TP, drawdown guard) | Planned |
| R11 | 24/7 ops (Docker, structured logs, Telegram) | Planned |
| R12 | Live MT5 via `ffi-napi` DLL bridge (Windows + terminal required) | Planned |
