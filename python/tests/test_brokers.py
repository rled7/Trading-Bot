"""Unit tests for the algoforge.brokers package (S5 foundation).

Covers the registry/factory, env-var credential loading, paper/live endpoint
resolution, canonical<->broker symbol mapping, and the not-yet-implemented
trading surface. No network calls are made.

Run with:
    cd python && python -m pytest tests/test_brokers.py -v
"""
from __future__ import annotations

import unittest

from algoforge.broker import IBroker
from algoforge.brokers import (BrokerConfig, RestBroker, available_brokers,
                               make_broker)
from algoforge.brokers.binance import BinanceBroker
from algoforge.brokers.coinbase import CoinbaseBroker
from algoforge.brokers.oanda import OandaBroker
from algoforge.paper_broker import PaperBroker


class TestRegistry(unittest.TestCase):
    def test_available_brokers_lists_all(self):
        names = available_brokers()
        for expected in ("paper", "mt5", "oanda", "alpaca", "ibkr",
                         "binance", "coinbase"):
            self.assertIn(expected, names)

    def test_paper_routes_to_paper_broker(self):
        broker = make_broker("paper", balance=5_000.0)
        self.assertIsInstance(broker, PaperBroker)
        self.assertEqual(broker.connect(), 0)

    def test_rest_adapter_is_ibroker(self):
        broker = make_broker("oanda", paper=True)
        self.assertIsInstance(broker, OandaBroker)
        self.assertIsInstance(broker, RestBroker)
        self.assertIsInstance(broker, IBroker)
        self.assertEqual(broker.broker_name(), "oanda")

    def test_unknown_broker_raises(self):
        with self.assertRaises(ValueError):
            make_broker("dogecoin-casino")

    def test_case_insensitive(self):
        self.assertIsInstance(make_broker("OANDA"), OandaBroker)


class TestEndpointResolution(unittest.TestCase):
    def test_paper_vs_live_url(self):
        paper = make_broker("oanda", paper=True)
        live = make_broker("oanda", paper=False)
        self.assertIn("fxpractice", paper._resolve_base_url())
        self.assertIn("fxtrade", live._resolve_base_url())

    def test_explicit_base_url_override(self):
        cfg = BrokerConfig(name="oanda", base_url="https://example.test/v3/")
        broker = OandaBroker(cfg)
        self.assertEqual(broker._resolve_base_url(), "https://example.test/v3")


class TestConfigFromEnv(unittest.TestCase):
    def test_reads_prefixed_vars(self):
        env = {
            "AF_OANDA_API_KEY": "key123",
            "AF_OANDA_ACCOUNT": "acct-9",
        }
        cfg = BrokerConfig.from_env("oanda", paper=True, environ=env)
        self.assertEqual(cfg.api_key, "key123")
        self.assertEqual(cfg.account_id, "acct-9")
        self.assertTrue(cfg.paper)

    def test_env_paper_override(self):
        env = {"AF_OANDA_PAPER": "false"}
        cfg = BrokerConfig.from_env("oanda", paper=True, environ=env)
        self.assertFalse(cfg.paper)

    def test_require_raises_on_missing(self):
        cfg = BrokerConfig(name="oanda")
        with self.assertRaises(ValueError):
            cfg.require("api_key", "account_id")


class TestSymbolMapping(unittest.TestCase):
    def test_oanda_underscore(self):
        b = OandaBroker()
        self.assertEqual(b._to_broker_symbol("EURUSD"), "EUR_USD")
        self.assertEqual(b._to_broker_symbol("EUR/USD"), "EUR_USD")
        self.assertEqual(b._from_broker_symbol("EUR_USD"), "EURUSD")

    def test_binance_concat(self):
        b = BinanceBroker()
        self.assertEqual(b._to_broker_symbol("BTC/USD"), "BTCUSD")

    def test_coinbase_hyphen(self):
        b = CoinbaseBroker()
        self.assertEqual(b._to_broker_symbol("BTCUSD"), "BTC-USD")
        self.assertEqual(b._to_broker_symbol("BTC/USD"), "BTC-USD")


class TestUnimplementedSurface(unittest.TestCase):
    def test_trading_methods_raise_notimplemented(self):
        broker = make_broker("oanda", paper=True)
        for call in (
            lambda: broker.get_account(),
            lambda: broker.get_tick("EURUSD"),
            lambda: broker.place_order("EURUSD", 0, 0.1),
            lambda: broker.get_positions(),
        ):
            with self.assertRaises(NotImplementedError):
                call()


if __name__ == "__main__":
    unittest.main()
