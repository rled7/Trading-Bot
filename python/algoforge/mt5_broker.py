"""MT5Broker — IBroker backed by the MetaTrader5 Python package.

Requirements: Windows, MetaTrader5 terminal running.
Install: pip install MetaTrader5

All credentials can come from environment variables:
  MT5_PATH, MT5_SERVER, MT5_LOGIN, MT5_PASSWORD
"""
from __future__ import annotations

import os
from typing import List, Optional

from .broker import IBroker, Tick, AccountInfo, SymbolInfo, Order, Position, OrderType
from .types import Bar, Direction, Timeframe

try:
    import MetaTrader5 as mt5
    _MT5_AVAILABLE = True
except ImportError:
    mt5 = None  # type: ignore[assignment]
    _MT5_AVAILABLE = False


def _build_tf_map() -> dict:
    """Build Timeframe → MT5 timeframe constant mapping (requires mt5 imported)."""
    if not _MT5_AVAILABLE:
        return {}
    return {
        Timeframe.M1:  mt5.TIMEFRAME_M1,   # type: ignore[union-attr]
        Timeframe.M5:  mt5.TIMEFRAME_M5,   # type: ignore[union-attr]
        Timeframe.M15: mt5.TIMEFRAME_M15,  # type: ignore[union-attr]
        Timeframe.H1:  mt5.TIMEFRAME_H1,   # type: ignore[union-attr]
        Timeframe.H4:  mt5.TIMEFRAME_H4,   # type: ignore[union-attr]
        Timeframe.D1:  mt5.TIMEFRAME_D1,   # type: ignore[union-attr]
    }


# Populated lazily on first MT5Broker instantiation
_TF_MAP: dict = {}


class MT5Broker(IBroker):
    """IBroker implementation backed by the MetaTrader5 Python package.

    Parameters
    ----------
    path:
        Path to the MT5 terminal executable.  Falls back to the
        ``MT5_PATH`` environment variable when *None*.
    server:
        Broker server name (e.g. ``'ICMarkets-Demo'``).  Falls back to
        ``MT5_SERVER``.
    login:
        Account login number.  Falls back to ``MT5_LOGIN``.
    password:
        Account password.  Falls back to ``MT5_PASSWORD``.
    """

    def __init__(
        self,
        path:     Optional[str] = None,
        server:   Optional[str] = None,
        login:    Optional[int] = None,
        password: Optional[str] = None,
    ) -> None:
        global _TF_MAP
        if not _TF_MAP:
            _TF_MAP = _build_tf_map()

        self._path     = path     or os.getenv('MT5_PATH')
        self._server   = server   or os.getenv('MT5_SERVER')
        self._login    = login    or (int(os.getenv('MT5_LOGIN', '0')) or None)
        self._password = password or os.getenv('MT5_PASSWORD')
        self._connected: bool = False

    # ------------------------------------------------------------------ #
    # IBroker — lifecycle                                                  #
    # ------------------------------------------------------------------ #

    def connect(self) -> int:
        """Initialise the MT5 terminal connection.

        Returns 0 on success, -1 on failure (including the case where the
        MetaTrader5 package is not installed — e.g. Linux/Mac).
        """
        if not _MT5_AVAILABLE:
            print(
                "[MT5] MetaTrader5 package not installed (Windows only). "
                "Use PaperBroker for simulation.",
            )
            return -1

        # Build kwargs — only pass non-None values so MT5 uses its defaults
        kwargs: dict = {}
        if self._path:
            kwargs['path'] = self._path
        if self._server:
            kwargs['server'] = self._server
        if self._login is not None:
            kwargs['login'] = self._login
        if self._password:
            kwargs['password'] = self._password

        ok = mt5.initialize(**kwargs)  # type: ignore[union-attr]
        if not ok:
            err = mt5.last_error()  # type: ignore[union-attr]
            print(f"[MT5] mt5.initialize() failed: {err}")
            return -1

        self._connected = True
        return 0

    def disconnect(self) -> None:
        """Shut down the MT5 connection."""
        if _MT5_AVAILABLE and self._connected:
            mt5.shutdown()  # type: ignore[union-attr]
        self._connected = False

    def is_connected(self) -> bool:
        return _MT5_AVAILABLE and self._connected

    def broker_name(self) -> str:
        return "MetaTrader5"

    # ------------------------------------------------------------------ #
    # IBroker — account / market data                                     #
    # ------------------------------------------------------------------ #

    def get_account(self) -> AccountInfo:
        info = mt5.account_info()  # type: ignore[union-attr]
        if info is None:
            raise RuntimeError(f"account_info() failed: {mt5.last_error()}")  # type: ignore[union-attr]
        return AccountInfo(
            balance=info.balance,
            equity=info.equity,
            margin=info.margin,
            free_margin=info.margin_free,
            profit=info.profit,
            leverage=int(info.leverage),
            currency=info.currency,
            login=int(info.login),
        )

    def get_tick(self, symbol: str) -> Tick:
        # Ensure the symbol is selected in Market Watch
        mt5.symbol_select(symbol, True)  # type: ignore[union-attr]
        tick = mt5.symbol_info_tick(symbol)  # type: ignore[union-attr]
        if tick is None:
            raise RuntimeError(
                f"symbol_info_tick({symbol!r}) failed: {mt5.last_error()}"  # type: ignore[union-attr]
            )
        return Tick(
            symbol=symbol,
            bid=tick.bid,
            ask=tick.ask,
            volume=tick.volume,
            timestamp=int(tick.time),
        )

    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        mt5.symbol_select(symbol, True)  # type: ignore[union-attr]
        info = mt5.symbol_info(symbol)  # type: ignore[union-attr]
        if info is None:
            raise RuntimeError(
                f"symbol_info({symbol!r}) failed: {mt5.last_error()}"  # type: ignore[union-attr]
            )
        return SymbolInfo(
            name=info.name,
            digits=info.digits,
            point=info.point,
            contract_size=info.trade_contract_size,
            volume_min=info.volume_min,
            volume_max=info.volume_max,
            volume_step=info.volume_step,
        )

    def get_bars(self, symbol: str, tf: int, count: int) -> List[Bar]:
        """Fetch *count* bars for *symbol* at timeframe *tf* (seconds).

        *tf* may be a raw ``int`` or a :class:`~algoforge.types.Timeframe`
        enum value.  An unknown timeframe falls back to ``TIMEFRAME_H1``.
        """
        mt5_tf = _TF_MAP.get(tf, mt5.TIMEFRAME_H1)  # type: ignore[union-attr]
        rates = mt5.copy_rates_from_pos(symbol, mt5_tf, 0, count)  # type: ignore[union-attr]
        if rates is None or len(rates) == 0:
            return []
        return [
            Bar(
                timestamp=int(r['time']),
                open=float(r['open']),
                high=float(r['high']),
                low=float(r['low']),
                close=float(r['close']),
                volume=float(r['tick_volume']),
            )
            for r in rates
        ]

    # ------------------------------------------------------------------ #
    # IBroker — order management                                          #
    # ------------------------------------------------------------------ #

    def place_order(
        self,
        symbol:     str,
        order_type: OrderType,
        lots:       float,
        price:      float = 0.0,
        sl:         float = 0.0,
        tp:         float = 0.0,
        magic:      int   = 0,
        comment:    str   = "",
    ) -> Order:
        """Send a market order and return the resulting :class:`Order`.

        Raises ``RuntimeError`` if MT5 rejects the request.
        """
        # Full mapping: market orders → DEAL, limit/stop → PENDING
        _order_map = {
            OrderType.BUY:        (mt5.TRADE_ACTION_DEAL,    mt5.ORDER_TYPE_BUY),         # type: ignore[union-attr]
            OrderType.SELL:       (mt5.TRADE_ACTION_DEAL,    mt5.ORDER_TYPE_SELL),        # type: ignore[union-attr]
            OrderType.BUY_LIMIT:  (mt5.TRADE_ACTION_PENDING, mt5.ORDER_TYPE_BUY_LIMIT),   # type: ignore[union-attr]
            OrderType.SELL_LIMIT: (mt5.TRADE_ACTION_PENDING, mt5.ORDER_TYPE_SELL_LIMIT),  # type: ignore[union-attr]
            OrderType.BUY_STOP:   (mt5.TRADE_ACTION_PENDING, mt5.ORDER_TYPE_BUY_STOP),    # type: ignore[union-attr]
            OrderType.SELL_STOP:  (mt5.TRADE_ACTION_PENDING, mt5.ORDER_TYPE_SELL_STOP),   # type: ignore[union-attr]
        }
        action, otype = _order_map[order_type]

        if action == mt5.TRADE_ACTION_PENDING:  # type: ignore[union-attr]
            # Limit/stop orders use the caller-supplied price directly
            fill_price = price
        else:
            tick = mt5.symbol_info_tick(symbol)  # type: ignore[union-attr]
            if tick is None:
                raise RuntimeError(
                    f"symbol_info_tick({symbol!r}) failed before order_send: "
                    f"{mt5.last_error()}"  # type: ignore[union-attr]
                )
            fill_price = tick.ask if order_type == OrderType.BUY else tick.bid

        request: dict = {
            "action":       action,
            "symbol":       symbol,
            "volume":       lots,
            "type":         otype,
            "price":        fill_price,
            "sl":           sl,
            "tp":           tp,
            "magic":        magic,
            "comment":      comment,
            "type_time":    mt5.ORDER_TIME_GTC,    # type: ignore[union-attr]
            "type_filling": mt5.ORDER_FILLING_IOC, # type: ignore[union-attr]
        }

        result = mt5.order_send(request)  # type: ignore[union-attr]
        if result and result.retcode == mt5.TRADE_RETCODE_DONE:  # type: ignore[union-attr]
            return Order(
                ticket=result.order,
                symbol=symbol,
                type=order_type,
                lots=lots,
                fill_price=result.price,
                sl=sl,
                tp=tp,
                magic=magic,
                comment=comment,
            )

        raise RuntimeError(
            f"order_send failed: retcode={getattr(result, 'retcode', None)} "
            f"last_error={mt5.last_error()}"  # type: ignore[union-attr]
        )

    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        """Close (or partially close) an open position by *ticket*.

        Returns 0 on success, -1 on failure.
        """
        pos = self.get_position(ticket)
        if pos is None:
            return -1

        # Determine the counter-direction order type
        if pos.side == Direction.LONG:
            otype = mt5.ORDER_TYPE_SELL  # type: ignore[union-attr]
            tick  = mt5.symbol_info_tick(pos.symbol)  # type: ignore[union-attr]
            price = tick.bid if tick else 0.0
        else:
            otype = mt5.ORDER_TYPE_BUY  # type: ignore[union-attr]
            tick  = mt5.symbol_info_tick(pos.symbol)  # type: ignore[union-attr]
            price = tick.ask if tick else 0.0

        close_lots = lots if lots > 0.0 else pos.lots

        request: dict = {
            "action":       mt5.TRADE_ACTION_DEAL,    # type: ignore[union-attr]
            "symbol":       pos.symbol,
            "volume":       close_lots,
            "type":         otype,
            "position":     ticket,
            "price":        price,
            "type_time":    mt5.ORDER_TIME_GTC,       # type: ignore[union-attr]
            "type_filling": mt5.ORDER_FILLING_IOC,    # type: ignore[union-attr]
        }

        result = mt5.order_send(request)  # type: ignore[union-attr]
        if result and result.retcode == mt5.TRADE_RETCODE_DONE:  # type: ignore[union-attr]
            return 0
        return -1

    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        """Modify SL/TP of an open position.

        Returns 0 on success, -1 on failure.
        """
        pos = self.get_position(ticket)
        if pos is None:
            return -1

        request: dict = {
            "action":   mt5.TRADE_ACTION_SLTP,  # type: ignore[union-attr]
            "symbol":   pos.symbol,
            "position": ticket,
            "sl":       sl,
            "tp":       tp,
        }
        result = mt5.order_send(request)  # type: ignore[union-attr]
        if result and result.retcode == mt5.TRADE_RETCODE_DONE:  # type: ignore[union-attr]
            return 0
        return -1

    def cancel_order(self, ticket: int) -> int:
        """Cancel a pending order.

        Returns 0 on success, -1 on failure.
        """
        request: dict = {
            "action": mt5.TRADE_ACTION_REMOVE,  # type: ignore[union-attr]
            "order":  ticket,
        }
        result = mt5.order_send(request)  # type: ignore[union-attr]
        if result and result.retcode == mt5.TRADE_RETCODE_DONE:  # type: ignore[union-attr]
            return 0
        return -1

    def get_positions(self) -> List[Position]:
        """Return all currently open positions."""
        raw = mt5.positions_get()  # type: ignore[union-attr]
        if raw is None:
            return []
        return [self._map_position(p) for p in raw]

    def get_position(self, ticket: int) -> Optional[Position]:
        """Return a single position by ticket, or ``None`` if not found."""
        raw = mt5.positions_get(ticket=ticket)  # type: ignore[union-attr]
        if not raw:
            return None
        return self._map_position(raw[0])

    def get_orders(self) -> List[Order]:
        """Return all currently pending orders."""
        raw = mt5.orders_get()  # type: ignore[union-attr]
        if raw is None:
            return []
        return [self._map_order(o) for o in raw]

    def poll_sl_tp(self) -> None:
        """No-op — MT5 handles SL/TP enforcement server-side."""

    # ------------------------------------------------------------------ #
    # Internal helpers                                                     #
    # ------------------------------------------------------------------ #

    @staticmethod
    def _map_position(p) -> Position:  # type: ignore[type-arg]
        """Convert an MT5 position record to a :class:`Position`."""
        side = Direction.LONG if p.type == 0 else Direction.SHORT
        return Position(
            ticket=int(p.ticket),
            symbol=p.symbol,
            side=side,
            lots=float(p.volume),
            open_price=float(p.price_open),
            current_price=float(p.price_current),
            sl=float(p.sl),
            tp=float(p.tp),
            profit=float(p.profit),
            commission=float(p.commission),
            swap=float(p.swap),
            magic=int(p.magic),
            comment=p.comment,
        )

    @staticmethod
    def _map_order(o) -> Order:  # type: ignore[type-arg]
        """Convert an MT5 pending order record to an :class:`Order`."""
        # MT5 order types: 0=BUY, 1=SELL, 2=BUY_LIMIT, 3=SELL_LIMIT,
        #                  4=BUY_STOP, 5=SELL_STOP
        try:
            otype = OrderType(o.type)
        except ValueError:
            otype = OrderType.BUY
        return Order(
            ticket=int(o.ticket),
            symbol=o.symbol,
            type=otype,
            lots=float(o.volume_initial),
            price=float(o.price_open),
            sl=float(o.sl),
            tp=float(o.tp),
            fill_price=0.0,
            magic=int(o.magic),
            comment=o.comment,
        )
