"""Unit tests for the IBKR broker adapter (algoforge.brokers.ibkr).

All HTTP is mocked — no real network calls are made. Tests patch
instance methods ``_get``, ``_post``, and ``_request`` directly, which
is cleaner than faking the ``urlopen`` context manager.

Run with:
    cd /Users/user/Trading-Bot/python && python3 -m pytest tests/test_brokers_ibkr.py -q
"""
from __future__ import annotations

import unittest
from unittest.mock import MagicMock, patch, call

from algoforge.broker import AccountInfo, Order, OrderType, Position
from algoforge.brokers.base import BrokerError
from algoforge.brokers.config import BrokerConfig
from algoforge.brokers.ibkr import IbkrBroker
from algoforge.types import Direction


def _make_broker(account_id: str = "U1234567") -> IbkrBroker:
    """Return a pre-connected IbkrBroker with mocked _on_connect."""
    cfg = BrokerConfig(name="ibkr", account_id=account_id, paper=True)
    broker = IbkrBroker(cfg)
    broker._connected = True
    broker._base_url = "https://localhost:5000/v1/api"
    return broker


class TestConnect(unittest.TestCase):
    """Tests for _on_connect and the connect() lifecycle."""

    def test_connect_authenticated_returns_zero(self):
        """connect() returns 0 when gateway reports authenticated=True."""
        broker = _make_broker()
        broker._connected = False  # reset so connect() runs

        with patch.object(broker, "_get", return_value={"authenticated": True}):
            result = broker.connect()

        self.assertEqual(result, 0)
        self.assertTrue(broker.is_connected())

    def test_connect_not_authenticated_returns_nonzero(self):
        """connect() returns a nonzero code when gateway not authenticated."""
        broker = _make_broker()
        broker._connected = False

        with patch.object(broker, "_get", return_value={"authenticated": False}):
            result = broker.connect()

        self.assertNotEqual(result, 0)
        self.assertFalse(broker.is_connected())

    def test_connect_missing_account_id_returns_nonzero(self):
        """connect() returns nonzero when account_id is not set."""
        cfg = BrokerConfig(name="ibkr", account_id="", paper=True)
        broker = IbkrBroker(cfg)
        broker._base_url = "https://localhost:5000/v1/api"

        result = broker.connect()

        self.assertNotEqual(result, 0)
        self.assertFalse(broker.is_connected())

    def test_connect_gateway_network_error_returns_nonzero(self):
        """connect() returns nonzero when gateway is unreachable."""
        broker = _make_broker()
        broker._connected = False

        with patch.object(broker, "_get", side_effect=BrokerError("network", code=3)):
            result = broker.connect()

        self.assertNotEqual(result, 0)


class TestConidResolution(unittest.TestCase):
    """Tests for _resolve_conid caching and lookup."""

    def test_resolve_conid_returns_correct_id(self):
        """_resolve_conid returns the conid from the API response."""
        broker = _make_broker()
        mock_resp = [{"conid": 265598, "ticker": "AAPL", "companyName": "Apple Inc"}]

        with patch.object(broker, "_get", return_value=mock_resp):
            conid = broker._resolve_conid("AAPL")

        self.assertEqual(conid, 265598)

    def test_resolve_conid_caches_result(self):
        """_resolve_conid only calls the API once per symbol."""
        broker = _make_broker()
        mock_resp = [{"conid": 265598, "ticker": "AAPL"}]
        mock_get = MagicMock(return_value=mock_resp)

        with patch.object(broker, "_get", mock_get):
            broker._resolve_conid("AAPL")
            broker._resolve_conid("AAPL")  # second call should use cache

        mock_get.assert_called_once()  # only one HTTP call despite two calls

    def test_resolve_conid_cache_keyed_uppercase(self):
        """Cache key is case-insensitive (AAPL == aapl)."""
        broker = _make_broker()
        mock_resp = [{"conid": 265598, "ticker": "AAPL"}]

        with patch.object(broker, "_get", return_value=mock_resp):
            broker._resolve_conid("AAPL")

        # After resolving AAPL, the uppercase key is cached.
        self.assertIn("AAPL", broker._conid_cache)
        self.assertEqual(broker._conid_cache["AAPL"], 265598)

    def test_resolve_conid_no_results_raises_broker_error(self):
        """_resolve_conid raises BrokerError when no results returned."""
        broker = _make_broker()

        with patch.object(broker, "_get", return_value=[]):
            with self.assertRaises(BrokerError):
                broker._resolve_conid("UNKNOWN")


class TestGetAccount(unittest.TestCase):
    """Tests for get_account."""

    def test_get_account_maps_fields_correctly(self):
        """get_account correctly maps portfolio summary fields."""
        broker = _make_broker()
        mock_summary = {
            "netliquidation": {"amount": 100_000.0, "currency": "USD"},
            "availablefunds": {"amount": 80_000.0, "currency": "USD"},
            "totalcashvalue": {"amount": 95_000.0, "currency": "USD"},
            "unrealizedpnl": {"amount": 2_500.0, "currency": "USD"},
            "grosspositionvalue": {"amount": 20_000.0, "currency": "USD"},
        }

        with patch.object(broker, "_get", return_value=mock_summary):
            info = broker.get_account()

        self.assertIsInstance(info, AccountInfo)
        self.assertAlmostEqual(info.equity, 100_000.0)
        self.assertAlmostEqual(info.free_margin, 80_000.0)
        self.assertAlmostEqual(info.balance, 95_000.0)
        self.assertAlmostEqual(info.profit, 2_500.0)
        self.assertAlmostEqual(info.margin, 20_000.0)
        self.assertEqual(info.currency, "USD")
        self.assertEqual(info.login, 1234567)

    def test_get_account_calls_correct_endpoint(self):
        """get_account calls /portfolio/{accountId}/summary."""
        broker = _make_broker("U9999999")
        mock_summary = {
            "netliquidation": {"amount": 1.0, "currency": "USD"},
            "availablefunds": {"amount": 1.0, "currency": "USD"},
        }
        mock_get = MagicMock(return_value=mock_summary)

        with patch.object(broker, "_get", mock_get):
            broker.get_account()

        called_path = mock_get.call_args[0][0]
        self.assertIn("U9999999", called_path)
        self.assertIn("summary", called_path)


class TestGetTick(unittest.TestCase):
    """Tests for get_tick snapshot parsing."""

    def test_get_tick_parses_snapshot_fields(self):
        """get_tick correctly extracts bid/ask from IBKR field IDs."""
        broker = _make_broker()
        broker._conid_cache["AAPL"] = 265598

        mock_snapshot = [{"31": "182.50", "84": "182.48", "86": "182.52", "conid": 265598}]

        with patch.object(broker, "_get", return_value=mock_snapshot):
            tick = broker.get_tick("AAPL")

        self.assertEqual(tick.symbol, "AAPL")
        self.assertAlmostEqual(tick.bid, 182.48)
        self.assertAlmostEqual(tick.ask, 182.52)
        self.assertGreater(tick.timestamp, 0)

    def test_get_tick_falls_back_to_last_when_bid_ask_missing(self):
        """get_tick uses last price for bid/ask when they're absent."""
        broker = _make_broker()
        broker._conid_cache["MSFT"] = 272093

        mock_snapshot = [{"31": "375.00", "conid": 272093}]

        with patch.object(broker, "_get", return_value=mock_snapshot):
            tick = broker.get_tick("MSFT")

        self.assertAlmostEqual(tick.bid, 375.0)
        self.assertAlmostEqual(tick.ask, 375.0)

    def test_get_tick_empty_response_raises(self):
        """get_tick raises BrokerError when snapshot response is empty."""
        broker = _make_broker()
        broker._conid_cache["XYZ"] = 999

        with patch.object(broker, "_get", return_value=[]):
            with self.assertRaises(BrokerError):
                broker.get_tick("XYZ")


class TestPlaceOrder(unittest.TestCase):
    """Tests for place_order including the reply-confirm flow."""

    def test_place_order_simple_success(self):
        """place_order returns Order with correct ticket when no confirmation needed."""
        broker = _make_broker()
        broker._conid_cache["AAPL"] = 265598

        mock_post = MagicMock(return_value=[{"order_id": 987654, "order_status": "Submitted"}])

        with patch.object(broker, "_post", mock_post):
            order = broker.place_order("AAPL", OrderType.BUY, 100)

        self.assertIsInstance(order, Order)
        self.assertEqual(order.ticket, 987654)
        self.assertEqual(order.symbol, "AAPL")
        self.assertEqual(order.type, OrderType.BUY)
        self.assertAlmostEqual(order.lots, 100.0)

    def test_place_order_confirm_reply_flow(self):
        """place_order handles one round of confirmation prompts correctly."""
        broker = _make_broker()
        broker._conid_cache["TSLA"] = 76792991

        # First response: confirmation prompt; second: actual order.
        confirm_resp = [{"id": "confirm-abc-123", "message": ["Confirm this order?"]}]
        success_resp = [{"order_id": 111222, "order_status": "PreSubmitted"}]

        call_count = {"n": 0}

        def mock_post_side_effect(path, **kwargs):
            call_count["n"] += 1
            if "orders" in path:
                return confirm_resp
            elif "reply" in path:
                return success_resp
            return []

        with patch.object(broker, "_post", side_effect=mock_post_side_effect):
            order = broker.place_order("TSLA", OrderType.SELL, 50)

        self.assertEqual(order.ticket, 111222)
        self.assertEqual(call_count["n"], 2)  # initial + one reply

    def test_place_order_chained_confirm_replies(self):
        """place_order handles multiple sequential confirmation rounds."""
        broker = _make_broker()
        broker._conid_cache["NVDA"] = 4815747

        call_sequence = [
            [{"id": "reply-1", "message": ["Confirm?"]}],
            [{"id": "reply-2", "message": ["Really confirm?"]}],
            [{"order_id": 555666, "order_status": "Submitted"}],
        ]
        call_idx = {"i": 0}

        def mock_post_side_effect(path, **kwargs):
            resp = call_sequence[call_idx["i"]]
            call_idx["i"] += 1
            return resp

        with patch.object(broker, "_post", side_effect=mock_post_side_effect):
            order = broker.place_order("NVDA", OrderType.BUY, 10)

        self.assertEqual(order.ticket, 555666)

    def test_place_order_no_order_id_raises(self):
        """place_order raises BrokerError when all rounds yield no order_id."""
        broker = _make_broker()
        broker._conid_cache["FOO"] = 12345

        # Always return confirmation prompts — never resolve.
        always_confirm = [{"id": "confirm-x", "message": ["Loop forever?"]}]

        with patch.object(broker, "_post", return_value=always_confirm):
            with self.assertRaises(BrokerError):
                broker.place_order("FOO", OrderType.BUY, 1)

    def test_place_order_lots_as_quantity(self):
        """place_order sends lots directly as IBKR quantity (not scaled)."""
        broker = _make_broker()
        broker._conid_cache["IBM"] = 8314

        captured_body = {}

        def capture_post(path, **kwargs):
            captured_body.update(kwargs.get("body", {}))
            return [{"order_id": 777, "order_status": "Submitted"}]

        with patch.object(broker, "_post", side_effect=capture_post):
            broker.place_order("IBM", OrderType.BUY, 250)

        orders_list = captured_body.get("orders", [])
        self.assertEqual(len(orders_list), 1)
        self.assertEqual(orders_list[0]["quantity"], 250)


class TestGetPositions(unittest.TestCase):
    """Tests for get_positions and get_position."""

    def test_get_positions_parses_response(self):
        """get_positions returns Position list from portfolio endpoint."""
        broker = _make_broker()
        mock_data = [
            {
                "conid": 265598,
                "ticker": "AAPL",
                "position": 100.0,
                "avgCost": 175.50,
                "mktPrice": 182.00,
                "unrealizedPnl": 650.0,
            }
        ]

        with patch.object(broker, "_get", return_value=mock_data):
            positions = broker.get_positions()

        self.assertEqual(len(positions), 1)
        pos = positions[0]
        self.assertIsInstance(pos, Position)
        self.assertEqual(pos.ticket, 265598)
        self.assertEqual(pos.symbol, "AAPL")
        self.assertEqual(pos.side, Direction.LONG)
        self.assertAlmostEqual(pos.lots, 100.0)
        self.assertAlmostEqual(pos.open_price, 175.50)
        self.assertAlmostEqual(pos.profit, 650.0)

    def test_get_positions_short_position_direction(self):
        """get_positions sets Direction.SHORT for negative position quantities."""
        broker = _make_broker()
        mock_data = [
            {"conid": 8314, "ticker": "IBM", "position": -50.0, "avgCost": 130.0,
             "mktPrice": 128.0, "unrealizedPnl": 100.0}
        ]

        with patch.object(broker, "_get", return_value=mock_data):
            positions = broker.get_positions()

        self.assertEqual(positions[0].side, Direction.SHORT)
        self.assertAlmostEqual(positions[0].lots, 50.0)

    def test_get_positions_filters_zero_quantity(self):
        """get_positions excludes positions with zero quantity."""
        broker = _make_broker()
        mock_data = [
            {"conid": 1, "ticker": "A", "position": 0.0},
            {"conid": 2, "ticker": "B", "position": 10.0, "avgCost": 50.0,
             "mktPrice": 55.0, "unrealizedPnl": 50.0},
        ]

        with patch.object(broker, "_get", return_value=mock_data):
            positions = broker.get_positions()

        self.assertEqual(len(positions), 1)
        self.assertEqual(positions[0].ticket, 2)

    def test_get_position_by_ticket(self):
        """get_position returns the matching position by conid ticket."""
        broker = _make_broker()
        mock_data = [
            {"conid": 265598, "ticker": "AAPL", "position": 100.0,
             "avgCost": 175.0, "mktPrice": 180.0, "unrealizedPnl": 500.0},
        ]

        with patch.object(broker, "_get", return_value=mock_data):
            pos = broker.get_position(265598)

        self.assertIsNotNone(pos)
        self.assertEqual(pos.ticket, 265598)

    def test_get_position_returns_none_for_missing(self):
        """get_position returns None when ticket is not found."""
        broker = _make_broker()

        with patch.object(broker, "_get", return_value=[]):
            pos = broker.get_position(99999)

        self.assertIsNone(pos)


class TestErrorPath(unittest.TestCase):
    """Tests for error handling paths."""

    def test_cancel_order_returns_zero_on_success(self):
        """cancel_order returns 0 when DELETE succeeds."""
        broker = _make_broker()

        with patch.object(broker, "_request", return_value={}):
            result = broker.cancel_order(12345)

        self.assertEqual(result, 0)

    def test_cancel_order_returns_nonzero_on_broker_error(self):
        """cancel_order returns nonzero code when the API raises BrokerError."""
        broker = _make_broker()

        with patch.object(broker, "_request", side_effect=BrokerError("not found", code=2)):
            result = broker.cancel_order(99999)

        self.assertNotEqual(result, 0)

    def test_get_account_http_error_propagates(self):
        """get_account propagates BrokerError from _get."""
        broker = _make_broker()

        with patch.object(broker, "_get", side_effect=BrokerError("HTTP 500", code=2)):
            with self.assertRaises(BrokerError):
                broker.get_account()


if __name__ == "__main__":
    unittest.main()
