/**
 * types.js — JSDoc typedefs for every dataclass from spec §3 + §7.
 * No runtime classes; plain object shapes. Factory helpers provided.
 */

/**
 * @typedef {Object} CompletionRequest
 * @property {string} model
 * @property {string} prompt
 * @property {number} [temperature=0.2]
 * @property {number|null} [maxTokens=null]
 * @property {string[]} [stop=[]]
 * @property {number|null} [seed=null]
 * @property {number} [timeoutS=60.0]
 */

/**
 * @typedef {Object} CompletionResponse
 * @property {string} text
 * @property {string} model
 * @property {number} promptTokens
 * @property {number} completionTokens
 * @property {number} totalDurationNs
 * @property {string} finishReason
 */

/**
 * @typedef {Object} ChatMessage
 * @property {string} role  "system" | "user" | "assistant"
 * @property {string} content
 */

/**
 * @typedef {Object} ChatRequest
 * @property {string} model
 * @property {ChatMessage[]} messages
 * @property {number} [temperature=0.2]
 * @property {number|null} [maxTokens=null]
 * @property {number|null} [seed=null]
 * @property {number} [timeoutS=60.0]
 */

/**
 * @typedef {Object} ChatResponse
 * @property {ChatMessage} message
 * @property {string} model
 * @property {number} promptTokens
 * @property {number} completionTokens
 * @property {number} totalDurationNs
 * @property {string} finishReason
 */

/**
 * @typedef {Object} ChatChunk
 * @property {string} delta
 * @property {boolean} done
 * @property {string|null} finishReason
 */

/**
 * @typedef {Object} EmbedRequest
 * @property {string} model
 * @property {string|string[]} input
 */

/**
 * @typedef {Object} EmbedResponse
 * @property {number[][]} embeddings
 * @property {string} model
 */

/**
 * @typedef {Object} HealthStatus
 * @property {boolean} ok
 * @property {boolean} modelLoaded
 * @property {string|null} model
 * @property {string|null} error
 */

/**
 * @typedef {Object} ModelInfo
 * @property {string} name
 * @property {number} sizeBytes
 * @property {string} modifiedAt
 */

/**
 * @typedef {Object} TradeRationaleInput
 * @property {string} symbol
 * @property {string} side  "long" | "short"
 * @property {number} entryPrice
 * @property {number} stopLoss
 * @property {number} takeProfit
 * @property {Object.<string,number>} indicators
 * @property {string[]} patterns
 * @property {string} timeframe
 */

/**
 * @typedef {Object} TradeRationaleOutput
 * @property {string} explanation
 * @property {string} model
 * @property {number} tokens
 */

/**
 * @typedef {Object} BacktestCommentaryInput
 * @property {string} strategyName
 * @property {string} period
 * @property {number} totalReturn
 * @property {number} sharpe
 * @property {number} sortino
 * @property {number} maxDrawdown
 * @property {number} winRate
 * @property {number} tradeCount
 * @property {number} bestTrade
 * @property {number} worstTrade
 */

/**
 * @typedef {Object} BacktestCommentaryOutput
 * @property {string} commentary
 * @property {string} model
 * @property {number} tokens
 */

/**
 * @typedef {Object} NewsSentimentOutput
 * @property {string} sentiment  "bullish" | "bearish" | "neutral"
 * @property {number} score
 * @property {number} confidence
 * @property {string} reason
 */

/**
 * @typedef {Object} StrategyDescribeInput
 * @property {string} name
 * @property {string[]} indicatorsUsed
 * @property {string[]} entryRules
 * @property {string[]} exitRules
 * @property {string[]} riskRules
 * @property {string} timeframe
 */

/**
 * @typedef {Object} StrategyDescribeOutput
 * @property {string} description
 * @property {string} model
 * @property {number} tokens
 */

// ── Factory helpers ───────────────────────────────────────────────────────────

/**
 * @param {string} model
 * @param {string} prompt
 * @param {Partial<CompletionRequest>} [opts]
 * @returns {CompletionRequest}
 */
export function makeCompletionRequest(model, prompt, opts = {}) {
    return {
        model,
        prompt,
        temperature: opts.temperature ?? 0.2,
        maxTokens: opts.maxTokens ?? null,
        stop: opts.stop ?? [],
        seed: opts.seed ?? null,
        timeoutS: opts.timeoutS ?? 60.0,
    };
}

/**
 * @param {string} model
 * @param {ChatMessage[]} messages
 * @param {Partial<ChatRequest>} [opts]
 * @returns {ChatRequest}
 */
export function makeChatRequest(model, messages, opts = {}) {
    return {
        model,
        messages,
        temperature: opts.temperature ?? 0.2,
        maxTokens: opts.maxTokens ?? null,
        seed: opts.seed ?? null,
        timeoutS: opts.timeoutS ?? 60.0,
    };
}

/**
 * @param {string} role
 * @param {string} content
 * @returns {ChatMessage}
 */
export function makeMessage(role, content) {
    return { role, content };
}
