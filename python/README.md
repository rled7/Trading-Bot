# AlgoForge — Python

Python 3.11+ implementation of the AlgoForge blueprint.

**Status:** Rounds 1–6 complete. Indicators, pattern engine (candlestick +
chart + harmonic), broker ABC, and paper broker all implemented and matching
the C++ reference.

## Install

```bash
cd python
pip install -e .             # core
pip install -e '.[live]'     # + MetaTrader5 for live trading
pip install -e '.[dev]'      # + pytest
```

## Test

```bash
python -m unittest discover tests   # zero deps
# or
pytest                              # if dev extras installed
```

## Benchmark

```bash
PYTHONPATH=. python3 -m algoforge.bench [bars] [iterations]
# defaults: 100000 bars, 10 iterations
```

## Layout

```
python/
├── pyproject.toml
├── algoforge/
│   ├── __init__.py
│   ├── types.py          — Bar, Direction, Timeframe dataclasses
│   ├── indicators.py     — 38 indicators
│   ├── patterns.py       — 20 patterns + scan_patterns()
│   ├── broker.py         — IBroker ABC + Tick/Order/Position/AccountInfo dataclasses
│   ├── paper_broker.py   — PaperBroker: seeded random-walk, 10 FX pairs, $7/lot
│   └── bench.py          — cross-language TSV benchmark harness
└── tests/
    └── test_smoke.py
```

## Completed modules

| Round | Module | Files |
|---|---|---|
| R1–R4 | Indicators (38) | `indicators.py` |
| R5 | Pattern engine (7 candle + 8 chart + 5 harmonic) | `patterns.py` |
| R6 | IBroker ABC + PaperBroker | `broker.py`, `paper_broker.py` |

## Broker quick-start

```python
from algoforge.paper_broker import make_paper_broker

broker = make_paper_broker(balance=10_000)
broker.connect()

tick = broker.get_tick("EURUSD")
order = broker.place_order("EURUSD", OrderType.BUY, lots=0.10, sl=1.08, tp=1.10)

broker.poll_sl_tp()   # simulates SL/TP hits
```

## Roadmap

| Round | Feature | Status |
|---|---|---|
| R7 | Backtest engine (bar-by-bar, ATR SL/TP, Sharpe/DD) | Next |
| R8 | Algorithm registry (trend, reversion, breakout, scalper, swing) | Planned |
| R9 | Multi-TF confluence + weighted vote | Planned |
| R10 | Risk & execution (sizer, SL/TP, drawdown guard) | Planned |
| R11 | 24/7 ops (Docker, structured logs, Telegram) | Planned |
| R12 | Live MT5 via `MetaTrader5` package (Windows + terminal required) | Planned |
