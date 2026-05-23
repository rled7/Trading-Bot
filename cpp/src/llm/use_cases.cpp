/**
 * AlgoForge — src/llm/use_cases.cpp
 * S2 LLM Layer — four use-case free functions.
 *
 * Each function:
 *   1. Builds a prompt from the canonical template in prompts.cpp
 *   2. Calls provider.chat(...)
 *   3. Parses / returns the structured output
 *
 * Float formatting uses snprintf with "%.4f" (locale-independent, §9).
 * Indicator keys are sorted alphabetically (§9).
 */

#include "llm_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace algoforge::llm {

/* ── Helper: replace first occurrence of placeholder in template ── */
static void replace_placeholder(std::string& s,
                                 const char* placeholder,
                                 const std::string& value) {
    std::string ph(placeholder);
    auto pos = s.find(ph);
    if (pos != std::string::npos) {
        s.replace(pos, ph.size(), value);
    }
}

/* ── Helper: format double as %.4f ── */
static std::string fmt4(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

/* ── Helper: join a list with commas ── */
static std::string csv(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ',';
        out += v[i];
    }
    return out;
}

/* =========================================================================
 * §7.1  trade_rationale
 * ========================================================================= */

TradeRationaleOutput trade_rationale(
    LLMProvider&               provider,
    const TradeRationaleInput& inp,
    const std::string&         model,
    float                      temperature,
    int                        max_tokens,
    int                        seed)
{
    /* Build indicators JSON: sort by key, use compact format */
    std::string indicators_json = json::emit_indicators(inp.indicators);

    /* Build patterns CSV in input order */
    std::string patterns_csv_str = csv(inp.patterns);

    /* Expand user template */
    std::string user_content(TRADE_RATIONALE_USER_TMPL);
    replace_placeholder(user_content, "{symbol}",              inp.symbol);
    replace_placeholder(user_content, "{side}",                inp.side);
    replace_placeholder(user_content, "{timeframe}",           inp.timeframe);
    replace_placeholder(user_content, "{entry_price}",         fmt4(inp.entry_price));
    replace_placeholder(user_content, "{stop_loss}",           fmt4(inp.stop_loss));
    replace_placeholder(user_content, "{take_profit}",         fmt4(inp.take_profit));
    replace_placeholder(user_content, "{indicators_json_sorted}", indicators_json);
    replace_placeholder(user_content, "{patterns_csv}",        patterns_csv_str);

    ChatRequest req;
    req.model       = model;
    req.temperature = temperature;
    req.max_tokens  = max_tokens;
    req.seed        = seed;
    req.messages    = {
        {TRADE_RATIONALE_SYSTEM, ""},  /* role/content order — fix below */
    };
    /* Build messages properly */
    req.messages.clear();
    req.messages.push_back({"system", TRADE_RATIONALE_SYSTEM});
    req.messages.push_back({"user",   user_content});

    ChatResponse resp = provider.chat(req);

    TradeRationaleOutput out;
    out.explanation = resp.message.content;
    out.model       = resp.model;
    out.tokens      = resp.completion_tokens;
    return out;
}

/* =========================================================================
 * §7.2  backtest_commentary
 * ========================================================================= */

BacktestCommentaryOutput backtest_commentary(
    LLMProvider&                    provider,
    const BacktestCommentaryInput&  inp,
    const std::string&              model,
    float                           temperature,
    int                             max_tokens,
    int                             seed)
{
    std::string user_content(BACKTEST_COMMENTARY_USER_TMPL);
    replace_placeholder(user_content, "{strategy_name}", inp.strategy_name);
    replace_placeholder(user_content, "{period}",        inp.period);
    replace_placeholder(user_content, "{total_return}",  fmt4(inp.total_return));
    replace_placeholder(user_content, "{sharpe}",        fmt4(inp.sharpe));
    replace_placeholder(user_content, "{sortino}",       fmt4(inp.sortino));
    replace_placeholder(user_content, "{max_drawdown}",  fmt4(inp.max_drawdown));
    replace_placeholder(user_content, "{win_rate}",      fmt4(inp.win_rate));
    replace_placeholder(user_content, "{trade_count}",   std::to_string(inp.trade_count));
    replace_placeholder(user_content, "{best_trade}",    fmt4(inp.best_trade));
    replace_placeholder(user_content, "{worst_trade}",   fmt4(inp.worst_trade));

    ChatRequest req;
    req.model       = model;
    req.temperature = temperature;
    req.max_tokens  = max_tokens;
    req.seed        = seed;
    req.messages    = {
        {"system", BACKTEST_COMMENTARY_SYSTEM},
        {"user",   user_content},
    };

    ChatResponse resp = provider.chat(req);

    BacktestCommentaryOutput out;
    out.commentary = resp.message.content;
    out.model      = resp.model;
    out.tokens     = resp.completion_tokens;
    return out;
}

/* =========================================================================
 * §7.3  news_sentiment
 * ========================================================================= */

NewsSentimentOutput news_sentiment(
    LLMProvider&       provider,
    const std::string& headline,
    const std::string& body,
    const std::string& model,
    float              temperature,
    int                max_tokens,
    int                seed)
{
    std::string user_content(NEWS_SENTIMENT_USER_TMPL);
    replace_placeholder(user_content, "{headline}",     headline);
    replace_placeholder(user_content, "{body_or_empty}", body);

    ChatRequest req;
    req.model       = model;
    req.temperature = temperature;
    req.max_tokens  = max_tokens;
    req.seed        = seed;
    req.messages    = {
        {"system", NEWS_SENTIMENT_SYSTEM},
        {"user",   user_content},
    };

    ChatResponse resp = provider.chat(req);

    /* Extract JSON from response text (§9 balanced-brace scanner) */
    std::string raw;
    try {
        raw = json::extract_first_json_object(resp.message.content);
    } catch (const LLMError&) {
        throw;
    }

    json::JsonValue jv;
    try {
        jv = json::parse(raw);
    } catch (const std::exception& e) {
        throw LLMError(LLMErrorKind::decode, std::string("news_sentiment parse: ") + e.what());
    }

    if (jv.type != json::JsonType::Object) {
        throw LLMError(LLMErrorKind::decode, "news_sentiment: response is not a JSON object");
    }

    auto get_str = [&](const char* k) -> std::string {
        if (!jv.has(k)) throw LLMError(LLMErrorKind::decode,
                                        std::string("news_sentiment: missing key: ") + k);
        return jv.get(k).as_str();
    };
    auto get_dbl = [&](const char* k) -> double {
        if (!jv.has(k)) throw LLMError(LLMErrorKind::decode,
                                        std::string("news_sentiment: missing key: ") + k);
        const auto& v = jv.get(k);
        if (v.type == json::JsonType::Number) return v.as_double();
        /* some models quote the number */
        try { return std::stod(v.as_str()); }
        catch (...) {
            throw LLMError(LLMErrorKind::decode,
                           std::string("news_sentiment: non-numeric value for key: ") + k);
        }
    };

    NewsSentimentOutput out;
    out.sentiment  = get_str("sentiment");
    out.score      = get_dbl("score");
    out.confidence = get_dbl("confidence");
    out.reason     = get_str("reason");
    return out;
}

/* =========================================================================
 * §7.4  strategy_describe
 * ========================================================================= */

StrategyDescribeOutput strategy_describe(
    LLMProvider&                   provider,
    const StrategyDescribeInput&   inp,
    const std::string&             model,
    float                          temperature,
    int                            max_tokens,
    int                            seed)
{
    std::string user_content(STRATEGY_DESCRIBE_USER_TMPL);
    replace_placeholder(user_content, "{name}",            inp.name);
    replace_placeholder(user_content, "{timeframe}",       inp.timeframe);
    replace_placeholder(user_content, "{indicators_csv}",  csv(inp.indicators_used));
    replace_placeholder(user_content, "{entry_rules_csv}", csv(inp.entry_rules));
    replace_placeholder(user_content, "{exit_rules_csv}",  csv(inp.exit_rules));
    replace_placeholder(user_content, "{risk_rules_csv}",  csv(inp.risk_rules));

    ChatRequest req;
    req.model       = model;
    req.temperature = temperature;
    req.max_tokens  = max_tokens;
    req.seed        = seed;
    req.messages    = {
        {"system", STRATEGY_DESCRIBE_SYSTEM},
        {"user",   user_content},
    };

    ChatResponse resp = provider.chat(req);

    StrategyDescribeOutput out;
    out.description = resp.message.content;
    out.model       = resp.model;
    out.tokens      = resp.completion_tokens;
    return out;
}

} /* namespace algoforge::llm */
