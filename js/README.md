# AlgoForge — JavaScript

Node.js 18+ implementation of the AlgoForge blueprint. ES modules, zero
runtime dependencies for the scaffold.

**Status:** Phase 0 scaffold. `node --test` discovers and runs smoke tests.

## Run

```bash
cd js
npm start        # runs src/index.js
npm test         # runs the smoke tests
```

## Layout

```
js/
├── package.json
├── src/
│   ├── index.js
│   └── types.js
└── test/
    └── smoke.test.js
```

## Roadmap

Live MT5 path will use `ffi-napi` (or N-API native addon) against the
MetaTrader 5 terminal's DLL. Backtester and indicators in plain JS;
FastAPI/Express dashboard planned for Phase S3.
