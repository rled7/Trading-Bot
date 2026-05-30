"""Interactive Brokers adapter — Client Portal Web API.

API docs: https://www.interactivebrokers.com/api/doc.html (Client Portal)
Transport: a locally-running Client Portal Gateway proxies an authenticated
session; requests hit https://localhost:5000/v1/api. There is no API-key
header — the gateway holds the session, so AF_IBKR_BASE_URL should point at
the gateway and AF_IBKR_ACCOUNT selects the account (AF_IBKR_ACCOUNT env var).

IBKR identifies instruments by **conid** (contract id), not ticker. The helper
``_resolve_conid(symbol) -> int`` calls GET /iserver/secdef/search and caches
results in ``self._conid_cache``.

**lots → quantity**: IBKR uses share/contract *quantity* (whole numbers for
stocks, fractional allowed for some products). This adapter treats the ``lots``
parameter as a quantity directly (i.e. lots=100 means 100 shares/contracts).
Document accordingly.

**Position ticket**: positions don't have order-ticket identity in IBKR; this
adapter uses ``conid`` as the ticket for positions, consistent across
``get_positions``, ``get_position``, ``close_position``.

**Order-confirm reply loop**: placing an order via POST
/iserver/account/{accountId}/orders may return a list of confirmation prompts
(dicts with ``id`` and ``message`` keys) instead of an order result. This
adapter iterates through confirmation rounds (up to _MAX_CONFIRM_ROUNDS) by
posting /iserver/reply/{replyId} with ``{"confirmed": true}`` until an
order_id surfaces or the limit is hit.
"""
from __future__ import annotations

import time
from typing import Any, Optional

from ..broker import AccountInfo, Order, OrderType, Position, SymbolInfo, Tick
from ..types import Bar, Direction, Timeframe
from .base import BrokerError, RestBroker
from ..logger import get_logger

log = get_logger("brokers.ibkr")

# Maximum rounds of order confirmation to attempt before giving up.
_MAX_CONFIRM_ROUNDS = 5

# IBKR market data field IDs used in snapshot requests.
_FIELD_LAST = "31"
_FIELD_BID = "84"
_FIELD_ASK = "86"

# Mapping from AlgoForge Timeframe seconds to IBKR bar/period strings.
# IBKR bar=<n><unit>, period=<n><unit> where unit: min, h, d, w, m
_TF_TO_IBKR: dict[int, tuple[str, str]] = {
    Timeframe.S15:  ("15secs",  "1d"),
    Timeframe.M1:   ("1min",    "1d"),
    Timeframe.M5:   ("5mins",   "5d"),
    Timeframe.M15:  ("15mins",  "5d"),
    Timeframe.M30:  ("30mins",  "5d"),
    Timeframe.H1:   ("1h",      "30d"),
    Timeframe.H4:   ("4h",      "30d"),
    Timeframe.D1:   ("1d",      "1y"),
    Timeframe.W1:   ("1w",      "5y"),
}


def _parse_account_login(acct: str) -> int:
    """Convert IBKR account ID (e.g. 'U1234567') to an integer login field.

    IBKR account IDs typically start with a letter prefix ('U', 'DU', etc.)
    followed by digits. Strip any leading non-digit characters before parsing.
    """
    digits = acct.lstrip("UuDdFfEe")
    if digits.isdigit():
        return int(digits)
    return 0


def _safe_float(val: Any, default: float = 0.0) -> float:
    """Parse a value that may be str/int/float, return default on failure."""
    if val is None:
        return default
    try:
        return float(str(val).replace(",", "").strip())
    except (ValueError, TypeError):
        return default


def _summary_amount(data: dict, key: str) -> float:
    """Extract .amount from a portfolio summary field (nested dict)."""
    raw = data.get(key, {})
    if isinstance(raw, dict):
        return _safe_float(raw.get("amount"))
    return _safe_float(raw)


class IbkrBroker(RestBroker):
    """Interactive Brokers REST adapter using the Client Portal Web API."""

    BROKER_NAME = "ibkr"
    # Both paper and live use the local gateway; base URL may differ per installation.
    LIVE_URL = "https://localhost:5000/v1/api"
    PAPER_URL = "https://localhost:5000/v1/api"

    def __init__(self, config=None):
        super().__init__(config)
        self._conid_cache: dict[str, int] = {}

    # ------------------------------------------------------------------ #
    # Connection
    # ------------------------------------------------------------------ #

    def _on_connect(self) -> None:
        """Verify account_id is configured and the gateway session is active."""
        self._config.require("account_id")
        try:
            status = self._get("/iserver/auth/status")
        except BrokerError as exc:
            raise BrokerError(
                f"ibkr: could not reach gateway auth/status: {exc}",
                code=3,
            ) from exc

        authenticated = status.get("authenticated", False)
        if not authenticated:
            raise BrokerError(
                "ibkr: Client Portal Gateway session is not authenticated. "
                "Start the gateway and complete SSO login first.",
                code=5,
            )
        log.info("ibkr: gateway authenticated, account=%s", self._config.account_id)

    # ------------------------------------------------------------------ #
    # conid resolution
    # ------------------------------------------------------------------ #

    def _resolve_conid(self, symbol: str) -> int:
        """Return the IBKR conid for *symbol*, with an in-process cache.

        Calls GET /iserver/secdef/search?symbol=<symbol>.  The first result
        whose ``ticker`` matches (case-insensitive) is accepted; otherwise the
        first result in the list.
        """
        key = symbol.upper()
        if key in self._conid_cache:
            return self._conid_cache[key]

        results = self._get("/iserver/secdef/search", params={"symbol": symbol})
        if not results or not isinstance(results, list):
            raise BrokerError(
                f"ibkr: no conid found for symbol '{symbol}'", code=6
            )

        # Prefer exact ticker match; fall back to first entry.
        conid: Optional[int] = None
        for entry in results:
            if str(entry.get("ticker", "")).upper() == key:
                conid = int(entry["conid"])
                break
        if conid is None:
            conid = int(results[0]["conid"])

        self._conid_cache[key] = conid
        log.debug("ibkr: resolved %s -> conid=%d", symbol, conid)
        return conid

    # ------------------------------------------------------------------ #
    # Account
    # ------------------------------------------------------------------ #

    def get_account(self) -> AccountInfo:
        """Return account snapshot from /portfolio/{accountId}/summary.

        Maps:
          netliquidation  -> equity
          availablefunds  -> free_margin
          totalcashvalue  -> balance  (cash approximation)
          unrealizedpnl   -> profit
          grosspositionvalue -> margin proxy
        """
        acct = self._config.account_id
        data: dict = self._get(f"/portfolio/{acct}/summary")
        equity = _summary_amount(data, "netliquidation")
        free_margin = _summary_amount(data, "availablefunds")
        balance = _summary_amount(data, "totalcashvalue")
        if balance == 0.0:
            balance = equity  # fallback
        profit = _summary_amount(data, "unrealizedpnl")
        margin = _summary_amount(data, "grosspositionvalue")

        # currency from any nested field
        currency = "USD"
        for field_key in ("netliquidation", "availablefunds"):
            raw = data.get(field_key, {})
            if isinstance(raw, dict) and raw.get("currency"):
                currency = str(raw["currency"])
                break

        return AccountInfo(
            balance=balance,
            equity=equity,
            margin=margin,
            free_margin=free_margin,
            profit=profit,
            leverage=1,   # IBKR does not expose a simple leverage ratio here
            currency=currency,
            login=_parse_account_login(acct),
        )

    # ------------------------------------------------------------------ #
    # Market data
    # ------------------------------------------------------------------ #

    def get_tick(self, symbol: str) -> Tick:
        """Fetch last/bid/ask snapshot via /iserver/marketdata/snapshot.

        Field IDs: 31=last, 84=bid, 86=ask.
        Note: the first snapshot call for a conid may return empty data
        live (IBKR requires a subscription warm-up); under normal usage a
        second call returns populated values.
        """
        conid = self._resolve_conid(symbol)
        fields = f"{_FIELD_LAST},{_FIELD_BID},{_FIELD_ASK}"
        results = self._get(
            "/iserver/marketdata/snapshot",
            params={"conids": str(conid), "fields": fields},
        )
        if not results or not isinstance(results, list):
            raise BrokerError(f"ibkr: empty snapshot for {symbol}", code=7)

        snap: dict = results[0]
        last = _safe_float(snap.get(_FIELD_LAST))
        bid = _safe_float(snap.get(_FIELD_BID))
        ask = _safe_float(snap.get(_FIELD_ASK))

        # If bid/ask missing but last available, use last for both.
        if bid == 0.0 and last > 0.0:
            bid = last
        if ask == 0.0 and last > 0.0:
            ask = last

        return Tick(
            symbol=symbol,
            bid=bid,
            ask=ask,
            volume=0.0,
            timestamp=int(time.time()),
        )

    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        """Return symbol metadata derived from secdef search results."""
        key = symbol.upper()
        # Re-use cached conid resolution which hits the same endpoint.
        results = self._get("/iserver/secdef/search", params={"symbol": symbol})
        if not results or not isinstance(results, list):
            raise BrokerError(f"ibkr: no secdef for {symbol}", code=6)

        entry: dict = {}
        for r in results:
            if str(r.get("ticker", "")).upper() == key:
                entry = r
                break
        if not entry:
            entry = results[0]

        # IBKR does not return min lot / contract size in secdef search response
        # directly — use sensible defaults. For equities qty=1 is min.
        return SymbolInfo(
            name=symbol,
            digits=2,
            point=0.01,
            contract_size=1.0,
            volume_min=1.0,
            volume_max=10_000_000.0,
            volume_step=1.0,
        )

    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]:
        """Fetch OHLCV bars via /iserver/marketdata/history.

        ``tf`` should be a ``Timeframe`` value (seconds). ``count`` is treated
        as the target bar count; IBKR returns a fixed period and this adapter
        trims/pads to ``count`` from the tail.
        """
        conid = self._resolve_conid(symbol)
        bar_str, period_str = _TF_TO_IBKR.get(
            tf, ("1d", "1y")  # fallback: daily bars
        )
        data = self._get(
            "/iserver/marketdata/history",
            params={
                "conid": str(conid),
                "period": period_str,
                "bar": bar_str,
                "outsideRth": "false",
            },
        )
        raw_bars = data.get("data", []) if isinstance(data, dict) else []
        bars: list[Bar] = []
        for rb in raw_bars:
            bars.append(Bar(
                timestamp=int(rb.get("t", 0)) // 1000,  # ms -> s
                open=_safe_float(rb.get("o")),
                high=_safe_float(rb.get("h")),
                low=_safe_float(rb.get("l")),
                close=_safe_float(rb.get("c")),
                volume=_safe_float(rb.get("v")),
            ))
        # Return up to `count` most recent bars.
        return bars[-count:] if len(bars) > count else bars

    # ------------------------------------------------------------------ #
    # Orders
    # ------------------------------------------------------------------ #

    def _map_order_type(self, order_type: OrderType) -> tuple[str, str]:
        """Return (side, ibkr_order_type) for an OrderType enum value."""
        mapping = {
            OrderType.BUY:       ("BUY",  "MKT"),
            OrderType.SELL:      ("SELL", "MKT"),
            OrderType.BUY_LIMIT: ("BUY",  "LMT"),
            OrderType.SELL_LIMIT: ("SELL", "LMT"),
            OrderType.BUY_STOP:  ("BUY",  "STP"),
            OrderType.SELL_STOP: ("SELL", "STP"),
        }
        return mapping.get(order_type, ("BUY", "MKT"))

    def place_order(
        self,
        symbol: str,
        order_type: OrderType,
        lots: float,
        price: float = 0.0,
        sl: float = 0.0,
        tp: float = 0.0,
        magic: int = 0,
        comment: str = "",
    ) -> Order:
        """Place an order. ``lots`` is used as the integer quantity directly.

        IBKR may return confirmation prompts; this method iterates up to
        _MAX_CONFIRM_ROUNDS replying with ``{"confirmed": true}`` until an
        order_id is received.

        Returns an Order with ticket=order_id.
        """
        conid = self._resolve_conid(symbol)
        side, ibkr_type = self._map_order_type(order_type)
        quantity = int(lots) if lots == int(lots) else lots

        order_body: dict[str, Any] = {
            "conid": conid,
            "orderType": ibkr_type,
            "side": side,
            "quantity": quantity,
            "tif": "GTC",
        }
        if ibkr_type in ("LMT",) and price:
            order_body["price"] = price
        if ibkr_type == "STP" and price:
            order_body["auxPrice"] = price

        acct = self._config.account_id
        resp = self._post(
            f"/iserver/account/{acct}/orders",
            body={"orders": [order_body]},
        )

        # resp may be a list or a dict — normalise to list.
        if isinstance(resp, dict):
            resp = [resp]

        order_id: Optional[int] = None

        for _round in range(_MAX_CONFIRM_ROUNDS):
            if not resp or not isinstance(resp, list):
                break

            item = resp[0]

            # Confirmation prompt: has "id" and "message" but no "order_id".
            if "id" in item and "message" in item and "order_id" not in item:
                reply_id = item["id"]
                log.info(
                    "ibkr: order confirmation required (round %d): %s",
                    _round + 1, item.get("message", ""),
                )
                resp = self._post(
                    f"/iserver/reply/{reply_id}",
                    body={"confirmed": True},
                )
                if isinstance(resp, dict):
                    resp = [resp]
                continue

            # Order result.
            if "order_id" in item:
                order_id = int(item["order_id"])
                break
            # Also handle "orderId" (camelCase variant in some versions).
            if "orderId" in item:
                order_id = int(item["orderId"])
                break
            # Unexpected shape — break to avoid infinite loop.
            log.warning("ibkr: unexpected order response item: %s", item)
            break

        if order_id is None:
            raise BrokerError(
                f"ibkr: could not extract order_id after {_MAX_CONFIRM_ROUNDS} "
                f"confirmation rounds; last resp={resp}",
                code=8,
            )

        direction = OrderType.BUY if side == "BUY" else OrderType.SELL
        return Order(
            ticket=order_id,
            symbol=symbol,
            type=order_type,
            lots=float(quantity),
            price=price,
            sl=sl,
            tp=tp,
            fill_price=0.0,  # not available synchronously from place
            magic=magic,
            comment=comment,
        )

    def cancel_order(self, ticket: int) -> int:
        """Cancel a pending order by order ID. Returns 0 on success."""
        acct = self._config.account_id
        try:
            self._request("DELETE", f"/iserver/account/{acct}/order/{ticket}")
            return 0
        except BrokerError as exc:
            log.error("ibkr: cancel_order(%d) failed: %s", ticket, exc)
            return exc.code or 1

    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        """Modify an existing order's stop-loss/take-profit.

        Uses POST /iserver/account/{accountId}/order/{orderId}.
        IBKR does not natively support SL/TP on orders at the gateway level
        (these are typically bracket orders). This sends a best-effort modify
        with auxPrice=sl. Returns 0 on success.
        """
        acct = self._config.account_id
        body: dict[str, Any] = {}
        if sl:
            body["auxPrice"] = sl
        if tp:
            body["price"] = tp
        if not body:
            return 0
        try:
            self._post(f"/iserver/account/{acct}/order/{ticket}", body=body)
            return 0
        except BrokerError as exc:
            log.error("ibkr: modify_position(%d) failed: %s", ticket, exc)
            return exc.code or 1

    # ------------------------------------------------------------------ #
    # Positions
    # ------------------------------------------------------------------ #

    def _parse_position(self, raw: dict) -> Position:
        """Convert a raw portfolio position dict to a Position dataclass.

        ticket = conid (IBKR does not expose an order-ticket for positions).
        """
        pos = float(raw.get("position", 0.0))
        side = Direction.LONG if pos > 0 else (Direction.SHORT if pos < 0 else Direction.NONE)
        return Position(
            ticket=int(raw.get("conid", 0)),
            symbol=str(raw.get("ticker", raw.get("contractDesc", ""))),
            side=side,
            lots=abs(pos),
            open_price=_safe_float(raw.get("avgCost")),
            current_price=_safe_float(raw.get("mktPrice")),
            sl=0.0,
            tp=0.0,
            profit=_safe_float(raw.get("unrealizedPnl")),
            commission=0.0,
            swap=0.0,
            magic=0,
            comment="",
        )

    def get_positions(self) -> list[Position]:
        """Return all open positions from /portfolio/{accountId}/positions/0."""
        acct = self._config.account_id
        data = self._get(f"/portfolio/{acct}/positions/0")
        if not data or not isinstance(data, list):
            return []
        return [self._parse_position(p) for p in data if float(p.get("position", 0)) != 0]

    def get_position(self, ticket: int) -> Optional[Position]:
        """Return the position whose conid == ticket, or None."""
        positions = self.get_positions()
        for pos in positions:
            if pos.ticket == ticket:
                return pos
        return None

    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        """Close a position identified by conid (= ticket).

        Finds the position to determine side + quantity, then places the
        opposite MKT order. If ``lots`` is provided and > 0, closes that
        partial quantity instead of the full position.
        """
        pos = self.get_position(ticket)
        if pos is None:
            log.warning("ibkr: close_position(%d): position not found", ticket)
            return 1

        close_qty = lots if lots > 0.0 else pos.lots
        close_side = OrderType.SELL if pos.side == Direction.LONG else OrderType.BUY

        # Find symbol — need it to resolve conid for the order.
        symbol = pos.symbol
        try:
            # Use conid directly since we already know it.
            conid = ticket
            self._conid_cache[symbol.upper()] = conid

            self.place_order(symbol, close_side, close_qty)
            return 0
        except BrokerError as exc:
            log.error("ibkr: close_position(%d) failed: %s", ticket, exc)
            return exc.code or 1

    # ------------------------------------------------------------------ #
    # Orders list
    # ------------------------------------------------------------------ #

    def _parse_order(self, raw: dict) -> Order:
        """Convert a raw order dict to an Order dataclass."""
        side_str = str(raw.get("side", "")).upper()
        if "BUY" in side_str:
            otype = OrderType.BUY
        else:
            otype = OrderType.SELL
        return Order(
            ticket=int(raw.get("orderId", raw.get("order_id", 0))),
            symbol=str(raw.get("ticker", raw.get("symbol", ""))),
            type=otype,
            lots=_safe_float(raw.get("remainingQuantity", raw.get("totalSize", 0))),
            price=_safe_float(raw.get("price")),
            sl=0.0,
            tp=0.0,
            fill_price=_safe_float(raw.get("avgPrice")),
            magic=0,
            comment=str(raw.get("description", "")),
        )

    def get_orders(self) -> list[Order]:
        """Return live/pending orders from /iserver/account/orders."""
        data = self._get("/iserver/account/orders")
        raw_orders: list[dict] = []
        if isinstance(data, dict):
            raw_orders = data.get("orders", [])
        elif isinstance(data, list):
            raw_orders = data
        return [self._parse_order(o) for o in raw_orders]
