/**
 * schema.js — JSON schema validation for AlgoManifest.
 *
 * Hand-rolled, no third-party dependencies. Mirrors python schema.py exactly.
 *
 * Public API:
 *   validateManifest(d) -> AlgoManifest  (throws Error on failure)
 */

// ── Schema constants ──────────────────────────────────────────────────────────

const SUPPORTED_SCHEMA_VERSIONS = new Set(['1.0']);

const VALID_TIMEFRAMES = new Set([
    'M1', 'M5', 'M15', 'M30', 'H1', 'H4', 'D1', 'W1', 'S15',
]);

/**
 * Indicator kind whitelist — mirrors Python INDICATOR_KINDS exactly.
 */
export const INDICATOR_KINDS = new Set([
    'sma', 'ema', 'rsi', 'atr', 'macd', 'bollinger',
    'stochastic', 'obv', 'wma', 'cci', 'williams_r', 'roc',
    'mfi', 'vwap', 'keltner', 'adx', 'hma', 'dema', 'tema',
    // extra indicators present in the module
    'trix', 'momentum', 'true_range', 'wilder_ema', 'vwma',
    'hist_vol', 'cmf', 'acc_dist', 'force_index', 'vol_osc',
    'donchian',
]);

const VALID_SIDES = new Set(['long', 'short']);
const VALID_SIZE_METHODS = new Set(['atr', 'fixed']);

// ── Helpers ───────────────────────────────────────────────────────────────────

function require_(d, key, type, where) {
    if (!(key in d)) {
        throw new Error(`${where}: missing required field '${key}'`);
    }
    const val = d[key];
    // eslint-disable-next-line valid-typeof
    if (typeof val !== type) {
        throw new Error(`${where}: field '${key}' must be ${type}, got ${typeof val}`);
    }
    return val;
}

function requireStr(d, key, where, maxLen = 0) {
    const val = require_(d, key, 'string', where);
    if (maxLen > 0 && val.length > maxLen) {
        throw new Error(`${where}: field '${key}' exceeds ${maxLen} characters`);
    }
    return val;
}

function optFloat(d, key, where) {
    const val = d[key];
    if (val === undefined || val === null) return null;
    if (typeof val !== 'number') {
        throw new Error(`${where}: '${key}' must be a number, got ${typeof val}`);
    }
    return val;
}

function optInt(d, key, defaultVal, where) {
    const val = d[key];
    if (val === undefined) return defaultVal;
    if (typeof val !== 'number' || !Number.isInteger(val)) {
        throw new Error(`${where}: '${key}' must be an integer`);
    }
    return val;
}

// ── Sub-validators ────────────────────────────────────────────────────────────

function validateIndicator(raw, idx) {
    const where = `indicators[${idx}]`;
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) {
        throw new Error(`${where}: must be an object`);
    }
    const id = requireStr(raw, 'id', where);
    if (!/^[a-zA-Z_][a-zA-Z0-9_]*$/.test(id)) {
        throw new Error(`${where}: 'id' must be a valid identifier, got '${id}'`);
    }
    const kind = requireStr(raw, 'kind', where);
    if (!INDICATOR_KINDS.has(kind)) {
        throw new Error(`${where}: unknown indicator kind '${kind}'; allowed: ${[...INDICATOR_KINDS].sort().join(', ')}`);
    }
    const paramsRaw = raw.params !== undefined ? raw.params : {};
    if (!paramsRaw || typeof paramsRaw !== 'object' || Array.isArray(paramsRaw)) {
        throw new Error(`${where}: 'params' must be an object`);
    }
    const params = {};
    for (const [k, v] of Object.entries(paramsRaw)) {
        if (typeof v !== 'number' && typeof v !== 'boolean') {
            throw new Error(`${where}.params.${k}: must be number or bool`);
        }
        params[k] = v;
    }
    return { id, kind, params };
}

function validateEntryRule(raw, idx) {
    const where = `entries[${idx}]`;
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) {
        throw new Error(`${where}: must be an object`);
    }
    const side = requireStr(raw, 'side', where);
    if (!VALID_SIDES.has(side)) {
        throw new Error(`${where}: 'side' must be 'long' or 'short'`);
    }
    const when = requireStr(raw, 'when', where);
    return { side, when };
}

function validateExitRule(raw, idx) {
    const where = `exits[${idx}]`;
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) {
        throw new Error(`${where}: must be an object`);
    }
    const side = requireStr(raw, 'side', where);
    if (!VALID_SIDES.has(side)) {
        throw new Error(`${where}: 'side' must be 'long' or 'short'`);
    }
    const when   = raw.when !== undefined ? raw.when : null;
    const slAtr  = optFloat(raw, 'sl_atr', where);
    const tpAtr  = optFloat(raw, 'tp_atr', where);

    if (when === null && slAtr === null && tpAtr === null) {
        throw new Error(`${where}: must have 'when', 'sl_atr', or 'tp_atr'`);
    }
    if (when !== null && typeof when !== 'string') {
        throw new Error(`${where}: 'when' must be a string`);
    }
    return { side, when, slAtr, tpAtr };
}

function validateRisk(raw) {
    const where = 'risk';
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) {
        throw new Error(`${where}: must be an object`);
    }
    const size = raw.size !== undefined ? raw.size : 'atr';
    if (!VALID_SIZE_METHODS.has(size)) {
        throw new Error(`${where}: 'size' must be 'atr' or 'fixed'`);
    }
    const atrMult       = typeof raw.atr_mult   === 'number' ? raw.atr_mult   : 1.5;
    const fixedLots     = typeof raw.fixed_lots  === 'number' ? raw.fixed_lots  : 0.01;
    const maxConcurrent = optInt(raw, 'max_concurrent', 1, where);
    const hedgeRaw      = raw.hedge !== undefined ? raw.hedge : false;
    if (typeof hedgeRaw !== 'boolean') {
        throw new Error(`${where}: 'hedge' must be boolean`);
    }
    const coolDownBars  = optInt(raw, 'cool_down_bars', 0, where);
    if (coolDownBars < 0) {
        throw new Error(`${where}: 'cool_down_bars' must be >= 0`);
    }
    return { size, atrMult, fixedLots, maxConcurrent, hedge: hedgeRaw, coolDownBars };
}

// ── Main validator ────────────────────────────────────────────────────────────

/**
 * Validate a raw manifest dict and return a parsed AlgoManifest object.
 * Throws Error with a descriptive message on any validation failure.
 *
 * @param {object} d  raw manifest dict
 * @returns {import('./types.js').AlgoManifest}
 */
export function validateManifest(d) {
    if (!d || typeof d !== 'object' || Array.isArray(d)) {
        throw new TypeError(`manifest must be an object, got ${typeof d}`);
    }

    // Schema version first
    const schemaVersion = requireStr(d, 'schema_version', 'manifest');
    if (!SUPPORTED_SCHEMA_VERSIONS.has(schemaVersion)) {
        throw new Error(
            `unsupported_schema_version: '${schemaVersion}' — supported: ${[...SUPPORTED_SCHEMA_VERSIONS].sort().join(', ')}`
        );
    }

    const name        = requireStr(d, 'name',        'manifest', 48);
    const description = requireStr(d, 'description', 'manifest', 280);
    const rationale   = requireStr(d, 'rationale',   'manifest', 1024);

    // Validate kebab-case name
    if (!/^[a-z][a-z0-9-]*$/.test(name)) {
        throw new Error(
            `manifest: 'name' must be kebab-case (lowercase letters, digits, hyphens), got '${name}'`
        );
    }

    // timeframes
    const tfRaw = d['timeframes'];
    if (tfRaw === undefined || tfRaw === null) {
        throw new Error("manifest: missing required field 'timeframes'");
    }
    if (!Array.isArray(tfRaw) || tfRaw.length === 0) {
        throw new Error("manifest: 'timeframes' must be a non-empty list");
    }
    for (const tf of tfRaw) {
        if (typeof tf !== 'string' || !VALID_TIMEFRAMES.has(tf)) {
            throw new Error(`manifest: unknown timeframe '${tf}'; allowed: ${[...VALID_TIMEFRAMES].sort().join(', ')}`);
        }
    }

    // symbols
    const symRaw = d['symbols'];
    if (symRaw === undefined || symRaw === null) {
        throw new Error("manifest: missing required field 'symbols'");
    }
    let symbols;
    if (typeof symRaw === 'string') {
        if (symRaw !== 'any') {
            throw new Error('manifest: \'symbols\' string must be "any"');
        }
        symbols = 'any';
    } else if (Array.isArray(symRaw)) {
        for (const s of symRaw) {
            if (typeof s !== 'string' || s.length === 0) {
                throw new Error("manifest: 'symbols' list elements must be non-empty strings");
            }
        }
        symbols = [...symRaw];
    } else {
        throw new Error('manifest: \'symbols\' must be a list or the string "any"');
    }

    // indicators
    const indsRaw = d['indicators'] !== undefined ? d['indicators'] : [];
    if (!Array.isArray(indsRaw)) {
        throw new Error("manifest: 'indicators' must be a list");
    }
    const indicators = indsRaw.map((r, i) => validateIndicator(r, i));

    // Check unique indicator IDs
    const idsSeen = new Set();
    for (const ind of indicators) {
        if (idsSeen.has(ind.id)) {
            throw new Error(`manifest: duplicate indicator id '${ind.id}'`);
        }
        idsSeen.add(ind.id);
    }

    // entries
    const entsRaw = d['entries'] !== undefined ? d['entries'] : [];
    if (!Array.isArray(entsRaw)) {
        throw new Error("manifest: 'entries' must be a list");
    }
    const entries = entsRaw.map((r, i) => validateEntryRule(r, i));

    // exits
    const extsRaw = d['exits'] !== undefined ? d['exits'] : [];
    if (!Array.isArray(extsRaw)) {
        throw new Error("manifest: 'exits' must be a list");
    }
    const exits = extsRaw.map((r, i) => validateExitRule(r, i));

    // risk
    const riskRaw = d['risk'] !== undefined ? d['risk'] : {};
    const risk    = validateRisk(riskRaw);

    // code (escape hatch)
    const codeRaw = d['code'];
    let code = null;
    if (codeRaw !== undefined && codeRaw !== null) {
        if (typeof codeRaw !== 'string') {
            throw new Error("manifest: 'code' must be a string or null");
        }
        code = codeRaw;
    }

    return {
        schemaVersion,
        name,
        description,
        rationale,
        timeframes: [...tfRaw],
        symbols,
        indicators,
        entries,
        exits,
        risk,
        code,
    };
}
