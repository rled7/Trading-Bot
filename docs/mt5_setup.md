# MetaTrader 5 Live Connectivity Setup (Round 12)

This document covers everything needed to run AlgoForge's Python implementation
against a live (or demo) MetaTrader 5 terminal.

---

## 1. Why MT5 is Python-only in this repo

The four language implementations share the same `IBroker` contract, but
live MT5 connectivity is currently wired only in Python:

| Language | MT5 support | Notes |
|----------|-------------|-------|
| **Python** | Full (`MT5Broker`) | Uses the official `MetaTrader5` PyPI package (Windows-only) |
| C | Stub | `IBroker` vtable present; DLL bridge not implemented yet |
| C++ | Stub | `IBroker` interface present; DLL bridge not implemented yet |
| JS | Stub | `IBroker` interface present; `ffi` bridge not implemented yet |

The `MetaTrader5` Python package communicates with the MT5 terminal process
directly through a Windows-native IPC mechanism, which is why it is
**Windows-only**. On Linux or macOS the package is not available; calling
`MT5Broker.connect()` will raise a `RuntimeError` (see Troubleshooting below).
Use `PaperBroker` for simulation on any platform.

---

## 2. Prerequisites

- **Windows 10 or 11** (64-bit)
- **MetaTrader 5 terminal** installed and running (download from your broker
  or from [metatrader5.com](https://www.metatrader5.com))
- A broker account (demo accounts work fine for testing)
- Python 3.11+ with pip
- AlgoForge Python dependencies installed:
  ```
  cd python && pip install -e .
  ```

---

## 3. Install the MetaTrader5 package

```bash
pip install MetaTrader5
```

> **Note for Linux / macOS users:** the package will install without error but
> the native extension will fail to load at runtime.  `MT5Broker.connect()`
> will raise `RuntimeError: MetaTrader5 not installed (Windows only)`.
> Use `PaperBroker` instead for all non-Windows development and testing.

Verify the installation:

```python
import MetaTrader5 as mt5
print(mt5.__version__)
```

---

## 4. Environment variables

`MT5Broker` reads credentials from environment variables when constructor
arguments are not supplied.  Set these before launching your script or in a
`.env` file:

| Variable | Purpose | Example |
|---|---|---|
| `MT5_PATH` | Absolute path to `terminal64.exe` | `C:\Program Files\MetaTrader 5\terminal64.exe` |
| `MT5_SERVER` | Broker server name shown in the MT5 login dialog | `ICMarkets-Demo` |
| `MT5_LOGIN` | Integer account number | `12345678` |
| `MT5_PASSWORD` | Account password | `my_secret` |

All four variables are optional.  When omitted, `MT5Broker` passes no
corresponding argument to `mt5.initialize()` and MT5 uses its own defaults
(last-used path, server, and stored credentials).

**Windows PowerShell example:**

```powershell
$env:MT5_PATH     = "C:\Program Files\MetaTrader 5\terminal64.exe"
$env:MT5_SERVER   = "ICMarkets-Demo"
$env:MT5_LOGIN    = "12345678"
$env:MT5_PASSWORD = "my_secret"
python run_bot.py
```

**Windows cmd example:**

```cmd
set MT5_PATH=C:\Program Files\MetaTrader 5\terminal64.exe
set MT5_SERVER=ICMarkets-Demo
set MT5_LOGIN=12345678
set MT5_PASSWORD=my_secret
python run_bot.py
```

---

## 5. Minimal usage snippet

```python
import os
from algoforge.mt5_broker import MT5Broker
from algoforge.types import Timeframe
from algoforge.broker import OrderType

# Credentials from env vars (or pass them explicitly as kwargs)
broker = MT5Broker(
    path=os.getenv("MT5_PATH"),
    server=os.getenv("MT5_SERVER"),
    login=int(os.getenv("MT5_LOGIN", "0")) or None,
    password=os.getenv("MT5_PASSWORD"),
)

# Connect to the terminal
broker.connect()          # raises RuntimeError on failure
print("Connected:", broker.is_connected())

# Account snapshot
account = broker.get_account()
print(f"Balance: {account.balance} {account.currency}")
print(f"Equity:  {account.equity}")

# Latest tick
tick = broker.get_tick("EURUSD")
print(f"EURUSD bid={tick.bid:.5f}  ask={tick.ask:.5f}")

# Historical bars (last 10 H1 candles)
bars = broker.get_bars("EURUSD", Timeframe.H1, 10)
for b in bars:
    print(f"  {b.timestamp}  O={b.open:.5f}  H={b.high:.5f}"
          f"  L={b.low:.5f}  C={b.close:.5f}  V={b.volume:.0f}")

# Market buy order (0.01 lots, no SL/TP)
order = broker.place_order(
    symbol="EURUSD",
    order_type=OrderType.BUY,
    lots=0.01,
    sl=0.0,
    tp=0.0,
    magic=1001,
    comment="algoforge-test",
)
print(f"Order ticket: {order.ticket}  fill_price: {order.fill_price}")

# Disconnect when done
broker.disconnect()
```

---

## 6. Troubleshooting

### `RuntimeError: MetaTrader5 not installed (Windows only)`

You are running on Linux or macOS.  The `MetaTrader5` package requires
Windows.  Use `PaperBroker` for simulation:

```python
from algoforge.paper_broker import PaperBroker
broker = PaperBroker()
broker.connect()
```

### `RuntimeError: mt5.initialize() failed`

Common causes:

| Cause | Fix |
|---|---|
| MT5 terminal is not running | Launch `terminal64.exe` before calling `connect()` |
| Wrong path in `MT5_PATH` | Verify the path points to the actual `terminal64.exe` |
| Wrong server / login | Check credentials in the MT5 login dialog; update env vars |
| AutoTrading disabled | In MT5 toolbar: `Tools → Options → Expert Advisors → Allow automated trading` |
| Account not authorised for API | Some brokers restrict API access; enable it in account settings or contact your broker |
| Terminal is already connected to a different account | Log out first, or pass the correct credentials explicitly |

### Orders rejected (`TRADE_RETCODE_*` errors)

MT5 returns a numeric retcode in the order result.  Common codes:

| Code | Name | Meaning |
|------|------|---------|
| 10004 | `TRADE_RETCODE_REQUOTE` | Price re-quoted — retry |
| 10006 | `TRADE_RETCODE_REJECT` | Request rejected by server |
| 10016 | `TRADE_RETCODE_INVALID_STOPS` | SL/TP too close to price |
| 10018 | `TRADE_RETCODE_MARKET_CLOSED` | Market is closed |
| 10027 | `TRADE_RETCODE_TRADE_DISABLED` | AutoTrading is disabled |

---

## 7. See also

- Round 12 row in the main [README](../README.md#round-tracker) — tracks
  live MT5 connectivity across all four language implementations.
- `python/algoforge/mt5_broker.py` — the full `MT5Broker` implementation.
- `python/tests/test_mt5_broker.py` — mocked unit tests (runs on any OS).
