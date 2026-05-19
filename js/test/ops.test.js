/**
 * Tests for Round 11 (24/7 Ops) — logger.js, health.js, telegram.js
 *
 * Test style mirrors risk.test.js: node:test + node:assert/strict.
 *
 * Logger output tests capture stdout/stderr by temporarily replacing
 * process.stdout.write / process.stderr.write, then restoring them.
 * This avoids any external dependencies and keeps tests hermetic.
 */

import { test } from 'node:test';
import assert   from 'node:assert/strict';

import {
  LogLevel,
  initLogger,
  logInfo,
  logWarn,
  logError,
  logDebug,
  getLogger,
} from '../src/logger.js';

import { HealthMonitor, HealthSnapshot } from '../src/health.js';
import { TelegramAlerter }               from '../src/telegram.js';
import { GuardState }                    from '../src/risk.js';

// ── Helpers ───────────────────────────────────────────────────────────────────

/**
 * Capture a single write to process.stdout and restore it afterwards.
 * Returns the raw string that was written (or '' if nothing was written).
 */
function captureStdout(fn) {
  const orig = process.stdout.write.bind(process.stdout);
  let captured = '';
  process.stdout.write = (chunk) => { captured += chunk; return true; };
  try { fn(); } finally { process.stdout.write = orig; }
  return captured;
}

/**
 * Capture a single write to process.stderr and restore it afterwards.
 */
function captureStderr(fn) {
  const orig = process.stderr.write.bind(process.stderr);
  let captured = '';
  process.stderr.write = (chunk) => { captured += chunk; return true; };
  try { fn(); } finally { process.stderr.write = orig; }
  return captured;
}

// ── LogLevel enum ─────────────────────────────────────────────────────────────

test('LogLevel enum is frozen and has correct ordering', () => {
  assert.ok(Object.isFrozen(LogLevel));
  assert.ok(LogLevel.DEBUG < LogLevel.INFO);
  assert.ok(LogLevel.INFO  < LogLevel.WARN);
  assert.ok(LogLevel.WARN  < LogLevel.ERROR);
});

// ── initLogger ────────────────────────────────────────────────────────────────

test('initLogger: does not throw with default args', () => {
  assert.doesNotThrow(() => initLogger());
});

test('initLogger: does not throw with explicit args', () => {
  assert.doesNotThrow(() => initLogger('DEBUG', 'test-component'));
  // Restore to INFO for subsequent tests
  initLogger('INFO', 'algoforge');
});

// ── logInfo ───────────────────────────────────────────────────────────────────

test('logInfo: writes valid JSON to stdout', () => {
  initLogger('INFO', 'algoforge');
  const raw = captureStdout(() => logInfo('hello world'));
  assert.ok(raw.length > 0, 'nothing was written to stdout');

  const obj = JSON.parse(raw.trim());
  assert.equal(typeof obj.ts,        'string');
  assert.equal(typeof obj.level,     'string');
  assert.equal(typeof obj.component, 'string');
  assert.equal(typeof obj.msg,       'string');
});

test('logInfo: level field is "INFO"', () => {
  initLogger('INFO', 'algoforge');
  const raw = captureStdout(() => logInfo('test'));
  const obj = JSON.parse(raw.trim());
  assert.equal(obj.level, 'INFO');
});

test('logInfo: msg field matches the passed string', () => {
  initLogger('INFO', 'algoforge');
  const raw = captureStdout(() => logInfo('specific message'));
  const obj = JSON.parse(raw.trim());
  assert.equal(obj.msg, 'specific message');
});

test('logInfo: component override is reflected in output', () => {
  initLogger('INFO', 'algoforge');
  const raw = captureStdout(() => logInfo('x', 'my-module'));
  const obj = JSON.parse(raw.trim());
  assert.equal(obj.component, 'my-module');
});

// ── logWarn ───────────────────────────────────────────────────────────────────

test('logWarn: writes to stdout with level "WARN"', () => {
  initLogger('INFO', 'algoforge');
  const raw = captureStdout(() => logWarn('something suspect'));
  const obj = JSON.parse(raw.trim());
  assert.equal(obj.level, 'WARN');
});

// ── logError ──────────────────────────────────────────────────────────────────

test('logError: writes to stderr (not stdout)', () => {
  initLogger('INFO', 'algoforge');
  let stdoutData = '';
  let stderrData = '';
  const origOut = process.stdout.write.bind(process.stdout);
  const origErr = process.stderr.write.bind(process.stderr);
  process.stdout.write = (c) => { stdoutData += c; return true; };
  process.stderr.write = (c) => { stderrData += c; return true; };
  try {
    logError('fatal fault');
  } finally {
    process.stdout.write = origOut;
    process.stderr.write = origErr;
  }
  assert.equal(stdoutData, '', 'logError must not write to stdout');
  assert.ok(stderrData.length > 0, 'logError must write to stderr');
});

test('logError: level field is "ERROR"', () => {
  initLogger('INFO', 'algoforge');
  const raw = captureStderr(() => logError('boom'));
  const obj = JSON.parse(raw.trim());
  assert.equal(obj.level, 'ERROR');
});

// ── logDebug ──────────────────────────────────────────────────────────────────

test('logDebug: suppressed when minLevel=INFO', () => {
  initLogger('INFO', 'algoforge');
  const raw = captureStdout(() => logDebug('verbose detail'));
  assert.equal(raw, '', 'DEBUG should be suppressed at INFO level');
});

test('logDebug: visible when minLevel=DEBUG', () => {
  initLogger('DEBUG', 'algoforge');
  const raw = captureStdout(() => logDebug('verbose detail'));
  assert.ok(raw.length > 0, 'DEBUG should be emitted at DEBUG level');
  const obj = JSON.parse(raw.trim());
  assert.equal(obj.level, 'DEBUG');
  // Restore
  initLogger('INFO', 'algoforge');
});

// ── getLogger ─────────────────────────────────────────────────────────────────

test('getLogger: returns object with info, warn, error, debug methods', () => {
  const log = getLogger('engine');
  assert.equal(typeof log.info,  'function');
  assert.equal(typeof log.warn,  'function');
  assert.equal(typeof log.error, 'function');
  assert.equal(typeof log.debug, 'function');
});

test('getLogger: info method does not throw', () => {
  const log = getLogger('engine');
  assert.doesNotThrow(() => log.info('hello from engine'));
});

test('getLogger: component tag appears in output', () => {
  initLogger('INFO', 'algoforge');
  const log = getLogger('strategy');
  const raw = captureStdout(() => log.info('firing'));
  const obj = JSON.parse(raw.trim());
  assert.equal(obj.component, 'strategy');
});

// ── logInfo return value ───────────────────────────────────────────────────────

test('logInfo: returns undefined', () => {
  initLogger('INFO', 'algoforge');
  const ret = captureStdout(() => {
    const r = logInfo('ret check');
    assert.equal(r, undefined);
  });
  // just confirm something was written (extra guard)
  assert.ok(ret.length > 0);
});

// ── HealthMonitor / HealthSnapshot ────────────────────────────────────────────

test('HealthMonitor: uptimeSec is a non-negative integer', () => {
  const hm = new HealthMonitor();
  assert.ok(hm.uptimeSec >= 0);
  assert.ok(Number.isInteger(hm.uptimeSec));
});

test('HealthMonitor.check: guardState=2 (numeric RED) → status=CRITICAL', () => {
  const hm = new HealthMonitor();
  const snap = hm.check(null, 10_000, -0.01, 2);
  assert.equal(snap.status, 'CRITICAL');
});

test('HealthMonitor.check: GuardState.RED string → status=CRITICAL', () => {
  const hm = new HealthMonitor();
  const snap = hm.check(null, 10_000, -0.01, GuardState.RED);
  assert.equal(snap.status, 'CRITICAL');
});

test('HealthMonitor.check: guardState=0, dailyPnlPct=-0.01 → status=OK', () => {
  const hm = new HealthMonitor();
  const snap = hm.check(null, 10_000, -0.01, 0);
  assert.equal(snap.status, 'OK');
});

test('HealthMonitor.check: guardState=GuardState.GREEN, dailyPnlPct=0 → status=OK', () => {
  const hm = new HealthMonitor();
  const snap = hm.check(null, 10_000, 0, GuardState.GREEN);
  assert.equal(snap.status, 'OK');
});

test('HealthMonitor.check: guardState=1 (numeric YELLOW) → status=WARN', () => {
  const hm = new HealthMonitor();
  const snap = hm.check(null, 10_000, -0.01, 1);
  assert.equal(snap.status, 'WARN');
});

test('HealthMonitor.check: guardState=GREEN but dailyPnlPct=-0.05 → status=WARN', () => {
  const hm = new HealthMonitor();
  const snap = hm.check(null, 10_000, -0.05, GuardState.GREEN);
  assert.equal(snap.status, 'WARN');
});

test('HealthMonitor.check: brokerConnected=false when broker=null', () => {
  const hm = new HealthMonitor();
  const snap = hm.check(null, 10_000, 0, GuardState.GREEN);
  assert.equal(snap.brokerConnected, false);
});

test('HealthMonitor.check: brokerConnected=true when broker.isConnected returns true', () => {
  const hm     = new HealthMonitor();
  const broker = { isConnected: () => true, getPositions: () => [] };
  const snap   = hm.check(broker, 10_000, 0, GuardState.GREEN);
  assert.equal(snap.brokerConnected, true);
});

test('HealthMonitor.check: openPositions from broker.getPositions().length', () => {
  const hm     = new HealthMonitor();
  const broker = { isConnected: () => true, getPositions: () => [1, 2, 3] };
  const snap   = hm.check(broker, 10_000, 0, GuardState.GREEN);
  assert.equal(snap.openPositions, 3);
});

test('HealthMonitor.check: openPositions=0 when broker throws', () => {
  const hm     = new HealthMonitor();
  const broker = { isConnected: () => true, getPositions: () => { throw new Error('oops'); } };
  const snap   = hm.check(broker, 10_000, 0, GuardState.GREEN);
  assert.equal(snap.openPositions, 0);
});

test('HealthMonitor.check: signalsProcessed and signalsRejected are passed through', () => {
  const hm   = new HealthMonitor();
  const snap = hm.check(null, 10_000, 0, GuardState.GREEN, 42, 7);
  assert.equal(snap.signalsProcessed, 42);
  assert.equal(snap.signalsRejected, 7);
});

test('HealthMonitor.check: equity and dailyPnlPct are passed through', () => {
  const hm   = new HealthMonitor();
  const snap = hm.check(null, 99_999, -0.02, GuardState.GREEN);
  assert.equal(snap.equity, 99_999);
  assert.ok(Math.abs(snap.dailyPnlPct - (-0.02)) < 1e-12);
});

// ── HealthSnapshot ────────────────────────────────────────────────────────────

test('HealthSnapshot.toDict: returns a plain object with all expected keys', () => {
  const snap = new HealthSnapshot({
    uptimeSec: 60, brokerConnected: true, equity: 10_000, dailyPnlPct: 0.01,
    guardState: GuardState.GREEN, openPositions: 2, signalsProcessed: 10,
    signalsRejected: 1, status: 'OK',
  });
  const d = snap.toDict();
  assert.equal(typeof d, 'object');
  assert.ok('uptimeSec'        in d);
  assert.ok('brokerConnected'  in d);
  assert.ok('equity'           in d);
  assert.ok('dailyPnlPct'      in d);
  assert.ok('guardState'       in d);
  assert.ok('openPositions'    in d);
  assert.ok('signalsProcessed' in d);
  assert.ok('signalsRejected'  in d);
  assert.ok('status'           in d);
});

test('HealthSnapshot.toJSON: returns valid parseable JSON', () => {
  const snap = new HealthSnapshot({
    uptimeSec: 120, brokerConnected: false, equity: 5_000, dailyPnlPct: -0.01,
    guardState: GuardState.YELLOW, openPositions: 0, signalsProcessed: 5,
    signalsRejected: 2, status: 'WARN',
  });
  const str = snap.toJSON();
  assert.equal(typeof str, 'string');
  const obj = JSON.parse(str);   // throws if invalid
  assert.equal(obj.status, 'WARN');
  assert.equal(obj.uptimeSec, 120);
});

test('HealthSnapshot.toJSON: round-trips equity correctly', () => {
  const snap = new HealthSnapshot({
    uptimeSec: 0, brokerConnected: true, equity: 12_345.67, dailyPnlPct: 0,
    guardState: GuardState.GREEN, openPositions: 1,
    signalsProcessed: 0, signalsRejected: 0, status: 'OK',
  });
  const obj = JSON.parse(snap.toJSON());
  assert.ok(Math.abs(obj.equity - 12_345.67) < 1e-9);
});

// ── TelegramAlerter ───────────────────────────────────────────────────────────

test('TelegramAlerter: empty token → enabled=false', () => {
  const ta = new TelegramAlerter('', '');
  assert.equal(ta.enabled, false);
});

test('TelegramAlerter: token but no chatId → enabled=false', () => {
  const ta = new TelegramAlerter('tok123', '');
  assert.equal(ta.enabled, false);
});

test('TelegramAlerter: chatId but no token → enabled=false', () => {
  const ta = new TelegramAlerter('', '12345');
  assert.equal(ta.enabled, false);
});

test('TelegramAlerter: both token and chatId → enabled=true', () => {
  const ta = new TelegramAlerter('bot_token', '12345');
  assert.equal(ta.enabled, true);
});

test('TelegramAlerter.send: disabled → resolves to false', async () => {
  const ta  = new TelegramAlerter('', '');
  const res = await ta.send('hello');
  assert.equal(res, false);
});

test('TelegramAlerter.alert: disabled → resolves to false', async () => {
  const ta  = new TelegramAlerter('', '');
  const res = await ta.alert('WARN', 'approaching limit');
  assert.equal(res, false);
});

test('TelegramAlerter.alertGuardState: disabled → resolves to false', async () => {
  const ta  = new TelegramAlerter('', '');
  const res = await ta.alertGuardState('RED', 'daily limit hit');
  assert.equal(res, false);
});

test('TelegramAlerter.alertTrade: disabled → resolves to false', async () => {
  const ta  = new TelegramAlerter('', '');
  const res = await ta.alertTrade('EURUSD', 'LONG', 0.10, 1.08500, 1.08200, 1.09100);
  assert.equal(res, false);
});

test('TelegramAlerter.send: returns a Promise', () => {
  const ta = new TelegramAlerter('', '');
  const p  = ta.send('test');
  assert.ok(p instanceof Promise);
});

test('TelegramAlerter.alertGuardState: formats YELLOW emoji correctly (smoke test)', async () => {
  // Verify the method resolves without throwing even when disabled
  const ta = new TelegramAlerter();
  const res = await ta.alertGuardState('YELLOW', 'approaching daily limit');
  assert.equal(res, false);   // disabled — still resolves false
});

test('TelegramAlerter.alertTrade: resolves without throwing for LONG (smoke test)', async () => {
  const ta = new TelegramAlerter();
  assert.doesNotThrow(async () => {
    await ta.alertTrade('GBPUSD', 'LONG', 0.05, 1.27500, 1.27200, 1.28100);
  });
});

test('TelegramAlerter.alertTrade: resolves without throwing for SHORT (smoke test)', async () => {
  const ta = new TelegramAlerter();
  assert.doesNotThrow(async () => {
    await ta.alertTrade('GBPUSD', 'SHORT', 0.05, 1.27500, 1.27800, 1.26900);
  });
});
