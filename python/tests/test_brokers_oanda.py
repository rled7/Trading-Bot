"""Unit tests for algoforge.brokers.oanda.OandaBroker.

All HTTP is mocked by patching the broker instance's ``_request`` method
so no network calls are made. Tests cover all 14 IBroker trading methods,
symbol mapping, and error paths.

Run with:
    cd python && python -m pytest tests/test_brokers_oanda.py -v
"""
from __future__ import annotations

import unittest
from unittest.mock import MagicMock, patch, call

from algoforge.broker import AccountInfo, Order, OrderType, Position, Tick, SymbolInfo
from algoforge.brokers.base import BrokerError
from algoforge.brokers.config import BrokerConfig
from algoforge.brokers.oanda import OandaBroker, _parse_ts
from algoforge.types import Bar, Direction


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_broker(api_key: str = "test-key",
                 account_id: str = "123-456-789",
                 paper: bool = True) -> OandaBroker:
    """Return an OandaBroker pre-configured with fake credentials."""
    cfg = BrokerConfig(name="oanda", api_key=api_key,
                       account_id=account_id, paper=paper)
    broker = OandaBroker(config=cfg)
    # Simulate connected state so trading methods don't need an HTTP round-trip
    broker._connected = True
    broker._base_url = broker.PAPER_URL
    return broker


def _mock_request(broker: OandaBroker, responses: dict[str, dict]) -> MagicMock:
    """Patch broker._request to return canned responses keyed by path fragment.

    The first key whose fragment appears in the requested path wins.
    Unmatched paths raise a RuntimeError to catch typos early.
    """
    def _side_effect(method, path, **kwargs):
        for fragment, resp in responses.items():
            if fragment in path:
                if isinstance(resp, Exception):
                    raise resp
                return resp
        raise RuntimeError(f"Unexpected request: {method} {path}")

    mock = MagicMock(side_effect=_side_effect)
    broker._request = mock
    return mock


# Sample data fixtures
_ACCOUNT_SUMMARY = {
    "account": {
        "balance": "10000.00",
        "NAV": "10050.00",
        "marginUsed": "200.00",
        "marginAvailable": "9800.00",
        "unrealizedPL": "50.00",
        "marginRate": "0.02",
        "currency": "USD",
        "id": "123-456-789",
    }
}

_PRICING = {
    "prices": [{
        "instrument": "EUR_USD",
        "bids": [{"price": "1.10990", "liquidity": 10000000}],
        "asks": [{"price": "1.11010", "liquidity": 10000000}],
        "time": "2024-01-15T10:30:00.000000000Z",
        "status": "tradeable",
    }]
}

_INSTRUMENTS = {
    "instruments": [{
        "name": "EUR_USD",
        "type": "CURRENCY",
        "displayPrecision": 5,
        "pipLocation": -4,
        "minimumTradeSize": "1",
        "maximumOrderUnits": "100000000",
    }]
}

_CANDLES = {
    "instrument": "EUR_USD",
    "granularity": "H1",
    "candles": [
        {
            "time": "2024-01-15T09:00:00.000000000Z",
            "mid": {"o": "1.10500", "h": "1.10800", "l": "1.10300", "c": "1.10700"},
            "volume": 15432,
            "complete": True,
        },
        {
            "time": "2024-01-15T10:00:00.000000000Z",
            "mid": {"o": "1.10700", "h": "1.11100", "l": "1.10600", "c": "1.11000"},
            "volume": 18921,
            "complete": False,
        },
    ],
}

_ORDER_FILL_RESPONSE = {
    "orderFillTransaction": {
        "id": "5001",
        "type": "ORDER_FILL",
        "price": "1.11010",
        "tradeOpened": {"tradeID": "4001", "units": "10000"},
    }
}

_ORDER_FILL_SELL_RESPONSE = {
    "orderFillTransaction": {
        "id": "5002",
        "type": "ORDER_FILL",
        "price": "1.10990",
        "tradeOpened": {"tradeID": "4002", "units": "-10000"},
    }
}

_OPEN_TRADES = {
    "trades": [
        {
            "id": "4001",
            "instrument": "EUR_USD",
            "price": "1.10500",
            "currentUnits": "10000",
            # NOTE: OANDA Trade objects do NOT include a "currentPrice" field;
            # current_price falls back to open_price in _trade_to_position().
            "unrealizedPL": "49.00",
            "financing": "-0.70",
            "stopLossOrder": {"price": "1.10000", "timeInForce": "GTC"},
            "takeProfitOrder": {"price": "1.12000", "timeInForce": "GTC"},
        },
        {
            "id": "4002",
            "instrument": "GBP_USD",
            "price": "1.27000",
            "currentUnits": "-5000",
            "currentPrice": "1.26800",
            "unrealizedPL": "10.00",
            "financing": "0.00",
        },
    ]
}

_PENDING_ORDERS = {
    "orders": [
        {
            "id": "7001",
            "type": "LIMIT",
            "instrument": "EUR_USD",
            "units": "10000",
            "price": "1.09000",
            "stopLossOnFill": {"price": "1.08500"},
            "takeProfitOnFill": {"price": "1.10500"},
        },
        {
            "id": "7002",
            "type": "STOP",
            "instrument": "GBP_USD",
            "units": "-5000",
            "price": "1.28000",
        },
    ]
}


# ---------------------------------------------------------------------------
# 1. connect() — success
# ---------------------------------------------------------------------------

class TestConnectSuccess(unittest.TestCase):

    def test_connect_returns_zero_with_valid_creds(self) -> None:
        cfg = BrokerConfig(name="oanda", api_key="key", account_id="acct-123",
                           paper=True)
        broker = OandaBroker(config=cfg)
        result = broker.connect()
        self.assertEqual(result, 0)

    def test_is_connected_true_after_connect(self) -> None:
        cfg = BrokerConfig(name="oanda", api_key="key", account_id="acct-123",
                           paper=True)
        broker = OandaBroker(config=cfg)
        broker.connect()
        self.assertTrue(broker.is_connected())

    def test_broker_name(self) -> None:
        broker = _make_broker()
        self.assertEqual(broker.broker_name(), "oanda")


# ---------------------------------------------------------------------------
# 2. connect() — missing credentials returns nonzero
# ---------------------------------------------------------------------------

class TestConnectFailure(unittest.TestCase):

    def test_connect_missing_api_key_returns_nonzero(self) -> None:
        cfg = BrokerConfig(name="oanda", api_key="", account_id="acct-123",
                           paper=True)
        broker = OandaBroker(config=cfg)
        result = broker.connect()
        self.assertNotEqual(result, 0)

    def test_connect_missing_account_id_returns_nonzero(self) -> None:
        cfg = BrokerConfig(name="oanda", api_key="my-key", account_id="",
                           paper=True)
        broker = OandaBroker(config=cfg)
        result = broker.connect()
        self.assertNotEqual(result, 0)

    def test_connect_empty_creds_returns_nonzero(self) -> None:
        cfg = BrokerConfig(name="oanda", api_key="", account_id="", paper=True)
        broker = OandaBroker(config=cfg)
        result = broker.connect()
        self.assertNotEqual(result, 0)


# ---------------------------------------------------------------------------
# 3. Symbol mapping
# ---------------------------------------------------------------------------

class TestSymbolMapping(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()

    def test_eurusd_to_eur_usd(self) -> None:
        self.assertEqual(self.broker._to_broker_symbol("EURUSD"), "EUR_USD")

    def test_slash_form_to_oanda(self) -> None:
        self.assertEqual(self.broker._to_broker_symbol("EUR/USD"), "EUR_USD")

    def test_already_underscore_form(self) -> None:
        self.assertEqual(self.broker._to_broker_symbol("EUR_USD"), "EUR_USD")

    def test_from_broker_symbol(self) -> None:
        self.assertEqual(self.broker._from_broker_symbol("EUR_USD"), "EURUSD")


# ---------------------------------------------------------------------------
# 4. get_account()
# ---------------------------------------------------------------------------

class TestGetAccount(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"summary": _ACCOUNT_SUMMARY})

    def test_returns_account_info(self) -> None:
        info = self.broker.get_account()
        self.assertIsInstance(info, AccountInfo)

    def test_balance_mapped_correctly(self) -> None:
        info = self.broker.get_account()
        self.assertAlmostEqual(info.balance, 10000.0)

    def test_equity_mapped_correctly(self) -> None:
        info = self.broker.get_account()
        self.assertAlmostEqual(info.equity, 10050.0)

    def test_margin_used_mapped(self) -> None:
        info = self.broker.get_account()
        self.assertAlmostEqual(info.margin, 200.0)

    def test_free_margin_mapped(self) -> None:
        info = self.broker.get_account()
        self.assertAlmostEqual(info.free_margin, 9800.0)

    def test_unrealized_pl_as_profit(self) -> None:
        info = self.broker.get_account()
        self.assertAlmostEqual(info.profit, 50.0)

    def test_leverage_from_margin_rate(self) -> None:
        # marginRate=0.02 -> leverage=50
        info = self.broker.get_account()
        self.assertEqual(info.leverage, 50)

    def test_currency(self) -> None:
        info = self.broker.get_account()
        self.assertEqual(info.currency, "USD")

    def test_login_is_zero_for_string_account_id(self) -> None:
        info = self.broker.get_account()
        self.assertEqual(info.login, 0)


# ---------------------------------------------------------------------------
# 5. get_tick()
# ---------------------------------------------------------------------------

class TestGetTick(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"pricing": _PRICING})

    def test_returns_tick(self) -> None:
        tick = self.broker.get_tick("EURUSD")
        self.assertIsInstance(tick, Tick)

    def test_symbol_preserved(self) -> None:
        tick = self.broker.get_tick("EURUSD")
        self.assertEqual(tick.symbol, "EURUSD")

    def test_bid_mapped(self) -> None:
        tick = self.broker.get_tick("EURUSD")
        self.assertAlmostEqual(tick.bid, 1.10990)

    def test_ask_mapped(self) -> None:
        tick = self.broker.get_tick("EURUSD")
        self.assertAlmostEqual(tick.ask, 1.11010)

    def test_timestamp_parsed(self) -> None:
        tick = self.broker.get_tick("EURUSD")
        self.assertGreater(tick.timestamp, 0)


# ---------------------------------------------------------------------------
# 6. get_symbol_info()
# ---------------------------------------------------------------------------

class TestGetSymbolInfo(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"instruments": _INSTRUMENTS})

    def test_returns_symbol_info(self) -> None:
        info = self.broker.get_symbol_info("EURUSD")
        self.assertIsInstance(info, SymbolInfo)

    def test_digits_correct(self) -> None:
        info = self.broker.get_symbol_info("EURUSD")
        self.assertEqual(info.digits, 5)

    def test_point_correct(self) -> None:
        info = self.broker.get_symbol_info("EURUSD")
        self.assertAlmostEqual(info.point, 0.00001, places=10)

    def test_volume_min(self) -> None:
        # minimumTradeSize=1 unit -> 1/100000 = 0.00001 lots
        info = self.broker.get_symbol_info("EURUSD")
        self.assertAlmostEqual(info.volume_min, 0.00001)


# ---------------------------------------------------------------------------
# 7. get_bars()
# ---------------------------------------------------------------------------

class TestGetBars(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"candles": _CANDLES})

    def test_returns_correct_count(self) -> None:
        bars = self.broker.get_bars("EURUSD", 3600, 2)
        self.assertEqual(len(bars), 2)

    def test_returns_bar_instances(self) -> None:
        bars = self.broker.get_bars("EURUSD", 3600, 2)
        self.assertIsInstance(bars[0], Bar)

    def test_bar_ohlc_mapped(self) -> None:
        bars = self.broker.get_bars("EURUSD", 3600, 2)
        b = bars[0]
        self.assertAlmostEqual(b.open, 1.10500)
        self.assertAlmostEqual(b.high, 1.10800)
        self.assertAlmostEqual(b.low, 1.10300)
        self.assertAlmostEqual(b.close, 1.10700)

    def test_bar_volume_mapped(self) -> None:
        bars = self.broker.get_bars("EURUSD", 3600, 2)
        self.assertAlmostEqual(bars[0].volume, 15432.0)

    def test_bar_timestamp_parsed(self) -> None:
        bars = self.broker.get_bars("EURUSD", 3600, 2)
        self.assertGreater(bars[0].timestamp, 0)

    def test_granularity_sent_correctly(self) -> None:
        """M1 tf=60 -> granularity=M1."""
        broker = _make_broker()
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["params"] = kwargs.get("params", {})
            return _CANDLES
        broker._request = _fake_request
        broker.get_bars("EURUSD", 60, 5)
        self.assertEqual(captured["params"]["granularity"], "M1")

    def test_empty_candles_returns_empty_list(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(return_value={"candles": []})
        bars = broker.get_bars("EURUSD", 3600, 10)
        self.assertEqual(bars, [])


# ---------------------------------------------------------------------------
# 8. place_order() — BUY market
# ---------------------------------------------------------------------------

class TestPlaceOrderBuy(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"orders": _ORDER_FILL_RESPONSE})

    def test_returns_order(self) -> None:
        order = self.broker.place_order("EURUSD", OrderType.BUY, 0.10)
        self.assertIsInstance(order, Order)

    def test_ticket_is_trade_id(self) -> None:
        order = self.broker.place_order("EURUSD", OrderType.BUY, 0.10)
        self.assertEqual(order.ticket, 4001)

    def test_fill_price_mapped(self) -> None:
        order = self.broker.place_order("EURUSD", OrderType.BUY, 0.10)
        self.assertAlmostEqual(order.fill_price, 1.11010)

    def test_order_type_preserved(self) -> None:
        order = self.broker.place_order("EURUSD", OrderType.BUY, 0.10)
        self.assertEqual(order.type, OrderType.BUY)

    def test_lots_preserved(self) -> None:
        order = self.broker.place_order("EURUSD", OrderType.BUY, 0.10)
        self.assertAlmostEqual(order.lots, 0.10)

    def test_buy_units_positive_in_request(self) -> None:
        """BUY 0.10 lots -> units = +10000 (as string)."""
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["body"] = kwargs.get("body", {})
            return _ORDER_FILL_RESPONSE
        self.broker._request = _fake_request
        self.broker.place_order("EURUSD", OrderType.BUY, 0.10)
        units = captured["body"]["order"]["units"]
        self.assertEqual(units, "10000")

    def test_sl_tp_attached_when_nonzero(self) -> None:
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["body"] = kwargs.get("body", {})
            return _ORDER_FILL_RESPONSE
        self.broker._request = _fake_request
        self.broker.place_order("EURUSD", OrderType.BUY, 0.10,
                                sl=1.10000, tp=1.12000)
        order_body = captured["body"]["order"]
        self.assertIn("stopLossOnFill", order_body)
        self.assertIn("takeProfitOnFill", order_body)
        self.assertAlmostEqual(
            float(order_body["stopLossOnFill"]["price"]), 1.10000)
        self.assertAlmostEqual(
            float(order_body["takeProfitOnFill"]["price"]), 1.12000)

    def test_magic_and_comment_preserved(self) -> None:
        order = self.broker.place_order("EURUSD", OrderType.BUY, 0.10,
                                        magic=42, comment="test comment")
        self.assertEqual(order.magic, 42)
        self.assertEqual(order.comment, "test comment")


# ---------------------------------------------------------------------------
# 9. place_order() — SELL market
# ---------------------------------------------------------------------------

class TestPlaceOrderSell(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"orders": _ORDER_FILL_SELL_RESPONSE})

    def test_sell_units_negative_in_request(self) -> None:
        """SELL 0.10 lots -> units = -10000 (as string)."""
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["body"] = kwargs.get("body", {})
            return _ORDER_FILL_SELL_RESPONSE
        self.broker._request = _fake_request
        self.broker.place_order("EURUSD", OrderType.SELL, 0.10)
        units = captured["body"]["order"]["units"]
        self.assertEqual(units, "-10000")

    def test_sell_ticket_returned(self) -> None:
        order = self.broker.place_order("EURUSD", OrderType.SELL, 0.10)
        self.assertEqual(order.ticket, 4002)

    def test_sell_lots_to_units_05(self) -> None:
        """0.05 lots SELL -> -5000 units."""
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["body"] = kwargs.get("body", {})
            return _ORDER_FILL_SELL_RESPONSE
        self.broker._request = _fake_request
        self.broker.place_order("EURUSD", OrderType.SELL, 0.05)
        self.assertEqual(captured["body"]["order"]["units"], "-5000")


# ---------------------------------------------------------------------------
# 10. close_position()
# ---------------------------------------------------------------------------

class TestClosePosition(unittest.TestCase):

    def test_close_returns_zero_on_success(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(return_value={"orderFillTransaction": {}})
        result = broker.close_position(4001)
        self.assertEqual(result, 0)

    def test_close_uses_PUT_method(self) -> None:
        broker = _make_broker()
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["method"] = method
            captured["path"] = path
            return {}
        broker._request = _fake_request
        broker.close_position(4001)
        self.assertEqual(captured["method"], "PUT")
        self.assertIn("4001", captured["path"])
        self.assertIn("close", captured["path"])

    def test_close_returns_nonzero_on_broker_error(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(
            side_effect=BrokerError("trade not found", status=404, code=2))
        result = broker.close_position(9999)
        self.assertNotEqual(result, 0)

    def test_partial_close_sends_units_in_body(self) -> None:
        broker = _make_broker()
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["body"] = kwargs.get("body", {})
            return {}
        broker._request = _fake_request
        broker.close_position(4001, lots=0.05)
        self.assertIn("units", captured["body"])
        self.assertEqual(captured["body"]["units"], "5000")


# ---------------------------------------------------------------------------
# 11. modify_position()
# ---------------------------------------------------------------------------

class TestModifyPosition(unittest.TestCase):

    def test_modify_returns_zero_on_success(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(return_value={})
        result = broker.modify_position(4001, sl=1.09500, tp=1.12500)
        self.assertEqual(result, 0)

    def test_modify_uses_PUT(self) -> None:
        broker = _make_broker()
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["method"] = method
            return {}
        broker._request = _fake_request
        broker.modify_position(4001, sl=1.09500, tp=1.12500)
        self.assertEqual(captured["method"], "PUT")

    def test_modify_returns_nonzero_on_error(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(
            side_effect=BrokerError("trade closed", status=404, code=2))
        result = broker.modify_position(9999, sl=1.09000, tp=1.12000)
        self.assertNotEqual(result, 0)

    def test_modify_sl_tp_in_body(self) -> None:
        broker = _make_broker()
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["body"] = kwargs.get("body", {})
            return {}
        broker._request = _fake_request
        broker.modify_position(4001, sl=1.09500, tp=1.12500)
        body = captured["body"]
        self.assertIn("stopLoss", body)
        self.assertIn("takeProfit", body)
        self.assertAlmostEqual(float(body["stopLoss"]["price"]), 1.09500)
        self.assertAlmostEqual(float(body["takeProfit"]["price"]), 1.12500)


# ---------------------------------------------------------------------------
# 12. cancel_order()
# ---------------------------------------------------------------------------

class TestCancelOrder(unittest.TestCase):

    def test_cancel_returns_zero_on_success(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(return_value={})
        result = broker.cancel_order(7001)
        self.assertEqual(result, 0)

    def test_cancel_uses_PUT(self) -> None:
        broker = _make_broker()
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["method"] = method
            captured["path"] = path
            return {}
        broker._request = _fake_request
        broker.cancel_order(7001)
        self.assertEqual(captured["method"], "PUT")
        self.assertIn("7001", captured["path"])
        self.assertIn("cancel", captured["path"])

    def test_cancel_returns_nonzero_on_error(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(
            side_effect=BrokerError("order not found", status=404, code=2))
        result = broker.cancel_order(9999)
        self.assertNotEqual(result, 0)


# ---------------------------------------------------------------------------
# 13. get_positions()
# ---------------------------------------------------------------------------

class TestGetPositions(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"openTrades": _OPEN_TRADES})

    def test_returns_list_of_positions(self) -> None:
        positions = self.broker.get_positions()
        self.assertEqual(len(positions), 2)
        self.assertIsInstance(positions[0], Position)

    def test_long_position_mapped(self) -> None:
        positions = self.broker.get_positions()
        long_pos = next(p for p in positions if p.ticket == 4001)
        self.assertEqual(long_pos.side, Direction.LONG)
        self.assertAlmostEqual(long_pos.lots, 0.10)
        self.assertAlmostEqual(long_pos.open_price, 1.10500)
        # Trade 4001 has no currentPrice field, so current_price falls back to
        # open_price per _trade_to_position() (see fixture note above).
        self.assertAlmostEqual(long_pos.current_price, 1.10500)
        self.assertAlmostEqual(long_pos.profit, 49.0)
        self.assertAlmostEqual(long_pos.sl, 1.10000)
        self.assertAlmostEqual(long_pos.tp, 1.12000)

    def test_short_position_mapped(self) -> None:
        positions = self.broker.get_positions()
        short_pos = next(p for p in positions if p.ticket == 4002)
        self.assertEqual(short_pos.side, Direction.SHORT)
        self.assertAlmostEqual(short_pos.lots, 0.05)

    def test_symbol_mapping_applied(self) -> None:
        positions = self.broker.get_positions()
        symbols = {p.symbol for p in positions}
        self.assertIn("EURUSD", symbols)
        self.assertIn("GBPUSD", symbols)


# ---------------------------------------------------------------------------
# 14. get_position() by ticket
# ---------------------------------------------------------------------------

class TestGetPosition(unittest.TestCase):

    def test_get_position_found(self) -> None:
        broker = _make_broker()
        _mock_request(broker, {"openTrades": _OPEN_TRADES})
        pos = broker.get_position(4001)
        self.assertIsNotNone(pos)
        self.assertEqual(pos.ticket, 4001)

    def test_get_position_not_found_returns_none(self) -> None:
        broker = _make_broker()
        _mock_request(broker, {"openTrades": _OPEN_TRADES})
        pos = broker.get_position(9999)
        self.assertIsNone(pos)


# ---------------------------------------------------------------------------
# 15. get_orders()
# ---------------------------------------------------------------------------

class TestGetOrders(unittest.TestCase):

    def setUp(self) -> None:
        self.broker = _make_broker()
        _mock_request(self.broker, {"pendingOrders": _PENDING_ORDERS})

    def test_returns_list_of_orders(self) -> None:
        orders = self.broker.get_orders()
        self.assertEqual(len(orders), 2)
        self.assertIsInstance(orders[0], Order)

    def test_buy_limit_order_mapped(self) -> None:
        orders = self.broker.get_orders()
        buy_limit = next(o for o in orders if o.ticket == 7001)
        self.assertEqual(buy_limit.type, OrderType.BUY_LIMIT)
        self.assertAlmostEqual(buy_limit.lots, 0.10)
        self.assertAlmostEqual(buy_limit.price, 1.09000)

    def test_sell_stop_order_mapped(self) -> None:
        orders = self.broker.get_orders()
        sell_stop = next(o for o in orders if o.ticket == 7002)
        self.assertEqual(sell_stop.type, OrderType.SELL_STOP)
        self.assertAlmostEqual(sell_stop.lots, 0.05)


# ---------------------------------------------------------------------------
# 16. BrokerError propagation on get_account
# ---------------------------------------------------------------------------

class TestBrokerErrorPropagation(unittest.TestCase):

    def test_get_account_raises_on_http_error(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(
            side_effect=BrokerError("HTTP 401", status=401, code=2))
        with self.assertRaises(BrokerError):
            broker.get_account()

    def test_get_tick_raises_when_no_prices(self) -> None:
        broker = _make_broker()
        broker._request = MagicMock(return_value={"prices": []})
        with self.assertRaises(BrokerError):
            broker.get_tick("EURUSD")

    def test_place_order_raises_on_malformed_response(self) -> None:
        """If neither orderFillTransaction nor orderCreateTransaction present."""
        broker = _make_broker()
        broker._request = MagicMock(return_value={"unexpectedKey": {}})
        with self.assertRaises(BrokerError):
            broker.place_order("EURUSD", OrderType.BUY, 0.10)


# ---------------------------------------------------------------------------
# 17. Timestamp parser
# ---------------------------------------------------------------------------

class TestParseTs(unittest.TestCase):

    def test_nanosecond_z_timestamp(self) -> None:
        ts = _parse_ts("2024-01-15T10:30:00.000000000Z")
        self.assertIsInstance(ts, int)
        self.assertGreater(ts, 0)

    def test_microsecond_timestamp(self) -> None:
        ts = _parse_ts("2024-01-15T10:30:00.123456Z")
        self.assertIsInstance(ts, int)

    def test_no_fractional(self) -> None:
        ts = _parse_ts("2024-01-15T10:30:00Z")
        self.assertIsInstance(ts, int)
        self.assertGreater(ts, 0)


# ---------------------------------------------------------------------------
# 18. Lots-to-units conversion edge cases
# ---------------------------------------------------------------------------

class TestLotsToUnitsConversion(unittest.TestCase):

    def _capture_units(self, broker, order_type, lots) -> str:
        captured = {}
        def _fake_request(method, path, **kwargs):
            captured["body"] = kwargs.get("body", {})
            return _ORDER_FILL_RESPONSE
        broker._request = _fake_request
        broker.place_order("EURUSD", order_type, lots)
        return captured["body"]["order"]["units"]

    def test_1_lot_buy_is_100000(self) -> None:
        broker = _make_broker()
        units = self._capture_units(broker, OrderType.BUY, 1.0)
        self.assertEqual(units, "100000")

    def test_0_01_lot_buy_is_1000(self) -> None:
        broker = _make_broker()
        units = self._capture_units(broker, OrderType.BUY, 0.01)
        self.assertEqual(units, "1000")

    def test_0_5_lot_sell_is_minus_50000(self) -> None:
        broker = _make_broker()
        units = self._capture_units(broker, OrderType.SELL, 0.5)
        self.assertEqual(units, "-50000")


if __name__ == "__main__":
    unittest.main()
