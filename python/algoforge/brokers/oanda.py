"""OANDA v20 REST adapter.

API docs: https://developer.oanda.com/rest-live-v20/introduction/
Auth:   Bearer token (AF_OANDA_API_KEY), account-scoped (AF_OANDA_ACCOUNT).
Symbols: underscore form, e.g. EUR_USD.

All 14 IBroker methods implemented.
"""
from __future__ import annotations

from typing import Optional

from ..broker import AccountInfo, Order, OrderType, Position, SymbolInfo, Tick
from ..types import Bar, Direction
from .base import BrokerError, RestBroker

# Seconds -> OANDA granularity string
_TF_MAP: dict[int, str] = {
    5:      "S5",
    10:     "S10",
    15:     "S15",
    30:     "S30",
    60:     "M1",
    120:    "M2",
    180:    "M4",
    300:    "M5",
    600:    "M10",
    900:    "M15",
    1200:   "M30",   # non-standard mapping, best-effort
    1800:   "M30",
    3600:   "H1",
    7200:   "H2",
    10800:  "H3",
    14400:  "H4",
    21600:  "H6",
    28800:  "H8",
    43200:  "H12",
    86400:  "D",
    604800: "W",
    2592000: "M",
}

# Default FX contract size when the instrument endpoint doesn't specify it
_FX_CONTRACT_SIZE = 100_000.0


def _parse_ts(ts_str: str) -> int:
    """Parse OANDA RFC3339 nanosecond timestamps -> unix epoch int.

    OANDA returns strings like ``"2016-10-05T14:14:00.000000000Z"``.
    datetime.fromisoformat cannot handle >6 fractional digits before Python 3.11
    so we normalise manually.
    """
    import datetime
    s = ts_str.rstrip("Z")
    # Truncate fractional seconds to 6 digits (microseconds)
    if "." in s:
        base, frac = s.split(".", 1)
        s = f"{base}.{frac[:6]}"
    dt = datetime.datetime.fromisoformat(s)
    dt = dt.replace(tzinfo=datetime.timezone.utc)
    return int(dt.timestamp())


class OandaBroker(RestBroker):
    BROKER_NAME = "oanda"
    LIVE_URL = "https://api-fxtrade.oanda.com/v3"
    PAPER_URL = "https://api-fxpractice.oanda.com/v3"

    # ------------------------------------------------------------------ #
    # Connection lifecycle
    # ------------------------------------------------------------------ #
    def _on_connect(self) -> None:
        self._config.require("api_key", "account_id")

    def _auth_headers(self) -> dict[str, str]:
        return {"Authorization": f"Bearer {self._config.api_key}"}

    # ------------------------------------------------------------------ #
    # Symbol mapping  (EURUSD / EUR/USD -> EUR_USD)
    # ------------------------------------------------------------------ #
    def _to_broker_symbol(self, symbol: str) -> str:
        s = symbol.replace("/", "").replace("_", "")
        return f"{s[:3]}_{s[3:]}" if len(s) == 6 else symbol

    def _from_broker_symbol(self, symbol: str) -> str:
        return symbol.replace("_", "")

    # ------------------------------------------------------------------ #
    # Convenience
    # ------------------------------------------------------------------ #
    def _acct_path(self, path: str = "") -> str:
        """Return the account-scoped path prefix (optionally with sub-path)."""
        base = f"accounts/{self._config.account_id}"
        return f"{base}/{path.lstrip('/')}" if path else base

    # ------------------------------------------------------------------ #
    # Account
    # ------------------------------------------------------------------ #
    def get_account(self) -> AccountInfo:
        data = self._get(self._acct_path("summary"))
        acct = data["account"]
        margin_rate = float(acct.get("marginRate", "0.02") or "0.02")
        leverage = round(1.0 / margin_rate) if margin_rate else 100
        # OANDA account id is a dashed string; use 0 as numeric login
        return AccountInfo(
            balance=float(acct.get("balance", 0)),
            equity=float(acct.get("NAV", acct.get("balance", 0))),
            margin=float(acct.get("marginUsed", 0)),
            free_margin=float(acct.get("marginAvailable", 0)),
            profit=float(acct.get("unrealizedPL", 0)),
            leverage=leverage,
            currency=acct.get("currency", "USD"),
            login=0,
        )

    # ------------------------------------------------------------------ #
    # Tick
    # ------------------------------------------------------------------ #
    def get_tick(self, symbol: str) -> Tick:
        instrument = self._to_broker_symbol(symbol)
        data = self._get(self._acct_path("pricing"),
                         params={"instruments": instrument})
        prices = data["prices"]
        if not prices:
            raise BrokerError(f"oanda: no pricing data for {instrument}")
        price = prices[0]
        bids = price.get("bids") or [{"price": "0"}]
        asks = price.get("asks") or [{"price": "0"}]
        bid = float(bids[0]["price"])
        ask = float(asks[0]["price"])
        ts_str = price.get("time", "")
        timestamp = _parse_ts(ts_str) if ts_str else 0
        return Tick(
            symbol=symbol,
            bid=bid,
            ask=ask,
            volume=0.0,       # OANDA pricing endpoint does not provide volume
            timestamp=timestamp,
        )

    # ------------------------------------------------------------------ #
    # Symbol info
    # ------------------------------------------------------------------ #
    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        instrument = self._to_broker_symbol(symbol)
        data = self._get(self._acct_path("instruments"),
                         params={"instruments": instrument})
        instruments = data.get("instruments", [])
        if not instruments:
            raise BrokerError(f"oanda: no instrument data for {instrument}")
        inst = instruments[0]
        # displayPrecision gives the correct number of decimal places
        digits = int(inst.get("displayPrecision", 5))
        point = 10 ** (-digits)
        # minimumTradeSize / maximumOrderUnits are in *units*; convert to lots
        min_units = float(inst.get("minimumTradeSize", 1))
        max_units = float(inst.get("maximumOrderUnits", 100_000_000))
        contract_size = _FX_CONTRACT_SIZE
        volume_min = min_units / contract_size
        volume_max = max_units / contract_size
        volume_step = volume_min      # smallest increment == minimum
        return SymbolInfo(
            name=symbol,
            digits=digits,
            point=point,
            contract_size=contract_size,
            volume_min=volume_min,
            volume_max=volume_max,
            volume_step=volume_step,
        )

    # ------------------------------------------------------------------ #
    # Bars
    # ------------------------------------------------------------------ #
    def get_bars(self, symbol: str, tf: int, count: int) -> list[Bar]:
        instrument = self._to_broker_symbol(symbol)
        granularity = _TF_MAP.get(tf, "M1")
        data = self._get(f"instruments/{instrument}/candles",
                         params={"granularity": granularity, "count": count,
                                 "price": "M"})   # midpoint candles
        candles = data.get("candles", [])
        bars: list[Bar] = []
        for c in candles:
            mid = c.get("mid", {})
            ts_str = c.get("time", "")
            timestamp = _parse_ts(ts_str) if ts_str else 0
            bars.append(Bar(
                timestamp=timestamp,
                open=float(mid.get("o", 0)),
                high=float(mid.get("h", 0)),
                low=float(mid.get("l", 0)),
                close=float(mid.get("c", 0)),
                volume=float(c.get("volume", 0)),
            ))
        return bars

    # ------------------------------------------------------------------ #
    # Orders
    # ------------------------------------------------------------------ #
    def place_order(self, symbol: str, order_type: OrderType,
                    lots: float, price: float = 0.0,
                    sl: float = 0.0, tp: float = 0.0,
                    magic: int = 0, comment: str = "") -> Order:
        instrument = self._to_broker_symbol(symbol)
        # Positive units = buy, negative = sell
        units_int = round(lots * _FX_CONTRACT_SIZE)
        if order_type in (OrderType.SELL, OrderType.SELL_LIMIT,
                          OrderType.SELL_STOP):
            units_int = -units_int
        units_str = str(units_int)

        if order_type in (OrderType.BUY, OrderType.SELL):
            order_spec: dict = {
                "type": "MARKET",
                "instrument": instrument,
                "units": units_str,
                "timeInForce": "FOK",
                "positionFill": "DEFAULT",
            }
        elif order_type in (OrderType.BUY_LIMIT, OrderType.SELL_LIMIT):
            order_spec = {
                "type": "LIMIT",
                "instrument": instrument,
                "units": units_str,
                "price": f"{price:.5f}",
                "timeInForce": "GTC",
                "positionFill": "DEFAULT",
            }
        elif order_type in (OrderType.BUY_STOP, OrderType.SELL_STOP):
            order_spec = {
                "type": "STOP",
                "instrument": instrument,
                "units": units_str,
                "price": f"{price:.5f}",
                "timeInForce": "GTC",
                "positionFill": "DEFAULT",
            }
        else:
            raise BrokerError(f"oanda: unsupported order type {order_type}")

        if sl > 0:
            order_spec["stopLossOnFill"] = {"price": f"{sl:.5f}",
                                            "timeInForce": "GTC"}
        if tp > 0:
            order_spec["takeProfitOnFill"] = {"price": f"{tp:.5f}",
                                              "timeInForce": "GTC"}

        data = self._post(self._acct_path("orders"),
                          body={"order": order_spec})

        # Market orders populate orderFillTransaction; limit/stop use
        # orderCreateTransaction and become pending.
        fill_tx = data.get("orderFillTransaction")
        create_tx = data.get("orderCreateTransaction")

        if fill_tx:
            trade_opened = fill_tx.get("tradeOpened", {})
            ticket = int(trade_opened.get("tradeID", 0))
            fill_price = float(fill_tx.get("price", 0))
        elif create_tx:
            ticket = int(create_tx.get("id", 0))
            fill_price = 0.0
        else:
            raise BrokerError(
                f"oanda: unexpected order response: {data}", code=2)

        return Order(
            ticket=ticket,
            symbol=symbol,
            type=order_type,
            lots=lots,
            price=price,
            sl=sl,
            tp=tp,
            fill_price=fill_price,
            magic=magic,
            comment=comment,
        )

    # ------------------------------------------------------------------ #
    # Close / modify / cancel
    # ------------------------------------------------------------------ #
    def close_position(self, ticket: int, lots: float = 0.0) -> int:
        """Close a trade by tradeID. Pass lots > 0 to partially close."""
        path = self._acct_path(f"trades/{ticket}/close")
        body: Optional[dict] = None
        if lots > 0:
            units = round(lots * _FX_CONTRACT_SIZE)
            body = {"units": str(units)}
        try:
            self._request("PUT", path, body=body)
            return 0
        except BrokerError:
            return 1

    def modify_position(self, ticket: int, sl: float, tp: float) -> int:
        """Update SL/TP on an open trade via /trades/{id}/orders."""
        path = self._acct_path(f"trades/{ticket}/orders")
        body: dict = {}
        if sl > 0:
            body["stopLoss"] = {"price": f"{sl:.5f}", "timeInForce": "GTC"}
        if tp > 0:
            body["takeProfit"] = {"price": f"{tp:.5f}", "timeInForce": "GTC"}
        try:
            self._request("PUT", path, body=body)
            return 0
        except BrokerError:
            return 1

    def cancel_order(self, ticket: int) -> int:
        """Cancel a pending order by orderID."""
        path = self._acct_path(f"orders/{ticket}/cancel")
        try:
            self._request("PUT", path)
            return 0
        except BrokerError:
            return 1

    # ------------------------------------------------------------------ #
    # Positions
    # ------------------------------------------------------------------ #
    def get_positions(self) -> list[Position]:
        data = self._get(self._acct_path("openTrades"))
        trades = data.get("trades", [])
        return [self._trade_to_position(t) for t in trades]

    def get_position(self, ticket: int) -> Optional[Position]:
        for pos in self.get_positions():
            if pos.ticket == ticket:
                return pos
        return None

    @staticmethod
    def _trade_to_position(trade: dict) -> Position:
        current_units = float(trade.get("currentUnits", 0))
        side = Direction.LONG if current_units > 0 else Direction.SHORT
        # Lots from absolute units
        lots = abs(current_units) / _FX_CONTRACT_SIZE
        instrument = trade.get("instrument", "")
        symbol = instrument.replace("_", "")
        sl_order = trade.get("stopLossOrder", {}) or {}
        tp_order = trade.get("takeProfitOrder", {}) or {}
        return Position(
            ticket=int(trade.get("id", 0)),
            symbol=symbol,
            side=side,
            lots=lots,
            open_price=float(trade.get("price", 0)),
            current_price=float(trade.get("currentPrice",
                                         trade.get("price", 0))),
            sl=float(sl_order.get("price", 0)) if sl_order else 0.0,
            tp=float(tp_order.get("price", 0)) if tp_order else 0.0,
            profit=float(trade.get("unrealizedPL", 0)),
            commission=float(trade.get("financing", 0)),
            swap=0.0,
            magic=0,
            comment=trade.get("clientExtensions", {}).get("comment", "")
                     if trade.get("clientExtensions") else "",
        )

    # ------------------------------------------------------------------ #
    # Pending orders
    # ------------------------------------------------------------------ #
    def get_orders(self) -> list[Order]:
        data = self._get(self._acct_path("pendingOrders"))
        orders_raw = data.get("orders", [])
        result: list[Order] = []
        for o in orders_raw:
            result.append(self._raw_order_to_order(o))
        return result

    @staticmethod
    def _raw_order_to_order(o: dict) -> Order:
        oanda_type = o.get("type", "MARKET")
        units = float(o.get("units", 0))
        is_sell = units < 0
        if oanda_type == "LIMIT":
            order_type = OrderType.SELL_LIMIT if is_sell else OrderType.BUY_LIMIT
        elif oanda_type == "STOP":
            order_type = OrderType.SELL_STOP if is_sell else OrderType.BUY_STOP
        else:
            order_type = OrderType.SELL if is_sell else OrderType.BUY

        instrument = o.get("instrument", "")
        symbol = instrument.replace("_", "")
        contract_size = _FX_CONTRACT_SIZE
        sl_order = o.get("stopLossOnFill", {}) or {}
        tp_order = o.get("takeProfitOnFill", {}) or {}
        return Order(
            ticket=int(o.get("id", 0)),
            symbol=symbol,
            type=order_type,
            lots=abs(units) / contract_size,
            price=float(o.get("price", 0)),
            sl=float(sl_order.get("price", 0)) if sl_order else 0.0,
            tp=float(tp_order.get("price", 0)) if tp_order else 0.0,
            fill_price=0.0,
            magic=0,
            comment=o.get("clientExtensions", {}).get("comment", "")
                     if o.get("clientExtensions") else "",
        )
