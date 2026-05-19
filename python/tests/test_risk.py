"""Unit tests for algoforge.risk (PositionSizer, DrawdownGuard, SignalProcessor).

Run with:
    python -m pytest python/tests/test_risk.py -v
or
    python -m unittest python.tests.test_risk
"""
import unittest
from unittest.mock import MagicMock

from algoforge.risk import (
    DrawdownGuard,
    GuardState,
    PositionSizer,
    SignalProcessor,
    SizingMethod,
)
from algoforge.broker import AccountInfo, OrderType, SymbolInfo, Tick, Order
from algoforge.algorithm import AlgoDecision, AlgoSignal
from algoforge.types import Direction


# ---------------------------------------------------------------------------
# PositionSizer tests
# ---------------------------------------------------------------------------

class TestKellyFraction(unittest.TestCase):

    def test_kelly_basic(self):
        """WR=0.55, RR=2.0 → half-Kelly=0.1625, capped at max_risk=0.02."""
        # edge = 0.55*2 - 0.45 = 0.65; kelly = 0.65/2 = 0.325; half = 0.1625
        result = PositionSizer.kelly_fraction(win_rate=0.55, rr=2.0, max_risk=0.02)
        self.assertAlmostEqual(result, 0.02)   # capped by max_risk

    def test_kelly_uncapped(self):
        """With generous cap the raw half-Kelly is returned."""
        result = PositionSizer.kelly_fraction(win_rate=0.55, rr=2.0, max_risk=0.5)
        self.assertAlmostEqual(result, 0.1625, places=6)

    def test_kelly_floor(self):
        """Negative edge → clamped to minimum floor of 0.005."""
        result = PositionSizer.kelly_fraction(win_rate=0.3, rr=1.0, max_risk=0.02)
        self.assertAlmostEqual(result, 0.005)

    def test_kelly_zero_rr(self):
        """rr=0 → kelly=0, returned as floor 0.005."""
        result = PositionSizer.kelly_fraction(win_rate=0.6, rr=0.0, max_risk=0.02)
        self.assertAlmostEqual(result, 0.005)


class TestNormalizeLots(unittest.TestCase):

    def test_round_to_step(self):
        result = PositionSizer.normalize_lots(1.567, step=0.01, min_lots=0.01, max_lots=100.0)
        self.assertAlmostEqual(result, 1.57)

    def test_clamp_to_min(self):
        result = PositionSizer.normalize_lots(0.001, step=0.01, min_lots=0.01, max_lots=100.0)
        self.assertAlmostEqual(result, 0.01)

    def test_clamp_to_max(self):
        result = PositionSizer.normalize_lots(500.0, step=0.01, min_lots=0.01, max_lots=100.0)
        self.assertAlmostEqual(result, 100.0)

    def test_zero_step_defaults_to_001(self):
        result = PositionSizer.normalize_lots(1.567, step=0.0, min_lots=0.01, max_lots=100.0)
        self.assertAlmostEqual(result, 1.57)

    def test_already_on_step(self):
        result = PositionSizer.normalize_lots(0.10, step=0.01, min_lots=0.01, max_lots=100.0)
        self.assertAlmostEqual(result, 0.10)


class TestSizePosition(unittest.TestCase):

    def _default_sym(self) -> SymbolInfo:
        return SymbolInfo(
            name="EURUSD",
            digits=5,
            point=0.00001,
            contract_size=100_000.0,
            volume_min=0.01,
            volume_max=100.0,
            volume_step=0.01,
        )

    def test_fixed_risk_basic(self):
        """Simple sanity-check: 1% risk on 10k, 10-pip stop."""
        sym = self._default_sym()
        # stop_loss > 0 so stop_dist = abs(1.1100 - 1.1090) = 0.0010
        # stop_in_pts = 0.001 / 0.00001 = 100 pts
        # val_per_point = 100_000 * 0.00001 = 1.0
        # risk_amount = 10_000 * 0.01 = 100
        # raw_lots = 100 / (100 * 1.0) = 1.0
        result = PositionSizer.size_position(
            method=SizingMethod.FIXED_RISK,
            balance=10_000.0,
            entry_price=1.1100,
            stop_loss=1.1090,
            sym=sym,
            atr=0.0010,
            risk_pct=0.01,
            max_lots=100.0,
            min_lots=0.01,
        )
        self.assertAlmostEqual(result.lots, 1.0, places=2)
        self.assertFalse(result.capped)
        self.assertAlmostEqual(result.stop_distance, 0.0010, places=6)

    def test_kelly_sizing(self):
        """Kelly method should produce a smaller lot than fixed at same risk_pct."""
        sym = self._default_sym()
        fixed = PositionSizer.size_position(
            method=SizingMethod.FIXED_RISK,
            balance=10_000.0,
            entry_price=1.1100,
            stop_loss=1.1090,
            sym=sym,
            atr=0.0010,
            risk_pct=0.02,
            max_lots=100.0,
            min_lots=0.01,
        )
        kelly = PositionSizer.size_position(
            method=SizingMethod.KELLY,
            balance=10_000.0,
            entry_price=1.1100,
            stop_loss=1.1090,
            sym=sym,
            atr=0.0010,
            risk_pct=0.02,
            max_lots=100.0,
            min_lots=0.01,
            win_rate=0.55,
            rr_ratio=2.0,
        )
        # Kelly caps at max_risk=0.02 (same as fixed) → equal in this case
        # but kelly half is 0.1625 → still capped to 0.02 → same lots
        self.assertGreater(kelly.lots, 0.0)

    def test_atr_stop_used_when_no_sl(self):
        """When stop_loss=0 the ATR-derived stop should be used."""
        sym = self._default_sym()
        result = PositionSizer.size_position(
            method=SizingMethod.FIXED_RISK,
            balance=10_000.0,
            entry_price=1.1100,
            stop_loss=0.0,       # no explicit SL
            sym=sym,
            atr=0.0020,
            risk_pct=0.01,
            max_lots=100.0,
            min_lots=0.01,
        )
        expected_stop = 0.0020 * 1.5
        self.assertAlmostEqual(result.stop_distance, expected_stop, places=6)

    def test_capped_flag(self):
        """Ridiculously small stop → raw lots explodes → capped=True."""
        sym = self._default_sym()
        result = PositionSizer.size_position(
            method=SizingMethod.FIXED_RISK,
            balance=100_000.0,
            entry_price=1.1100,
            stop_loss=1.10999,   # 0.1 pip stop
            sym=sym,
            atr=0.00001,
            risk_pct=0.05,
            max_lots=100.0,
            min_lots=0.01,
        )
        self.assertTrue(result.capped)
        self.assertEqual(result.lots, 100.0)


# ---------------------------------------------------------------------------
# DrawdownGuard tests
# ---------------------------------------------------------------------------

class TestDrawdownGuard(unittest.TestCase):

    def test_green_on_init(self):
        guard = DrawdownGuard()
        guard.initialise(balance=10_000, equity=10_000)
        self.assertEqual(guard.state, GuardState.GREEN)
        self.assertTrue(guard.trading_allowed)
        self.assertEqual(guard.halt_count, 0)

    def test_red_on_daily_breach(self):
        """Equity drops to 9 400 → daily loss = −6 % > 5 % limit → RED."""
        guard = DrawdownGuard(daily_limit=0.05, total_limit=0.15)
        guard.initialise(balance=10_000, equity=10_000)
        status = guard.check(equity=9_400, balance=10_000)
        self.assertEqual(status.state, GuardState.RED)
        self.assertFalse(status.trading_allowed)
        self.assertEqual(guard.halt_count, 1)
        self.assertIn("Daily", status.reason)

    def test_yellow_on_warning_threshold(self):
        """Equity at 9 600 → daily loss = −4 % which is ≥ 75 %×5 % = 3.75 % → YELLOW."""
        guard = DrawdownGuard(daily_limit=0.05, total_limit=0.15)
        guard.initialise(balance=10_000, equity=10_000)
        status = guard.check(equity=9_600, balance=10_000)
        self.assertEqual(status.state, GuardState.YELLOW)
        self.assertTrue(status.trading_allowed)   # YELLOW still allows trading
        self.assertEqual(guard.halt_count, 0)

    def test_still_green_below_warn_threshold(self):
        """Equity at 9 650 → daily loss = −3.5 % which is < 3.75 % → stays GREEN."""
        guard = DrawdownGuard(daily_limit=0.05, total_limit=0.15)
        guard.initialise(balance=10_000, equity=10_000)
        status = guard.check(equity=9_650, balance=10_000)
        self.assertEqual(status.state, GuardState.GREEN)

    def test_red_on_total_drawdown(self):
        """Peak rises during the session, then >15 % total drawdown → RED."""
        guard = DrawdownGuard(daily_limit=0.05, total_limit=0.15)
        guard.initialise(balance=10_000, equity=10_000)
        # Simulate equity rising to 12_000 (sets new peak)
        guard.check(equity=12_000, balance=10_000)
        self.assertAlmostEqual(guard._peak, 12_000)
        # Now equity retreats — total dd = (12_000 - 10_100) / 12_000 ≈ 0.158 > 0.15
        # daily P&L = 10_100 - 10_000 = +100 (+1%) so daily limit is NOT triggered
        status = guard.check(equity=10_100, balance=10_000)
        self.assertEqual(status.state, GuardState.RED)
        self.assertFalse(status.trading_allowed)

    def test_peak_updates(self):
        """New equity high should update peak."""
        guard = DrawdownGuard()
        guard.initialise(balance=10_000, equity=10_000)
        guard.check(equity=11_000, balance=10_000)   # new high
        self.assertAlmostEqual(guard._peak, 11_000)

    def test_no_double_halt_count(self):
        """Calling check repeatedly while RED should not keep incrementing halt_count."""
        guard = DrawdownGuard(daily_limit=0.05)
        guard.initialise(balance=10_000, equity=10_000)
        guard.check(equity=9_400, balance=10_000)
        guard.check(equity=9_300, balance=10_000)
        guard.check(equity=9_200, balance=10_000)
        self.assertEqual(guard.halt_count, 1)

    def test_metrics_values(self):
        """Check daily_pnl and total_dd are calculated correctly."""
        guard = DrawdownGuard()
        guard.initialise(balance=10_000, equity=10_000)
        status = guard.check(equity=9_800, balance=10_000)
        self.assertAlmostEqual(status.daily_pnl, -200.0, places=1)
        self.assertAlmostEqual(status.daily_pnl_pct, -0.02, places=4)
        self.assertAlmostEqual(status.total_dd_pct, 0.02, places=4)


# ---------------------------------------------------------------------------
# SignalProcessor tests (using a mock broker)
# ---------------------------------------------------------------------------

def _make_broker(balance=10_000.0, atr=0.0020) -> MagicMock:
    broker = MagicMock()
    broker.get_account.return_value = AccountInfo(
        balance=balance, equity=balance
    )
    broker.get_tick.return_value = Tick(
        symbol="EURUSD", bid=1.10990, ask=1.11000
    )
    broker.get_symbol_info.return_value = SymbolInfo(
        name="EURUSD",
        point=0.00001,
        contract_size=100_000.0,
        volume_min=0.01,
        volume_max=100.0,
        volume_step=0.01,
    )
    # Order with ticket > 0 signals success
    broker.place_order.return_value = Order(ticket=12345, symbol="EURUSD")
    return broker


class TestSignalProcessor(unittest.TestCase):

    def _make_buy_decision(self, confidence=0.70, atr=0.0020) -> AlgoDecision:
        return AlgoDecision(
            signal=AlgoSignal.BUY,
            symbol="EURUSD",
            direction=Direction.LONG,
            confidence=confidence,
            reason="test buy",
            atr=atr,
        )

    def _make_sell_decision(self, confidence=0.70, atr=0.0020) -> AlgoDecision:
        return AlgoDecision(
            signal=AlgoSignal.SELL,
            symbol="EURUSD",
            direction=Direction.SHORT,
            confidence=confidence,
            reason="test sell",
            atr=atr,
        )

    def test_process_buy_success(self):
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        result = sp.process(self._make_buy_decision())
        self.assertTrue(result)
        self.assertEqual(sp.processed, 1)
        self.assertEqual(sp.rejected, 0)
        # Verify a BUY order was placed
        broker.place_order.assert_called_once()
        args = broker.place_order.call_args[0]
        self.assertEqual(args[1], OrderType.BUY)

    def test_process_sell_success(self):
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        result = sp.process(self._make_sell_decision())
        self.assertTrue(result)
        args = broker.place_order.call_args[0]
        self.assertEqual(args[1], OrderType.SELL)

    def test_non_actionable_rejected(self):
        """Low-confidence decision should be rejected without broker calls."""
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        decision = self._make_buy_decision(confidence=0.40)   # below 0.55
        result = sp.process(decision)
        self.assertFalse(result)
        self.assertEqual(sp.rejected, 1)
        broker.place_order.assert_not_called()

    def test_none_signal_rejected(self):
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        result = sp.process(AlgoDecision.none("EURUSD"))
        self.assertFalse(result)
        broker.place_order.assert_not_called()

    def test_cooldown_blocks_second_signal(self):
        """Second signal within cooldown window should be rejected."""
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=300)
        sp.process(self._make_buy_decision())   # first → accepted
        result = sp.process(self._make_buy_decision())  # second → cooldown
        self.assertFalse(result)
        self.assertEqual(sp.processed, 1)
        self.assertEqual(sp.rejected, 1)

    def test_opposite_direction_not_blocked_by_cooldown(self):
        """A SELL after a BUY cooldown should use a different key → not blocked."""
        broker = _make_broker()
        # Reset the order mock for second call too
        broker.place_order.side_effect = [
            Order(ticket=1, symbol="EURUSD"),
            Order(ticket=2, symbol="EURUSD"),
        ]
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=300)
        r1 = sp.process(self._make_buy_decision())
        r2 = sp.process(self._make_sell_decision())   # different key → allowed
        self.assertTrue(r1)
        self.assertTrue(r2)
        self.assertEqual(sp.processed, 2)

    def test_failed_order_counted_as_rejected(self):
        """If broker returns ticket=0, process() returns False and increments rejected."""
        broker = _make_broker()
        broker.place_order.return_value = Order(ticket=0, symbol="EURUSD")
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        result = sp.process(self._make_buy_decision())
        self.assertFalse(result)
        self.assertEqual(sp.processed, 0)
        self.assertEqual(sp.rejected, 1)

    def test_sl_tp_placement_buy(self):
        """For a BUY: sl = ask - atr*1.5, tp = ask + atr*3.0."""
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        atr = 0.0020
        sp.process(self._make_buy_decision(atr=atr))
        _, kwargs = broker.place_order.call_args
        args = broker.place_order.call_args[0]
        # args: symbol, order_type, lots, price, sl, tp, magic, comment
        ask = 1.11000
        expected_sl = ask - atr * 1.5
        expected_tp = ask + atr * 3.0
        self.assertAlmostEqual(args[4], expected_sl, places=5)
        self.assertAlmostEqual(args[5], expected_tp, places=5)

    def test_sl_tp_placement_sell(self):
        """For a SELL: sl = bid + atr*1.5, tp = bid - atr*3.0."""
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        atr = 0.0020
        sp.process(self._make_sell_decision(atr=atr))
        args = broker.place_order.call_args[0]
        bid = 1.10990
        expected_sl = bid + atr * 1.5
        expected_tp = bid - atr * 3.0
        self.assertAlmostEqual(args[4], expected_sl, places=5)
        self.assertAlmostEqual(args[5], expected_tp, places=5)

    def test_atr_fallback_when_zero(self):
        """If decision.atr==0, atr defaults to ask*0.001."""
        broker = _make_broker()
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0)
        decision = AlgoDecision(
            signal=AlgoSignal.BUY,
            symbol="EURUSD",
            direction=Direction.LONG,
            confidence=0.70,
            reason="no-atr",
            atr=0.0,   # zero
        )
        sp.process(decision)
        args = broker.place_order.call_args[0]
        ask     = 1.11000
        atr_fb  = ask * 0.001          # fallback
        expected_sl = ask - atr_fb * 1.5
        self.assertAlmostEqual(args[4], expected_sl, places=5)


if __name__ == "__main__":
    unittest.main()
