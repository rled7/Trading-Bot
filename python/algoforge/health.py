"""Health monitoring — HealthMonitor and HealthSnapshot.

:class:`HealthMonitor` gathers a point-in-time snapshot of the bot's
operational state from the broker, risk guard, and internal counters.

Usage
-----
    from algoforge.health import HealthMonitor

    monitor = HealthMonitor()
    snap = monitor.check(
        broker=broker,
        equity=10_000.0,
        daily_pnl_pct=-0.01,
        guard_state=0,          # GuardState.GREEN
        signals_processed=42,
        signals_rejected=3,
    )
    print(snap.status)          # 'OK'
    print(snap.to_json())
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    from .broker import IBroker


# ---------------------------------------------------------------------------
# HealthSnapshot
# ---------------------------------------------------------------------------

@dataclass
class HealthSnapshot:
    """Immutable point-in-time health report.

    Attributes
    ----------
    uptime_sec:
        Seconds since the :class:`HealthMonitor` was created.
    broker_connected:
        Result of ``IBroker.is_connected()`` (or ``False`` if no broker).
    equity:
        Current account equity in account currency.
    daily_pnl_pct:
        Fractional daily P&L (e.g. ``-0.02`` = −2 %).
    guard_state:
        Integer representation of ``GuardState``:
        ``0`` = GREEN, ``1`` = YELLOW, ``2`` = RED.
    open_positions:
        Number of open positions returned by ``IBroker.get_positions()``.
    signals_processed:
        Cumulative signals successfully processed.
    signals_rejected:
        Cumulative signals rejected (cooldown, guard block, etc.).
    status:
        Derived overall status string: ``'OK'``, ``'WARN'``, or
        ``'CRITICAL'``.
    """

    uptime_sec:        int
    broker_connected:  bool
    equity:            float
    daily_pnl_pct:     float
    guard_state:       int
    open_positions:    int
    signals_processed: int
    signals_rejected:  int
    status:            str  # 'OK' | 'WARN' | 'CRITICAL'

    # ------------------------------------------------------------------
    # Serialisation helpers
    # ------------------------------------------------------------------

    def to_dict(self) -> dict:
        """Return a plain :class:`dict` representation of the snapshot."""
        return {
            'uptime_sec':        self.uptime_sec,
            'broker_connected':  self.broker_connected,
            'equity':            self.equity,
            'daily_pnl_pct':     self.daily_pnl_pct,
            'guard_state':       self.guard_state,
            'open_positions':    self.open_positions,
            'signals_processed': self.signals_processed,
            'signals_rejected':  self.signals_rejected,
            'status':            self.status,
        }

    def to_json(self) -> str:
        """Return a JSON string representation of the snapshot."""
        return json.dumps(self.to_dict(), ensure_ascii=False)


# ---------------------------------------------------------------------------
# HealthMonitor
# ---------------------------------------------------------------------------

class HealthMonitor:
    """Builds :class:`HealthSnapshot` objects on demand.

    The monitor records its creation time and uses it to compute
    :attr:`uptime_sec` on every :meth:`check` call.
    """

    def __init__(self) -> None:
        self._start_time: float = time.time()

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    def check(
        self,
        broker: Optional['IBroker'],
        equity: float,
        daily_pnl_pct: float,
        guard_state: int,
        signals_processed: int = 0,
        signals_rejected: int = 0,
    ) -> HealthSnapshot:
        """Build and return a :class:`HealthSnapshot`.

        Parameters
        ----------
        broker:
            Live broker instance, or ``None`` if not yet connected.
        equity:
            Current account equity in account currency.
        daily_pnl_pct:
            Fractional daily P&L (negative = loss).
        guard_state:
            Integer ``GuardState`` value (0 GREEN, 1 YELLOW, 2 RED).
        signals_processed:
            Cumulative processed signal count (from
            :attr:`SignalProcessor.processed`).
        signals_rejected:
            Cumulative rejected signal count (from
            :attr:`SignalProcessor.rejected`).
        """
        uptime = int(time.time() - self._start_time)
        connected = broker.is_connected() if broker is not None else False

        # Count open positions — swallow any broker errors gracefully
        open_pos = 0
        if broker is not None and connected:
            try:
                positions = broker.get_positions()
                open_pos = len(positions)
            except Exception:
                pass

        # Derive overall status string
        if guard_state == 2:                        # GuardState.RED
            status = 'CRITICAL'
        elif guard_state == 1 or daily_pnl_pct < -0.03:  # YELLOW or >3 % loss
            status = 'WARN'
        else:
            status = 'OK'

        return HealthSnapshot(
            uptime_sec=uptime,
            broker_connected=connected,
            equity=equity,
            daily_pnl_pct=daily_pnl_pct,
            guard_state=guard_state,
            open_positions=open_pos,
            signals_processed=signals_processed,
            signals_rejected=signals_rejected,
            status=status,
        )

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def uptime_sec(self) -> int:
        """Seconds elapsed since this :class:`HealthMonitor` was created."""
        return int(time.time() - self._start_time)
