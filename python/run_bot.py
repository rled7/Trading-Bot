#!/usr/bin/env python3
"""AlgoForge — run_bot.py
Live (or paper) trading runner.

This is the broker-agnostic orchestration loop that ties an algorithm to a
broker in real time.  It mirrors the bar-by-bar logic of
``algoforge.backtest.BacktestEngine`` (ATR-based SL/TP, fixed-fractional
position sizing) but instead of iterating a fixed array it polls the broker
for fresh bars on an interval and places real orders.

Because both ``PaperBroker`` and ``MT5Broker`` implement the same ``IBroker``
contract, the exact same loop runs against simulated data on any OS *or*
against a live MetaTrader 5 terminal on Windows — you only change ``--broker``.

SAFETY
------
  * Default broker is ``paper`` (simulated account, no real money).
  * ``--broker mt5`` WITHOUT ``--live-confirm`` runs READ-ONLY: it connects,
    proves connectivity, evaluates signals and PRINTS the orders it *would*
    place, but sends nothing.  This is the staged smoke test.
  * Live MT5 order placement requires the explicit ``--live-confirm`` flag.

Usage
-----
  # Paper trading on any OS (Mac/Linux/Windows) — safe, simulated:
  python run_bot.py --broker paper --symbol EURUSD --tf H1 --algo trend

  # Read-only smoke test against a live MT5 terminal (Windows):
  python run_bot.py --broker mt5 --symbol EURUSD --tf H1 --algo trend

  # ACTUAL live trading on MT5 (Windows, demo account strongly advised):
  python run_bot.py --broker mt5 --symbol EURUSD --tf H1 --algo trend --live-confirm
"""
from __future__ import annotations

import argparse
import sys
import time
from typing import Optional

from algoforge.algorithm import AlgoSignal, IAlgorithm
from algoforge.algorithms import (
    BreakoutTrader,
    MeanReversion,
    SwingTrader,
    TrendFollower,
)
from algoforge.broker import IBroker, OrderType
from algoforge.indicators import IndicatorEngine
from algoforge.paper_broker import PaperBroker
from algoforge.patterns import PatternEngine
from algoforge.types import Direction, Timeframe

# Sizing constants — identical to algoforge.backtest
_CONTRACT_SIZE: float = 100_000.0   # standard forex contract size (1 lot)

_ALGOS = {
    "trend":     TrendFollower,
    "reversion": MeanReversion,
    "breakout":  BreakoutTrader,
    "swing":     SwingTrader,
}

_TIMEFRAMES = {
    "M1":  Timeframe.M1,
    "M5":  Timeframe.M5,
    "M15": Timeframe.M15,
    "H1":  Timeframe.H1,
    "H4":  Timeframe.H4,
    "D1":  Timeframe.D1,
}


def build_broker(kind: str) -> IBroker:
    """Construct the requested broker.  MT5 is imported lazily so the paper
    path never depends on the (Windows-only) MetaTrader5 package."""
    if kind == "paper":
        return PaperBroker()
    if kind == "mt5":
        from algoforge.mt5_broker import MT5Broker  # lazy: avoids Windows-only import on Mac
        return MT5Broker()
    raise ValueError(f"unknown broker {kind!r} (use 'paper' or 'mt5')")


def build_algo(name: str) -> IAlgorithm:
    try:
        return _ALGOS[name]()
    except KeyError:
        raise SystemExit(f"unknown algo {name!r}. Use one of: {', '.join(_ALGOS)}")


def have_open_position(broker: IBroker, symbol: str, magic: int) -> bool:
    """True if THIS bot already holds a position (matched by magic + symbol)."""
    try:
        return any(
            p.symbol == symbol and p.magic == magic
            for p in broker.get_positions()
        )
    except Exception:
        return False


def evaluate_and_trade(
    broker: IBroker,
    algo: IAlgorithm,
    ind: IndicatorEngine,
    pat: PatternEngine,
    symbol: str,
    tf: Timeframe,
    *,
    capital: float,
    risk_pct: float,
    atr_sl_mult: float,
    rr_ratio: float,
    history: int,
    magic: int,
    live: bool,
) -> None:
    """One decision cycle — mirrors BacktestEngine.run's per-bar logic."""
    bars = broker.get_bars(symbol, tf, history)
    if len(bars) < 2:
        print("  · not enough bars yet, skipping")
        return

    # Let the broker enforce SL/TP (paper polls; MT5 is server-side no-op)
    broker.poll_sl_tp()

    # Already in a trade from this bot → manage only, don't stack entries
    if have_open_position(broker, symbol, magic):
        print("  · position open — holding (SL/TP managed by broker)")
        return

    # Evaluate on the latest CLOSED bar (last element of the series)
    last_idx = len(bars) - 1
    ind_result = ind.compute(bars, last_idx)
    decision = algo.safe_evaluate(symbol, tf, bars, last_idx, ind_result, pat)

    if not decision.is_actionable() or ind_result.atr_value is None:
        print(f"  · {algo.name()} → no actionable signal "
              f"(signal={decision.signal.name}, conf={decision.confidence:.2f})")
        return

    atr_val = ind_result.atr_value
    if atr_val <= 0.0:
        print("  · ATR unavailable (<=0), skipping")
        return

    direction = Direction.LONG if decision.signal == AlgoSignal.BUY else Direction.SHORT
    tick = broker.get_tick(symbol)

    if direction == Direction.LONG:
        entry = tick.ask
        sl = entry - atr_val * atr_sl_mult
        tp = entry + atr_val * atr_sl_mult * rr_ratio
        otype = OrderType.BUY
    else:
        entry = tick.bid
        sl = entry + atr_val * atr_sl_mult
        tp = entry - atr_val * atr_sl_mult * rr_ratio
        otype = OrderType.SELL

    stop_dist = abs(entry - sl)
    if stop_dist <= 1e-12:
        print("  · stop distance ~0, skipping")
        return

    # Fixed-fractional sizing — identical formula to the backtester
    lots_raw = capital * risk_pct / (stop_dist * _CONTRACT_SIZE)
    lots = max(0.01, min(10.0, round(lots_raw, 2)))

    summary = (f"{otype.name} {symbol} {lots} lots @ {entry:.5f} "
               f"SL={sl:.5f} TP={tp:.5f}  ({decision.reason or algo.name()})")

    if not live:
        print(f"  · [DRY-RUN] would place: {summary}")
        return

    try:
        order = broker.place_order(symbol, otype, lots, sl=sl, tp=tp,
                                   magic=magic, comment="algoforge-live")
        print(f"  ✓ PLACED ticket={order.ticket}: {summary}")
    except Exception as exc:
        print(f"  ✗ order rejected: {exc}")


def main() -> int:
    ap = argparse.ArgumentParser(description="AlgoForge live/paper trading runner")
    ap.add_argument("--broker", choices=["paper", "mt5"], default="paper")
    ap.add_argument("--symbol", default="EURUSD")
    ap.add_argument("--tf", choices=list(_TIMEFRAMES), default="H1")
    ap.add_argument("--algo", choices=list(_ALGOS), default="trend")
    ap.add_argument("--risk", type=float, default=0.01, help="fraction of capital risked per trade")
    ap.add_argument("--capital", type=float, default=0.0,
                    help="override starting capital (0 = read from broker account)")
    ap.add_argument("--interval", type=float, default=60.0, help="seconds between polls")
    ap.add_argument("--history", type=int, default=300, help="bars to fetch each poll")
    ap.add_argument("--magic", type=int, default=20240601, help="magic number to tag this bot's trades")
    ap.add_argument("--atr-sl-mult", type=float, default=1.5)
    ap.add_argument("--rr-ratio", type=float, default=2.0)
    ap.add_argument("--max-cycles", type=int, default=0, help="stop after N cycles (0 = run forever)")
    ap.add_argument("--once", action="store_true", help="run a single read-only smoke test and exit")
    ap.add_argument("--live-confirm", action="store_true",
                    help="REQUIRED to place real MT5 orders; without it MT5 is read-only")
    args = ap.parse_args()

    tf = _TIMEFRAMES[args.tf]

    # Decide trading mode + safety gate
    if args.broker == "mt5":
        live = bool(args.live_confirm)
    else:
        live = True  # paper orders are simulated → safe to "place"

    print("=" * 64)
    print("AlgoForge Runner")
    print(f"  Broker  : {args.broker}")
    print(f"  Symbol  : {args.symbol}   TF: {args.tf}   Algo: {args.algo}")
    print(f"  Risk    : {args.risk:.2%} per trade   ATRx{args.atr_sl_mult}  RR 1:{args.rr_ratio}")
    if args.broker == "mt5" and not live:
        print("  MODE    : READ-ONLY smoke test (no orders sent). Add --live-confirm to trade.")
    elif args.broker == "mt5" and live:
        print("  MODE    : *** LIVE MT5 — REAL ORDERS WILL BE SENT ***  (use a DEMO account!)")
    else:
        print("  MODE    : PAPER (simulated account, no real money)")
    print("=" * 64)

    broker = build_broker(args.broker)
    algo = build_algo(args.algo)
    ind = IndicatorEngine()
    pat = PatternEngine()

    # ── Stage 1: connect ──────────────────────────────────────────────────
    if broker.connect() != 0:
        print(f"[FATAL] could not connect to {args.broker}. "
              f"(On Mac/Linux, MT5 is unavailable — use --broker paper.)")
        return 1
    print(f"[ok] connected to {broker.broker_name()}")

    # ── Stage 2: read-only connectivity proof (account / tick / bars) ─────
    try:
        acct = broker.get_account()
        print(f"[ok] account: balance={acct.balance:.2f} {acct.currency}  equity={acct.equity:.2f}")
        capital = args.capital if args.capital > 0 else acct.balance
        tick = broker.get_tick(args.symbol)
        print(f"[ok] tick {args.symbol}: bid={tick.bid:.5f} ask={tick.ask:.5f}")
        bars = broker.get_bars(args.symbol, tf, args.history)
        print(f"[ok] fetched {len(bars)} {args.tf} bars (last close={bars[-1].close:.5f})")
    except Exception as exc:
        print(f"[FATAL] read-only smoke test failed: {exc}")
        broker.disconnect()
        return 1

    if args.once:
        print("\n[--once] smoke test complete — connectivity verified, exiting.")
        broker.disconnect()
        return 0

    # ── Stage 3: trading loop ─────────────────────────────────────────────
    print(f"\nStarting loop (interval={args.interval}s). Ctrl-C to stop.\n")
    cycle = 0
    last_bar_ts: Optional[int] = None
    try:
        while True:
            cycle += 1
            bars = broker.get_bars(args.symbol, tf, args.history)
            newest_ts = bars[-1].timestamp if bars else None
            stamp = time.strftime("%H:%M:%S")

            if newest_ts is not None and newest_ts == last_bar_ts:
                print(f"[{stamp}] cycle {cycle}: no new bar, waiting")
            else:
                last_bar_ts = newest_ts
                print(f"[{stamp}] cycle {cycle}: new bar @ {newest_ts}")
                evaluate_and_trade(
                    broker, algo, ind, pat, args.symbol, tf,
                    capital=capital, risk_pct=args.risk,
                    atr_sl_mult=args.atr_sl_mult, rr_ratio=args.rr_ratio,
                    history=args.history, magic=args.magic, live=live,
                )

            if args.max_cycles and cycle >= args.max_cycles:
                print(f"\nReached --max-cycles={args.max_cycles}, stopping.")
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n[interrupt] shutting down…")
    finally:
        broker.disconnect()
        print("[ok] disconnected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
