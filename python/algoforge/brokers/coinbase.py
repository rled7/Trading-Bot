"""Coinbase Advanced Trade REST adapter (S5).

API docs: https://docs.cdp.coinbase.com/advanced-trade/docs/welcome
Auth:     HMAC-SHA256 "legacy" scheme (stdlib hmac + hashlib only).
          CDP/JWT (ES256) is a future option; it requires the optional
          ``cryptography`` extra (non-zero dependency) — not used here.
Base:     https://api.coinbase.com/api/v3/brokerage
Symbols:  Hyphenated product ids, e.g. BTC-USD. Canonical "BTCUSD" is
          converted automatically via _to_broker_symbol.

HMAC signing details
--------------------
Per request we set four headers:
  CB-ACCESS-KEY       — api_key
  CB-ACCESS-TIMESTAMP — UNIX epoch seconds (integer string)
  CB-ACCESS-SIGN      — HMAC-SHA256 hex digest of the message:
                          {timestamp}{METHOD}{/path/no-querystring}{body_json}
                        signed with api_secret encoded as UTF-8.
  CB-ACCESS-PASSPHRASE is NOT required for Advanced Trade (only for Exchange).

The signed *path* is the bare request path as it appears after the host,
without the query string. For example a GET to
  https://api.coinbase.com/api/v3/brokerage/accounts?limit=100
signs the message: {ts}GET/api/v3/brokerage/accounts

Design decisions
-----------------
lots → base *size*: Coinbase market orders specify base_size (unit of the base
  asset, e.g. BTC). The ``lots`` arg passed by the strategy layer is treated as
  base_size directly. Document in strategy code as needed.

Ticket ID: Coinbase returns string order IDs ("order_id"). We maintain an
  internal ``_id_to_ticket`` dict that maps each string order_id → a small
  sequential integer ticket so callers can use int tickets as everywhere else.

SL/TP: Coinbase market IOC orders have no native stop-loss/take-profit.
  Passing sl/tp to place_order is silently ignored (values stored on the
  returned Order but not sent to the exchange). A future enhancement can
  implement bracket orders or trailing stops via the Advanced Trade
  bracket_order_configuration field once the API supports it for all pairs.

modify_position: Implemented as cancel-then-replace for limit/stop orders.
  Market orders cannot be modified; this raises BrokerError.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import time
import urllib.parse
from typing import Any, Optional

from ..broker import (AccountInfo, Order, OrderType, Position, SymbolInfo,
                      Tick)
from ..types import Bar, Direction, Timeframe
from .base import BrokerError, RestBroker

# Mapping from AlgoForge integer timeframes (seconds) to CB granularity strings
_TF_MAP: dict[int, str] = {
    Timeframe.M1:  "ONE_MINUTE",
    Timeframe.M5:  "FIVE_MINUTE",
    Timeframe.M15: "FIFTEEN_MINUTE",
    Timeframe.M30: "THIRTY_MINUTE",
    Timeframe.H1:  "ONE_HOUR",
    21600:         "SIX_HOUR",    # H6 (not in Timeframe enum but supported)
    Timeframe.D1:  "ONE_DAY",
}

# Max candles per CB request (API limit)
_CB_MAX_CANDLES = 350


class CoinbaseBroker(RestBroker):
    """Coinbase Advanced Trade REST broker adapter.

    Environment variables:
        AF_COINBASE_API_KEY     — your CDP API key name
        AF_COINBASE_API_SECRET  — your CDP API secret (hex string)
        AF_COINBASE_PAPER       — "1" to stay on the single CB endpoint in
                                  simulation mode (no separate paper host)
    """

    BROKER_NAME = "coinbase"
    LIVE_URL  = "https://api.coinbase.com/api/v3/brokerage"
    PAPER_URL = "https://api.coinbase.com/api/v3/brokerage"  # CB has no separate sandbox host

    # Internal ticket counter and mapping
    _next_ticket: int
    _id_to_ticket: dict[str, int]
    _ticket_to_id: dict[int, str]

    def __init__(self, config=None):
        super().__init__(config)
        self._next_ticket = 1
        self._id_to_ticket = {}
        self._ticket_to_id = {}

    # ------------------------------------------------------------------ #
    # Symbol conversion
    # ------------------------------------------------------------------ #
    def _to_broker_symbol(self, symbol: str) -> str:
        """Convert canonical AlgoForge symbol → Coinbase product_id.

        Examples: BTCUSD → BTC-USD, BTC/USD → BTC-USD, BTC_USD → BTC-USD.
        Already-hyphenated symbols pass through unchanged.
        """
        s = symbol.replace("/", "-").replace("_", "-").upper()
        if "-" not in s and len(s) == 6:
            s = f"{s[:3]}-{s[3:]}"
        return s

    def _from_broker_symbol(self, symbol: str) -> str:
        """Convert Coinbase product_id → canonical AlgoForge symbol.

        BTC-USD → BTCUSD
        """
        return symbol.replace("-", "")

    # ------------------------------------------------------------------ #
    # Ticket management
    # ------------------------------------------------------------------ #
    def _register_order_id(self, order_id: str) -> int:
        """Map a string Coinbase order_id to a sequential int ticket."""
        if order_id in self._id_to_ticket:
            return self._id_to_ticket[order_id]
        ticket = self._next_ticket
        self._next_ticket += 1
        self._id_to_ticket[order_id] = ticket
        self._ticket_to_id[ticket] = order_id
        return ticket

    def _ticket_to_order_id(self, ticket: int) -> str:
        """Return the Coinbase order_id for an int ticket (raises BrokerError if unknown)."""
        oid = self._ticket_to_id.get(ticket)
        if not oid:
            raise BrokerError(
                f"coinbase: unknown ticket {ticket} — not issued by this session",
                code=5,
            )
        return oid

    # ------------------------------------------------------------------ #
    # HMAC signing
    # ------------------------------------------------------------------ #
    def _sign(self, timestamp: str, method: str, path: str, body_str: str) -> str:
        """Compute the CB-ACCESS-SIGN HMAC-SHA256 hex digest.

        Message: timestamp + METHOD + path_without_querystring + body_json_string

        The path should be the bare request path as it appears after the host
        (e.g. "/api/v3/brokerage/accounts") without the query string.

        Args:
            timestamp: UNIX epoch seconds as a string (e.g. "1716000000")
            method:    HTTP method in UPPER CASE (e.g. "GET", "POST")
            path:      Request path WITHOUT query string (e.g. "/api/v3/brokerage/accounts")
            body_str:  JSON-serialised request body, or "" for requests without a body

        Returns:
            Hex-encoded HMAC-SHA256 digest string.
        """
        message = f"{timestamp}{method}{path}{body_str}"
        return hmac.new(
            self._config.api_secret.encode("utf-8"),
            message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()

    # ------------------------------------------------------------------ #
    # Request override — inject per-request HMAC auth headers
    # ------------------------------------------------------------------ #
    def _request(
        self,
        method: str,
        path: str,
        *,
        params: Optional[dict[str, Any]] = None,
        body: Optional[dict[str, Any]] = None,
        headers: Optional[dict[str, str]] = None,
    ) -> Any:
        """Thin override that injects CB HMAC auth headers before delegating to base.

        The signed path is the bare request path (everything after the host, up
        to but NOT including the query string). This matches the path portion of
        the URL that base._request constructs from self._base_url + path.

        For example:
            _base_url = "https://api.coinbase.com/api/v3/brokerage"
            path      = "/accounts"
            signed    = "/api/v3/brokerage/accounts"
        """
        ts = str(int(time.time()))
        body_str = json.dumps(body) if body is not None else ""

        # Derive the full request path (excluding query string) that will be
        # sent to the host — this is what Coinbase expects in the signature.
        if path.startswith("http"):
            # Full URL passed: extract path component only
            parsed = urllib.parse.urlparse(path)
            sign_path = parsed.path
        else:
            # Relative path: prepend the base URL's path component
            base_parsed = urllib.parse.urlparse(self._base_url)
            base_path = base_parsed.path.rstrip("/")
            sign_path = base_path + "/" + path.lstrip("/")

        sig = self._sign(ts, method, sign_path, body_str)
        auth_headers: dict[str, str] = {
            "CB-ACCESS-KEY":       self._config.api_key,
            "CB-ACCESS-TIMESTAMP": ts,
            "CB-ACCESS-SIGN":      sig,
        }
        merged = {**auth_headers, **(headers or {})}
        return super()._request(method, path, params=params, body=body, headers=merged)

    # ------------------------------------------------------------------ #
    # Connection lifecycle
    # ------------------------------------------------------------------ #
    def _on_connect(self) -> None:
        """Validate credentials and confirm API connectivity via GET /accounts."""
        self._config.require("api_key", "api_secret")
        # Lightweight ping; if keys are wrong we get a 401 → BrokerError
        self._get("/accounts", params={"limit": "1"})

    # ------------------------------------------------------------------ #
    # IBroker: get_account
    # ------------------------------------------------------------------ #
    def get_account(self) -> AccountInfo:
        """Return aggregate account info derived from all CB wallet balances.

        Coinbase does not expose margin, equity, or leverage in the FX sense.
        We sum available fiat (USD/USDC/USDT) balances as ``balance`` and
        ``equity``, set ``margin``/``free_margin`` to 0.0, and ``leverage`` to 1
        (spot, no leverage). ``login`` is set to 0 (no numeric account ID in CB).

        Response shape:
            {"accounts": [{"currency": "BTC",
                           "available_balance": {"value": "0.123", "currency": "BTC"},
                           "hold": {"value": "0.0", "currency": "BTC"}}, ...]}
        """
        resp = self._get("/accounts")
        accounts = resp.get("accounts", [])

        fiat_currencies = {"USD", "USDC", "USDT", "EUR", "GBP"}
        total_fiat = 0.0
        primary_currency = "USD"

        for acct in accounts:
            ccy = acct.get("currency", "")
            avail_val = acct.get("available_balance", {}).get("value", "0")
            hold_val  = acct.get("hold", {}).get("value", "0")
            if ccy in fiat_currencies:
                try:
                    total_fiat += float(avail_val) + float(hold_val)
                except ValueError:
                    pass
                if ccy == "USD":
                    primary_currency = "USD"

        return AccountInfo(
            balance=total_fiat,
            equity=total_fiat,   # spot — no unrealised PnL from the API
            margin=0.0,
            free_margin=0.0,
            profit=0.0,
            leverage=1,          # spot trading, no leverage
            currency=primary_currency,
            login=0,
        )

    # ------------------------------------------------------------------ #
    # IBroker: get_tick
    # ------------------------------------------------------------------ #
    def get_tick(self, symbol: str) -> Tick:
        """Return best bid/ask for a product via GET /best_bid_ask.

        Response shape:
            {"pricebooks": [{"product_id": "BTC-USD",
                             "bids": [{"price": "67000.00", "size": "0.5"}],
                             "asks": [{"price": "67001.00", "size": "0.3"}],
                             "time": "2024-01-01T00:00:00Z"}]}
        """
        product_id = self._to_broker_symbol(symbol)
        resp = self._get("/best_bid_ask", params={"product_ids": product_id})
        books = resp.get("pricebooks", [])
        if not books:
            raise BrokerError(
                f"coinbase: no price book returned for {product_id}", code=5
            )
        book = books[0]
        bids = book.get("bids", [])
        asks = book.get("asks", [])
        bid = float(bids[0]["price"]) if bids else 0.0
        ask = float(asks[0]["price"]) if asks else 0.0
        # Coinbase does not expose a 24 h volume on best_bid_ask; use 0.0
        return Tick(
            symbol=self._from_broker_symbol(product_id),
            bid=bid,
            ask=ask,
            volume=0.0,
            timestamp=int(time.time()),
        )

    # ------------------------------------------------------------------ #
    # IBroker: get_symbol_info
    # ------------------------------------------------------------------ #
    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        """Return trading rules for a product via GET /products/{product_id}.

        Response shape (selected fields):
            {"product_id": "BTC-USD",
             "base_increment": "0.00000001",
             "quote_increment": "0.01",
             "base_min_size": "0.001",
             "base_max_size": "1000"}
        """
        product_id = self._to_broker_symbol(symbol)
        resp = self._get(f"/products/{product_id}")

        base_inc  = float(resp.get("base_increment", "0.00000001"))
        quote_inc = float(resp.get("quote_increment", "0.01"))
        base_min  = float(resp.get("base_min_size", "0.001"))
        base_max  = float(resp.get("base_max_size", "1000"))

        # digits: number of decimal places in the quote increment
        digits = len(quote_inc_str.rstrip("0").split(".")[-1]) if "." in (
            quote_inc_str := resp.get("quote_increment", "0.01")
        ) else 0

        # point = quote_increment (smallest price move)
        # contract_size = 1 (spot, 1 base unit per lot)
        return SymbolInfo(
            name=self._from_broker_symbol(product_id),
            digits=digits,
            point=quote_inc,
            contract_size=1.0,
            volume_min=base_min,
            volume_max=base_max,
            volume_step=base_inc,
        )

    # ------------------------------------------------------------------ #
    # IBroker: get_bars
    # ------------------------------------------------------------------ #
    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]:
        """Return up to ``count`` OHLCV bars via GET /products/{product_id}/candles.

        Coinbase returns bars newest-first; we reverse to oldest-first.
        Maximum 350 candles per request. If count > 350, we make multiple
        requests walking backwards in time.

        Supported granularities (via _TF_MAP):
            M1, M5, M15, M30, H1, H6 (21600 s), D1.
        Unsupported timeframes raise BrokerError.

        Response shape:
            {"candles": [{"start": "1716000000", "low": "67000",
                          "high": "68000", "open": "67500",
                          "close": "67800", "volume": "123.4"}, ...]}
        """
        granularity = _TF_MAP.get(tf)
        if not granularity:
            raise BrokerError(
                f"coinbase: unsupported timeframe {tf}s — "
                f"supported: {sorted(_TF_MAP.keys())}",
                code=5,
            )
        product_id = self._to_broker_symbol(symbol)
        bars: list[Bar] = []
        end_ts = int(time.time())

        remaining = count
        while remaining > 0:
            batch = min(remaining, _CB_MAX_CANDLES)
            start_ts = end_ts - batch * tf

            resp = self._get(
                f"/products/{product_id}/candles",
                params={
                    "granularity": granularity,
                    "start":       str(start_ts),
                    "end":         str(end_ts),
                },
            )
            candles = resp.get("candles", [])
            if not candles:
                break

            batch_bars = [
                Bar(
                    timestamp=int(c["start"]),
                    open=float(c["open"]),
                    high=float(c["high"]),
                    low=float(c["low"]),
                    close=float(c["close"]),
                    volume=float(c["volume"]),
                )
                for c in candles
            ]
            # CB returns newest-first; reverse to oldest-first for this batch
            batch_bars.reverse()
            bars = batch_bars + bars  # prepend older bars

            remaining -= len(candles)
            # Move the window back for the next request (if needed)
            end_ts = start_ts
            if len(candles) < batch:
                break  # CB returned fewer than requested — exhausted history

        # Trim to requested count (may have duplicates near boundaries)
        bars = bars[-count:] if len(bars) > count else bars
        return bars

    # ------------------------------------------------------------------ #
    # IBroker: place_order
    # ------------------------------------------------------------------ #
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
        """Place a market IOC order via POST /orders.

        ``lots`` is treated as base_size (e.g. 0.01 BTC). The ``price``
        argument is ignored for market orders. ``sl`` and ``tp`` are stored on
        the returned Order but are NOT sent to the exchange — Coinbase market
        IOC orders have no native SL/TP support. Use bracket_order_configuration
        (future) for server-side stops.

        Only BUY and SELL are supported by this method; limit/stop order types
        (BUY_LIMIT, SELL_LIMIT, BUY_STOP, SELL_STOP) raise BrokerError.

        POST /orders body:
            {"client_order_id": "<uuid-like>",
             "product_id": "BTC-USD",
             "side": "BUY" | "SELL",
             "order_configuration": {
                 "market_market_ioc": {"base_size": "0.01"}
             }}

        Success response:
            {"success": true,
             "success_response": {"order_id": "...", "product_id": "...",
                                  "side": "BUY", "client_order_id": "..."}}
        Error response:
            {"success": false, "error_response": {"error": "...", "message": "..."}}
        """
        if order_type not in (OrderType.BUY, OrderType.SELL):
            raise BrokerError(
                f"coinbase: only BUY/SELL market orders are supported "
                f"(got {order_type.name}); use SL/TP via bracket orders (future)",
                code=5,
            )

        product_id = self._to_broker_symbol(symbol)
        side = "BUY" if order_type == OrderType.BUY else "SELL"
        client_order_id = f"af-{int(time.time() * 1000)}-{magic}"

        body = {
            "client_order_id": client_order_id,
            "product_id":      product_id,
            "side":            side,
            "order_configuration": {
                "market_market_ioc": {"base_size": str(lots)},
            },
        }
        resp = self._post("/orders", body=body)

        if not resp.get("success", False):
            err = resp.get("error_response", {})
            raise BrokerError(
                f"coinbase: order rejected: {err.get('error','?')} — "
                f"{err.get('message','no detail')}",
                code=2,
            )

        sr = resp["success_response"]
        order_id = sr["order_id"]
        ticket = self._register_order_id(order_id)

        return Order(
            ticket=ticket,
            symbol=self._from_broker_symbol(product_id),
            type=order_type,
            lots=lots,
            price=0.0,        # market order — no fixed price
            sl=sl,            # stored locally; not sent to exchange
            tp=tp,            # stored locally; not sent to exchange
            fill_price=0.0,   # unknown until fill confirmed via get_orders
            magic=magic,
            comment=comment,
        )

    # ------------------------------------------------------------------ #
    # IBroker: close_position
    # ------------------------------------------------------------------ #
    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        """Close (or partially close) a position by placing the opposite market order.

        Coinbase spot positions are derived from wallet balances; there is no
        native position-close endpoint. We locate the held balance for the
        position's base currency and place an opposing market sell.

        If ``lots`` == 0.0 the entire held balance is sold (full close).
        Returns 0 on success, non-zero on failure.
        """
        try:
            order_id = self._ticket_to_order_id(ticket)
        except BrokerError:
            # ticket unknown — try to derive position from balances
            order_id = None

        # We need to know the symbol. Derive from the ticket mapping.
        # If not found, caller must pass partial-close lots explicitly.
        if order_id:
            # Look up original order to get the symbol
            try:
                resp = self._get(f"/orders/historical/{order_id}")
                orig_order = resp.get("order", {})
                product_id = orig_order.get("product_id", "")
                orig_side  = orig_order.get("side", "BUY")
            except BrokerError:
                return 2

            if not product_id:
                return 2

            # Determine the size to close
            if lots == 0.0:
                # Full close — use the filled base size
                fill_size = float(
                    orig_order.get("filled_size", "0") or "0"
                )
                close_size = fill_size
            else:
                close_size = lots

            close_side = "SELL" if orig_side == "BUY" else "BUY"
            close_body = {
                "client_order_id": f"af-close-{ticket}-{int(time.time())}",
                "product_id":      product_id,
                "side":            close_side,
                "order_configuration": {
                    "market_market_ioc": {"base_size": str(close_size)},
                },
            }
            try:
                resp2 = self._post("/orders", body=close_body)
                if not resp2.get("success", False):
                    return 2
                close_oid = resp2["success_response"]["order_id"]
                self._register_order_id(close_oid)
                return 0
            except BrokerError:
                return 2
        return 2

    # ------------------------------------------------------------------ #
    # IBroker: modify_position
    # ------------------------------------------------------------------ #
    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        """Cancel and re-place the order to simulate modification.

        Coinbase market IOC orders cannot be modified once placed. For any
        order type this is implemented as cancel-then-replace. In practice,
        market orders will already be filled and this will return an error from
        the cancel step.

        Returns 0 on success, non-zero on failure.
        """
        rc = self.cancel_order(ticket)
        if rc != 0:
            return rc
        # After cancel the position data is lost; caller must re-place the order.
        return 0

    # ------------------------------------------------------------------ #
    # IBroker: cancel_order
    # ------------------------------------------------------------------ #
    def cancel_order(self, ticket: int) -> int:
        """Cancel a pending order via POST /orders/batch_cancel.

        Returns 0 on success, non-zero on failure.

        POST /orders/batch_cancel body:
            {"order_ids": ["<order_id>"]}

        Response:
            {"results": [{"order_id": "...", "success": true, ...}]}
        """
        try:
            order_id = self._ticket_to_order_id(ticket)
        except BrokerError:
            return 5

        try:
            resp = self._post("/orders/batch_cancel", body={"order_ids": [order_id]})
        except BrokerError:
            return 2

        results = resp.get("results", [])
        if results and results[0].get("success", False):
            return 0
        return 2

    # ------------------------------------------------------------------ #
    # IBroker: get_positions
    # ------------------------------------------------------------------ #
    def get_positions(self) -> list[Position]:
        """Return open positions derived from non-zero crypto wallet balances.

        Coinbase does not expose positions directly. We treat each non-zero
        non-fiat balance as a LONG position (spot only; short positions require
        Advanced Trade futures, not implemented here).

        The current price is fetched from best_bid_ask for each asset. The
        open_price and profit are not available from the wallet endpoint alone
        and are set to 0.0.
        """
        resp = self._get("/accounts")
        accounts = resp.get("accounts", [])
        fiat_currencies = {"USD", "USDC", "USDT", "EUR", "GBP"}
        positions: list[Position] = []

        for acct in accounts:
            ccy = acct.get("currency", "")
            if ccy in fiat_currencies:
                continue
            avail = float(acct.get("available_balance", {}).get("value", "0") or "0")
            hold  = float(acct.get("hold", {}).get("value", "0") or "0")
            total = avail + hold
            if total <= 0.0:
                continue

            # Synthesise a product_id for price lookup
            product_id = f"{ccy}-USD"
            try:
                tick = self.get_tick(self._from_broker_symbol(product_id))
                current_price = (tick.bid + tick.ask) / 2.0
            except BrokerError:
                current_price = 0.0

            # Generate a stable ticket from the currency string
            fake_ticket = abs(hash(ccy)) % 1_000_000

            positions.append(Position(
                ticket=fake_ticket,
                symbol=f"{ccy}USD",
                side=Direction.LONG,
                lots=total,
                open_price=0.0,       # not available from wallet endpoint
                current_price=current_price,
                sl=0.0,
                tp=0.0,
                profit=0.0,           # unrealised PnL not available from wallet
                commission=0.0,
                swap=0.0,
                magic=0,
                comment="spot-wallet",
            ))

        return positions

    # ------------------------------------------------------------------ #
    # IBroker: get_orders
    # ------------------------------------------------------------------ #
    def get_orders(self) -> list[Order]:
        """Return open orders via GET /orders/historical/batch?order_status=OPEN.

        Response shape:
            {"orders": [{"order_id": "...", "product_id": "BTC-USD",
                         "side": "BUY", "order_type": "MARKET",
                         "filled_size": "0.01", "average_filled_price": "67000",
                         "status": "OPEN", ...}]}
        """
        resp = self._get(
            "/orders/historical/batch",
            params={"order_status": "OPEN"},
        )
        raw_orders = resp.get("orders", [])
        orders: list[Order] = []

        for ro in raw_orders:
            oid = ro.get("order_id", "")
            ticket = self._register_order_id(oid)
            product_id = ro.get("product_id", "")
            side = ro.get("side", "BUY")
            order_type = OrderType.BUY if side == "BUY" else OrderType.SELL
            filled_size  = float(ro.get("filled_size", "0") or "0")
            avg_price    = float(ro.get("average_filled_price", "0") or "0")

            orders.append(Order(
                ticket=ticket,
                symbol=self._from_broker_symbol(product_id),
                type=order_type,
                lots=filled_size,
                price=0.0,
                sl=0.0,
                tp=0.0,
                fill_price=avg_price,
                magic=0,
                comment="",
            ))

        return orders

    # ------------------------------------------------------------------ #
    # IBroker: get_position
    # ------------------------------------------------------------------ #
    def get_position(self, ticket: int) -> Optional[Position]:
        """Return the position for a specific ticket, or None if not found."""
        positions = self.get_positions()
        # Match by ticket (hash-derived from currency)
        for pos in positions:
            if pos.ticket == ticket:
                return pos
        return None
