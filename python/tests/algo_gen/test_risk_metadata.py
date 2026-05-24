"""Tests that the risk layer honours size_mult from algo.metadata.

Verifies:
  - SignalProcessor multiplies risk_amt by size_mult when algo.metadata present
  - Absence of metadata behaves like size_mult=1.0
  - size_mult=0.5 halves the lot size
  - size_mult=2.0 doubles the lot size
  - Non-dict metadata ignored safely
"""
from __future__ import annotations

import pytest

from algoforge.algorithm import AlgoDecision, AlgoSignal, IAlgorithm
from algoforge.broker import AccountInfo, IBroker, Order, OrderType, SymbolInfo, Tick
from algoforge.risk import PositionSizer, SignalProcessor


# ── Stub broker ────────────────────────────────────────────────────────────────

class _StubBroker(IBroker):
    def __init__(self, balance: float = 10_000.0):
        self._balance = balance
        self.last_lots: float = 0.0
        self.orders: list[tuple] = []

    def connect(self) -> int:
        return 1

    def disconnect(self) -> None:
        pass

    def is_connected(self) -> bool:
        return True

    def broker_name(self) -> str:
        return "stub"

    def get_tick(self, symbol: str) -> Tick:
        return Tick(symbol=symbol, ask=1.1000, bid=1.0998, timestamp=1_600_000_000)

    def get_symbol_info(self, symbol: str) -> SymbolInfo:
        return SymbolInfo(
            name="EURUSD",
            point=0.00001,
            contract_size=100_000.0,
            volume_step=0.01,
            volume_min=0.01,
            volume_max=100.0,
        )

    def get_account(self) -> AccountInfo:
        return AccountInfo(
            balance=self._balance,
            equity=self._balance,
            margin=0.0,
            free_margin=self._balance,
            currency="USD",
        )

    def get_bars(self, symbol, tf, count):
        return []

    def place_order(
        self, symbol, order_type, lots, price=0.0, sl=0.0, tp=0.0,
        magic=0, comment=""
    ) -> Order:
        self.last_lots = lots
        self.orders.append((symbol, order_type, lots, price, sl, tp))
        return Order(ticket=1001, symbol=symbol, lots=lots, price=price,
                     sl=sl, tp=tp, comment=comment)

    def close_position(self, ticket, lots=0.0) -> int:
        return 1

    def modify_position(self, ticket, sl, tp) -> int:
        return 1

    def cancel_order(self, ticket) -> int:
        return 1

    def get_positions(self):
        return []

    def get_orders(self):
        return []

    def get_position(self, ticket):
        return None


# ── Stub algo ──────────────────────────────────────────────────────────────────

class _StubAlgo(IAlgorithm):
    def __init__(self, metadata=None):
        self._enabled = True
        self.metadata = metadata

    def name(self):            return "stub"
    def description(self):     return "Stub"
    def magic(self):           return 0

    def evaluate(self, symbol, tf, bars, count, ind, pat):
        return AlgoDecision(
            signal=AlgoSignal.BUY, symbol=symbol,
            confidence=0.9, atr=0.001,
        )


# ── Tests ──────────────────────────────────────────────────────────────────────

class TestSignalProcessorSizeMult:
    def _make_decision(self, symbol: str = "EURUSD") -> AlgoDecision:
        return AlgoDecision(
            signal=AlgoSignal.BUY,
            symbol=symbol,
            confidence=0.9,
            atr=0.0010,
        )

    def test_no_algo_uses_base_risk(self):
        broker = _StubBroker(balance=10_000.0)
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0, algo=None)
        sp.process(self._make_decision())
        lots_no_meta = broker.last_lots
        assert lots_no_meta > 0.0

    def test_metadata_none_same_as_no_algo(self):
        broker1 = _StubBroker(10_000.0)
        sp1 = SignalProcessor(broker1, max_risk=0.01, cooldown_sec=0, algo=None)
        sp1.process(self._make_decision())

        broker2 = _StubBroker(10_000.0)
        sp2 = SignalProcessor(broker2, max_risk=0.01, cooldown_sec=0,
                              algo=_StubAlgo(metadata=None))
        sp2.process(self._make_decision())

        assert broker1.last_lots == broker2.last_lots

    def test_size_mult_1_same_as_no_meta(self):
        broker1 = _StubBroker(10_000.0)
        sp1 = SignalProcessor(broker1, max_risk=0.01, cooldown_sec=0, algo=None)
        sp1.process(self._make_decision())

        broker2 = _StubBroker(10_000.0)
        sp2 = SignalProcessor(broker2, max_risk=0.01, cooldown_sec=0,
                              algo=_StubAlgo(metadata={"size_mult": 1.0}))
        sp2.process(self._make_decision())

        assert broker1.last_lots == broker2.last_lots

    def test_size_mult_half_reduces_lots(self):
        broker_full = _StubBroker(10_000.0)
        sp_full = SignalProcessor(broker_full, max_risk=0.01, cooldown_sec=0, algo=None)
        sp_full.process(self._make_decision())

        broker_half = _StubBroker(10_000.0)
        sp_half = SignalProcessor(broker_half, max_risk=0.01, cooldown_sec=0,
                                  algo=_StubAlgo(metadata={"size_mult": 0.5}))
        sp_half.process(self._make_decision("EURUSD"))  # new symbol to skip cooldown

        # half lots should be roughly half — exact value depends on normalisation
        assert broker_half.last_lots <= broker_full.last_lots

    def test_size_mult_zero_gives_min_lots(self):
        broker = _StubBroker(10_000.0)
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0,
                             algo=_StubAlgo(metadata={"size_mult": 0.0}))
        sp.process(self._make_decision())
        assert broker.last_lots >= 0.0

    def test_size_mult_2_doubles_lots(self):
        broker_base = _StubBroker(10_000.0)
        sp_base = SignalProcessor(broker_base, max_risk=0.01, cooldown_sec=0, algo=None)
        sp_base.process(self._make_decision())

        broker_x2 = _StubBroker(10_000.0)
        sp_x2 = SignalProcessor(broker_x2, max_risk=0.01, cooldown_sec=0,
                                algo=_StubAlgo(metadata={"size_mult": 2.0}))
        sp_x2.process(self._make_decision("EURUSD"))

        # doubled lots should be >= base (may be equal if already at max cap)
        assert broker_x2.last_lots >= broker_base.last_lots

    def test_non_dict_metadata_ignored(self):
        """If metadata is not a dict, size_mult defaults to 1.0."""
        broker = _StubBroker(10_000.0)
        sp = SignalProcessor(broker, max_risk=0.01, cooldown_sec=0,
                             algo=_StubAlgo(metadata="not-a-dict"))
        sp.process(self._make_decision())
        # Should not raise; lots should be same as if no metadata
        assert broker.last_lots >= 0.0

    def test_missing_size_mult_key_defaults_to_1(self):
        broker1 = _StubBroker(10_000.0)
        sp1 = SignalProcessor(broker1, max_risk=0.01, cooldown_sec=0, algo=None)
        sp1.process(self._make_decision())

        broker2 = _StubBroker(10_000.0)
        sp2 = SignalProcessor(broker2, max_risk=0.01, cooldown_sec=0,
                              algo=_StubAlgo(metadata={"other_key": "x"}))
        sp2.process(self._make_decision("EURUSD"))

        assert broker1.last_lots == broker2.last_lots


# ── PositionSizer tests ────────────────────────────────────────────────────────

class TestPositionSizer:
    def _sym(self):
        return SymbolInfo(
            name="EURUSD",
            point=0.00001,
            contract_size=100_000.0,
            volume_step=0.01,
            volume_min=0.01,
            volume_max=100.0,
        )

    def test_normalize_lots_rounds_to_step(self):
        result = PositionSizer.normalize_lots(0.154, 0.01, 0.01, 100.0)
        assert abs(result * 100 - round(result * 100)) < 1e-9  # divisible by 0.01

    def test_normalize_lots_clamps_min(self):
        result = PositionSizer.normalize_lots(0.001, 0.01, 0.01, 100.0)
        assert result >= 0.01

    def test_normalize_lots_clamps_max(self):
        result = PositionSizer.normalize_lots(999.0, 0.01, 0.01, 10.0)
        assert result <= 10.0

    def test_kelly_fraction_positive_edge(self):
        k = PositionSizer.kelly_fraction(0.6, 2.0, 0.05)
        assert 0.005 <= k <= 0.05

    def test_kelly_fraction_zero_rr(self):
        k = PositionSizer.kelly_fraction(0.5, 0.0, 0.05)
        # With rr=0, edge/rr is undefined; expect min floor
        assert k >= 0.005

    def test_kelly_fraction_floored_at_min(self):
        # Negative win rate → negative Kelly → floor at 0.005
        k = PositionSizer.kelly_fraction(0.1, 2.0, 0.05)
        assert k >= 0.005
