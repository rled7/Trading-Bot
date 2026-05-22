"""Unit tests for algoforge.mt5_broker.MT5Broker.

All MT5 interaction is mocked via ``unittest.mock.patch`` so these tests
run correctly on Linux / macOS (no MetaTrader5 package required).

Run with:
    cd python && python -m pytest tests/test_mt5_broker.py -v
"""
from __future__ import annotations

import unittest
from unittest.mock import MagicMock, patch, PropertyMock
from types import SimpleNamespace

from algoforge.broker import AccountInfo, Order, OrderType, Position, Tick
from algoforge.types import Bar, Direction, Timeframe


# ---------------------------------------------------------------------------
# Helpers — reusable MT5 stub constants
# ---------------------------------------------------------------------------

# Numeric constants that MT5 normally exports as module-level ints.
_MT5_CONSTANTS = {
    # Timeframes
    "TIMEFRAME_M1":  1,
    "TIMEFRAME_M5":  5,
    "TIMEFRAME_M15": 15,
    "TIMEFRAME_H1":  60,
    "TIMEFRAME_H4":  240,
    "TIMEFRAME_D1":  1440,
    # Trade actions
    "TRADE_ACTION_DEAL":   1,
    "TRADE_ACTION_SLTP":   6,
    "TRADE_ACTION_REMOVE": 8,
    # Order types
    "ORDER_TYPE_BUY":  0,
    "ORDER_TYPE_SELL": 1,
    # Order time / filling
    "ORDER_TIME_GTC":    0,
    "ORDER_FILLING_IOC": 1,
    # Return codes
    "TRADE_RETCODE_DONE": 10009,
}


def _make_mt5_module(**overrides) -> MagicMock:
    """Return a MagicMock that pretends to be the MetaTrader5 module."""
    m = MagicMock()
    for name, val in {**_MT5_CONSTANTS, **overrides}.items():
        setattr(m, name, val)
    return m


def _make_account_ns(**kwargs) -> SimpleNamespace:
    defaults = dict(
        balance=10_000.0, equity=10_050.0, margin=200.0,
        margin_free=9_800.0, profit=50.0, leverage=100,
        currency="USD", login=12345678,
    )
    defaults.update(kwargs)
    return SimpleNamespace(**defaults)


def _make_tick_ns(**kwargs) -> SimpleNamespace:
    defaults = dict(bid=1.10990, ask=1.11000, volume=100, time=1_700_000_000)
    defaults.update(kwargs)
    return SimpleNamespace(**defaults)


def _make_position_ns(**kwargs) -> SimpleNamespace:
    defaults = dict(
        ticket=1001, symbol="EURUSD", type=0,  # 0=LONG
        volume=0.10, price_open=1.10500, price_current=1.10990,
        sl=1.10000, tp=1.12000, profit=49.0,
        commission=-0.70, swap=0.0, magic=42, comment="test",
    )
    defaults.update(kwargs)
    return SimpleNamespace(**defaults)


def _make_order_ns(**kwargs) -> SimpleNamespace:
    defaults = dict(
        ticket=2001, symbol="EURUSD", type=2,  # 2=BUY_LIMIT
        volume_initial=0.10, price_open=1.09000,
        sl=1.08500, tp=1.10500, magic=42, comment="limit",
    )
    defaults.update(kwargs)
    return SimpleNamespace(**defaults)


def _make_send_result(retcode: int = 10009, order: int = 3001,
                      price: float = 1.11000) -> SimpleNamespace:
    return SimpleNamespace(retcode=retcode, order=order, price=price)


# ---------------------------------------------------------------------------
# Helper: import MT5Broker with a given mt5 mock already in place
# ---------------------------------------------------------------------------

def _import_broker_with_mock(mt5_mock: MagicMock):
    """
    Force-reload mt5_broker so _MT5_AVAILABLE and _TF_MAP are re-evaluated
    with the mock installed.
    """
    import importlib
    import sys

    # Plant the fake module under the real package name
    sys.modules["MetaTrader5"] = mt5_mock

    # Remove any cached version so the module re-runs its import block
    for key in list(sys.modules.keys()):
        if "mt5_broker" in key:
            del sys.modules[key]

    import algoforge.mt5_broker as mod
    importlib.reload(mod)
    return mod


# ---------------------------------------------------------------------------
# 1. Construction without MT5 installed
# ---------------------------------------------------------------------------

class TestConstructWithoutMT5(unittest.TestCase):
    """MT5Broker can be instantiated even when MetaTrader5 is absent."""

    def setUp(self) -> None:
        import sys
        # Ensure MetaTrader5 is not importable
        self._orig = sys.modules.pop("MetaTrader5", None)
        # Also remove the cached broker module so it re-runs the try/except
        import importlib
        for key in list(sys.modules.keys()):
            if "mt5_broker" in key:
                del sys.modules[key]
        import algoforge.mt5_broker as mod
        importlib.reload(mod)
        self.mod = mod

    def tearDown(self) -> None:
        import sys
        import importlib
        if self._orig is not None:
            sys.modules["MetaTrader5"] = self._orig
        for key in list(sys.modules.keys()):
            if "mt5_broker" in key:
                del sys.modules[key]
        import algoforge.mt5_broker as mod
        importlib.reload(mod)

    def test_instantiation_does_not_raise(self) -> None:
        broker = self.mod.MT5Broker()
        self.assertIsNotNone(broker)

    def test_connect_returns_minus_one_when_mt5_absent(self) -> None:
        broker = self.mod.MT5Broker()
        self.assertEqual(broker.connect(), -1)

    def test_is_connected_false(self) -> None:
        broker = self.mod.MT5Broker()
        self.assertFalse(broker.is_connected())

    def test_broker_name(self) -> None:
        broker = self.mod.MT5Broker()
        self.assertEqual(broker.broker_name(), "MetaTrader5")


# ---------------------------------------------------------------------------
# Fixtures — shared patch context for tests that need MT5 available
# ---------------------------------------------------------------------------

class _MT5MockBase(unittest.TestCase):
    """Base class that patches algoforge.mt5_broker.mt5 before each test."""

    def setUp(self) -> None:
        self.mt5_mock = _make_mt5_module()
        self._patcher = patch("algoforge.mt5_broker.mt5", self.mt5_mock)
        self._patcher.start()
        # Also force _MT5_AVAILABLE to True
        self._avail_patcher = patch("algoforge.mt5_broker._MT5_AVAILABLE", True)
        self._avail_patcher.start()

    def tearDown(self) -> None:
        self._avail_patcher.stop()
        self._patcher.stop()

    def _make_broker(self, **kwargs):
        from algoforge.mt5_broker import MT5Broker
        return MT5Broker(**kwargs)


# ---------------------------------------------------------------------------
# 2. connect() success
# ---------------------------------------------------------------------------

class TestConnectSuccess(_MT5MockBase):

    def test_connect_returns_zero_on_success(self) -> None:
        self.mt5_mock.initialize.return_value = True
        broker = self._make_broker()
        result = broker.connect()
        self.assertEqual(result, 0)

    def test_is_connected_true_after_success(self) -> None:
        self.mt5_mock.initialize.return_value = True
        broker = self._make_broker()
        broker.connect()
        self.assertTrue(broker.is_connected())

    def test_initialize_called_with_no_args_when_no_credentials(self) -> None:
        self.mt5_mock.initialize.return_value = True
        broker = self._make_broker()
        broker.connect()
        # Only called once; kwargs should be empty (no path/server/login/password)
        self.mt5_mock.initialize.assert_called_once_with()

    def test_initialize_called_with_kwargs_when_credentials_provided(self) -> None:
        self.mt5_mock.initialize.return_value = True
        broker = self._make_broker(
            path="C:/MT5/terminal64.exe",
            server="ICMarkets-Demo",
            login=123,
            password="secret",
        )
        broker.connect()
        call_kwargs = self.mt5_mock.initialize.call_args[1]
        self.assertEqual(call_kwargs["path"], "C:/MT5/terminal64.exe")
        self.assertEqual(call_kwargs["server"], "ICMarkets-Demo")
        self.assertEqual(call_kwargs["login"], 123)
        self.assertEqual(call_kwargs["password"], "secret")


# ---------------------------------------------------------------------------
# 3. connect() failure
# ---------------------------------------------------------------------------

class TestConnectFailure(_MT5MockBase):

    def test_connect_returns_minus_one_on_initialize_failure(self) -> None:
        self.mt5_mock.initialize.return_value = False
        self.mt5_mock.last_error.return_value = (10006, "No connection")
        broker = self._make_broker()
        self.assertEqual(broker.connect(), -1)

    def test_is_connected_false_after_failed_connect(self) -> None:
        self.mt5_mock.initialize.return_value = False
        self.mt5_mock.last_error.return_value = (10006, "No connection")
        broker = self._make_broker()
        broker.connect()
        self.assertFalse(broker.is_connected())


# ---------------------------------------------------------------------------
# 4. disconnect()
# ---------------------------------------------------------------------------

class TestDisconnect(_MT5MockBase):

    def test_disconnect_calls_shutdown_when_connected(self) -> None:
        self.mt5_mock.initialize.return_value = True
        broker = self._make_broker()
        broker.connect()
        broker.disconnect()
        self.mt5_mock.shutdown.assert_called_once()

    def test_disconnect_sets_connected_false(self) -> None:
        self.mt5_mock.initialize.return_value = True
        broker = self._make_broker()
        broker.connect()
        broker.disconnect()
        self.assertFalse(broker.is_connected())

    def test_disconnect_not_connected_does_not_call_shutdown(self) -> None:
        broker = self._make_broker()
        broker.disconnect()
        self.mt5_mock.shutdown.assert_not_called()


# ---------------------------------------------------------------------------
# 5. get_account()
# ---------------------------------------------------------------------------

class TestGetAccount(_MT5MockBase):

    def test_returns_correct_account_info(self) -> None:
        ns = _make_account_ns()
        self.mt5_mock.account_info.return_value = ns
        broker = self._make_broker()
        info = broker.get_account()
        self.assertIsInstance(info, AccountInfo)
        self.assertAlmostEqual(info.balance, ns.balance)
        self.assertAlmostEqual(info.equity, ns.equity)
        self.assertAlmostEqual(info.margin, ns.margin)
        self.assertAlmostEqual(info.free_margin, ns.margin_free)
        self.assertAlmostEqual(info.profit, ns.profit)
        self.assertEqual(info.leverage, ns.leverage)
        self.assertEqual(info.currency, ns.currency)
        self.assertEqual(info.login, ns.login)

    def test_raises_when_account_info_returns_none(self) -> None:
        self.mt5_mock.account_info.return_value = None
        self.mt5_mock.last_error.return_value = (10004, "Not connected")
        broker = self._make_broker()
        with self.assertRaises(RuntimeError):
            broker.get_account()


# ---------------------------------------------------------------------------
# 6. get_tick()
# ---------------------------------------------------------------------------

class TestGetTick(_MT5MockBase):

    def test_returns_correct_tick(self) -> None:
        ns = _make_tick_ns()
        self.mt5_mock.symbol_info_tick.return_value = ns
        self.mt5_mock.symbol_select.return_value = True
        broker = self._make_broker()
        tick = broker.get_tick("EURUSD")
        self.assertIsInstance(tick, Tick)
        self.assertEqual(tick.symbol, "EURUSD")
        self.assertAlmostEqual(tick.bid, ns.bid)
        self.assertAlmostEqual(tick.ask, ns.ask)
        self.assertAlmostEqual(tick.volume, ns.volume)
        self.assertEqual(tick.timestamp, int(ns.time))

    def test_symbol_select_called(self) -> None:
        self.mt5_mock.symbol_info_tick.return_value = _make_tick_ns()
        self.mt5_mock.symbol_select.return_value = True
        broker = self._make_broker()
        broker.get_tick("EURUSD")
        self.mt5_mock.symbol_select.assert_called_with("EURUSD", True)

    def test_raises_when_tick_returns_none(self) -> None:
        self.mt5_mock.symbol_select.return_value = True
        self.mt5_mock.symbol_info_tick.return_value = None
        self.mt5_mock.last_error.return_value = (10004, "Not connected")
        broker = self._make_broker()
        with self.assertRaises(RuntimeError):
            broker.get_tick("EURUSD")


# ---------------------------------------------------------------------------
# 7. get_bars()
# ---------------------------------------------------------------------------

class TestGetBars(_MT5MockBase):

    def _make_rates(self):
        """Return a list of dict-like objects (mimics numpy structured array rows)."""
        return [
            {"time": 1_700_000_000, "open": 1.1000, "high": 1.1050,
             "low": 1.0980, "close": 1.1030, "tick_volume": 500},
            {"time": 1_700_003_600, "open": 1.1030, "high": 1.1080,
             "low": 1.1010, "close": 1.1060, "tick_volume": 620},
        ]

    def test_returns_list_of_bars(self) -> None:
        rates = self._make_rates()
        self.mt5_mock.copy_rates_from_pos.return_value = rates
        broker = self._make_broker()
        bars = broker.get_bars("EURUSD", Timeframe.H1, 2)
        self.assertEqual(len(bars), 2)
        self.assertIsInstance(bars[0], Bar)

    def test_bar_fields_mapped_correctly(self) -> None:
        rates = self._make_rates()
        self.mt5_mock.copy_rates_from_pos.return_value = rates
        broker = self._make_broker()
        bars = broker.get_bars("EURUSD", Timeframe.H1, 2)
        b = bars[0]
        r = rates[0]
        self.assertEqual(b.timestamp, int(r["time"]))
        self.assertAlmostEqual(b.open, float(r["open"]))
        self.assertAlmostEqual(b.high, float(r["high"]))
        self.assertAlmostEqual(b.low, float(r["low"]))
        self.assertAlmostEqual(b.close, float(r["close"]))
        self.assertAlmostEqual(b.volume, float(r["tick_volume"]))

    def test_returns_empty_list_when_rates_none(self) -> None:
        self.mt5_mock.copy_rates_from_pos.return_value = None
        broker = self._make_broker()
        bars = broker.get_bars("EURUSD", Timeframe.H1, 10)
        self.assertEqual(bars, [])

    def test_returns_empty_list_when_rates_empty(self) -> None:
        self.mt5_mock.copy_rates_from_pos.return_value = []
        broker = self._make_broker()
        bars = broker.get_bars("EURUSD", Timeframe.H1, 10)
        self.assertEqual(bars, [])


# ---------------------------------------------------------------------------
# 8. place_order() — buy success
# ---------------------------------------------------------------------------

class TestPlaceOrderBuySuccess(_MT5MockBase):

    def setUp(self) -> None:
        super().setUp()
        self.mt5_mock.symbol_info_tick.return_value = _make_tick_ns(
            bid=1.10990, ask=1.11000
        )
        self.mt5_mock.order_send.return_value = _make_send_result(
            retcode=_MT5_CONSTANTS["TRADE_RETCODE_DONE"],
            order=3001, price=1.11000,
        )

    def test_returns_order_on_success(self) -> None:
        broker = self._make_broker()
        order = broker.place_order(
            "EURUSD", OrderType.BUY, 0.10,
            sl=1.10500, tp=1.12000, magic=42, comment="test",
        )
        self.assertIsInstance(order, Order)
        self.assertEqual(order.ticket, 3001)
        self.assertEqual(order.symbol, "EURUSD")
        self.assertEqual(order.type, OrderType.BUY)
        self.assertAlmostEqual(order.lots, 0.10)
        self.assertAlmostEqual(order.fill_price, 1.11000)
        self.assertAlmostEqual(order.sl, 1.10500)
        self.assertAlmostEqual(order.tp, 1.12000)
        self.assertEqual(order.magic, 42)
        self.assertEqual(order.comment, "test")

    def test_order_send_called(self) -> None:
        broker = self._make_broker()
        broker.place_order("EURUSD", OrderType.BUY, 0.10)
        self.mt5_mock.order_send.assert_called_once()

    def test_buy_uses_ask_price(self) -> None:
        broker = self._make_broker()
        broker.place_order("EURUSD", OrderType.BUY, 0.10)
        request = self.mt5_mock.order_send.call_args[0][0]
        self.assertAlmostEqual(request["price"], 1.11000)  # ask


# ---------------------------------------------------------------------------
# 9. place_order() — sell success
# ---------------------------------------------------------------------------

class TestPlaceOrderSellSuccess(_MT5MockBase):

    def setUp(self) -> None:
        super().setUp()
        self.mt5_mock.symbol_info_tick.return_value = _make_tick_ns(
            bid=1.10990, ask=1.11000
        )
        self.mt5_mock.order_send.return_value = _make_send_result(
            retcode=_MT5_CONSTANTS["TRADE_RETCODE_DONE"],
            order=3002, price=1.10990,
        )

    def test_sell_uses_bid_price(self) -> None:
        broker = self._make_broker()
        broker.place_order("EURUSD", OrderType.SELL, 0.10)
        request = self.mt5_mock.order_send.call_args[0][0]
        self.assertAlmostEqual(request["price"], 1.10990)  # bid


# ---------------------------------------------------------------------------
# 10. place_order() — rejection
# ---------------------------------------------------------------------------

class TestPlaceOrderRejected(_MT5MockBase):

    def test_raises_on_non_done_retcode(self) -> None:
        self.mt5_mock.symbol_info_tick.return_value = _make_tick_ns()
        self.mt5_mock.order_send.return_value = _make_send_result(retcode=10018)
        self.mt5_mock.last_error.return_value = (10018, "Market closed")
        broker = self._make_broker()
        with self.assertRaises(RuntimeError):
            broker.place_order("EURUSD", OrderType.BUY, 0.10)

    def test_raises_when_result_is_none(self) -> None:
        self.mt5_mock.symbol_info_tick.return_value = _make_tick_ns()
        self.mt5_mock.order_send.return_value = None
        self.mt5_mock.last_error.return_value = (10004, "Disconnected")
        broker = self._make_broker()
        with self.assertRaises(RuntimeError):
            broker.place_order("EURUSD", OrderType.BUY, 0.10)

    def test_raises_when_tick_unavailable(self) -> None:
        self.mt5_mock.symbol_info_tick.return_value = None
        self.mt5_mock.last_error.return_value = (10004, "Disconnected")
        broker = self._make_broker()
        with self.assertRaises(RuntimeError):
            broker.place_order("EURUSD", OrderType.BUY, 0.10)


# ---------------------------------------------------------------------------
# 11. close_position()
# ---------------------------------------------------------------------------

class TestClosePosition(_MT5MockBase):

    def setUp(self) -> None:
        super().setUp()
        self.mt5_mock.positions_get.return_value = [_make_position_ns()]
        self.mt5_mock.symbol_info_tick.return_value = _make_tick_ns()
        self.mt5_mock.order_send.return_value = _make_send_result(
            retcode=_MT5_CONSTANTS["TRADE_RETCODE_DONE"]
        )

    def test_close_long_returns_zero_on_success(self) -> None:
        broker = self._make_broker()
        result = broker.close_position(1001)
        self.assertEqual(result, 0)

    def test_close_nonexistent_position_returns_minus_one(self) -> None:
        self.mt5_mock.positions_get.return_value = []
        broker = self._make_broker()
        result = broker.close_position(9999)
        self.assertEqual(result, -1)

    def test_close_returns_minus_one_on_send_failure(self) -> None:
        self.mt5_mock.order_send.return_value = _make_send_result(retcode=10018)
        broker = self._make_broker()
        result = broker.close_position(1001)
        self.assertEqual(result, -1)

    def test_close_short_position(self) -> None:
        short_pos = _make_position_ns(type=1)  # 1=SHORT
        self.mt5_mock.positions_get.return_value = [short_pos]
        broker = self._make_broker()
        result = broker.close_position(1001)
        self.assertEqual(result, 0)
        # Verify the counter-side order type was sent
        request = self.mt5_mock.order_send.call_args[0][0]
        self.assertEqual(request["type"], _MT5_CONSTANTS["ORDER_TYPE_BUY"])


# ---------------------------------------------------------------------------
# 12. modify_position()
# ---------------------------------------------------------------------------

class TestModifyPosition(_MT5MockBase):

    def setUp(self) -> None:
        super().setUp()
        self.mt5_mock.positions_get.return_value = [_make_position_ns()]
        self.mt5_mock.order_send.return_value = _make_send_result(
            retcode=_MT5_CONSTANTS["TRADE_RETCODE_DONE"]
        )

    def test_modify_returns_zero_on_success(self) -> None:
        broker = self._make_broker()
        result = broker.modify_position(1001, sl=1.09500, tp=1.12500)
        self.assertEqual(result, 0)

    def test_modify_nonexistent_position_returns_minus_one(self) -> None:
        self.mt5_mock.positions_get.return_value = []
        broker = self._make_broker()
        result = broker.modify_position(9999, sl=1.09500, tp=1.12500)
        self.assertEqual(result, -1)

    def test_modify_passes_correct_sl_tp(self) -> None:
        broker = self._make_broker()
        broker.modify_position(1001, sl=1.09500, tp=1.12500)
        request = self.mt5_mock.order_send.call_args[0][0]
        self.assertAlmostEqual(request["sl"], 1.09500)
        self.assertAlmostEqual(request["tp"], 1.12500)
        self.assertEqual(request["action"], _MT5_CONSTANTS["TRADE_ACTION_SLTP"])

    def test_modify_returns_minus_one_on_failure(self) -> None:
        self.mt5_mock.order_send.return_value = _make_send_result(retcode=10018)
        broker = self._make_broker()
        result = broker.modify_position(1001, sl=1.09500, tp=1.12500)
        self.assertEqual(result, -1)


# ---------------------------------------------------------------------------
# 13. cancel_order()
# ---------------------------------------------------------------------------

class TestCancelOrder(_MT5MockBase):

    def setUp(self) -> None:
        super().setUp()
        self.mt5_mock.order_send.return_value = _make_send_result(
            retcode=_MT5_CONSTANTS["TRADE_RETCODE_DONE"]
        )

    def test_cancel_returns_zero_on_success(self) -> None:
        broker = self._make_broker()
        result = broker.cancel_order(2001)
        self.assertEqual(result, 0)

    def test_cancel_uses_remove_action(self) -> None:
        broker = self._make_broker()
        broker.cancel_order(2001)
        request = self.mt5_mock.order_send.call_args[0][0]
        self.assertEqual(request["action"], _MT5_CONSTANTS["TRADE_ACTION_REMOVE"])
        self.assertEqual(request["order"], 2001)

    def test_cancel_returns_minus_one_on_failure(self) -> None:
        self.mt5_mock.order_send.return_value = _make_send_result(retcode=10018)
        broker = self._make_broker()
        result = broker.cancel_order(2001)
        self.assertEqual(result, -1)


# ---------------------------------------------------------------------------
# 14. _map_position() and _map_order()  (private static mappers)
# ---------------------------------------------------------------------------

class TestMapPositionAndOrder(unittest.TestCase):
    """Direct tests of the mapper staticmethods via minimal SimpleNamespace input."""

    def test_map_position_long(self) -> None:
        from algoforge.mt5_broker import MT5Broker
        ns = _make_position_ns(type=0)
        pos = MT5Broker._map_position(ns)
        self.assertIsInstance(pos, Position)
        self.assertEqual(pos.ticket, 1001)
        self.assertEqual(pos.symbol, "EURUSD")
        self.assertEqual(pos.side, Direction.LONG)
        self.assertAlmostEqual(pos.lots, 0.10)
        self.assertAlmostEqual(pos.open_price, 1.10500)
        self.assertAlmostEqual(pos.current_price, 1.10990)
        self.assertAlmostEqual(pos.sl, 1.10000)
        self.assertAlmostEqual(pos.tp, 1.12000)
        self.assertAlmostEqual(pos.profit, 49.0)
        self.assertAlmostEqual(pos.commission, -0.70)
        self.assertEqual(pos.magic, 42)
        self.assertEqual(pos.comment, "test")

    def test_map_position_short(self) -> None:
        from algoforge.mt5_broker import MT5Broker
        ns = _make_position_ns(type=1)
        pos = MT5Broker._map_position(ns)
        self.assertEqual(pos.side, Direction.SHORT)

    def test_map_order_buy_limit(self) -> None:
        from algoforge.mt5_broker import MT5Broker
        ns = _make_order_ns(type=2)  # 2=BUY_LIMIT
        order = MT5Broker._map_order(ns)
        self.assertIsInstance(order, Order)
        self.assertEqual(order.ticket, 2001)
        self.assertEqual(order.symbol, "EURUSD")
        self.assertEqual(order.type, OrderType.BUY_LIMIT)
        self.assertAlmostEqual(order.lots, 0.10)
        self.assertAlmostEqual(order.price, 1.09000)
        self.assertAlmostEqual(order.sl, 1.08500)
        self.assertAlmostEqual(order.tp, 1.10500)
        self.assertAlmostEqual(order.fill_price, 0.0)
        self.assertEqual(order.magic, 42)

    def test_map_order_sell(self) -> None:
        from algoforge.mt5_broker import MT5Broker
        ns = _make_order_ns(type=1)  # 1=SELL
        order = MT5Broker._map_order(ns)
        self.assertEqual(order.type, OrderType.SELL)

    def test_map_order_unknown_type_defaults_to_buy(self) -> None:
        """Type 99 is not in OrderType enum — should default to BUY."""
        from algoforge.mt5_broker import MT5Broker
        ns = _make_order_ns(type=99)
        order = MT5Broker._map_order(ns)
        self.assertEqual(order.type, OrderType.BUY)


# ---------------------------------------------------------------------------
# 15. get_positions() / get_orders()
# ---------------------------------------------------------------------------

class TestGetPositionsAndOrders(_MT5MockBase):

    def test_get_positions_returns_list(self) -> None:
        self.mt5_mock.positions_get.return_value = [
            _make_position_ns(ticket=101),
            _make_position_ns(ticket=102),
        ]
        broker = self._make_broker()
        positions = broker.get_positions()
        self.assertEqual(len(positions), 2)
        self.assertEqual(positions[0].ticket, 101)
        self.assertEqual(positions[1].ticket, 102)

    def test_get_positions_returns_empty_when_none(self) -> None:
        self.mt5_mock.positions_get.return_value = None
        broker = self._make_broker()
        self.assertEqual(broker.get_positions(), [])

    def test_get_orders_returns_list(self) -> None:
        self.mt5_mock.orders_get.return_value = [
            _make_order_ns(ticket=201),
        ]
        broker = self._make_broker()
        orders = broker.get_orders()
        self.assertEqual(len(orders), 1)
        self.assertEqual(orders[0].ticket, 201)

    def test_get_orders_returns_empty_when_none(self) -> None:
        self.mt5_mock.orders_get.return_value = None
        broker = self._make_broker()
        self.assertEqual(broker.get_orders(), [])


# ---------------------------------------------------------------------------
# 16. poll_sl_tp() — no-op
# ---------------------------------------------------------------------------

class TestPollSlTp(_MT5MockBase):

    def test_poll_sl_tp_is_no_op(self) -> None:
        broker = self._make_broker()
        broker.poll_sl_tp()   # must not raise or call any mt5 method


if __name__ == "__main__":
    unittest.main()
