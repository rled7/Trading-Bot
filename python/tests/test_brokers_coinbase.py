"""Tests for CoinbaseBroker (Coinbase Advanced Trade REST adapter).

All HTTP calls are mocked — no real network traffic.
The mock patches ``algoforge.brokers.base.urllib.request.urlopen``.

Test coverage:
    1.  _sign — deterministic HMAC-SHA256 against a hand-computed expected value
    2.  connect — success path (keys present, /accounts ping succeeds)
    3.  connect — missing credentials raise / return non-zero
    4.  get_account — fiat balances aggregated correctly
    5.  get_tick — bid/ask parsed from best_bid_ask response
    6.  get_symbol_info — base_increment / quote_increment parsed into SymbolInfo
    7.  get_bars — candle shapes parsed, oldest-first ordering
    8.  place_order — BUY market order; ticket mapping; response fields
    9.  place_order — SELL market order
    10. place_order — unsupported order type raises BrokerError
    11. place_order — exchange rejection (success:false) raises BrokerError
    12. get_positions — non-zero crypto balances → LONG positions
    13. get_orders — open orders parsed correctly
    14. cancel_order — batch_cancel success path (returns 0)
    15. cancel_order — unknown ticket returns non-zero
"""
from __future__ import annotations

import hashlib
import hmac
import io
import json
import time
import unittest
from unittest.mock import MagicMock, patch

from algoforge.broker import AccountInfo, Order, OrderType, Position, SymbolInfo, Tick
from algoforge.brokers.base import BrokerError
from algoforge.brokers.coinbase import CoinbaseBroker
from algoforge.brokers.config import BrokerConfig
from algoforge.types import Bar, Direction


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_API_KEY    = "test_api_key"
_API_SECRET = "test_api_secret"


def _make_config(*, paper: bool = True) -> BrokerConfig:
    return BrokerConfig(
        name="coinbase",
        paper=paper,
        api_key=_API_KEY,
        api_secret=_API_SECRET,
    )


def _make_resp(payload: object, status: int = 200) -> MagicMock:
    """Return a mock urllib response context-manager object."""
    raw = json.dumps(payload).encode("utf-8")
    resp = MagicMock()
    resp.read.return_value = raw
    resp.status = status
    resp.__enter__ = lambda s: s
    resp.__exit__ = MagicMock(return_value=False)
    return resp


def _urlopen_seq(*responses):
    """Return a side_effect that yields responses in sequence."""
    it = iter(responses)
    def _side(*args, **kwargs):
        return next(it)
    return _side


def _connected_broker(*extra_responses):
    """Return a CoinbaseBroker that has been connected with mocked HTTP.

    The first mock response is the /accounts ping in _on_connect.
    ``extra_responses`` are appended and returned for subsequent calls.
    """
    cfg = _make_config()
    broker = CoinbaseBroker(cfg)
    ping_resp = _make_resp({"accounts": []})
    responses = [ping_resp] + list(extra_responses)
    with patch("algoforge.brokers.base.urllib.request.urlopen",
               side_effect=_urlopen_seq(*responses)):
        broker.connect()
    # Manually mark connected so subsequent calls work without re-connecting
    broker._connected = True
    broker._base_url = broker.LIVE_URL
    return broker


# ---------------------------------------------------------------------------
# 1. _sign — deterministic HMAC
# ---------------------------------------------------------------------------

class TestSign(unittest.TestCase):
    """Verify _sign produces the expected HMAC-SHA256 hex digest."""

    def setUp(self):
        self.broker = CoinbaseBroker(_make_config())
        self.broker._base_url = CoinbaseBroker.LIVE_URL

    def test_signature_matches_hand_computed(self):
        """For a known secret, timestamp, method, path, body the digest must
        match an inline hand-computed expected value.

        Message format: {timestamp}{METHOD}{path}{body_json}
        Signing key: api_secret encoded as UTF-8.
        Expected computed separately from _sign to avoid circular verification.
        """
        secret    = _API_SECRET
        timestamp = "1716000000"
        method    = "GET"
        path      = "/api/v3/brokerage/accounts"
        body_str  = ""

        # Hand-compute expected — inline, NOT by calling _sign again
        expected_message = f"{timestamp}{method}{path}{body_str}"
        expected_sig = hmac.new(
            secret.encode("utf-8"),
            expected_message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()

        actual_sig = self.broker._sign(timestamp, method, path, body_str)

        self.assertEqual(actual_sig, expected_sig)

    def test_signature_includes_body(self):
        """Signature changes when body content changes."""
        ts = "1716000001"
        sig_empty = self.broker._sign(ts, "POST", "/api/v3/brokerage/orders", "")
        sig_body  = self.broker._sign(ts, "POST", "/api/v3/brokerage/orders", '{"key":"val"}')
        self.assertNotEqual(sig_empty, sig_body)

    def test_signature_differs_by_method(self):
        """GET and POST over the same path produce different signatures."""
        ts = "1716000002"
        sig_get  = self.broker._sign(ts, "GET",  "/api/v3/brokerage/accounts", "")
        sig_post = self.broker._sign(ts, "POST", "/api/v3/brokerage/accounts", "")
        self.assertNotEqual(sig_get, sig_post)


# ---------------------------------------------------------------------------
# 2. connect — success
# ---------------------------------------------------------------------------

class TestConnect(unittest.TestCase):

    def test_connect_success(self):
        """connect() returns 0 when credentials are present and ping succeeds."""
        cfg = _make_config()
        broker = CoinbaseBroker(cfg)
        resp = _make_resp({"accounts": []})
        with patch("algoforge.brokers.base.urllib.request.urlopen", return_value=resp):
            rc = broker.connect()
        self.assertEqual(rc, 0)
        self.assertTrue(broker.is_connected())

    def test_connect_missing_credentials(self):
        """connect() returns non-zero when api_key or api_secret is missing."""
        cfg = BrokerConfig(name="coinbase", api_key="", api_secret="")
        broker = CoinbaseBroker(cfg)
        rc = broker.connect()
        self.assertNotEqual(rc, 0)
        self.assertFalse(broker.is_connected())

    def test_connect_http_error_returns_nonzero(self):
        """connect() returns non-zero when the ping returns HTTP 401."""
        import urllib.error
        cfg = _make_config()
        broker = CoinbaseBroker(cfg)
        err = urllib.error.HTTPError(
            url="https://api.coinbase.com/api/v3/brokerage/accounts",
            code=401,
            msg="Unauthorized",
            hdrs={},  # type: ignore[arg-type]
            fp=None,
        )
        with patch("algoforge.brokers.base.urllib.request.urlopen", side_effect=err):
            rc = broker.connect()
        self.assertNotEqual(rc, 0)


# ---------------------------------------------------------------------------
# 4. get_account
# ---------------------------------------------------------------------------

class TestGetAccount(unittest.TestCase):

    def test_fiat_balances_aggregated(self):
        """get_account sums available + hold for USD accounts into balance."""
        accounts_payload = {
            "accounts": [
                {
                    "currency": "USD",
                    "available_balance": {"value": "1000.00", "currency": "USD"},
                    "hold": {"value": "200.00", "currency": "USD"},
                },
                {
                    "currency": "BTC",
                    "available_balance": {"value": "0.5", "currency": "BTC"},
                    "hold": {"value": "0.0", "currency": "BTC"},
                },
                {
                    "currency": "USDC",
                    "available_balance": {"value": "500.00", "currency": "USDC"},
                    "hold": {"value": "0.00", "currency": "USDC"},
                },
            ]
        }
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp(accounts_payload)):
            info = broker.get_account()

        # USD: 1000 + 200 = 1200; USDC: 500 + 0 = 500  → total 1700
        self.assertAlmostEqual(info.balance, 1700.0)
        self.assertAlmostEqual(info.equity,  1700.0)
        self.assertEqual(info.leverage, 1)
        self.assertEqual(info.margin, 0.0)

    def test_empty_accounts_returns_zero_balance(self):
        """get_account with no accounts returns zero balance."""
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp({"accounts": []})):
            info = broker.get_account()
        self.assertEqual(info.balance, 0.0)
        self.assertIsInstance(info, AccountInfo)


# ---------------------------------------------------------------------------
# 5. get_tick
# ---------------------------------------------------------------------------

class TestGetTick(unittest.TestCase):

    def test_bid_ask_parsed(self):
        """get_tick returns correct bid and ask from best_bid_ask response."""
        payload = {
            "pricebooks": [
                {
                    "product_id": "BTC-USD",
                    "bids": [{"price": "67000.00", "size": "0.5"}],
                    "asks": [{"price": "67001.50", "size": "0.3"}],
                    "time": "2024-01-01T00:00:00Z",
                }
            ]
        }
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp(payload)):
            tick = broker.get_tick("BTCUSD")

        self.assertAlmostEqual(tick.bid, 67000.00)
        self.assertAlmostEqual(tick.ask, 67001.50)
        self.assertEqual(tick.symbol, "BTCUSD")
        self.assertIsInstance(tick, Tick)

    def test_empty_pricebooks_raises(self):
        """get_tick raises BrokerError when pricebooks list is empty."""
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp({"pricebooks": []})):
            with self.assertRaises(BrokerError):
                broker.get_tick("BTCUSD")


# ---------------------------------------------------------------------------
# 6. get_symbol_info
# ---------------------------------------------------------------------------

class TestGetSymbolInfo(unittest.TestCase):

    def test_symbol_info_parsed(self):
        """get_symbol_info extracts base_increment / quote_increment correctly."""
        payload = {
            "product_id":     "BTC-USD",
            "base_increment": "0.00000001",
            "quote_increment":"0.01",
            "base_min_size":  "0.001",
            "base_max_size":  "10000",
        }
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp(payload)):
            info = broker.get_symbol_info("BTCUSD")

        self.assertIsInstance(info, SymbolInfo)
        self.assertAlmostEqual(info.point, 0.01)
        self.assertAlmostEqual(info.volume_min, 0.001)
        self.assertAlmostEqual(info.volume_step, 0.00000001)
        self.assertEqual(info.contract_size, 1.0)
        self.assertEqual(info.digits, 2)


# ---------------------------------------------------------------------------
# 7. get_bars
# ---------------------------------------------------------------------------

class TestGetBars(unittest.TestCase):

    def _make_candle(self, start: int, open_: float, high: float,
                     low: float, close: float, volume: float) -> dict:
        return {
            "start":  str(start),
            "open":   str(open_),
            "high":   str(high),
            "low":    str(low),
            "close":  str(close),
            "volume": str(volume),
        }

    def test_candles_parsed_oldest_first(self):
        """get_bars returns Bar objects in oldest-first order."""
        # CB returns newest-first; two candles here:
        candles = [
            self._make_candle(1716000060, 100.0, 105.0, 99.0,  103.0, 50.0),  # newer
            self._make_candle(1716000000, 98.0,  101.0, 97.0,  100.0, 40.0),  # older
        ]
        payload = {"candles": candles}
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp(payload)):
            bars = broker.get_bars("BTCUSD", tf=60, count=2)

        self.assertEqual(len(bars), 2)
        # After reversing, the older candle should come first
        self.assertEqual(bars[0].timestamp, 1716000000)
        self.assertEqual(bars[1].timestamp, 1716000060)
        self.assertAlmostEqual(bars[0].open,  98.0)
        self.assertAlmostEqual(bars[0].high,  101.0)
        self.assertAlmostEqual(bars[0].low,   97.0)
        self.assertAlmostEqual(bars[0].close, 100.0)
        self.assertAlmostEqual(bars[0].volume, 40.0)
        self.assertIsInstance(bars[0], Bar)

    def test_unsupported_timeframe_raises(self):
        """get_bars raises BrokerError for unsupported timeframes (e.g. 15 s)."""
        broker = _connected_broker()
        with self.assertRaises(BrokerError):
            broker.get_bars("BTCUSD", tf=15, count=10)  # S15 not supported by CB


# ---------------------------------------------------------------------------
# 8 & 9. place_order — BUY and SELL
# ---------------------------------------------------------------------------

class TestPlaceOrder(unittest.TestCase):

    def _place_resp(self, order_id: str, side: str) -> MagicMock:
        return _make_resp({
            "success": True,
            "success_response": {
                "order_id":       order_id,
                "product_id":     "BTC-USD",
                "side":           side,
                "client_order_id": "af-123",
            },
        })

    def test_place_buy_order(self):
        """place_order BUY returns Order with correct fields and positive ticket."""
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=self._place_resp("ord-abc-buy", "BUY")):
            order = broker.place_order("BTCUSD", OrderType.BUY, lots=0.01, magic=42)

        self.assertIsInstance(order, Order)
        self.assertGreater(order.ticket, 0)
        self.assertEqual(order.type, OrderType.BUY)
        self.assertAlmostEqual(order.lots, 0.01)
        self.assertEqual(order.symbol, "BTCUSD")

    def test_place_sell_order(self):
        """place_order SELL returns Order with SELL type."""
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=self._place_resp("ord-abc-sell", "SELL")):
            order = broker.place_order("BTCUSD", OrderType.SELL, lots=0.005)

        self.assertEqual(order.type, OrderType.SELL)
        self.assertAlmostEqual(order.lots, 0.005)

    def test_ticket_mapping_is_unique(self):
        """Two distinct orders receive distinct integer tickets."""
        broker = _connected_broker()
        resps = _urlopen_seq(
            self._place_resp("ord-001", "BUY"),
            self._place_resp("ord-002", "SELL"),
        )
        with patch("algoforge.brokers.base.urllib.request.urlopen", side_effect=resps):
            o1 = broker.place_order("BTCUSD", OrderType.BUY,  lots=0.01)
            o2 = broker.place_order("BTCUSD", OrderType.SELL, lots=0.01)

        self.assertNotEqual(o1.ticket, o2.ticket)

    def test_place_order_unsupported_type_raises(self):
        """place_order with BUY_LIMIT raises BrokerError (not supported)."""
        broker = _connected_broker()
        with self.assertRaises(BrokerError):
            broker.place_order("BTCUSD", OrderType.BUY_LIMIT, lots=0.01, price=65000.0)

    def test_place_order_exchange_rejection_raises(self):
        """place_order raises BrokerError when success:false is returned."""
        broker = _connected_broker()
        rejection = _make_resp({
            "success": False,
            "error_response": {
                "error":   "INSUFFICIENT_FUND",
                "message": "Insufficient funds",
            },
        })
        with patch("algoforge.brokers.base.urllib.request.urlopen", return_value=rejection):
            with self.assertRaises(BrokerError):
                broker.place_order("BTCUSD", OrderType.BUY, lots=100.0)


# ---------------------------------------------------------------------------
# 12. get_positions
# ---------------------------------------------------------------------------

class TestGetPositions(unittest.TestCase):

    def test_nonzero_crypto_balances_become_positions(self):
        """Non-zero, non-fiat balances are returned as LONG positions."""
        accounts_payload = {
            "accounts": [
                {
                    "currency": "USD",
                    "available_balance": {"value": "5000.00", "currency": "USD"},
                    "hold": {"value": "0.00", "currency": "USD"},
                },
                {
                    "currency": "BTC",
                    "available_balance": {"value": "0.25", "currency": "BTC"},
                    "hold": {"value": "0.05", "currency": "BTC"},
                },
                {
                    "currency": "ETH",
                    "available_balance": {"value": "0.00", "currency": "ETH"},
                    "hold": {"value": "0.00", "currency": "ETH"},
                },
            ]
        }
        # Second call is get_tick for BTC-USD price lookup
        tick_payload = {
            "pricebooks": [{
                "product_id": "BTC-USD",
                "bids": [{"price": "67000.00", "size": "1.0"}],
                "asks": [{"price": "67001.00", "size": "1.0"}],
            }]
        }
        broker = _connected_broker()
        resps = _urlopen_seq(
            _make_resp(accounts_payload),
            _make_resp(tick_payload),
        )
        with patch("algoforge.brokers.base.urllib.request.urlopen", side_effect=resps):
            positions = broker.get_positions()

        # Only BTC has non-zero balance (ETH is 0, USD is fiat)
        self.assertEqual(len(positions), 1)
        pos = positions[0]
        self.assertIsInstance(pos, Position)
        self.assertEqual(pos.symbol, "BTCUSD")
        self.assertEqual(pos.side, Direction.LONG)
        self.assertAlmostEqual(pos.lots, 0.30)  # 0.25 + 0.05


# ---------------------------------------------------------------------------
# 13. get_orders
# ---------------------------------------------------------------------------

class TestGetOrders(unittest.TestCase):

    def test_open_orders_parsed(self):
        """get_orders returns Order objects with correct fields."""
        payload = {
            "orders": [
                {
                    "order_id":              "ord-open-001",
                    "product_id":            "BTC-USD",
                    "side":                  "BUY",
                    "order_type":            "MARKET",
                    "filled_size":           "0.01",
                    "average_filled_price":  "67000.00",
                    "status":                "OPEN",
                }
            ]
        }
        broker = _connected_broker()
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp(payload)):
            orders = broker.get_orders()

        self.assertEqual(len(orders), 1)
        o = orders[0]
        self.assertIsInstance(o, Order)
        self.assertEqual(o.type, OrderType.BUY)
        self.assertAlmostEqual(o.lots, 0.01)
        self.assertAlmostEqual(o.fill_price, 67000.00)
        self.assertEqual(o.symbol, "BTCUSD")


# ---------------------------------------------------------------------------
# 14 & 15. cancel_order
# ---------------------------------------------------------------------------

class TestCancelOrder(unittest.TestCase):

    def test_cancel_order_success(self):
        """cancel_order returns 0 when batch_cancel reports success."""
        broker = _connected_broker()
        # Register a fake order ticket
        ticket = broker._register_order_id("ord-to-cancel")
        payload = {
            "results": [{"order_id": "ord-to-cancel", "success": True}]
        }
        with patch("algoforge.brokers.base.urllib.request.urlopen",
                   return_value=_make_resp(payload)):
            rc = broker.cancel_order(ticket)

        self.assertEqual(rc, 0)

    def test_cancel_unknown_ticket_returns_nonzero(self):
        """cancel_order returns non-zero for an unrecognised ticket (9999)."""
        broker = _connected_broker()
        rc = broker.cancel_order(9999)
        self.assertNotEqual(rc, 0)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main()
