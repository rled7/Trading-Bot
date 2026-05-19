/**
 * Health-check snapshot and monitor — Round 11 (24/7 Ops).
 * Mirrors the Python HealthMonitor / HealthSnapshot contract.
 */

import { GuardState } from './risk.js';

// ── HealthSnapshot ────────────────────────────────────────────────────────────

export class HealthSnapshot {
  /**
   * @param {object} opts
   * @param {number}  opts.uptimeSec
   * @param {boolean} opts.brokerConnected
   * @param {number}  opts.equity
   * @param {number}  opts.dailyPnlPct
   * @param {string|number} opts.guardState   GuardState value (or numeric sentinel)
   * @param {number}  opts.openPositions
   * @param {number}  opts.signalsProcessed
   * @param {number}  opts.signalsRejected
   * @param {string}  opts.status            'OK' | 'WARN' | 'CRITICAL'
   */
  constructor({
    uptimeSec, brokerConnected, equity, dailyPnlPct, guardState,
    openPositions, signalsProcessed, signalsRejected, status,
  }) {
    this.uptimeSec        = uptimeSec;
    this.brokerConnected  = brokerConnected;
    this.equity           = equity;
    this.dailyPnlPct      = dailyPnlPct;
    this.guardState       = guardState;
    this.openPositions    = openPositions;
    this.signalsProcessed = signalsProcessed;
    this.signalsRejected  = signalsRejected;
    this.status           = status;
  }

  /** Plain object representation — suitable for structured logging. */
  toDict() { return { ...this }; }

  /** Compact JSON string. */
  toJSON() { return JSON.stringify(this.toDict()); }
}

// ── HealthMonitor ─────────────────────────────────────────────────────────────

export class HealthMonitor {
  constructor() {
    this._startTime = Date.now();
  }

  /** Seconds since this monitor was constructed. */
  get uptimeSec() { return Math.floor((Date.now() - this._startTime) / 1000); }

  /**
   * Build a health snapshot from the current runtime state.
   *
   * guardState accepts:
   *   - GuardState string values ('green' | 'yellow' | 'red')
   *   - Numeric sentinels used by tests/legacy code (0 = GREEN, 1 = YELLOW, 2 = RED)
   *
   * @param {object|null} broker           IBroker-compatible instance (may be null)
   * @param {number}      equity           Current account equity
   * @param {number}      dailyPnlPct      Daily P&L as a fraction (e.g. -0.03 = -3%)
   * @param {string|number} guardState     GuardState value (or numeric sentinel)
   * @param {number}      [signalsProcessed=0]
   * @param {number}      [signalsRejected=0]
   * @returns {HealthSnapshot}
   */
  check(broker, equity, dailyPnlPct, guardState, signalsProcessed = 0, signalsRejected = 0) {
    const uptimeSec      = this.uptimeSec;
    const brokerConnected = broker ? broker.isConnected() : false;

    let openPositions = 0;
    if (broker && brokerConnected) {
      try { openPositions = broker.getPositions().length; } catch { /* ignore */ }
    }

    // Derive status from guardState (string or numeric) and dailyPnlPct.
    // Numeric sentinels: 0 = GREEN, 1 = YELLOW, 2 = RED  (matches GuardState ordering).
    const isRed =
      guardState === GuardState.RED    ||
      guardState === 'red'             ||
      guardState === 2;

    const isYellow =
      guardState === GuardState.YELLOW ||
      guardState === 'yellow'          ||
      guardState === 1;

    let status;
    if (isRed) {
      status = 'CRITICAL';
    } else if (isYellow || dailyPnlPct < -0.03) {
      status = 'WARN';
    } else {
      status = 'OK';
    }

    return new HealthSnapshot({
      uptimeSec,
      brokerConnected,
      equity,
      dailyPnlPct,
      guardState,
      openPositions,
      signalsProcessed,
      signalsRejected,
      status,
    });
  }
}
