"""IBroker — abstract broker interface (mirrors C++ IBroker).

Data classes mirror the C AF_* structs. Direction is imported from
types to avoid a duplicate definition.
"""
from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional

from .types import Bar, Direction


class OrderType(IntEnum):
    BUY        = 0
    SELL       = 1
    BUY_LIMIT  = 2
    SELL_LIMIT = 3
    BUY_STOP   = 4
    SELL_STOP  = 5


@dataclass
class Tick:
    symbol:    str   = ""
    bid:       float = 0.0
    ask:       float = 0.0
    volume:    float = 0.0
    timestamp: int   = 0


@dataclass
class AccountInfo:
    balance:     float = 0.0
    equity:      float = 0.0
    margin:      float = 0.0
    free_margin: float = 0.0
    profit:      float = 0.0
    leverage:    int   = 100
    currency:    str   = "USD"
    login:       int   = 0


@dataclass
class SymbolInfo:
    name:          str   = ""
    digits:        int   = 5
    point:         float = 0.00001
    contract_size: float = 100_000.0
    volume_min:    float = 0.01
    volume_max:    float = 100.0
    volume_step:   float = 0.01


@dataclass
class Order:
    ticket:     int       = 0
    symbol:     str       = ""
    type:       OrderType = OrderType.BUY
    lots:       float     = 0.0
    price:      float     = 0.0
    sl:         float     = 0.0
    tp:         float     = 0.0
    fill_price: float     = 0.0
    magic:      int       = 0
    comment:    str       = ""


@dataclass
class Position:
    ticket:        int       = 0
    symbol:        str       = ""
    side:          Direction = Direction.NONE
    lots:          float     = 0.0
    open_price:    float     = 0.0
    current_price: float     = 0.0
    sl:            float     = 0.0
    tp:            float     = 0.0
    profit:        float     = 0.0
    commission:    float     = 0.0
    swap:          float     = 0.0
    magic:         int       = 0
    comment:       str       = ""


class IBroker(ABC):
    """Abstract broker interface. Subclass and implement all abstract methods."""

    @abstractmethod
    def connect(self) -> int: ...

    @abstractmethod
    def disconnect(self) -> None: ...

    @abstractmethod
    def is_connected(self) -> bool: ...

    @abstractmethod
    def broker_name(self) -> str: ...

    @abstractmethod
    def get_account(self) -> AccountInfo: ...

    @abstractmethod
    def get_tick(self, symbol: str) -> Tick: ...

    @abstractmethod
    def get_symbol_info(self, symbol: str) -> SymbolInfo: ...

    @abstractmethod
    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]: ...

    @abstractmethod
    def place_order(self, symbol: str, order_type: OrderType,
                    lots: float, price: float = 0.0,
                    sl: float = 0.0, tp: float = 0.0,
                    magic: int = 0, comment: str = "") -> Order: ...

    @abstractmethod
    def close_position(self, ticket: int, lots: float = 0.0) -> int: ...

    @abstractmethod
    def modify_position(self, ticket: int, sl: float, tp: float) -> int: ...

    @abstractmethod
    def cancel_order(self, ticket: int) -> int: ...

    @abstractmethod
    def get_positions(self) -> list[Position]: ...

    @abstractmethod
    def get_orders(self) -> list[Order]: ...

    @abstractmethod
    def get_position(self, ticket: int) -> Optional[Position]: ...

    def poll_sl_tp(self) -> None:
        """Poll SL/TP against latest simulated prices (paper mode only)."""
