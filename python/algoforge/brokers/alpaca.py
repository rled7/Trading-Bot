"""Alpaca REST adapter (S5).

API docs: https://docs.alpaca.markets/reference/
Auth:   APCA-API-KEY-ID + APCA-API-SECRET-KEY headers.
Paper:  https://paper-api.alpaca.markets/v2
Live:   https://api.alpaca.markets/v2
Data:   https://data.alpaca.markets/v2  (separate host for market data)

Symbols: equities e.g. AAPL; crypto e.g. BTC/USD.

--- Ticket-ID mapping decision ---
Alpaca identifies orders by UUID strings and positions by symbol string.
AlgoForge's Order.ticket and Position.ticket are int fields.

We maintain two dicts:
  _ticket_to_id: dict[int, str]  — int ticket  -> Alpaca UUID (for orders)
                                                   or symbol (for positions)
  _id_to_ticket: dict[str, int]  — Alpaca UUID -> int ticket (orders)
                                   symbol       -> int ticket (positions)

Tickets are assigned from a monotonically-incrementing counter
``_next_ticket`` so they are stable within a session.  Position tickets
use the symbol as the lookup key (Alpaca positions are keyed by symbol,
not UUID); order tickets use the Alpaca UUID.

--- Lots / Qty mapping decision ---
Alpaca uses shares/contracts (integer or fractional) not MetaTrader lots.
``lots`` in AlgoForge maps 1-to-1 to ``qty`` in Alpaca requests.
That is: place_order(..., lots=10.0) submits qty=10 to Alpaca.
Comment is preserved in the ``client_order_id`` prefix where possible.

--- modify_position limitation ---
Alpaca does not expose a direct SL/TP field on filled positions.
True bracket-order SL/TP management requires creating attached
stop/limit legs at order placement time.  ``modify_position`` here
attempts to PATCH an open (unfilled) order via the replace endpoint;
if the ticket resolves to a position (symbol key), it returns 1 and
logs a warning.  Callers needing live SL/TP management should use
bracket orders via place_order extensions.
"""
from __future__ import annotations

import datetime
import hashlib
import time
from typing import Optional

from ..broker import AccountInfo, Order, OrderType, Position, SymbolInfo, Tick
from ..logger import get_logger
from ..types import Bar, Direction
from .base import BrokerError, RestBroker

log = get_logger("brokers.alpaca")

# ---------------------------------------------------------------------------
# Timeframe (seconds) -> Alpaca API timeframe string
# ---------------------------------------------------------------------------
_TF_MAP: dict[int, str] = {
    15:     "15Sec",
    60:     "1Min",
    300:    "5Min",
    900:    "15Min",
    1800:   "30Min",
    3600:   "1Hour",
    14400:  "4Hour",
    86400:  "1Day",
    604800: "1Week",
}

_DEFAULT_DATA_URL = "https://data.alpaca.markets/v2"


def _iso_to_epoch(ts: str) -> int:
    """Convert an ISO-8601 / RFC-3339 timestamp string to a Unix epoch int.

    Handles the trailing ``Z`` which Python < 3.11 ``fromisoformat`` rejects.
    """
    if ts.endswith("Z"):
        ts = ts[:-1] + "+00:00"
    dt = datetime.datetime.fromisoformat(ts)
    # Ensure timezone-aware then convert to UTC epoch
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=datetime.timezone.utc)
    return int(dt.timestamp())


def _alpaca_side_to_direction(side: str) -> Direction:
    return Direction.LONG if side == "long" else Direction.SHORT


def _order_type_to_alpaca(order_type: OrderType) -> tuple[str, str]:
    """Return (side, type) strings for the Alpaca orders payload."""
    mapping = {
        OrderType.BUY:        ("buy",  "market"),
        OrderType.SELL:       ("sell", "market"),
        OrderType.BUY_LIMIT:  ("buy",  "limit"),
        OrderType.SELL_LIMIT: ("sell", "limit"),
        OrderType.BUY_STOP:   ("buy",  "stop"),
        OrderType.SELL_STOP:  ("sell", "stop"),
    }
    return mapping[order_type]


def _alpaca_status_to_order_type(side: str, order_type: str) -> OrderType:
    if side == "buy":
        if order_type == "limit":
            return OrderType.BUY_LIMIT
        if order_type == "stop":
            return OrderType.BUY_STOP
        return OrderType.BUY
    else:
        if order_type == "limit":
            return OrderType.SELL_LIMIT
        if order_type == "stop":
            return OrderType.SELL_STOP
        return OrderType.SELL


class AlpacaBroker(RestBroker):
    BROKER_NAME = "alpaca"
    LIVE_URL  = "https://api.alpaca.markets/v2"
    PAPER_URL = "https://paper-api.alpaca.markets/v2"

    # ------------------------------------------------------------------
    # Ticket management
    # ------------------------------------------------------------------
    def __init__(self, config=None) -> None:
        super().__init__(config)
        self._ticket_to_id: dict[int, str] = {}
        self._id_to_ticket: dict[str, int] = {}
        self._next_ticket: int = 1

    def _alloc_ticket(self, key: str) -> int:
        """Return an existing int ticket for *key* or allocate a new one."""
        if key in self._id_to_ticket:
            return self._id_to_ticket[key]
        ticket = self._next_ticket
        self._next_ticket += 1
        self._ticket_to_id[ticket] = key
        self._id_to_ticket[key] = ticket
        return ticket

    def _resolve_key(self, ticket: int) -> Optional[str]:
        """Return the Alpaca UUID or symbol for a given int ticket, or None."""
        return self._ticket_to_id.get(ticket)

    # ------------------------------------------------------------------
    # Connection
    # ------------------------------------------------------------------
    def _on_connect(self) -> None:
        self._config.require("api_key", "api_secret")

    def _auth_headers(self) -> dict[str, str]:
        return {
            "APCA-API-KEY-ID":     self._config.api_key,
            "APCA-API-SECRET-KEY": self._config.api_secret,
        }

    def _data_url(self) -> str:
        return self._config.extra.get("data_url", _DEFAULT_DATA_URL).rstrip("/")

    # ------------------------------------------------------------------
    # Account
    # ------------------------------------------------------------------
    def get_account(self) -> AccountInfo:
        """GET /account -> AccountInfo.

        Alpaca fields used:
          cash         -> balance
          equity       -> equity
          buying_power -> free_margin
          initial_margin (margin_used if available) -> margin
          unrealized_pl -> profit
          currency     -> currency
          account_number -> login (hashed to int)
        """
        data = self._get("/account")
        # Stable int login from account_number string
        acct_num = data.get("account_number", "")
        login = int(hashlib.md5(acct_num.encode()).hexdigest()[:8], 16) if acct_num else 0

        return AccountInfo(
            balance=float(data.get("cash", 0)),
            equity=float(data.get("equity", 0)),
            margin=float(data.get("initial_margin", 0)),
            free_margin=float(data.get("buying_power", 0)),
            profit=float(data.get("unrealized_pl", 0)),
            leverage=1,      # Alpaca does not expose a leverage multiplier in REST
            currency=data.get("currency", "USD"),
            login=login,
        )

    # ------------------------------------------------------------------
    # Market data  (separate data host)
    # ------------------------------------------------------------------
    def get_tick(self, symbol: str) -> Tick:
        """GET data.alpaca.markets/v2/stocks/{symbol}/quotes/latest -> Tick.

        Response shape (equities):
          {"quote": {"bp": ..., "ap": ..., "bs": ..., "as": ..., "t": "..."}}

        ``bp`` = bid price, ``ap`` = ask price, ``bs`` = bid size,
        ``as`` = ask size, ``t`` = RFC-3339 timestamp.
        """
        broker_sym = self._to_broker_symbol(symbol)
        url = f"{self._data_url()}/stocks/{broker_sym}/quotes/latest"
        data = self._get(url)
        quote = data.get("quote", {})
        return Tick(
            symbol=symbol,
            bid=float(quote.get("bp", 0)),
            ask=float(quote.get("ap", 0)),
            volume=float(quote.get("bs", 0)),   # bid size as proxy volume
            timestamp=_iso_to_epoch(quote["t"]) if quote.get("t") else int(time.time()),
        )

    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        """GET /assets/{symbol} -> SymbolInfo.

        Alpaca does not expose tick-size / lot-size for equities via the
        Trading v2 API in a machine-readable numeric form.  We return
        reasonable defaults for US equity trading.
        """
        broker_sym = self._to_broker_symbol(symbol)
        # Best-effort: fetch the asset to confirm it exists
        self._get(f"/assets/{broker_sym}")
        return SymbolInfo(
            name=symbol,
            digits=2,
            point=0.01,
            contract_size=1.0,   # 1 share per unit for equities
            volume_min=1.0,
            volume_max=10_000.0,
            volume_step=1.0,
        )

    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]:
        """GET data.alpaca.markets/v2/stocks/{symbol}/bars -> list[Bar].

        ``tf`` is the timeframe in seconds (Timeframe enum values).
        ``count`` maps to the ``limit`` query parameter.

        Response shape:
          {"bars": [{"t": "...", "o": ..., "h": ..., "l": ..., "c": ..., "v": ...}]}
        """
        alpaca_tf = _TF_MAP.get(tf, "1Min")
        broker_sym = self._to_broker_symbol(symbol)
        url = f"{self._data_url()}/stocks/{broker_sym}/bars"
        data = self._get(url, params={"timeframe": alpaca_tf, "limit": count})
        bars: list[Bar] = []
        for b in data.get("bars", []):
            bars.append(Bar(
                timestamp=_iso_to_epoch(b["t"]) if b.get("t") else 0,
                open=float(b.get("o", 0)),
                high=float(b.get("h", 0)),
                low=float(b.get("l", 0)),
                close=float(b.get("c", 0)),
                volume=float(b.get("v", 0)),
                spread=0.0,
            ))
        return bars

    # ------------------------------------------------------------------
    # Orders
    # ------------------------------------------------------------------
    def place_order(self, symbol: str, order_type: OrderType,
                    lots: float, price: float = 0.0,
                    sl: float = 0.0, tp: float = 0.0,
                    magic: int = 0, comment: str = "") -> Order:
        """POST /orders -> Order.

        Lots/Qty mapping: ``lots`` is passed directly as ``qty`` to Alpaca.
        1 lot == 1 share/contract.  Fractional shares are supported when the
        asset allows it (Alpaca accepts fractional qty as a float string).

        SL/TP are NOT attached here; Alpaca requires bracket order legs.
        Pass sl/tp=0.0 for plain market/limit/stop orders.
        """
        side, alpaca_type = _order_type_to_alpaca(order_type)
        broker_sym = self._to_broker_symbol(symbol)
        body: dict = {
            "symbol":        broker_sym,
            "qty":           str(lots),
            "side":          side,
            "type":          alpaca_type,
            "time_in_force": "gtc",
        }
        # For limit and stop orders, a price is required
        if alpaca_type == "limit" and price:
            body["limit_price"] = str(price)
        if alpaca_type == "stop" and price:
            body["stop_price"] = str(price)
        # Encode comment/magic in client_order_id (64-char max)
        if comment or magic:
            coid = f"af-{magic}-{comment}"[:64]
            body["client_order_id"] = coid

        data = self._post("/orders", body=body)
        alpaca_id = data.get("id", "")
        ticket = self._alloc_ticket(alpaca_id)

        return Order(
            ticket=ticket,
            symbol=symbol,
            type=order_type,
            lots=lots,
            price=price,
            sl=sl,
            tp=tp,
            fill_price=float(data.get("filled_avg_price") or 0),
            magic=magic,
            comment=comment,
        )

    def get_orders(self) -> list[Order]:
        """GET /orders -> list[Order] (open orders only; status=open)."""
        data = self._get("/orders", params={"status": "open"})
        orders: list[Order] = []
        for o in data:
            alpaca_id = o.get("id", "")
            ticket = self._alloc_ticket(alpaca_id)
            ot = _alpaca_status_to_order_type(o.get("side", "buy"), o.get("type", "market"))
            orders.append(Order(
                ticket=ticket,
                symbol=self._from_broker_symbol(o.get("symbol", "")),
                type=ot,
                lots=float(o.get("qty") or o.get("notional") or 0),
                price=float(o.get("limit_price") or o.get("stop_price") or 0),
                fill_price=float(o.get("filled_avg_price") or 0),
            ))
        return orders

    def cancel_order(self, ticket: int) -> int:
        """DELETE /orders/{order_id} -> 0 on success, nonzero on error."""
        alpaca_id = self._resolve_key(ticket)
        if alpaca_id is None:
            log.warning("alpaca: cancel_order: unknown ticket %d", ticket)
            return 1
        try:
            self._request("DELETE", f"/orders/{alpaca_id}")
        except BrokerError as exc:
            log.error("alpaca: cancel_order failed: %s", exc)
            return exc.code or 1
        return 0

    def modify_order(self, ticket: int, price: float = 0.0,
                     qty: float = 0.0) -> int:
        """PATCH /orders/{order_id} — replace limit/stop price or qty.

        This is NOT part of IBroker; it is an Alpaca-specific extension.
        """
        alpaca_id = self._resolve_key(ticket)
        if alpaca_id is None:
            return 1
        body: dict = {}
        if qty:
            body["qty"] = str(qty)
        if price:
            body["limit_price"] = str(price)
        try:
            self._request("PATCH", f"/orders/{alpaca_id}", body=body)
        except BrokerError as exc:
            log.error("alpaca: modify_order failed: %s", exc)
            return exc.code or 1
        return 0

    # ------------------------------------------------------------------
    # Positions
    # ------------------------------------------------------------------
    def get_positions(self) -> list[Position]:
        """GET /positions -> list[Position].

        Alpaca positions are keyed by symbol, so we store symbol as the
        ticket key (not a UUID).
        """
        data = self._get("/positions")
        positions: list[Position] = []
        for p in data:
            symbol = self._from_broker_symbol(p.get("symbol", ""))
            ticket = self._alloc_ticket(symbol)   # symbol is the stable key
            positions.append(self._parse_position(p, ticket, symbol))
        return positions

    def get_position(self, ticket: int) -> Optional[Position]:
        """Return a single position by its int ticket, or None if not found."""
        key = self._resolve_key(ticket)
        if key is None:
            return None
        # key is a symbol for positions
        try:
            broker_sym = self._to_broker_symbol(key)
            data = self._get(f"/positions/{broker_sym}")
        except BrokerError:
            return None
        symbol = self._from_broker_symbol(data.get("symbol", key))
        return self._parse_position(data, ticket, symbol)

    def _parse_position(self, p: dict, ticket: int, symbol: str) -> Position:
        side = _alpaca_side_to_direction(p.get("side", "long"))
        return Position(
            ticket=ticket,
            symbol=symbol,
            side=side,
            lots=float(p.get("qty", 0)),
            open_price=float(p.get("avg_entry_price", 0)),
            current_price=float(p.get("current_price", 0)),
            sl=0.0,   # Alpaca does not expose SL/TP on position objects
            tp=0.0,
            profit=float(p.get("unrealized_pl", 0)),
            commission=0.0,
            swap=0.0,
        )

    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        """Close a position via DELETE /positions/{symbol}.

        ``ticket`` must resolve to a symbol (positions are keyed by symbol in
        Alpaca, not by UUID).  ``lots`` > 0 triggers a partial close via the
        ``qty`` query parameter; lots=0.0 closes the full position.

        Returns 0 on success, nonzero on error.
        """
        key = self._resolve_key(ticket)
        if key is None:
            log.warning("alpaca: close_position: unknown ticket %d", ticket)
            return 1
        broker_sym = self._to_broker_symbol(key)
        params = {"qty": str(lots)} if lots > 0 else None
        try:
            self._request("DELETE", f"/positions/{broker_sym}",
                          params=params)
        except BrokerError as exc:
            log.error("alpaca: close_position failed: %s", exc)
            return exc.code or 1
        return 0

    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        """Attempt to modify SL/TP on an open order via the replace endpoint.

        LIMITATION: Alpaca does not expose SL/TP fields on filled positions.
        If the ticket resolves to a position symbol (not an order UUID), this
        method logs a warning and returns 1.  True bracket SL/TP management
        requires attaching stop/limit legs at order placement time.
        """
        key = self._resolve_key(ticket)
        if key is None:
            return 1
        # Heuristic: UUIDs contain hyphens; symbols do not
        if "-" not in key:
            log.warning(
                "alpaca: modify_position(%d): ticket maps to symbol %r — "
                "Alpaca does not support live SL/TP modification on filled "
                "positions via REST.  Use bracket orders at placement time.",
                ticket, key,
            )
            return 1
        # key is an order UUID — attempt replace
        body: dict = {}
        if sl:
            body["stop_price"] = str(sl)
        if tp:
            body["limit_price"] = str(tp)
        if not body:
            return 0
        try:
            self._request("PATCH", f"/orders/{key}", body=body)
        except BrokerError as exc:
            log.error("alpaca: modify_position/replace failed: %s", exc)
            return exc.code or 1
        return 0
