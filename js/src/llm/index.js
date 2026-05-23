/**
 * index.js — public API re-exports for the LLM submodule.
 */

export { LLMError } from './errors.js';
export { LLMProvider } from './provider.js';
export { OllamaProvider } from './ollama.js';
export { loadFixture, MockTransport } from './fixtures.js';
export {
    tradeRationale,
    backtestCommentary,
    newsSentiment,
    strategyDescribe,
    extractJson,
} from './useCases.js';
export {
    TRADE_RATIONALE_SYSTEM,
    TRADE_RATIONALE_USER,
    BACKTEST_COMMENTARY_SYSTEM,
    BACKTEST_COMMENTARY_USER,
    NEWS_SENTIMENT_SYSTEM,
    NEWS_SENTIMENT_USER,
    STRATEGY_DESCRIBE_SYSTEM,
    STRATEGY_DESCRIBE_USER,
} from './prompts.js';
