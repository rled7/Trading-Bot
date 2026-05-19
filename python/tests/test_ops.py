"""Tests for Round 11 ops modules: logger, health, telegram."""
from __future__ import annotations

import json
import logging
import time
import unittest

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_log_record(msg: str = 'hello', level: int = logging.INFO,
                     name: str = 'algoforge') -> logging.LogRecord:
    record = logging.LogRecord(
        name=name,
        level=level,
        pathname='',
        lineno=0,
        msg=msg,
        args=(),
        exc_info=None,
    )
    return record


# ---------------------------------------------------------------------------
# JsonFormatter
# ---------------------------------------------------------------------------

class TestJsonFormatter(unittest.TestCase):
    """1. JsonFormatter outputs valid JSON with required keys."""

    def setUp(self) -> None:
        from algoforge.logger import JsonFormatter
        self.fmt = JsonFormatter()

    def test_output_is_valid_json(self) -> None:
        record = _make_log_record('test message')
        raw = self.fmt.format(record)
        obj = json.loads(raw)               # must not raise
        self.assertIsInstance(obj, dict)

    def test_required_keys_present(self) -> None:
        record = _make_log_record('check keys')
        obj = json.loads(self.fmt.format(record))
        for key in ('ts', 'level', 'component', 'msg'):
            self.assertIn(key, obj, f"Missing key: {key}")

    def test_ts_is_iso8601_utc(self) -> None:
        record = _make_log_record()
        obj = json.loads(self.fmt.format(record))
        ts: str = obj['ts']
        # Must end with 'Z' and match YYYY-MM-DDTHH:MM:SSZ
        self.assertTrue(ts.endswith('Z'), f"ts does not end with Z: {ts!r}")
        self.assertEqual(len(ts), 20, f"Unexpected ts length: {ts!r}")

    def test_level_field(self) -> None:
        record = _make_log_record(level=logging.WARNING)
        obj = json.loads(self.fmt.format(record))
        self.assertEqual(obj['level'], 'WARNING')

    def test_component_defaults_to_logger_name(self) -> None:
        record = _make_log_record(name='mycomponent')
        obj = json.loads(self.fmt.format(record))
        self.assertEqual(obj['component'], 'mycomponent')

    def test_component_from_extra(self) -> None:
        record = _make_log_record(name='algoforge')
        record.component = 'broker'         # set by LoggerAdapter
        obj = json.loads(self.fmt.format(record))
        self.assertEqual(obj['component'], 'broker')

    def test_msg_field(self) -> None:
        record = _make_log_record('specific message content')
        obj = json.loads(self.fmt.format(record))
        self.assertEqual(obj['msg'], 'specific message content')

    def test_exception_appended_to_msg(self) -> None:
        try:
            raise ValueError('boom')
        except ValueError:
            import sys
            exc_info = sys.exc_info()

        record = _make_log_record('error occurred')
        record.exc_info = exc_info
        obj = json.loads(self.fmt.format(record))
        self.assertIn('ValueError', obj['msg'])
        self.assertIn('boom', obj['msg'])


# ---------------------------------------------------------------------------
# init_logger / get_logger
# ---------------------------------------------------------------------------

class TestLoggerInit(unittest.TestCase):
    """2. init_logger doesn't raise; get_logger returns a logger."""

    def test_init_logger_returns_logger(self) -> None:
        from algoforge.logger import init_logger
        logger = init_logger(level='WARNING', component='test')
        self.assertIsNotNone(logger)
        self.assertIsInstance(logger, logging.Logger)

    def test_init_logger_sets_level(self) -> None:
        from algoforge.logger import init_logger
        logger = init_logger(level='DEBUG')
        self.assertEqual(logger.level, logging.DEBUG)

    def test_get_logger_returns_adapter(self) -> None:
        from algoforge.logger import init_logger, get_logger
        init_logger()
        log = get_logger('risk')
        self.assertIsNotNone(log)
        # Must have the standard logging methods
        self.assertTrue(callable(log.info))
        self.assertTrue(callable(log.warning))
        self.assertTrue(callable(log.error))
        self.assertTrue(callable(log.debug))

    def test_get_logger_lazy_init(self) -> None:
        """get_logger must work even if init_logger was never called."""
        import algoforge.logger as lg
        lg._logger = None                   # reset module state
        log = lg.get_logger('lazy')
        self.assertIsNotNone(log)

    def test_convenience_functions_do_not_raise(self) -> None:
        from algoforge.logger import init_logger, log_info, log_warn, log_error, log_debug
        init_logger(level='DEBUG')
        log_info('info msg', component='test')
        log_warn('warn msg', component='test')
        log_error('error msg', component='test')
        log_debug('debug msg', component='test')


# ---------------------------------------------------------------------------
# HealthMonitor / HealthSnapshot
# ---------------------------------------------------------------------------

class TestHealthMonitor(unittest.TestCase):
    """3. HealthMonitor.check returns HealthSnapshot with correct status."""

    def setUp(self) -> None:
        from algoforge.health import HealthMonitor
        self.monitor = HealthMonitor()

    def _check(self, guard_state: int, daily_pnl_pct: float = 0.0):
        from algoforge.health import HealthSnapshot
        snap = self.monitor.check(
            broker=None,
            equity=10_000.0,
            daily_pnl_pct=daily_pnl_pct,
            guard_state=guard_state,
            signals_processed=10,
            signals_rejected=2,
        )
        self.assertIsInstance(snap, HealthSnapshot)
        return snap

    def test_guard_red_gives_critical(self) -> None:
        snap = self._check(guard_state=2)
        self.assertEqual(snap.status, 'CRITICAL')

    def test_guard_yellow_gives_warn(self) -> None:
        snap = self._check(guard_state=1)
        self.assertEqual(snap.status, 'WARN')

    def test_guard_green_gives_ok(self) -> None:
        snap = self._check(guard_state=0)
        self.assertEqual(snap.status, 'OK')

    def test_large_loss_gives_warn_even_when_green(self) -> None:
        # daily_pnl_pct < -0.03 should escalate GREEN → WARN
        snap = self._check(guard_state=0, daily_pnl_pct=-0.05)
        self.assertEqual(snap.status, 'WARN')

    def test_broker_none_not_connected(self) -> None:
        snap = self._check(guard_state=0)
        self.assertFalse(snap.broker_connected)

    def test_open_positions_zero_when_no_broker(self) -> None:
        snap = self._check(guard_state=0)
        self.assertEqual(snap.open_positions, 0)

    def test_signals_propagated(self) -> None:
        snap = self._check(guard_state=0)
        self.assertEqual(snap.signals_processed, 10)
        self.assertEqual(snap.signals_rejected, 2)

    def test_uptime_non_negative(self) -> None:
        snap = self._check(guard_state=0)
        self.assertGreaterEqual(snap.uptime_sec, 0)

    def test_uptime_property(self) -> None:
        uptime = self.monitor.uptime_sec
        self.assertGreaterEqual(uptime, 0)

    def test_equity_stored(self) -> None:
        snap = self._check(guard_state=0)
        self.assertAlmostEqual(snap.equity, 10_000.0)

    def test_broker_connected_via_mock(self) -> None:
        """HealthMonitor queries broker.is_connected() when broker is set."""
        class _MockBroker:
            def is_connected(self) -> bool:
                return True
            def get_positions(self) -> list:
                return ['pos1', 'pos2']

        from algoforge.health import HealthMonitor
        mon = HealthMonitor()
        snap = mon.check(
            broker=_MockBroker(),       # type: ignore[arg-type]
            equity=5_000.0,
            daily_pnl_pct=0.01,
            guard_state=0,
        )
        self.assertTrue(snap.broker_connected)
        self.assertEqual(snap.open_positions, 2)

    def test_broker_get_positions_exception_is_swallowed(self) -> None:
        """Errors in get_positions() must not propagate."""
        class _BrokenBroker:
            def is_connected(self) -> bool:
                return True
            def get_positions(self):
                raise RuntimeError('network error')

        from algoforge.health import HealthMonitor
        mon = HealthMonitor()
        snap = mon.check(
            broker=_BrokenBroker(),     # type: ignore[arg-type]
            equity=5_000.0,
            daily_pnl_pct=0.0,
            guard_state=0,
        )
        self.assertEqual(snap.open_positions, 0)    # falls back to 0


# ---------------------------------------------------------------------------
# HealthSnapshot serialisation
# ---------------------------------------------------------------------------

class TestHealthSnapshot(unittest.TestCase):
    """5. HealthSnapshot.to_json() returns valid JSON string."""

    def _make_snap(self):
        from algoforge.health import HealthSnapshot
        return HealthSnapshot(
            uptime_sec=120,
            broker_connected=True,
            equity=12_000.0,
            daily_pnl_pct=0.02,
            guard_state=0,
            open_positions=3,
            signals_processed=50,
            signals_rejected=5,
            status='OK',
        )

    def test_to_json_is_valid_json(self) -> None:
        snap = self._make_snap()
        raw = snap.to_json()
        obj = json.loads(raw)               # must not raise
        self.assertIsInstance(obj, dict)

    def test_to_dict_keys(self) -> None:
        snap = self._make_snap()
        d = snap.to_dict()
        for key in (
            'uptime_sec', 'broker_connected', 'equity', 'daily_pnl_pct',
            'guard_state', 'open_positions', 'signals_processed',
            'signals_rejected', 'status',
        ):
            self.assertIn(key, d, f"Missing key in to_dict(): {key}")

    def test_to_json_values_match(self) -> None:
        snap = self._make_snap()
        obj = json.loads(snap.to_json())
        self.assertEqual(obj['status'], 'OK')
        self.assertEqual(obj['uptime_sec'], 120)
        self.assertTrue(obj['broker_connected'])
        self.assertAlmostEqual(obj['equity'], 12_000.0)


# ---------------------------------------------------------------------------
# TelegramAlerter
# ---------------------------------------------------------------------------

class TestTelegramAlerterDisabled(unittest.TestCase):
    """4. TelegramAlerter with empty token → enabled=False, send returns False."""

    def setUp(self) -> None:
        # Ensure env vars are absent for these tests
        import os
        os.environ.pop('TELEGRAM_TOKEN', None)
        os.environ.pop('TELEGRAM_CHAT_ID', None)
        from algoforge.telegram import TelegramAlerter
        self.tg = TelegramAlerter()

    def test_disabled_when_no_credentials(self) -> None:
        self.assertFalse(self.tg.enabled)

    def test_send_returns_false_when_disabled(self) -> None:
        result = self.tg.send('hello')
        self.assertFalse(result)

    def test_alert_returns_false_when_disabled(self) -> None:
        result = self.tg.alert('CRITICAL', 'trading halted')
        self.assertFalse(result)

    def test_alert_guard_state_returns_false_when_disabled(self) -> None:
        result = self.tg.alert_guard_state('RED', 'daily loss limit')
        self.assertFalse(result)

    def test_alert_trade_returns_false_when_disabled(self) -> None:
        result = self.tg.alert_trade('EURUSD', 'LONG', 0.10, 1.085, 1.082, 1.091)
        self.assertFalse(result)

    def test_explicit_empty_strings_disables(self) -> None:
        from algoforge.telegram import TelegramAlerter
        tg = TelegramAlerter(bot_token='', chat_id='')
        self.assertFalse(tg.enabled)
        self.assertFalse(tg.send('noop'))


class TestTelegramAlerterFormatting(unittest.TestCase):
    """Verify helper message formatting without making real HTTP calls."""

    def _make_capture_alerter(self):
        """Return a TelegramAlerter whose send() is monkeypatched to capture calls."""
        from algoforge.telegram import TelegramAlerter
        tg = TelegramAlerter.__new__(TelegramAlerter)
        tg._token   = 'fake'
        tg._chat_id = '123'
        tg._enabled = True
        tg._captured: list[str] = []

        def _fake_send(message: str, parse_mode: str = '') -> bool:
            tg._captured.append(message)
            return True

        tg.send = _fake_send  # type: ignore[method-assign]
        return tg

    def test_alert_level_uppercased(self) -> None:
        tg = self._make_capture_alerter()
        tg.alert('critical', 'halted')
        self.assertIn('[CRITICAL]', tg._captured[0])

    def test_alert_guard_state_green_emoji(self) -> None:
        tg = self._make_capture_alerter()
        tg.alert_guard_state('GREEN', 'all good')
        self.assertIn('✅', tg._captured[0])
        self.assertIn('GREEN', tg._captured[0])

    def test_alert_guard_state_yellow_emoji(self) -> None:
        tg = self._make_capture_alerter()
        tg.alert_guard_state('YELLOW', 'warning')
        self.assertIn('⚠️', tg._captured[0])

    def test_alert_guard_state_red_emoji(self) -> None:
        tg = self._make_capture_alerter()
        tg.alert_guard_state('RED', 'halted')
        self.assertIn('🚨', tg._captured[0])

    def test_alert_trade_long_arrow(self) -> None:
        tg = self._make_capture_alerter()
        tg.alert_trade('EURUSD', 'LONG', 0.10, 1.085, 1.082, 1.091)
        msg = tg._captured[0]
        self.assertIn('📈', msg)
        self.assertIn('EURUSD', msg)
        self.assertIn('LONG', msg)
        self.assertIn('0.10', msg)

    def test_alert_trade_short_arrow(self) -> None:
        tg = self._make_capture_alerter()
        tg.alert_trade('GBPUSD', 'SHORT', 0.05, 1.270, 1.275, 1.260)
        self.assertIn('📉', tg._captured[0])

    def test_alert_trade_prices_formatted(self) -> None:
        tg = self._make_capture_alerter()
        tg.alert_trade('EURUSD', 'LONG', 0.10, 1.08500, 1.08200, 1.09100)
        msg = tg._captured[0]
        self.assertIn('1.08500', msg)
        self.assertIn('1.08200', msg)
        self.assertIn('1.09100', msg)


# ---------------------------------------------------------------------------
# Package-level import smoke test
# ---------------------------------------------------------------------------

class TestPackageExports(unittest.TestCase):
    """Verify the new symbols are accessible from the top-level package."""

    def test_init_logger_exported(self) -> None:
        from algoforge import init_logger
        self.assertTrue(callable(init_logger))

    def test_get_logger_exported(self) -> None:
        from algoforge import get_logger
        self.assertTrue(callable(get_logger))

    def test_health_monitor_exported(self) -> None:
        from algoforge import HealthMonitor
        self.assertIsNotNone(HealthMonitor)

    def test_health_snapshot_exported(self) -> None:
        from algoforge import HealthSnapshot
        self.assertIsNotNone(HealthSnapshot)

    def test_telegram_alerter_exported(self) -> None:
        from algoforge import TelegramAlerter
        self.assertIsNotNone(TelegramAlerter)


if __name__ == '__main__':
    unittest.main()
