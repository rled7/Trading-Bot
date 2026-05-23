/**
 * useCases.js — Four use-case functions.
 * All floats in prompts use x.toFixed(4) to match Python's f"{x:.4f}".
 * Indicator dicts are serialized with sorted keys, no spaces.
 */

import { LLMError } from './errors.js';
import {
    TRADE_RATIONALE_SYSTEM,
    TRADE_RATIONALE_USER,
    BACKTEST_COMMENTARY_SYSTEM,
    BACKTEST_COMMENTARY_USER,
    NEWS_SENTIMENT_SYSTEM,
    NEWS_SENTIMENT_USER,
    STRATEGY_DESCRIBE_SYSTEM,
    STRATEGY_DESCRIBE_USER,
} from './prompts.js';

const DEFAULT_MODEL = 'llama3.1:8b';
const DEFAULT_TEMPERATURE = 0.2;
const DEFAULT_MAX_TOKENS = 512;
const DEFAULT_SEED = 42;

// ── JSON extractor (spec §9) ──────────────────────────────────────────────────

/**
 * Extract the first balanced JSON object from a string.
 * Respects quoted strings and \" escapes.
 * @param {string} text
 * @returns {object}
 * @throws {LLMError} kind="decode" if no valid JSON object found
 */
export function extractJson(text) {
    let depth = 0;
    let inString = false;
    let escape = false;
    let start = null;

    for (let i = 0; i < text.length; i++) {
        const ch = text[i];
        if (escape) {
            escape = false;
            continue;
        }
        if (ch === '\\' && inString) {
            escape = true;
            continue;
        }
        if (ch === '"') {
            inString = !inString;
            continue;
        }
        if (inString) continue;
        if (ch === '{') {
            if (depth === 0) start = i;
            depth++;
        } else if (ch === '}') {
            depth--;
            if (depth === 0 && start !== null) {
                const candidate = text.slice(start, i + 1);
                try {
                    return JSON.parse(candidate);
                } catch {
                    // keep scanning
                }
                start = null;
            }
        }
    }
    throw new LLMError('decode', 'no valid JSON object found in response');
}

// ── Indicator serialization ───────────────────────────────────────────────────

/**
 * Serialize indicators dict with sorted keys, no spaces — matches Python's
 * json.dumps(d, sort_keys=True, separators=(',', ':'))
 * @param {Object.<string,number>} indicators
 * @returns {string}
 */
function serializeIndicators(indicators) {
    const sortedKeys = Object.keys(indicators).sort();
    return JSON.stringify(indicators, sortedKeys);
}

// ── Simple template substitution ─────────────────────────────────────────────

/**
 * @param {string} template
 * @param {Object.<string,string>} vars
 * @returns {string}
 */
function renderTemplate(template, vars) {
    return template.replace(/\{([^}]+)\}/g, (_, key) => {
        if (!(key in vars)) throw new Error(`Template key not found: ${key}`);
        return vars[key];
    });
}

// ── Use cases ─────────────────────────────────────────────────────────────────

/**
 * Generate a trade rationale explanation.
 * @param {import('./provider.js').LLMProvider} provider
 * @param {import('./types.js').TradeRationaleInput} inp
 * @param {object} [opts]
 * @param {string} [opts.model]
 * @param {number} [opts.temperature]
 * @param {number|null} [opts.maxTokens]
 * @param {number|null} [opts.seed]
 * @returns {Promise<import('./types.js').TradeRationaleOutput>}
 */
export async function tradeRationale(provider, inp, opts = {}) {
    const model = opts.model ?? DEFAULT_MODEL;
    const temperature = opts.temperature ?? DEFAULT_TEMPERATURE;
    const maxTokens = opts.maxTokens !== undefined ? opts.maxTokens : DEFAULT_MAX_TOKENS;
    const seed = opts.seed !== undefined ? opts.seed : DEFAULT_SEED;

    const indicatorsJson = serializeIndicators(inp.indicators);
    const patternsCsv = inp.patterns.join(',');

    const userContent = renderTemplate(TRADE_RATIONALE_USER, {
        symbol: inp.symbol,
        side: inp.side,
        timeframe: inp.timeframe,
        entry_price: inp.entryPrice.toFixed(4),
        stop_loss: inp.stopLoss.toFixed(4),
        take_profit: inp.takeProfit.toFixed(4),
        indicators_json_sorted: indicatorsJson,
        patterns_csv: patternsCsv,
    });

    const req = {
        model,
        messages: [
            { role: 'system', content: TRADE_RATIONALE_SYSTEM },
            { role: 'user', content: userContent },
        ],
        temperature,
        maxTokens,
        seed,
    };

    const resp = await provider.chat(req);
    return {
        explanation: resp.message.content,
        model: resp.model,
        tokens: resp.completionTokens,
    };
}

/**
 * Generate a backtest commentary narrative.
 * @param {import('./provider.js').LLMProvider} provider
 * @param {import('./types.js').BacktestCommentaryInput} inp
 * @param {object} [opts]
 * @param {string} [opts.model]
 * @param {number} [opts.temperature]
 * @param {number|null} [opts.maxTokens]
 * @param {number|null} [opts.seed]
 * @returns {Promise<import('./types.js').BacktestCommentaryOutput>}
 */
export async function backtestCommentary(provider, inp, opts = {}) {
    const model = opts.model ?? DEFAULT_MODEL;
    const temperature = opts.temperature ?? DEFAULT_TEMPERATURE;
    const maxTokens = opts.maxTokens !== undefined ? opts.maxTokens : DEFAULT_MAX_TOKENS;
    const seed = opts.seed !== undefined ? opts.seed : DEFAULT_SEED;

    const userContent = renderTemplate(BACKTEST_COMMENTARY_USER, {
        strategy_name: inp.strategyName,
        period: inp.period,
        total_return: inp.totalReturn.toFixed(4),
        sharpe: inp.sharpe.toFixed(4),
        sortino: inp.sortino.toFixed(4),
        max_drawdown: inp.maxDrawdown.toFixed(4),
        win_rate: inp.winRate.toFixed(4),
        trade_count: String(inp.tradeCount),
        best_trade: inp.bestTrade.toFixed(4),
        worst_trade: inp.worstTrade.toFixed(4),
    });

    const req = {
        model,
        messages: [
            { role: 'system', content: BACKTEST_COMMENTARY_SYSTEM },
            { role: 'user', content: userContent },
        ],
        temperature,
        maxTokens,
        seed,
    };

    const resp = await provider.chat(req);
    return {
        commentary: resp.message.content,
        model: resp.model,
        tokens: resp.completionTokens,
    };
}

/**
 * Classify financial news sentiment.
 * @param {import('./provider.js').LLMProvider} provider
 * @param {string} headline
 * @param {object} [opts]
 * @param {string} [opts.body]
 * @param {string} [opts.model]
 * @param {number} [opts.temperature]
 * @param {number|null} [opts.maxTokens]
 * @param {number|null} [opts.seed]
 * @returns {Promise<import('./types.js').NewsSentimentOutput>}
 */
export async function newsSentiment(provider, headline, opts = {}) {
    const model = opts.model ?? DEFAULT_MODEL;
    const temperature = opts.temperature ?? DEFAULT_TEMPERATURE;
    const maxTokens = opts.maxTokens !== undefined ? opts.maxTokens : DEFAULT_MAX_TOKENS;
    const seed = opts.seed !== undefined ? opts.seed : DEFAULT_SEED;
    const body = opts.body ?? '';

    const userContent = renderTemplate(NEWS_SENTIMENT_USER, {
        headline,
        body_or_empty: body,
    });

    const req = {
        model,
        messages: [
            { role: 'system', content: NEWS_SENTIMENT_SYSTEM },
            { role: 'user', content: userContent },
        ],
        temperature,
        maxTokens,
        seed,
    };

    const resp = await provider.chat(req);
    const data = extractJson(resp.message.content);
    return {
        sentiment: data.sentiment,
        score: Number(data.score),
        confidence: Number(data.confidence),
        reason: data.reason,
    };
}

/**
 * Generate a plain-English strategy description.
 * @param {import('./provider.js').LLMProvider} provider
 * @param {import('./types.js').StrategyDescribeInput} inp
 * @param {object} [opts]
 * @param {string} [opts.model]
 * @param {number} [opts.temperature]
 * @param {number|null} [opts.maxTokens]
 * @param {number|null} [opts.seed]
 * @returns {Promise<import('./types.js').StrategyDescribeOutput>}
 */
export async function strategyDescribe(provider, inp, opts = {}) {
    const model = opts.model ?? DEFAULT_MODEL;
    const temperature = opts.temperature ?? DEFAULT_TEMPERATURE;
    const maxTokens = opts.maxTokens !== undefined ? opts.maxTokens : DEFAULT_MAX_TOKENS;
    const seed = opts.seed !== undefined ? opts.seed : DEFAULT_SEED;

    const userContent = renderTemplate(STRATEGY_DESCRIBE_USER, {
        name: inp.name,
        timeframe: inp.timeframe,
        indicators_csv: inp.indicatorsUsed.join(','),
        entry_rules_csv: inp.entryRules.join(','),
        exit_rules_csv: inp.exitRules.join(','),
        risk_rules_csv: inp.riskRules.join(','),
    });

    const req = {
        model,
        messages: [
            { role: 'system', content: STRATEGY_DESCRIBE_SYSTEM },
            { role: 'user', content: userContent },
        ],
        temperature,
        maxTokens,
        seed,
    };

    const resp = await provider.chat(req);
    return {
        description: resp.message.content,
        model: resp.model,
        tokens: resp.completionTokens,
    };
}
