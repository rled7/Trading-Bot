"""Tests for AlpacaBroker (S5).

All tests use mocked HTTP — no real network calls.
We patch algoforge.brokers.alpaca.RestBroker._request (or _get/_post) with a
side_effect that records calls and returns canned JSON fixtures.

Ticket-ID contract under test:
  - int tickets are allocated from a counter (starting at 1)
  - orders map ticket -> Alpaca UUID string
  - positions map ticket -> symbol string
  - same key always returns the same ticket (idempotent allocation)

Lots/Qty contract under test:
  - lots=10.5 in place_order -> qty="10.5" in the POST body
"""
from __future__ import annotations

import json
import unittest
import urllib.error
import urllib.request
from io import BytesIO
from typing import Any
from unittest.mock import MagicMock, call, patch

from algoforge.broker import AccountInfo, Order, OrderType, Position
from algoforge.brokers.alpaca import AlpacaBroker, _DEFAULT_DATA_URL
from algoforge.brokers.base import BrokerError
from algoforge.brokers.config import BrokerConfig
from algoforge.types import Bar, Direction


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_config(*, paper: bool = True, api_key: str = "KEY",
                 api_secret: str = "SECRET") -> BrokerConfig:
    return BrokerConfig(
        name="alpaca",
        paper=paper,
        api_key=api_key,
        api_secret=api_secret,
    )


def _connected_broker(cfg: BrokerConfig | None = None) -> AlpacaBroker:
    broker = AlpacaBroker(cfg or _make_config())
    rc = broker.connect()
    assert rc == 0, f"connect() returned {rc}"
    return broker


def _mock_request(responses: dict[str, Any]):
    """Return a side_effect callable for _request that matches on the path."""
    def _side(method: str, path: str, **kwargs) -> Any:
        # Match by substring of path — host+path may be full URL
        for key, val in responses.items():
            if key in path:
                return val
        raise AssertionError(f"Unexpected _request call: {method} {path}")
    return _side


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

class TestConnect(unittest.TestCase):
    """connect() lifecycle — no network needed (Alpaca _on_connect only calls require())."""

    def test_connect_with_valid_creds(self):
        broker = AlpacaBroker(_make_config(api_key="K", api_secret="S"))
        rc = broker.connect()
        self.assertEqual(rc, 0)
        self.assertTrue(broker.is_connected())

    def test_connect_missing_api_key(self):
        broker = AlpacaBroker(_make_config(api_key="", api_secret="S"))
        rc = broker.connect()
        self.assertNotEqual(rc, 0)
        self.assertFalse(broker.is_connected())

    def test_connect_missing_api_secret(self):
        broker = AlpacaBroker(_make_config(api_key="K", api_secret=""))
        rc = broker.connect()
        self.assertNotEqual(rc, 0)
        self.assertFalse(broker.is_connected())

    def test_connect_idempotent(self):
        broker = _connected_broker()
        rc2 = broker.connect()
        self.assertEqual(rc2, 0)   # second call is a no-op

    def test_disconnect(self):
        broker = _connected_broker()
        broker.disconnect()
        self.assertFalse(broker.is_connected())


class TestGetAccount(unittest.TestCase):

    def test_get_account_maps_fields(self):
        broker = _connected_broker()
        acct_payload = {
            "account_number": "PA1234",
            "cash":            "10000.50",
            "equity":          "11000.25",
            "buying_power":    "20000.00",
            "initial_margin":  "1000.00",
            "unrealized_pl":   "500.00",
            "currency":        "USD",
        }
        with patch.object(broker, "_request", side_effect=_mock_request(
                {"/account": acct_payload})):
            info = broker.get_account()

        self.assertIsInstance(info, AccountInfo)
        self.assertAlmostEqual(info.balance,     10000.50)
        self.assertAlmostEqual(info.equity,      11000.25)
        self.assertAlmostEqual(info.free_margin, 20000.00)
        self.assertAlmostEqual(info.margin,       1000.00)
        self.assertAlmostEqual(info.profit,        500.00)
        self.assertEqual(info.currency, "USD")
        self.assertIsInstance(info.login, int)


class TestGetPositions(unittest.TestCase):

    _POS_LIST = [
        {
            "symbol":           "AAPL",
            "side":             "long",
            "qty":              "5",
            "avg_entry_price":  "170.00",
            "current_price":    "175.00",
            "unrealized_pl":    "25.00",
        },
        {
            "symbol":           "TSLA",
            "side":             "short",
            "qty":              "2",
            "avg_entry_price":  "250.00",
            "current_price":    "245.00",
            "unrealized_pl":    "10.00",
        },
    ]

    def test_get_positions_returns_list(self):
        broker = _connected_broker()
        with patch.object(broker, "_request", side_effect=_mock_request(
                {"/positions": self._POS_LIST})):
            positions = broker.get_positions()

        self.assertEqual(len(positions), 2)
        aapl = positions[0]
        self.assertIsInstance(aapl, Position)
        self.assertEqual(aapl.symbol, "AAPL")
        self.assertEqual(aapl.side, Direction.LONG)
        self.assertAlmostEqual(aapl.lots, 5.0)
        self.assertAlmostEqual(aapl.open_price, 170.00)
        self.assertAlmostEqual(aapl.current_price, 175.00)
        self.assertAlmostEqual(aapl.profit, 25.00)

    def test_positions_ticket_is_int(self):
        broker = _connected_broker()
        with patch.object(broker, "_request", side_effect=_mock_request(
                {"/positions": self._POS_LIST})):
            positions = broker.get_positions()
        for pos in positions:
            self.assertIsInstance(pos.ticket, int)
            self.assertGreater(pos.ticket, 0)

    def test_positions_ticket_stable_across_calls(self):
        """The same symbol always produces the same ticket."""
        broker = _connected_broker()
        with patch.object(broker, "_request", side_effect=_mock_request(
                {"/positions": self._POS_LIST})):
            p1 = broker.get_positions()
        with patch.object(broker, "_request", side_effect=_mock_request(
                {"/positions": self._POS_LIST})):
            p2 = broker.get_positions()
        self.assertEqual(p1[0].ticket, p2[0].ticket)


class TestPlaceOrder(unittest.TestCase):

    _ORDER_RESP = {
        "id":               "abc-123-uuid",
        "symbol":           "AAPL",
        "side":             "buy",
        "type":             "market",
        "qty":              "10",
        "filled_avg_price": None,
        "status":           "accepted",
    }

    def _place(self, broker: AlpacaBroker, order_type=OrderType.BUY, lots=10.0,
               price=0.0):
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, body))
            return self._ORDER_RESP

        with patch.object(broker, "_request", side_effect=_capture):
            order = broker.place_order("AAPL", order_type, lots, price=price)
        return order, calls

    def test_place_buy_market_order(self):
        broker = _connected_broker()
        order, calls = self._place(broker, OrderType.BUY, lots=10.0)

        self.assertIsInstance(order, Order)
        self.assertEqual(order.symbol, "AAPL")
        self.assertEqual(order.type, OrderType.BUY)
        self.assertAlmostEqual(order.lots, 10.0)
        self.assertIsInstance(order.ticket, int)
        self.assertGreater(order.ticket, 0)

        method, path, body = calls[0]
        self.assertEqual(method, "POST")
        self.assertIn("/orders", path)
        self.assertEqual(body["side"], "buy")
        self.assertEqual(body["type"], "market")
        self.assertEqual(body["qty"], "10.0")
        self.assertEqual(body["symbol"], "AAPL")

    def test_place_sell_market_order(self):
        broker = _connected_broker()
        sell_resp = dict(self._ORDER_RESP)
        sell_resp.update({"side": "sell", "id": "def-456-uuid"})
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, body))
            return sell_resp

        with patch.object(broker, "_request", side_effect=_capture):
            order = broker.place_order("AAPL", OrderType.SELL, 5.0)

        self.assertEqual(order.type, OrderType.SELL)
        body = calls[0][2]
        self.assertEqual(body["side"], "sell")

    def test_place_limit_order_includes_limit_price(self):
        broker = _connected_broker()
        limit_resp = dict(self._ORDER_RESP, id="lim-789-uuid")
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, body))
            return limit_resp

        with patch.object(broker, "_request", side_effect=_capture):
            broker.place_order("AAPL", OrderType.BUY_LIMIT, 3.0, price=150.0)

        body = calls[0][2]
        self.assertEqual(body["type"], "limit")
        self.assertIn("limit_price", body)

    def test_lots_maps_to_qty_string(self):
        """lots=7.5 -> qty='7.5' in POST body (1 lot == 1 share)."""
        broker = _connected_broker()
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, body))
            return self._ORDER_RESP

        with patch.object(broker, "_request", side_effect=_capture):
            broker.place_order("AAPL", OrderType.BUY, lots=7.5)

        self.assertEqual(calls[0][2]["qty"], "7.5")

    def test_place_order_drops_sl_tp_honestly(self):
        """sl/tp are not transmittable on plain Alpaca orders.

        Contract: they must NOT appear in the POST body, and the returned
        Order must report sl/tp=0 (not echo the requested values) so callers
        cannot mistake un-sent stops for active ones.
        """
        broker = _connected_broker()
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, body))
            return self._ORDER_RESP

        with patch.object(broker, "_request", side_effect=_capture):
            order = broker.place_order("AAPL", OrderType.BUY, 10.0,
                                       sl=140.0, tp=160.0)

        # Returned Order is honest: no phantom stops
        self.assertEqual(order.sl, 0.0)
        self.assertEqual(order.tp, 0.0)
        # Body never carried sl/tp as stop_price/limit_price
        body = calls[0][2]
        self.assertNotIn("stop_price", body)
        self.assertNotIn("limit_price", body)


class TestGetTick(unittest.TestCase):

    _QUOTE_RESP = {
        "quote": {
            "bp": 175.10,
            "ap": 175.15,
            "bs": 100,
            "as": 200,
            "t":  "2024-01-15T14:30:00Z",
        }
    }

    def test_get_tick_hits_data_host(self):
        broker = _connected_broker()
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path))
            return self._QUOTE_RESP

        with patch.object(broker, "_request", side_effect=_capture):
            tick = broker.get_tick("AAPL")

        self.assertAlmostEqual(tick.bid, 175.10)
        self.assertAlmostEqual(tick.ask, 175.15)
        self.assertEqual(tick.symbol, "AAPL")
        self.assertIsInstance(tick.timestamp, int)
        self.assertGreater(tick.timestamp, 0)

        # Assert the request went to the data host, not the trading host
        _, hit_path = calls[0]
        self.assertIn(_DEFAULT_DATA_URL, hit_path)
        self.assertIn("AAPL", hit_path)
        self.assertIn("quotes/latest", hit_path)


class TestGetBars(unittest.TestCase):

    _BARS_RESP = {
        "bars": [
            {"t": "2024-01-15T14:00:00Z", "o": 174.0, "h": 176.0,
             "l": 173.5, "c": 175.5, "v": 1234567},
            {"t": "2024-01-15T15:00:00Z", "o": 175.5, "h": 177.0,
             "l": 175.0, "c": 176.8, "v": 987654},
        ]
    }

    def test_get_bars_hits_data_host(self):
        broker = _connected_broker()
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, params))
            return self._BARS_RESP

        with patch.object(broker, "_request", side_effect=_capture):
            bars = broker.get_bars("AAPL", tf=3600, count=2)

        self.assertEqual(len(bars), 2)
        self.assertIsInstance(bars[0], Bar)
        self.assertAlmostEqual(bars[0].open, 174.0)
        self.assertAlmostEqual(bars[0].high, 176.0)
        self.assertIsInstance(bars[0].timestamp, int)

        _, hit_path, hit_params = calls[0]
        self.assertIn(_DEFAULT_DATA_URL, hit_path)
        self.assertIn("AAPL", hit_path)
        self.assertIn("bars", hit_path)
        self.assertEqual(hit_params["timeframe"], "1Hour")
        self.assertEqual(hit_params["limit"], 2)

    def test_get_bars_timeframe_mapping(self):
        """tf=86400 (D1) -> timeframe='1Day'."""
        broker = _connected_broker()
        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, params))
            return self._BARS_RESP

        with patch.object(broker, "_request", side_effect=_capture):
            broker.get_bars("AAPL", tf=86400, count=5)

        _, _, params = calls[0]
        self.assertEqual(params["timeframe"], "1Day")


class TestCancelOrder(unittest.TestCase):

    def test_cancel_order_success(self):
        broker = _connected_broker()
        # Pre-register a ticket
        broker._alloc_ticket("order-uuid-999")
        ticket = broker._id_to_ticket["order-uuid-999"]

        with patch.object(broker, "_request", return_value={}):
            rc = broker.cancel_order(ticket)

        self.assertEqual(rc, 0)

    def test_cancel_order_unknown_ticket(self):
        broker = _connected_broker()
        rc = broker.cancel_order(99999)
        self.assertNotEqual(rc, 0)


class TestClosePosition(unittest.TestCase):

    def test_close_position_success(self):
        broker = _connected_broker()
        broker._alloc_ticket("AAPL")   # positions keyed by symbol
        ticket = broker._id_to_ticket["AAPL"]

        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, params))
            return {}

        with patch.object(broker, "_request", side_effect=_capture):
            rc = broker.close_position(ticket)

        self.assertEqual(rc, 0)
        method, path, params = calls[0]
        self.assertEqual(method, "DELETE")
        self.assertIn("AAPL", path)
        self.assertIsNone(params)   # full close, no qty

    def test_close_position_partial(self):
        broker = _connected_broker()
        broker._alloc_ticket("TSLA")
        ticket = broker._id_to_ticket["TSLA"]

        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, params))
            return {}

        with patch.object(broker, "_request", side_effect=_capture):
            rc = broker.close_position(ticket, lots=2.0)

        self.assertEqual(rc, 0)
        _, _, params = calls[0]
        self.assertEqual(params, {"qty": "2.0"})


class TestModifyPosition(unittest.TestCase):
    """modify_position limitation contract (S5).

    Alpaca exposes no SL/TP on filled positions. A position-keyed ticket
    (symbol, no hyphen) cannot be modified -> returns 1 and does NOT call
    the API. An order-keyed ticket (UUID, has hyphen) attempts a PATCH
    replace of the open order.
    """

    def test_modify_position_on_position_ticket_returns_one(self):
        broker = _connected_broker()
        broker._alloc_ticket("AAPL")   # position key (symbol, no hyphen)
        ticket = broker._id_to_ticket["AAPL"]

        # Must not touch the network for a position-keyed ticket.
        with patch.object(broker, "_request",
                          side_effect=AssertionError("must not call API")):
            rc = broker.modify_position(ticket, sl=140.0, tp=160.0)

        self.assertEqual(rc, 1)

    def test_modify_position_on_order_uuid_attempts_replace(self):
        broker = _connected_broker()
        broker._alloc_ticket("ord-abc-123")   # order key (UUID, has hyphen)
        ticket = broker._id_to_ticket["ord-abc-123"]

        calls: list[tuple] = []

        def _capture(method, path, *, params=None, body=None, headers=None):
            calls.append((method, path, body))
            return {}

        with patch.object(broker, "_request", side_effect=_capture):
            rc = broker.modify_position(ticket, sl=140.0, tp=160.0)

        self.assertEqual(rc, 0)
        method, path, body = calls[0]
        self.assertEqual(method, "PATCH")
        self.assertIn("ord-abc-123", path)
        self.assertEqual(body["stop_price"], "140.0")
        self.assertEqual(body["limit_price"], "160.0")

    def test_modify_position_unknown_ticket_returns_one(self):
        broker = _connected_broker()
        rc = broker.modify_position(99999, sl=1.0, tp=2.0)
        self.assertEqual(rc, 1)


class TestErrorPath(unittest.TestCase):

    def test_http_error_raises_broker_error(self):
        """An HTTP 4xx from urlopen raises BrokerError."""
        broker = _connected_broker()

        # Build a fake HTTPError
        http_err = urllib.error.HTTPError(
            url="https://paper-api.alpaca.markets/v2/account",
            code=401,
            msg="Unauthorized",
            hdrs={},  # type: ignore[arg-type]
            fp=BytesIO(b'{"message":"forbidden"}'),
        )

        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   side_effect=http_err):
            with self.assertRaises(BrokerError) as ctx:
                broker.get_account()

        self.assertEqual(ctx.exception.status, 401)


if __name__ == "__main__":
    unittest.main()
