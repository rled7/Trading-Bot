/**
 * AlgoForge — include/core/llm.hpp
 * S2 LLM Layer — C++20 header for namespace algoforge::llm.
 *
 * All types, interfaces, and free-function declarations.
 * Implementations live in cpp/src/llm/.
 *
 * Design rules:
 *   - Namespace algoforge::llm
 *   - snake_case fields, idiomatic C++20
 *   - No external JSON dep (hand-rolled in ollama.cpp / json.cpp)
 *   - libcurl optional: if AF_HAS_LIBCURL not defined, transport returns error
 *   - All resources RAII; no raw owning pointers
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace algoforge::llm {

/* =========================================================================
 * §3  Error model
 * ========================================================================= */

enum class LLMErrorKind {
    unreachable,
    timeout,
    http,
    decode,
    model_missing
};

class LLMError : public std::runtime_error {
public:
    LLMErrorKind kind;
    int          status;  /* HTTP status if applicable, 0 otherwise */

    LLMError(LLMErrorKind k, const std::string& msg, int s = 0)
        : std::runtime_error(msg), kind(k), status(s) {}
};

/* =========================================================================
 * §3  Transport abstraction
 * ========================================================================= */

struct TransportResult {
    int         status  = 0;
    std::string body;
    bool        ok      = false;   /* true when status 2xx */
};

/**
 * ITransport — abstract HTTP client.
 * OllamaProvider holds an ITransport (injected in constructor).
 * MockTransport replaces it in tests.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    /**
     * Perform an HTTP request.
     * @param method  "GET" or "POST"
     * @param path    e.g. "/api/generate"
     * @param body    JSON body string (may be empty for GET)
     */
    virtual TransportResult request(const std::string& method,
                                    const std::string& path,
                                    const std::string& body) = 0;
};

/* =========================================================================
 * §3  Core types
 * ========================================================================= */

struct CompletionRequest {
    std::string              model;
    std::string              prompt;
    float                    temperature  = 0.2f;
    std::optional<int>       max_tokens;       /* maps to num_predict */
    std::vector<std::string> stop;
    std::optional<int>       seed;
    float                    timeout_s    = 60.0f;
};

struct CompletionResponse {
    std::string text;
    std::string model;
    int         prompt_tokens    = 0;
    int         completion_tokens= 0;
    long long   total_duration_ns= 0;
    std::string finish_reason;   /* "stop" | "length" | "error" */
};

struct ChatMessage {
    std::string role;    /* "system" | "user" | "assistant" */
    std::string content;
};

struct ChatRequest {
    std::string              model;
    std::vector<ChatMessage> messages;
    float                    temperature  = 0.2f;
    std::optional<int>       max_tokens;
    std::optional<int>       seed;
    float                    timeout_s    = 60.0f;
};

struct ChatResponse {
    ChatMessage message;
    std::string model;
    int         prompt_tokens    = 0;
    int         completion_tokens= 0;
    long long   total_duration_ns= 0;
    std::string finish_reason;
};

struct ChatChunk {
    std::string              delta;         /* incremental text */
    bool                     done  = false;
    std::optional<std::string> finish_reason;
};

struct EmbedRequest {
    std::string              model;
    std::vector<std::string> input;   /* one or more strings */
};

struct EmbedResponse {
    std::vector<std::vector<float>> embeddings;
    std::string                     model;
};

struct HealthStatus {
    bool        ok           = false;
    bool        model_loaded = false;
    std::optional<std::string> model;
    std::optional<std::string> error;
};

struct ModelInfo {
    std::string name;
    long long   size_bytes  = 0;
    std::string modified_at;  /* ISO-8601 string */
};

/* =========================================================================
 * §3  Provider interface
 * ========================================================================= */

/**
 * LLMProvider — abstract interface that all provider implementations satisfy.
 */
class LLMProvider {
public:
    virtual ~LLMProvider() = default;

    virtual HealthStatus              health()                       = 0;
    virtual std::vector<ModelInfo>    list_models()                  = 0;
    virtual CompletionResponse        complete(const CompletionRequest& req) = 0;
    virtual ChatResponse              chat(const ChatRequest& req)   = 0;
    virtual std::vector<ChatChunk>    chat_stream(const ChatRequest& req) = 0;
    virtual EmbedResponse             embed(const EmbedRequest& req) = 0;
};

/* =========================================================================
 * §3  OllamaProvider — concrete implementation
 * ========================================================================= */

/**
 * OllamaProvider — talks to a running Ollama server over HTTP.
 *
 * Inject a custom ITransport for testing.
 * Reads AF_LLM_HOST / AF_LLM_MODEL / etc. from env when constructed
 * via from_env().
 */
class OllamaProvider : public LLMProvider {
public:
    /**
     * Construct with explicit base URL and default model.
     * Ownership of transport is shared (nullptr → build libcurl transport).
     */
    explicit OllamaProvider(
        std::string             base_url       = "http://localhost:11434",
        std::string             default_model  = "llama3.1:8b",
        std::string             embed_model    = "nomic-embed-text",
        std::shared_ptr<ITransport> transport  = nullptr);

    /**
     * Construct from environment variables (AF_LLM_HOST, AF_LLM_MODEL, …).
     */
    static OllamaProvider from_env();

    /* ── LLMProvider implementation ── */
    HealthStatus           health()                              override;
    std::vector<ModelInfo> list_models()                        override;
    CompletionResponse     complete(const CompletionRequest&)   override;
    ChatResponse           chat(const ChatRequest&)             override;
    std::vector<ChatChunk> chat_stream(const ChatRequest&)      override;
    EmbedResponse          embed(const EmbedRequest&)           override;

private:
    std::string                  base_url_;
    std::string                  default_model_;
    std::string                  embed_model_;
    std::shared_ptr<ITransport>  transport_;

    /* Helpers */
    TransportResult do_request(const std::string& method,
                               const std::string& path,
                               const std::string& body = "");
};

/* =========================================================================
 * §7  Use-case input/output types
 * ========================================================================= */

struct TradeRationaleInput {
    std::string                      symbol;
    std::string                      side;         /* "long" | "short" */
    double                           entry_price;
    double                           stop_loss;
    double                           take_profit;
    std::vector<std::pair<std::string,double>> indicators; /* sorted by key */
    std::vector<std::string>         patterns;
    std::string                      timeframe;
};

struct TradeRationaleOutput {
    std::string explanation;
    std::string model;
    int         tokens = 0;
};

struct BacktestCommentaryInput {
    std::string strategy_name;
    std::string period;
    double      total_return;
    double      sharpe;
    double      sortino;
    double      max_drawdown;
    double      win_rate;
    int         trade_count;
    double      best_trade;
    double      worst_trade;
};

struct BacktestCommentaryOutput {
    std::string commentary;
    std::string model;
    int         tokens = 0;
};

struct NewsSentimentOutput {
    std::string sentiment;    /* "bullish" | "bearish" | "neutral" */
    double      score      = 0.0;  /* -1.0 .. 1.0 */
    double      confidence = 0.0;  /* 0.0 .. 1.0 */
    std::string reason;
};

struct StrategyDescribeInput {
    std::string              name;
    std::vector<std::string> indicators_used;
    std::vector<std::string> entry_rules;
    std::vector<std::string> exit_rules;
    std::vector<std::string> risk_rules;
    std::string              timeframe;
};

struct StrategyDescribeOutput {
    std::string description;
    std::string model;
    int         tokens = 0;
};

/* =========================================================================
 * §7  Use-case free functions
 * ========================================================================= */

TradeRationaleOutput trade_rationale(
    LLMProvider&              provider,
    const TradeRationaleInput& inp,
    const std::string&        model       = "llama3.1:8b",
    float                     temperature = 0.2f,
    int                       max_tokens  = 512,
    int                       seed        = 42);

BacktestCommentaryOutput backtest_commentary(
    LLMProvider&                   provider,
    const BacktestCommentaryInput& inp,
    const std::string&             model       = "llama3.1:8b",
    float                          temperature = 0.2f,
    int                            max_tokens  = 512,
    int                            seed        = 42);

NewsSentimentOutput news_sentiment(
    LLMProvider&       provider,
    const std::string& headline,
    const std::string& body        = "",
    const std::string& model       = "llama3.1:8b",
    float              temperature = 0.2f,
    int                max_tokens  = 512,
    int                seed        = 42);

StrategyDescribeOutput strategy_describe(
    LLMProvider&                  provider,
    const StrategyDescribeInput&  inp,
    const std::string&            model       = "llama3.1:8b",
    float                         temperature = 0.2f,
    int                           max_tokens  = 512,
    int                           seed        = 42);

} /* namespace algoforge::llm */
