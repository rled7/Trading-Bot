/**
 * AlgoForge — src/llm/prompts.cpp
 * S2 LLM Layer — prompt string constants.
 *
 * Byte-identical to python/algoforge/llm/prompts.py.
 * The U+2013 en-dash characters (encoded as \xe2\x80\x93 in UTF-8)
 * are used in the range indicators (1–3, 3–5, 2–4).
 */

#include "llm_internal.hpp"

namespace algoforge::llm {

/* ── §7.1  trade_rationale ── */

const char* TRADE_RATIONALE_SYSTEM =
    "You are an experienced trading assistant. Given a fired signal, explain in 1\xe2\x80\x93" "3 "
    "plain sentences why this trade triggered. Be concrete; reference the indicator "
    "values and patterns. Do not give financial advice.";

const char* TRADE_RATIONALE_USER_TMPL =
    "SIGNAL\n"
    "symbol={symbol}\n"
    "side={side}\n"
    "timeframe={timeframe}\n"
    "entry={entry_price}\n"
    "sl={stop_loss}\n"
    "tp={take_profit}\n"
    "indicators={indicators_json_sorted}\n"
    "patterns={patterns_csv}\n"
    "\n"
    "Explain.";

/* ── §7.2  backtest_commentary ── */

const char* BACKTEST_COMMENTARY_SYSTEM =
    "You are an experienced quant analyst. Given a backtest summary, write a 3\xe2\x80\x93" "5 "
    "sentence narrative covering: overall result, risk-adjusted performance, drawdown "
    "behavior, and one concrete improvement suggestion. Plain prose, no bullet points.";

const char* BACKTEST_COMMENTARY_USER_TMPL =
    "BACKTEST\n"
    "strategy={strategy_name}\n"
    "period={period}\n"
    "total_return={total_return}\n"
    "sharpe={sharpe}\n"
    "sortino={sortino}\n"
    "max_drawdown={max_drawdown}\n"
    "win_rate={win_rate}\n"
    "trade_count={trade_count}\n"
    "best_trade={best_trade}\n"
    "worst_trade={worst_trade}\n"
    "\n"
    "Comment.";

/* ── §7.3  news_sentiment ── */

const char* NEWS_SENTIMENT_SYSTEM =
    "You are a financial news sentiment classifier. Given a headline (and optional "
    "body), respond with exactly one JSON object: "
    "{\"sentiment\":\"bullish|bearish|neutral\",\"score\":-1.0..1.0,\"confidence\":0.0..1.0,"
    "\"reason\":\"<one short sentence>\"}. No prose outside the JSON.";

const char* NEWS_SENTIMENT_USER_TMPL =
    "HEADLINE: {headline}\n"
    "BODY: {body_or_empty}";

/* ── §7.4  strategy_describe ── */

const char* STRATEGY_DESCRIBE_SYSTEM =
    "You are a trading strategy documenter. Given a structured strategy spec, write a "
    "2\xe2\x80\x93" "4 sentence plain-English description suitable for a strategy catalog. Do not "
    "invent rules.";

const char* STRATEGY_DESCRIBE_USER_TMPL =
    "STRATEGY\n"
    "name={name}\n"
    "timeframe={timeframe}\n"
    "indicators={indicators_csv}\n"
    "entry_rules={entry_rules_csv}\n"
    "exit_rules={exit_rules_csv}\n"
    "risk_rules={risk_rules_csv}\n"
    "\n"
    "Describe.";

} /* namespace algoforge::llm */
