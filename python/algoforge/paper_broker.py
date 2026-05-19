"""PaperBroker — paper trading implementation of IBroker.

Simulates price via a seeded random walk (one per symbol).
Commission: $7/lot charged on open. P&L = (exit − open) × lots × 100 000.
"""
from __future__ import annotations

import math
import random
from typing import Optional

from .types import Bar, Direction
from .broker import (IBroker, OrderType, Tick, AccountInfo,
                     SymbolInfo, Order, Position)

_PAIRS: list[tuple[str, float, float, int]] = [
    ("EURUSD", 1.08500, 0.0080,  1),
    ("GBPUSD", 1.26500, 0.0100,  2),
    ("USDJPY", 149.500, 0.0070,  3),
    ("XAUUSD", 2050.00, 0.0120,  4),
    ("EURJPY", 162.000, 0.0090,  5),
    ("GBPJPY", 188.000, 0.0110,  6),
    ("AUDUSD", 0.65500, 0.0090,  7),
    ("USDCAD", 1.36000, 0.0075,  8),
    ("USDCHF", 0.90500, 0.0070,  9),
    ("EURGBP", 0.85800, 0.0060, 10),
]

_LONG_TYPES  = {OrderType.BUY, OrderType.BUY_LIMIT, OrderType.BUY_STOP}
_MARKET_BUY  = {OrderType.BUY, OrderType.BUY_STOP}
_MARKET_SELL = {OrderType.SELL, OrderType.SELL_STOP}


def _default_spread(sym: str) -> float:
    if "XAU" in sym or "XAG" in sym:
        return 0.25
    if "JPY" in sym:
        return 0.030
    return 0.00012


class _PriceSim:
    def __init__(self, start: float, vol: float, seed: int) -> None:
        self.mid = start
        self._vol = vol
        self._rng = random.Random(seed)

    def next_mid(self) -> float:
        step = self._vol / math.sqrt(24.0 * 60.0)
        self.mid = max(self.mid * (1.0 + self._rng.gauss(0.0, step)), 0.0001)
        return self.mid


class PaperBroker(IBroker):
    def __init__(self, balance: float = 10_000.0) -> None:
        self._balance     = balance
        self._connected   = False
        self._next_ticket = 1_000_001
        self._sims: dict[str, _PriceSim] = {
            sym: _PriceSim(start, vol, seed)
            for sym, start, vol, seed in _PAIRS
        }
        self._positions: dict[int, Position] = {}
        self._rng_global = random.Random(1337)

    # ── Connection ──────────────────────────────────────────────────────────

    def connect(self) -> int:
        self._connected = True
        print(f"[PAPER] Connected | balance={self._balance:.2f}")
        return 0

    def disconnect(self) -> None:
        self._connected = False

    def is_connected(self) -> bool:
        return self._connected

    def broker_name(self) -> str:
        return "PaperSimulator"

    # ── Account ─────────────────────────────────────────────────────────────

    def get_account(self) -> AccountInfo:
        floating = sum(p.profit for p in self._positions.values())
        return AccountInfo(
            balance=self._balance,
            equity=self._balance + floating,
            free_margin=self._balance,
            leverage=100,
            currency="USD",
            login=99999999,
        )

    # ── Market data ──────────────────────────────────────────────────────────

    def get_tick(self, symbol: str) -> Tick:
        sim = self._sims.get(symbol)
        if sim is None:
            raise ValueError(f"Unknown symbol: {symbol}")
        mid = sim.next_mid()
        sp  = _default_spread(symbol)
        return Tick(
            symbol=symbol,
            bid=mid - sp / 2.0,
            ask=mid + sp / 2.0,
            volume=500.0 + self._rng_global.uniform(0, 2000),
        )

    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        if "XAU" in symbol or "XAG" in symbol:
            digits = 2
        elif "JPY" in symbol:
            digits = 3
        else:
            digits = 5
        return SymbolInfo(
            name=symbol, digits=digits,
            point=10.0 ** -digits, contract_size=100_000.0,
            volume_min=0.01, volume_max=100.0, volume_step=0.01,
        )

    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]:
        sim = self._sims.get(symbol)
        if sim is None:
            raise ValueError(f"Unknown symbol: {symbol}")
        rng = random.Random(42 ^ tf)
        vol_per_bar = 0.0080 / math.sqrt(86400.0 / tf) / math.sqrt(252.0)
        price = sim.mid
        bars: list[Bar] = []
        for _ in range(count):
            o = price
            h = l = o
            for _ in range(4):
                p = o * (1.0 + rng.gauss(0.0, vol_per_bar))
                h = max(h, p)
                l = min(l, p)
            c = o * (1.0 + rng.gauss(0.0, vol_per_bar))
            h = max(h, c)
            l = min(l, c)
            bars.append(Bar(timestamp=0, open=o, high=h, low=l, close=c,
                            volume=500.0 + rng.uniform(0, 4500),
                            spread=_default_spread(symbol)))
            price = c
        return bars

    # ── Trading ──────────────────────────────────────────────────────────────

    def place_order(self, symbol: str, order_type: OrderType,
                    lots: float, price: float = 0.0,
                    sl: float = 0.0, tp: float = 0.0,
                    magic: int = 0, comment: str = "") -> Order:
        if lots < 0.01:
            raise ValueError("lots must be >= 0.01")
        sim = self._sims.get(symbol)
        if sim is None:
            raise ValueError(f"Unknown symbol: {symbol}")

        mid = sim.next_mid()
        sp  = _default_spread(symbol)
        bid, ask = mid - sp / 2.0, mid + sp / 2.0

        if order_type in _MARKET_BUY:
            fill_p = ask
        elif order_type in _MARKET_SELL:
            fill_p = bid
        elif order_type == OrderType.BUY_LIMIT:
            fill_p = min(price, ask)
        else:
            fill_p = max(price, bid)

        is_long    = order_type in _LONG_TYPES
        commission = 7.0 * lots
        ticket     = self._next_ticket
        self._next_ticket += 1

        pos = Position(
            ticket=ticket, symbol=symbol,
            side=Direction.LONG if is_long else Direction.SHORT,
            lots=lots, open_price=fill_p, current_price=fill_p,
            sl=sl, tp=tp, profit=0.0, commission=-commission,
            magic=magic, comment=comment,
        )
        self._positions[ticket] = pos
        self._balance -= commission

        print(f"[PAPER] {'BUY' if is_long else 'SELL'} {lots:.2f} {symbol} @ {fill_p:.5f}"
              f" | SL={sl:.5f} TP={tp:.5f} | comm=-{commission:.2f}")
        return Order(ticket=ticket, symbol=symbol, type=order_type,
                     lots=lots, fill_price=fill_p, sl=sl, tp=tp, magic=magic)

    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        pos = self._positions.get(ticket)
        if pos is None:
            return -6  # AF_ERR_POSITION_NOT_FOUND
        sim   = self._sims.get(pos.symbol)
        mid   = sim.next_mid() if sim else pos.open_price
        sp    = _default_spread(pos.symbol)
        exit_p = mid - sp / 2.0 if pos.side == Direction.LONG else mid + sp / 2.0
        close_lots = lots if (0 < lots < pos.lots) else pos.lots
        raw_pnl = ((exit_p - pos.open_price) if pos.side == Direction.LONG
                   else (pos.open_price - exit_p)) * close_lots * 100_000.0
        self._balance += raw_pnl
        print(f"[PAPER] CLOSE {pos.symbol} "
              f"{'LONG' if pos.side == Direction.LONG else 'SHORT'} "
              f"{close_lots:.2f} @ {exit_p:.5f} | P&L={raw_pnl:.2f}")
        if close_lots >= pos.lots:
            del self._positions[ticket]
        else:
            pos.lots -= close_lots
        return 0

    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        pos = self._positions.get(ticket)
        if pos is None:
            return -6
        pos.sl = sl
        pos.tp = tp
        return 0

    def cancel_order(self, ticket: int) -> int:
        return 0

    # ── Queries ──────────────────────────────────────────────────────────────

    def get_positions(self) -> list[Position]:
        return list(self._positions.values())

    def get_orders(self) -> list[Order]:
        return []

    def get_position(self, ticket: int) -> Optional[Position]:
        return self._positions.get(ticket)

    # ── Simulation helper ────────────────────────────────────────────────────

    def poll_sl_tp(self) -> None:
        to_close: list[int] = []
        for ticket, pos in self._positions.items():
            sim = self._sims.get(pos.symbol)
            if not sim:
                continue
            mid = sim.next_mid()
            sp  = _default_spread(pos.symbol)
            bid, ask = mid - sp / 2.0, mid + sp / 2.0
            pos.current_price = bid if pos.side == Direction.LONG else ask
            hit = False
            if pos.sl > 0:
                hit = (pos.side == Direction.LONG  and bid <= pos.sl) or \
                      (pos.side == Direction.SHORT and ask >= pos.sl)
            if not hit and pos.tp > 0:
                hit = (pos.side == Direction.LONG  and bid >= pos.tp) or \
                      (pos.side == Direction.SHORT and ask <= pos.tp)
            if hit:
                to_close.append(ticket)
        for t in to_close:
            self.close_position(t)


def make_paper_broker(balance: float = 10_000.0) -> PaperBroker:
    return PaperBroker(balance)
