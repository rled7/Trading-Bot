/**
 * Telegram alerting — Round 11 (24/7 Ops).
 * Uses the Node.js built-in `https` module only — no external dependencies.
 * Mirrors the Python TelegramAlerter contract.
 */

import https from 'node:https';

export class TelegramAlerter {
  /**
   * @param {string} [botToken]  Bot API token; falls back to TELEGRAM_TOKEN env var
   * @param {string} [chatId]   Destination chat ID; falls back to TELEGRAM_CHAT_ID env var
   */
  constructor(botToken = '', chatId = '') {
    this._token   = botToken   || process.env.TELEGRAM_TOKEN    || '';
    this._chatId  = chatId     || process.env.TELEGRAM_CHAT_ID  || '';
    this._enabled = Boolean(this._token && this._chatId);
  }

  /** True when both token and chat-id are configured. */
  get enabled() { return this._enabled; }

  /**
   * Send a raw text message.
   * @param {string} message
   * @returns {Promise<boolean>}  Resolves to true on HTTP 200, false otherwise
   */
  send(message) {
    if (!this._enabled) return Promise.resolve(false);

    const payload = JSON.stringify({ chat_id: this._chatId, text: message });
    const url     = `https://api.telegram.org/bot${this._token}/sendMessage`;

    return new Promise((resolve) => {
      const req = https.request(url, {
        method:  'POST',
        headers: {
          'Content-Type':   'application/json',
          'Content-Length': Buffer.byteLength(payload),
        },
      }, (res) => {
        res.resume();                       // drain response body
        resolve(res.statusCode === 200);
      });
      req.on('error', () => resolve(false));
      req.write(payload);
      req.end();
    });
  }

  /**
   * Send a prefixed level alert.
   * @param {string} level  e.g. 'INFO', 'WARN', 'ERROR'
   * @param {string} msg
   * @returns {Promise<boolean>}
   */
  alert(level, msg) {
    return this.send(`[${level.toUpperCase()}] ${msg}`);
  }

  /**
   * Notify that the DrawdownGuard has transitioned state.
   * @param {string} stateName  'GREEN' | 'YELLOW' | 'RED'
   * @param {string} reason     Human-readable explanation
   * @returns {Promise<boolean>}
   */
  alertGuardState(stateName, reason) {
    const emoji = { GREEN: '✅', YELLOW: '⚠️', RED: '🚨' }[stateName.toUpperCase()] ?? '❓';
    return this.send(`${emoji} AlgoForge Guard: ${stateName}\n${reason}`);
  }

  /**
   * Notify that a trade has been placed.
   * @param {string} symbol
   * @param {string} direction  'LONG' | 'SHORT'
   * @param {number} lots
   * @param {number} entry
   * @param {number} sl        Stop-loss price
   * @param {number} tp        Take-profit price
   * @returns {Promise<boolean>}
   */
  alertTrade(symbol, direction, lots, entry, sl, tp) {
    const arrow = direction.toUpperCase() === 'LONG' ? '📈' : '📉';
    const msg   =
      `${arrow} Trade Placed\n` +
      `Symbol: ${symbol}\n` +
      `Direction: ${direction}\n` +
      `Lots: ${lots.toFixed(2)}\n` +
      `Entry: ${entry.toFixed(5)}  SL: ${sl.toFixed(5)}  TP: ${tp.toFixed(5)}`;
    return this.send(msg);
  }
}
