"""Risk management — PositionSizer, DrawdownGuard, SignalProcessor.

Ported from the C++ reference implementations:
  - position_sizer.cpp  → PositionSizer
  - drawdown_guard.cpp  → DrawdownGuard
  - signal_processor.cpp → SignalProcessor
"""
from __future__ import annotations

import math
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import IntEnum
from typing import Dict, Optional

from .algorithm import AlgoDecision, AlgoSignal, Direction
from .broker import AccountInfo, IBroker, OrderType, SymbolInfo, Tick


# ---------------------------------------------------------------------------
# Enums
# ---------------------------------------------------------------------------

class SizingMethod(IntEnum):
    FIXED_RISK = 0
    ATR_RISK   = 1
    KELLY      = 2


class GuardState(IntEnum):
    GREEN  = 0
    YELLOW = 1
    RED    = 2


# ---------------------------------------------------------------------------
# Result dataclasses
# ---------------------------------------------------------------------------

@dataclass
class SizeResult:
    lots:          float
    risk_amount:   float
    risk_pct:      float
    stop_distance: float
    capped:        bool


@dataclass
class GuardStatus:
    state:           GuardState
    daily_pnl:       float
    daily_pnl_pct:   float
    total_dd_pct:    float
    peak_equity:     float
    current_equity:  float
    trading_allowed: bool
    reason:          str


# ---------------------------------------------------------------------------
# PositionSizer
# ---------------------------------------------------------------------------

class PositionSizer:
    """Static helpers that mirror af_normalize_lots / af_kelly_fraction /
    af_size_position from position_sizer.cpp."""

    @staticmethod
    def normalize_lots(
        lots: float,
        step: float,
        min_lots: float,
        max_lots: float,
    ) -> float:
        """Round *lots* to the nearest broker *step* then clamp to [min, max]."""
        if step <= 0.0:
            step = 0.01
        n = round(lots / step) * step
        if n < min_lots:
            n = min_lots
        if n > max_lots:
            n = max_lots
        return n

    @staticmethod
    def kelly_fraction(win_rate: float, rr: float, max_risk: float) -> float:
        """Half-Kelly position-size fraction, floored at 0.005 and capped at max_risk."""
        edge  = win_rate * rr - (1.0 - win_rate)
        kelly = (edge / rr) if rr > 1e-10 else 0.0
        half  = kelly * 0.5
        if half < 0.005:
            half = 0.005
        if half > max_risk:
            half = max_risk
        return half

    @staticmethod
    def size_position(
        method:    SizingMethod,
        balance:   float,
        entry_price: float,
        stop_loss: float,
        sym:       SymbolInfo,
        atr:       float,
        risk_pct:  float,
        max_lots:  float,
        min_lots:  float,
        win_rate:  float = 0.5,
        rr_ratio:  float = 2.0,
    ) -> SizeResult:
        """Compute a normalised lot size matching the C++ af_size_position logic."""
        # --- stop distance ---
        if stop_loss <= 0.0:
            stop_dist = atr * 1.5
        else:
            stop_dist = abs(entry_price - stop_loss)
        if stop_dist < 1e-10:
            stop_dist = entry_price * 0.005

        # --- effective risk fraction ---
        if method == SizingMethod.KELLY:
            eff_risk = PositionSizer.kelly_fraction(win_rate, rr_ratio, risk_pct)
        else:
            eff_risk = risk_pct

        risk_amount = balance * eff_risk

        # --- instrument constants ---
        point         = sym.point if sym.point > 0.0 else 0.00001
        contract_size = sym.contract_size if sym.contract_size > 0.0 else 100_000.0
        val_per_point = contract_size * point
        stop_in_pts   = stop_dist / point

        # --- raw lots ---
        if stop_in_pts > 1e-10 and val_per_point > 1e-10:
            raw_lots = risk_amount / (stop_in_pts * val_per_point)
        else:
            raw_lots = min_lots

        # --- normalise ---
        capped = raw_lots > max_lots
        lots   = PositionSizer.normalize_lots(raw_lots, sym.volume_step, min_lots, max_lots)

        # --- actual risk at normalised size ---
        actual_risk = lots * stop_in_pts * val_per_point
        actual_pct  = actual_risk / balance if balance > 1e-10 else 0.0

        return SizeResult(
            lots=lots,
            risk_amount=actual_risk,
            risk_pct=actual_pct,
            stop_distance=stop_dist,
            capped=capped,
        )


# ---------------------------------------------------------------------------
# DrawdownGuard
# ---------------------------------------------------------------------------

def _utc_day_key() -> int:
    """Return a unique integer per UTC calendar day: year*1000 + day-of-year."""
    t = datetime.now(timezone.utc)
    tt = t.timetuple()
    return tt.tm_year * 1000 + tt.tm_yday


class DrawdownGuard:
    """Tracks daily and total drawdown; halts trading when limits are breached.

    Mirrors drawdown_guard.cpp behaviour exactly:
      - daily_limit  (default 5%)  → RED when daily loss >= limit
      - total_limit  (default 15%) → RED when total drawdown >= limit
      - YELLOW warning at 75% of each threshold
      - Daily P&L resets at each new UTC day; if RED at reset → back to GREEN
    """

    _WARN_FRAC = 0.75

    def __init__(
        self,
        daily_limit: float = 0.05,
        total_limit: float = 0.15,
    ) -> None:
        self._daily_limit = daily_limit
        self._total_limit = total_limit

        self._state:       GuardState = GuardState.GREEN
        self._peak:        float = 0.0
        self._daily_start: float = 0.0
        self._reset_day:   int   = 0
        self._halt_count:  int   = 0

    # --- public setup ---

    def initialise(self, balance: float, equity: float) -> None:
        """Call once after connecting to the broker."""
        self._peak        = equity
        self._daily_start = equity
        self._state       = GuardState.GREEN
        self._reset_day   = _utc_day_key()

    # --- main check ---

    def check(self, equity: float, balance: float) -> GuardStatus:
        """Evaluate current equity against risk limits and return a GuardStatus."""
        today = _utc_day_key()

        # --- daily reset ---
        if today != self._reset_day:
            self._daily_start = equity
            self._reset_day   = today
            # Lift a RED suspension at day-roll
            if self._state == GuardState.RED:
                self._state = GuardState.GREEN

        # --- update peak ---
        if equity > self._peak:
            self._peak = equity

        # --- metrics ---
        daily_pnl = equity - self._daily_start
        daily_pct = daily_pnl / (self._daily_start + 1e-10)
        total_dd  = (self._peak - equity) / (self._peak + 1e-10)

        reason = ""

        if daily_pct <= -self._daily_limit or total_dd >= self._total_limit:
            if self._state != GuardState.RED:
                self._halt_count += 1
            self._state = GuardState.RED
            if daily_pct <= -self._daily_limit:
                reason = (
                    f"Daily loss {daily_pct*100:.2f}% exceeds limit "
                    f"{self._daily_limit*100:.0f}%"
                )
            else:
                reason = (
                    f"Total drawdown {total_dd*100:.2f}% exceeds limit "
                    f"{self._total_limit*100:.0f}%"
                )

        elif (
            daily_pct <= -(self._daily_limit * self._WARN_FRAC)
            or total_dd >= self._total_limit * self._WARN_FRAC
        ):
            if self._state == GuardState.GREEN:
                self._state = GuardState.YELLOW
            reason = "Approaching drawdown limit"

        else:
            if self._state == GuardState.YELLOW:
                self._state = GuardState.GREEN

        trading_allowed = self._state != GuardState.RED

        return GuardStatus(
            state=self._state,
            daily_pnl=daily_pnl,
            daily_pnl_pct=daily_pct,
            total_dd_pct=total_dd,
            peak_equity=self._peak,
            current_equity=equity,
            trading_allowed=trading_allowed,
            reason=reason,
        )

    # --- properties ---

    @property
    def state(self) -> GuardState:
        return self._state

    @property
    def trading_allowed(self) -> bool:
        return self._state != GuardState.RED

    @property
    def halt_count(self) -> int:
        return self._halt_count


# ---------------------------------------------------------------------------
# SignalProcessor
# ---------------------------------------------------------------------------

class SignalProcessor:
    """Convert an AlgoDecision into a live broker order.

    Mirrors signal_processor.cpp:
      - Cooldown per (symbol, direction) pair
      - ATR-based lot sizing (simple formula matching C++ reference)
      - Places market order with 1.5×ATR stop and 3×ATR take-profit
    """

    def __init__(
        self,
        broker:       IBroker,
        max_risk:     float = 0.01,
        cooldown_sec: int   = 300,
    ) -> None:
        self._broker       = broker
        self._max_risk     = max_risk
        self._cooldown_sec = cooldown_sec

        self._last_signal: Dict[str, float] = {}
        self._processed:   int = 0
        self._rejected:    int = 0

    # --- helpers ---

    @staticmethod
    def _cooldown_key(symbol: str, signal: AlgoSignal) -> str:
        suffix = "L" if signal == AlgoSignal.BUY else "S"
        return f"{symbol}_{suffix}"

    # --- main method ---

    def process(self, decision: AlgoDecision) -> bool:
        """Attempt to place an order for *decision*.  Returns True on success."""
        if not decision.is_actionable():
            self._rejected += 1
            return False

        # --- cooldown check ---
        key = self._cooldown_key(decision.symbol, decision.signal)
        now = time.time()
        if now - self._last_signal.get(key, 0.0) < self._cooldown_sec:
            self._rejected += 1
            return False

        # --- broker data ---
        tick:    Tick        = self._broker.get_tick(decision.symbol)
        sym:     SymbolInfo  = self._broker.get_symbol_info(decision.symbol)
        account: AccountInfo = self._broker.get_account()

        # --- sizing ---
        atr = decision.atr if decision.atr > 0.0 else tick.ask * 0.001
        stop_dist  = atr * 1.5
        risk_amt   = account.balance * self._max_risk
        contract   = sym.contract_size if sym.contract_size > 0.0 else 100_000.0
        raw_lots   = risk_amt / (stop_dist * contract) if stop_dist * contract > 1e-10 else sym.volume_min
        lots       = PositionSizer.normalize_lots(
            raw_lots, sym.volume_step, sym.volume_min, sym.volume_max
        )

        # --- entry, SL, TP ---
        if decision.signal == AlgoSignal.BUY:
            order_type = OrderType.BUY
            entry = tick.ask
            sl    = entry - atr * 1.5
            tp    = entry + atr * 3.0
        else:
            order_type = OrderType.SELL
            entry = tick.bid
            sl    = entry + atr * 1.5
            tp    = entry - atr * 3.0

        # --- place order ---
        order = self._broker.place_order(
            decision.symbol,
            order_type,
            lots,
            0.0,          # market order → price = 0
            sl,
            tp,
            1000,
            decision.reason,
        )

        if order.ticket > 0:
            self._last_signal[key] = now
            self._processed += 1
            print(
                f"[SignalProcessor] ORDER #{order.ticket}  "
                f"{decision.symbol} {order_type.name}  "
                f"lots={lots:.2f}  sl={sl:.5f}  tp={tp:.5f}"
            )
            return True

        self._rejected += 1
        return False

    # --- properties ---

    @property
    def processed(self) -> int:
        return self._processed

    @property
    def rejected(self) -> int:
        return self._rejected
