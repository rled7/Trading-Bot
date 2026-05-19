/**
 * Structured JSON logger — Round 11 (24/7 Ops).
 * Outputs one JSON object per line to stdout (DEBUG/INFO/WARN) or stderr (ERROR).
 * Mirrors the Python logger contract used in the C++ / Python reference implementations.
 */

export const LogLevel = Object.freeze({
  DEBUG: 0,
  INFO:  1,
  WARN:  2,
  ERROR: 3,
});

let _minLevel  = LogLevel.INFO;
let _component = 'algoforge';

/**
 * Configure the global logger.
 * @param {string} level     One of DEBUG | INFO | WARN | ERROR (case-insensitive)
 * @param {string} component Default component tag written into every log line
 */
export function initLogger(level = 'INFO', component = 'algoforge') {
  _component = component;
  _minLevel  = LogLevel[level.toUpperCase()] ?? LogLevel.INFO;
}

/** ISO-8601 timestamp with second precision (drops milliseconds). */
function _ts() {
  return new Date().toISOString().replace(/\.\d{3}Z$/, 'Z');
}

/**
 * Core emit function.
 * @param {number} level      Numeric LogLevel value
 * @param {string} levelName  String label written to the JSON
 * @param {*}      msg        Message (coerced to string)
 * @param {string} [component] Override the module-level component tag
 */
function _log(level, levelName, msg, component) {
  if (level < _minLevel) return;
  const line = JSON.stringify({
    ts:        _ts(),
    level:     levelName,
    component: component || _component,
    msg:       String(msg),
  });
  if (level >= LogLevel.ERROR) {
    process.stderr.write(line + '\n');
  } else {
    process.stdout.write(line + '\n');
  }
}

export function logInfo(msg, component)  { _log(LogLevel.INFO,  'INFO',  msg, component); }
export function logWarn(msg, component)  { _log(LogLevel.WARN,  'WARN',  msg, component); }
export function logError(msg, component) { _log(LogLevel.ERROR, 'ERROR', msg, component); }
export function logDebug(msg, component) { _log(LogLevel.DEBUG, 'DEBUG', msg, component); }

/**
 * Create a component-scoped logger object.
 * @param {string} component  Component tag for every line emitted by this logger
 * @returns {{ info, warn, error, debug }}
 */
export function getLogger(component = '') {
  return {
    info:  (msg) => logInfo(msg,  component),
    warn:  (msg) => logWarn(msg,  component),
    error: (msg) => logError(msg, component),
    debug: (msg) => logDebug(msg, component),
  };
}
