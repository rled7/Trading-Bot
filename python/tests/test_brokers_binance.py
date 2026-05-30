"""Tests for BinanceBroker (Binance Spot REST adapter).

All HTTP calls are mocked — no real network traffic.
The mock patches ``algoforge.brokers.base.urllib.request.urlopen``.
"""
from __future__ import annotations

import hashlib
import hmac
import io
import json
import time
import unittest
import urllib.parse
from unittest.mock import MagicMock, patch

from algoforge.broker import AccountInfo, Order, OrderType, Position, Tick
from algoforge.brokers.base import BrokerError
from algoforge.brokers.binance import BinanceBroker
from algoforge.brokers.config import BrokerConfig
from algoforge.types import Bar, Direction


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_config(*, paper: bool = True) -> BrokerConfig:
    return BrokerConfig(
        name="binance",
        paper=paper,
        api_key="test_api_key",
        api_secret="test_api_secret",
    )


def _make_resp(payload: object, status: int = 200) -> MagicMock:
    """Return a mock urllib response object."""
    raw = json.dumps(payload).encode("utf-8")
    resp = MagicMock()
    resp.read.return_value = raw
    resp.status = status
    resp.__enter__ = lambda s: s
    resp.__exit__ = MagicMock(return_value=False)
    return resp


def _urlopen_side_effect(*responses):
    """Produce a side_effect callable that returns responses in order."""
    it = iter(responses)
    def _side(*args, **kwargs):
        return next(it)
    return _side


# ---------------------------------------------------------------------------
# 1. _signed_params — deterministic HMAC test
# ---------------------------------------------------------------------------

class TestSignedParams(unittest.TestCase):
    """Verify the HMAC-SHA256 signature is computed correctly."""

    def setUp(self):
        cfg = _make_config()
        self.broker = BinanceBroker(cfg)
        self.broker._base_url = "https://testnet.binance.vision"

    def test_signature_matches_hand_computed(self):
        """
        For a known secret + known params, the signature must match a
        hand-computed HMAC-SHA256.  We freeze the timestamp by patching
        time.time so the output is deterministic.
        """
        secret = "test_api_secret"
        fixed_ts = 1_700_000_000_000  # fixed millisecond timestamp
        params_in = {"symbol": "BTCUSDT", "side": "BUY", "type": "MARKET", "quantity": "0.001"}

        with patch("algoforge.brokers.binance.time.time", return_value=fixed_ts / 1000):
            result = self.broker._signed_params(dict(params_in))

        # Reconstruct expected query string (same order as _signed_params builds)
        expected_params = dict(params_in)
        expected_params["timestamp"] = fixed_ts
        expected_params["recvWindow"] = 5000
        query_string = urllib.parse.urlencode(expected_params)
        expected_sig = hmac.new(
            secret.encode("utf-8"),
            query_string.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()

        self.assertEqual(result["signature"], expected_sig)
        self.assertEqual(result["timestamp"], fixed_ts)
        self.assertEqual(result["recvWindow"], 5000)

    def test_signature_changes_with_different_secret(self):
        """Different secrets must produce different signatures."""
        cfg2 = BrokerConfig(name="binance", api_key="k", api_secret="other_secret")
        broker2 = BinanceBroker(cfg2)
        broker2._base_url = "https://testnet.binance.vision"

        with patch("algoforge.brokers.binance.time.time", return_value=1_700_000.0):
            sig1 = self.broker._signed_params({"a": "1"})["signature"]
            sig2 = broker2._signed_params({"a": "1"})["signature"]

        self.assertNotEqual(sig1, sig2)


# ---------------------------------------------------------------------------
# 2. connect / _on_connect
# ---------------------------------------------------------------------------

class TestConnect(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_connect_success(self, mock_urlopen):
        """connect() pings the API, calls /api/v3/account, sets connected."""
        mock_urlopen.side_effect = _urlopen_side_effect(
            _make_resp({}),                      # /api/v3/ping
            _make_resp({"balances": []}),         # /api/v3/account (signed)
        )
        result = self.broker.connect()
        self.assertEqual(result, 0)
        self.assertTrue(self.broker.is_connected())
        self.assertEqual(mock_urlopen.call_count, 2)

    def test_connect_missing_credentials_raises(self):
        """connect() must return non-zero when api_secret is missing."""
        cfg = BrokerConfig(name="binance", api_key="k", api_secret="")
        broker = BinanceBroker(cfg)
        result = broker.connect()
        self.assertNotEqual(result, 0)
        self.assertFalse(broker.is_connected())


# ---------------------------------------------------------------------------
# 3. get_account
# ---------------------------------------------------------------------------

class TestGetAccount(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_account_sums_stablecoins(self, mock_urlopen):
        payload = {
            "balances": [
                {"asset": "USDT", "free": "1000.00", "locked": "200.00"},
                {"asset": "BTC",  "free": "0.5",     "locked": "0.0"},
                {"asset": "BUSD", "free": "300.00",  "locked": "0.0"},
            ]
        }
        mock_urlopen.return_value = _make_resp(payload)
        info = self.broker.get_account()
        self.assertIsInstance(info, AccountInfo)
        self.assertAlmostEqual(info.balance, 1500.0)
        self.assertEqual(info.currency, "USDT")
        self.assertEqual(info.leverage, 1)

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_account_empty_balances(self, mock_urlopen):
        mock_urlopen.return_value = _make_resp({"balances": []})
        info = self.broker.get_account()
        self.assertAlmostEqual(info.balance, 0.0)


# ---------------------------------------------------------------------------
# 4. get_tick
# ---------------------------------------------------------------------------

class TestGetTick(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_tick_parses_bid_ask(self, mock_urlopen):
        payload = {
            "symbol": "BTCUSDT",
            "bidPrice": "29000.50",
            "bidQty": "1.0",
            "askPrice": "29001.00",
            "askQty": "0.5",
        }
        mock_urlopen.return_value = _make_resp(payload)
        tick = self.broker.get_tick("BTC/USDT")
        self.assertIsInstance(tick, Tick)
        self.assertEqual(tick.symbol, "BTC/USDT")
        self.assertAlmostEqual(tick.bid, 29000.50)
        self.assertAlmostEqual(tick.ask, 29001.00)
        self.assertGreater(tick.timestamp, 0)

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_tick_symbol_normalised(self, mock_urlopen):
        """BTC_USDT should be normalised to BTCUSDT in the URL."""
        mock_urlopen.return_value = _make_resp(
            {"bidPrice": "1.0", "askPrice": "1.1"}
        )
        self.broker.get_tick("BTC_USDT")
        call_args = mock_urlopen.call_args[0][0]
        url = call_args.full_url
        self.assertIn("BTCUSDT", url)


# ---------------------------------------------------------------------------
# 5. get_bars / klines parsing
# ---------------------------------------------------------------------------

class TestGetBars(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_bars_parses_klines(self, mock_urlopen):
        """Kline array should map correctly to Bar objects."""
        klines = [
            # open_time, open, high, low, close, volume, close_time, ...
            [1700000000000, "29000.0", "29100.0", "28900.0", "29050.0", "100.5",
             1700000059999, "2000000", 10, "50.0", "1000000", "0"],
            [1700000060000, "29050.0", "29200.0", "29000.0", "29150.0", "80.0",
             1700000119999, "2200000", 8, "40.0", "1200000", "0"],
        ]
        mock_urlopen.return_value = _make_resp(klines)
        bars = self.broker.get_bars("BTCUSDT", 60, 2)
        self.assertEqual(len(bars), 2)
        b = bars[0]
        self.assertIsInstance(b, Bar)
        self.assertEqual(b.timestamp, 1700000000000)
        self.assertAlmostEqual(b.open, 29000.0)
        self.assertAlmostEqual(b.high, 29100.0)
        self.assertAlmostEqual(b.low, 28900.0)
        self.assertAlmostEqual(b.close, 29050.0)
        self.assertAlmostEqual(b.volume, 100.5)

    def test_get_bars_unsupported_timeframe(self):
        """Unsupported timeframe should raise BrokerError."""
        with self.assertRaises(BrokerError):
            self.broker.get_bars("BTCUSDT", 999, 10)


# ---------------------------------------------------------------------------
# 6. place_order — BUY and SELL
# ---------------------------------------------------------------------------

class TestPlaceOrder(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    def _order_resp(self, side: str, order_id: int = 12345) -> dict:
        return {
            "orderId": order_id,
            "symbol": "BTCUSDT",
            "status": "FILLED",
            "side": side,
            "type": "MARKET",
            "origQty": "0.001",
            "executedQty": "0.001",
            "cummulativeQuoteQty": "29.05",
            "fills": [{"price": "29050.0", "qty": "0.001", "commission": "0"}],
        }

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_place_order_buy(self, mock_urlopen):
        mock_urlopen.return_value = _make_resp(self._order_resp("BUY", 111))
        order = self.broker.place_order("BTC/USDT", OrderType.BUY, 0.001)
        self.assertIsInstance(order, Order)
        self.assertEqual(order.ticket, 111)
        self.assertEqual(order.type, OrderType.BUY)
        self.assertAlmostEqual(order.lots, 0.001)
        self.assertAlmostEqual(order.fill_price, 29050.0)

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_place_order_sell(self, mock_urlopen):
        mock_urlopen.return_value = _make_resp(self._order_resp("SELL", 222))
        order = self.broker.place_order("BTC/USDT", OrderType.SELL, 0.001)
        self.assertEqual(order.ticket, 222)
        self.assertEqual(order.type, OrderType.SELL)

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_place_order_fill_price_from_cumulative(self, mock_urlopen):
        """When fills list absent, fill_price computed from cummulativeQuoteQty / executedQty."""
        resp = {
            "orderId": 333,
            "executedQty": "0.002",
            "cummulativeQuoteQty": "60.0",
            "origQty": "0.002",
            "side": "BUY",
        }
        mock_urlopen.return_value = _make_resp(resp)
        order = self.broker.place_order("BTC/USDT", OrderType.BUY, 0.002)
        self.assertAlmostEqual(order.fill_price, 30000.0)

    def test_place_order_rejects_limit_type(self):
        with self.assertRaises(BrokerError):
            self.broker.place_order("BTCUSDT", OrderType.BUY_LIMIT, 0.001)


# ---------------------------------------------------------------------------
# 7. balances → positions
# ---------------------------------------------------------------------------

class TestGetPositions(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_non_zero_balances_become_positions(self, mock_urlopen):
        payload = {
            "balances": [
                {"asset": "USDT", "free": "1000.0", "locked": "0.0"},   # stablecoin skipped
                {"asset": "BTC",  "free": "0.5",    "locked": "0.1"},   # → LONG position
                {"asset": "ETH",  "free": "2.0",    "locked": "0.0"},   # → LONG position
                {"asset": "SOL",  "free": "0.0",    "locked": "0.0"},   # zero → skipped
            ]
        }
        mock_urlopen.return_value = _make_resp(payload)
        positions = self.broker.get_positions()
        self.assertEqual(len(positions), 2)
        assets = {p.symbol for p in positions}
        self.assertIn("BTCUSDT", assets)
        self.assertIn("ETHUSDT", assets)
        for p in positions:
            self.assertIsInstance(p, Position)
            self.assertEqual(p.side, Direction.LONG)
            self.assertGreater(p.lots, 0)

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_btc_lots_equals_free_plus_locked(self, mock_urlopen):
        payload = {
            "balances": [
                {"asset": "BTC", "free": "0.3", "locked": "0.2"},
            ]
        }
        mock_urlopen.return_value = _make_resp(payload)
        positions = self.broker.get_positions()
        self.assertEqual(len(positions), 1)
        self.assertAlmostEqual(positions[0].lots, 0.5)


# ---------------------------------------------------------------------------
# 8. get_orders
# ---------------------------------------------------------------------------

class TestGetOrders(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_orders_maps_fields(self, mock_urlopen):
        payload = [
            {
                "orderId": 999,
                "symbol": "BTCUSDT",
                "side": "BUY",
                "type": "LIMIT",
                "origQty": "0.01",
                "price": "28000.0",
                "cummulativeQuoteQty": "0.0",
            }
        ]
        mock_urlopen.return_value = _make_resp(payload)
        orders = self.broker.get_orders()
        self.assertEqual(len(orders), 1)
        o = orders[0]
        self.assertEqual(o.ticket, 999)
        self.assertEqual(o.type, OrderType.BUY)
        self.assertAlmostEqual(o.price, 28000.0)


# ---------------------------------------------------------------------------
# 9. cancel_order
# ---------------------------------------------------------------------------

class TestCancelOrder(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_cancel_existing_order(self, mock_urlopen):
        """cancel_order(ticket) should find and DELETE the order."""
        open_orders = [
            {"orderId": 555, "symbol": "BTCUSDT", "side": "BUY",
             "origQty": "0.01", "price": "28000.0"}
        ]
        mock_urlopen.side_effect = _urlopen_side_effect(
            _make_resp(open_orders),    # GET /api/v3/openOrders
            _make_resp({"orderId": 555, "status": "CANCELED"}),  # DELETE
        )
        result = self.broker.cancel_order(555)
        self.assertEqual(result, 0)

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_cancel_nonexistent_order_returns_nonzero(self, mock_urlopen):
        """cancel_order for unknown ticket should return non-zero."""
        mock_urlopen.return_value = _make_resp([])  # empty open orders
        result = self.broker.cancel_order(9999)
        self.assertNotEqual(result, 0)


# ---------------------------------------------------------------------------
# 10. Error path — HTTP 400 from Binance
# ---------------------------------------------------------------------------

class TestErrorHandling(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_http_error_raises_broker_error(self, mock_urlopen):
        import urllib.error
        err = urllib.error.HTTPError(
            url="https://testnet.binance.vision/api/v3/order",
            code=400,
            msg="Bad Request",
            hdrs={},
            fp=io.BytesIO(b'{"code":-1100,"msg":"Illegal characters found"}'),
        )
        mock_urlopen.side_effect = err
        with self.assertRaises(BrokerError) as ctx:
            self.broker.get_tick("BTCUSDT")
        self.assertEqual(ctx.exception.status, 400)


# ---------------------------------------------------------------------------
# 11. get_position — filter by ticket
# ---------------------------------------------------------------------------

class TestGetPosition(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_position_returns_matching(self, mock_urlopen):
        payload = {
            "balances": [
                {"asset": "BTC", "free": "0.5", "locked": "0.0"},
            ]
        }
        mock_urlopen.return_value = _make_resp(payload)
        positions = self.broker.get_positions()
        self.assertEqual(len(positions), 1)
        ticket = positions[0].ticket

        # Reset mock for second call
        mock_urlopen.return_value = _make_resp(payload)
        pos = self.broker.get_position(ticket)
        self.assertIsNotNone(pos)
        self.assertEqual(pos.ticket, ticket)

    @patch("algoforge.brokers.base.urllib.request.urlopen")
    def test_get_position_unknown_ticket_returns_none(self, mock_urlopen):
        mock_urlopen.return_value = _make_resp({"balances": []})
        pos = self.broker.get_position(9999999)
        self.assertIsNone(pos)


# ---------------------------------------------------------------------------
# 12. to_broker_symbol
# ---------------------------------------------------------------------------

class TestSymbolMapping(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())

    def test_slash_removed(self):
        self.assertEqual(self.broker._to_broker_symbol("BTC/USDT"), "BTCUSDT")

    def test_underscore_removed(self):
        self.assertEqual(self.broker._to_broker_symbol("ETH_USDT"), "ETHUSDT")

    def test_already_normalised(self):
        self.assertEqual(self.broker._to_broker_symbol("BTCUSDT"), "BTCUSDT")

    def test_lowercase_uppercased(self):
        self.assertEqual(self.broker._to_broker_symbol("btcusdt"), "BTCUSDT")


# ---------------------------------------------------------------------------
# 13. modify_position — no-op, returns 0
# ---------------------------------------------------------------------------

class TestModifyPosition(unittest.TestCase):

    def setUp(self):
        self.broker = BinanceBroker(_make_config())
        self.broker._base_url = "https://testnet.binance.vision"
        self.broker._connected = True

    def test_modify_position_returns_zero(self):
        """modify_position is a no-op on Spot — must return 0."""
        result = self.broker.modify_position(12345, sl=28000.0, tp=31000.0)
        self.assertEqual(result, 0)


if __name__ == "__main__":
    unittest.main()
