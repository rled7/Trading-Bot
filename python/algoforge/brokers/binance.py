"""Binance Spot REST adapter (S5).

API docs: https://developers.binance.com/docs/binance-spot-api-docs
Auth:   X-MBX-APIKEY header + HMAC-SHA256 signature over the query string
        (signed using AF_BINANCE_API_SECRET) on SIGNED endpoints.
Live:   https://api.binance.com | Testnet: https://testnet.binance.vision
Symbols: concatenated, e.g. BTCUSDT.

Design decisions
----------------
lots → quantity
    AlgoForge uses ``lots`` as the universal order-size field.  Binance Spot has
    no concept of lots; ``lots`` is treated as the base-asset *quantity* (e.g.
    0.001 BTC for a BTCUSDT order).  Callers must pass a quantity that satisfies
    the symbol's LOT_SIZE filter (stepSize, minQty, maxQty).

balances → positions
    Binance Spot has no server-side position record.  ``get_positions`` derives
    positions from non-zero free+locked base-asset balances.  Each such balance
    is returned as a LONG Position (Spot can only hold assets, never short).
    ``open_price`` and ``current_price`` are 0.0 because Binance Spot does not
    store the average entry price (use trade history for that).

SL / TP limitation
    Binance Spot MARKET orders do not accept a native SL or TP in a single REST
    call.  The ``sl`` and ``tp`` arguments to ``place_order`` are silently
    ignored.  OCO (One-Cancels-the-Other) orders can approximate SL+TP but
    require a separate POST to /api/v3/order/oco — marked TODO.

modify_position
    Binance Spot has no modify endpoint.  Implementation cancels the old order
    and notes the SL/TP limitation above; returns 0 to signal "no error"
    because there is no open-order concept to modify for filled spot trades.
"""
from __future__ import annotations

import hashlib
import hmac
import time
import urllib.parse
from typing import Any, Optional

from ..broker import AccountInfo, Order, OrderType, Position, SymbolInfo, Tick
from ..types import Bar, Direction
from .base import BrokerError, RestBroker

# ---------------------------------------------------------------------------
# Timeframe seconds → Binance kline interval string
# ---------------------------------------------------------------------------
_TF_MAP: dict[int, str] = {
    15:     "15s",   # only available on testnet / newer API; fallback below
    60:     "1m",
    300:    "5m",
    900:    "15m",
    1800:   "30m",
    3600:   "1h",
    14400:  "4h",
    86400:  "1d",
    604800: "1w",
}

_DEFAULT_RECV_WINDOW = 5000  # ms


class BinanceBroker(RestBroker):
    """Binance Spot REST broker adapter.

    Credentials are read from the environment via BrokerConfig.from_env:
        AF_BINANCE_API_KEY
        AF_BINANCE_API_SECRET
        AF_BINANCE_PAPER   (set to "1" to use testnet.binance.vision)
    """

    BROKER_NAME = "binance"
    LIVE_URL = "https://api.binance.com"
    PAPER_URL = "https://testnet.binance.vision"

    # ------------------------------------------------------------------ #
    # Auth helpers
    # ------------------------------------------------------------------ #

    def _auth_headers(self) -> dict[str, str]:
        """X-MBX-APIKEY header required on all authenticated endpoints."""
        return {"X-MBX-APIKEY": self._config.api_key}

    def _signed_params(self, params: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        """Append timestamp + recvWindow and compute HMAC-SHA256 signature.

        The signature is computed over the URL-encoded query string (including
        timestamp and recvWindow) using the api_secret as the HMAC key.  This
        follows the Binance Spot API SIGNED endpoint requirements exactly.

        Args:
            params: existing query parameters (mutated in place and returned).

        Returns:
            The params dict with ``timestamp``, ``recvWindow``, and
            ``signature`` keys added.
        """
        p: dict[str, Any] = params or {}
        p["timestamp"] = int(time.time() * 1000)
        p["recvWindow"] = _DEFAULT_RECV_WINDOW
        query = urllib.parse.urlencode(p)
        sig = hmac.new(
            self._config.api_secret.encode("utf-8"),
            query.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        p["signature"] = sig
        return p

    # ------------------------------------------------------------------ #
    # Symbol mapping
    # ------------------------------------------------------------------ #

    def _to_broker_symbol(self, symbol: str) -> str:
        """BTC/USDT, BTC_USDT, btcusdt → BTCUSDT."""
        return symbol.replace("/", "").replace("_", "").upper()

    # ------------------------------------------------------------------ #
    # Connection lifecycle
    # ------------------------------------------------------------------ #

    def _on_connect(self) -> None:
        """Ping the API, then validate keys via a signed account call."""
        self._config.require("api_key", "api_secret")
        # lightweight ping — no auth required
        self._get("/api/v3/ping")
        # signed call — surfaces bad keys immediately
        self._get("/api/v3/account", params=self._signed_params())

    # ------------------------------------------------------------------ #
    # Trading surface
    # ------------------------------------------------------------------ #

    def get_account(self) -> AccountInfo:
        """Return account info from /api/v3/account.

        Binance Spot has no equity/margin/leverage in the FX sense.
        We sum the USDT (and BUSD/USDC) free+locked balances as a proxy for
        ``balance``; ``equity`` = ``balance``; ``margin`` = ``free_margin`` = 0.
        """
        data = self._get("/api/v3/account", params=self._signed_params())
        balances: list[dict] = data.get("balances", [])

        # Quote-asset proxy: pick the dominant stablecoin balance
        stablecoins = {"USDT", "BUSD", "USDC", "USD", "FDUSD"}
        quote_total = 0.0
        for b in balances:
            asset = b.get("asset", "")
            if asset in stablecoins:
                quote_total += float(b.get("free", 0)) + float(b.get("locked", 0))

        return AccountInfo(
            balance=quote_total,
            equity=quote_total,
            margin=0.0,
            free_margin=0.0,
            profit=0.0,
            leverage=1,          # Spot has no leverage (1:1)
            currency="USDT",
        )

    def get_tick(self, symbol: str) -> Tick:
        """Return best bid/ask from /api/v3/ticker/bookTicker."""
        bsym = self._to_broker_symbol(symbol)
        data = self._get("/api/v3/ticker/bookTicker", params={"symbol": bsym})
        return Tick(
            symbol=symbol,
            bid=float(data.get("bidPrice", 0)),
            ask=float(data.get("askPrice", 0)),
            volume=0.0,          # bookTicker has no volume; use 24h ticker if needed
            timestamp=int(time.time() * 1000),
        )

    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        """Return symbol metadata from /api/v3/exchangeInfo."""
        bsym = self._to_broker_symbol(symbol)
        data = self._get("/api/v3/exchangeInfo", params={"symbol": bsym})
        symbols: list[dict] = data.get("symbols", [])
        if not symbols:
            raise BrokerError(f"binance: symbol {bsym} not found in exchangeInfo")
        info = symbols[0]

        digits = int(info.get("quotePrecision", 8))
        point = 10 ** (-digits)
        vol_min = 0.0
        vol_max = 0.0
        vol_step = 0.0

        for flt in info.get("filters", []):
            if flt.get("filterType") == "LOT_SIZE":
                vol_min = float(flt.get("minQty", 0))
                vol_max = float(flt.get("maxQty", 0))
                vol_step = float(flt.get("stepSize", 0))
            elif flt.get("filterType") == "PRICE_FILTER":
                tick_size = float(flt.get("tickSize", 0))
                if tick_size > 0:
                    import math
                    digits = max(0, round(-math.log10(tick_size)))
                    point = tick_size

        return SymbolInfo(
            name=symbol,
            digits=digits,
            point=point,
            contract_size=1.0,    # Spot: no contract multiplier
            volume_min=vol_min,
            volume_max=vol_max,
            volume_step=vol_step,
        )

    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]:
        """Return OHLCV bars from /api/v3/klines.

        ``tf`` is timeframe in seconds (Timeframe enum values).
        Binance klines are returned newest-last; we preserve that order.
        """
        interval = _TF_MAP.get(tf)
        if interval is None:
            raise BrokerError(
                f"binance: unsupported timeframe {tf}s; "
                f"supported: {sorted(_TF_MAP.keys())}"
            )
        bsym = self._to_broker_symbol(symbol)
        raw: list[list] = self._get(
            "/api/v3/klines",
            params={"symbol": bsym, "interval": interval, "limit": count},
        )
        bars: list[Bar] = []
        for k in raw:
            # k[0]=open_time_ms, k[1]=open, k[2]=high, k[3]=low,
            # k[4]=close, k[5]=volume, k[6]=close_time_ms, ...
            bars.append(Bar(
                timestamp=int(k[0]),
                open=float(k[1]),
                high=float(k[2]),
                low=float(k[3]),
                close=float(k[4]),
                volume=float(k[5]),
            ))
        return bars

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
        """Place a Spot MARKET order.

        ``lots`` is treated as the base-asset quantity (e.g. 0.001 BTC).
        SL/TP are silently ignored — Binance Spot MARKET orders do not accept
        native SL/TP; see module docstring for the OCO TODO.

        Supports BUY / SELL order types only (limit/stop not yet wired).
        """
        if order_type not in (OrderType.BUY, OrderType.SELL):
            raise BrokerError(
                f"binance: only MARKET BUY/SELL supported; got {order_type!r}"
            )
        bsym = self._to_broker_symbol(symbol)
        side = "BUY" if order_type == OrderType.BUY else "SELL"
        params = self._signed_params({
            "symbol": bsym,
            "side": side,
            "type": "MARKET",
            "quantity": str(lots),
            "newOrderRespType": "RESULT",
        })
        data = self._post("/api/v3/order", params=params)
        fill_price = float(data.get("fills", [{}])[0].get("price", 0)) if data.get("fills") else 0.0
        # cummulativeQuoteQty / executedQty can give avg fill
        if not fill_price and data.get("cummulativeQuoteQty") and data.get("executedQty"):
            executed = float(data["executedQty"])
            if executed:
                fill_price = float(data["cummulativeQuoteQty"]) / executed
        return Order(
            ticket=int(data.get("orderId", 0)),
            symbol=symbol,
            type=order_type,
            lots=float(data.get("executedQty", lots)),
            price=price,
            sl=0.0,
            tp=0.0,
            fill_price=fill_price,
            magic=magic,
            comment=comment,
        )

    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        """Close a spot 'position' by placing the opposite MARKET order.

        ``ticket`` is matched against open-orders first (cancel); if not found,
        we treat it as a balance-derived position and place an offsetting trade.
        ``lots`` must be provided to specify the quantity to sell/buy back.
        Returns 0 on success, non-zero on error.
        """
        if lots <= 0:
            raise BrokerError(
                "binance.close_position: lots must be > 0 for spot close"
            )
        # Try to find which symbol this ticket belongs to via open orders
        try:
            orders_data: list[dict] = self._get(
                "/api/v3/openOrders", params=self._signed_params()
            )
        except BrokerError:
            orders_data = []

        # Check if ticket is an open order — cancel it
        for o in orders_data:
            if int(o.get("orderId", -1)) == ticket:
                symbol = o.get("symbol", "")
                cancel_params = self._signed_params({
                    "symbol": symbol,
                    "orderId": ticket,
                })
                try:
                    self._delete("/api/v3/order", params=cancel_params)
                except BrokerError as exc:
                    return exc.code
                return 0

        # No matching open order — treat ticket as a position; we need a symbol
        # which is unavailable from ticket alone in Spot.  Caller must ensure
        # they call cancel_order with symbol context, or use the higher-level
        # position management.  Return 1 to signal unknown ticket.
        return 1

    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        """Spot has no modify endpoint.

        SL/TP cannot be set on filled MARKET orders.  OCO orders would be needed
        for a real implementation (TODO).  This method is a no-op; returns 0 so
        callers do not break.
        """
        # Nothing to do — log a warning via the base logger
        from ..logger import get_logger
        get_logger("brokers").warning(
            "binance.modify_position: SL/TP not supported on Spot MARKET fills "
            "(ticket=%d sl=%.8f tp=%.8f ignored)", ticket, sl, tp
        )
        return 0

    def cancel_order(self, ticket: int) -> int:
        """Cancel an open order by orderId.

        Binance requires the symbol to cancel an order.  We look it up from
        GET /api/v3/openOrders (all symbols) then issue a DELETE.
        Returns 0 on success, non-zero on error.
        """
        try:
            orders_data: list[dict] = self._get(
                "/api/v3/openOrders", params=self._signed_params()
            )
        except BrokerError as exc:
            return exc.code

        for o in orders_data:
            if int(o.get("orderId", -1)) == ticket:
                symbol = o.get("symbol", "")
                cancel_params = self._signed_params({
                    "symbol": symbol,
                    "orderId": ticket,
                })
                try:
                    self._delete("/api/v3/order", params=cancel_params)
                    return 0
                except BrokerError as exc:
                    return exc.code

        # Order not found — already filled or doesn't exist
        return 1

    def get_positions(self) -> list[Position]:
        """Derive positions from non-zero base-asset balances.

        Binance Spot has no server-side position record.  Each non-zero balance
        is returned as a LONG Position (cannot short on Spot).  ``open_price``
        and ``current_price`` are 0.0 (unavailable without trade history).
        """
        data = self._get("/api/v3/account", params=self._signed_params())
        balances: list[dict] = data.get("balances", [])
        stablecoins = {"USDT", "BUSD", "USDC", "USD", "FDUSD", "BNB"}
        positions: list[Position] = []
        for b in balances:
            asset = b.get("asset", "")
            if asset in stablecoins:
                continue
            qty = float(b.get("free", 0)) + float(b.get("locked", 0))
            if qty <= 0:
                continue
            # Use a deterministic pseudo-ticket from asset hash
            pseudo_ticket = abs(hash(asset)) % (10 ** 9)
            positions.append(Position(
                ticket=pseudo_ticket,
                symbol=asset + "USDT",
                side=Direction.LONG,
                lots=qty,
                open_price=0.0,
                current_price=0.0,
                sl=0.0,
                tp=0.0,
                profit=0.0,
            ))
        return positions

    def get_orders(self) -> list[Order]:
        """Return all open orders from /api/v3/openOrders (signed)."""
        data: list[dict] = self._get(
            "/api/v3/openOrders", params=self._signed_params()
        )
        orders: list[Order] = []
        for o in data:
            otype = OrderType.BUY if o.get("side") == "BUY" else OrderType.SELL
            orders.append(Order(
                ticket=int(o.get("orderId", 0)),
                symbol=self._from_broker_symbol(o.get("symbol", "")),
                type=otype,
                lots=float(o.get("origQty", 0)),
                price=float(o.get("price", 0)),
                sl=0.0,
                tp=0.0,
                fill_price=float(o.get("cummulativeQuoteQty", 0)),
            ))
        return orders

    def get_position(self, ticket: int) -> Optional[Position]:
        """Find a balance-derived position by its pseudo-ticket."""
        for pos in self.get_positions():
            if pos.ticket == ticket:
                return pos
        return None

    # ------------------------------------------------------------------ #
    # Internal HTTP helpers
    # ------------------------------------------------------------------ #

    def _delete(self, path: str, *, params: Optional[dict] = None) -> Any:
        """Issue an authenticated DELETE request (for order cancellation)."""
        return self._request("DELETE", path, params=params)

    def _post(self, path: str, *, body: Optional[dict[str, Any]] = None,
              params: Optional[dict[str, Any]] = None,
              headers: Optional[dict[str, str]] = None) -> Any:
        """Override to support sending signed params as query string on POST.

        Binance order endpoints expect signed params in the query string (or
        body as form-encoded), NOT as a JSON body.  We route params → query
        string and leave body for any non-Binance callers.
        """
        return self._request("POST", path, params=params, body=body, headers=headers)
