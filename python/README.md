# AlgoForge — Python

Python 3.11+ implementation of the AlgoForge blueprint.

**Status:** Phase 0 scaffold. `unittest` discovery picks up smoke tests.

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
├── algoforge/        — package source
│   ├── __init__.py
│   └── types.py
└── tests/
    └── test_smoke.py
```

## Roadmap

Live MT5 path uses the official `MetaTrader5` package (Windows + MT5 terminal
required). Backtester, indicators, pattern engine all native Python. FastAPI
dashboard planned for Phase S3.
