/**
 * AlgoForge — include/dashboard/handlers.hpp
 *
 * Phase 5 (dashboard) — PURE request handlers: (inputs) -> JSON string.
 * Python oracle: python/algoforge/dashboard/server.py route bodies.
 *
 * The handlers hold all the logic and are the unit of parity test (mirrors the
 * broker MockTransport / S6 injected-deps discipline). The httplib socket layer in
 * server.cpp is a thin, untested-by-design binding that only calls these.
 */
#pragma once

#include <string>
#include <vector>

#include "dashboard/log_buffer.hpp"
#include "core/types.h"     // AF_AccountInfo, AF_Position, AF_Order, AF_Bar, AF_Timeframe
#include "core/llm.hpp"     // HealthStatus, ModelInfo, ChatResponse, LLMErrorKind
#include "core/algo_gen.hpp" // BacktestSummary (for /api/algos serialisers)

namespace algoforge::dashboard {

/** RFC-8259 string escaping for embedding in JSON. */
std::string json_escape(const std::string& s);

/** GET /api/health → {broker_name,is_connected,version,uptime_seconds}. */
std::string health_json(const std::string& broker_name,
                        bool is_connected,
                        const std::string& version,
                        double uptime_seconds);

/** GET /api/symbols → ["EURUSD", ...]. */
std::string symbols_json(const std::vector<std::string>& symbols);

/** GET /api/logs → [{ts,level,component,msg}, ...]. */
std::string logs_json(const std::vector<LogRecord>& records);

/** Server clamp for the ?n= query of /api/logs: max(1, min(n, 500)). */
int clamp_log_count(int n);

// ── Slice 2: broker-backed routes (Python oracle: server.py account/positions/orders/bars) ──
// Serialise the C++ broker's AF_* structs. side/type are coerced to ints to mirror the Python
// _position_dict/_order_dict convention. (Field sets follow the C++ broker types, which a C++
// dashboard serves; these differ slightly from the Python dataclasses by design.)

/** GET /api/account → {balance,equity,margin,free_margin,profit,leverage,currency,login}. */
std::string account_json(const AF_AccountInfo& a);

/** One position object with side as int. */
std::string position_json(const AF_Position& p);
/** GET /api/positions → [position, ...]. */
std::string positions_json(const std::vector<AF_Position>& ps);

/** One order object with type as int. */
std::string order_json(const AF_Order& o);
/** GET /api/orders → [order, ...]. */
std::string orders_json(const std::vector<AF_Order>& os);

/** One bar object → {timestamp,open,high,low,close,volume,spread} (Python Bar fields). */
std::string bar_json(const AF_Bar& b);
/** GET /api/bars/{symbol} → [bar, ...]. */
std::string bars_json(const std::vector<AF_Bar>& bs);

/** Parse a timeframe name ("M5", "H1", ...) → AF_Timeframe. Returns false if unknown. */
bool parse_timeframe(const std::string& name, AF_Timeframe& out);
/** Valid timeframe names, in enum order (for the 422 error detail). */
std::vector<std::string> timeframe_names();
/** Server clamp for /api/bars ?count=: max(1, min(count, 5000)). */
int clamp_bar_count(int count);

// ── Slice 4: tick stream (Python oracle: server.py stream_ticks) ──

/** One SSE tick payload → {"symbol","bid","ask","time"} (Python json.dumps field set). */
std::string tick_json(const AF_Tick& t);
/** Server clamp for /api/stream/ticks ?interval=: max(0.1, min(interval, 10.0)). */
double clamp_stream_interval(double interval);
/**
 * Resolve the ?symbols= CSV into a symbol list (mirrors server.py stream_ticks):
 * comma-split, trimmed, empties dropped; if the result is empty, fall back to defaults.
 */
std::vector<std::string> parse_stream_symbols(const std::string& csv,
                                              const std::vector<std::string>& defaults);

// ── Slice 3: llm routes (Python oracle: server.py llm_health/models/chat[/stream]) ──

/** HTTP status for an LLM error kind (Python _LLM_ERROR_STATUS; default 500). */
int llm_error_status(algoforge::llm::LLMErrorKind kind);
/** Stable string name of an LLM error kind (for the {"error": <kind>} field). */
std::string llm_error_kind_name(algoforge::llm::LLMErrorKind kind);
/** Generic error body → {"error": <kind>, "detail": <detail>}. */
std::string llm_error_json(const std::string& kind, const std::string& detail);

/** GET /api/llm/health success body → {status,host,model,model_loaded}. host="" → null. */
std::string llm_health_ok_json(const algoforge::llm::HealthStatus& s, const std::string& host);
/** GET /api/llm/models → {"models":[name, ...]}. */
std::string llm_models_json(const std::vector<algoforge::llm::ModelInfo>& models);
/** POST /api/llm/chat → {content,model,tokens}. */
std::string llm_chat_response_json(const algoforge::llm::ChatResponse& resp);

/** Format one SSE event line: "data: <payload>\n\n". */
std::string sse_data_line(const std::string& payload);

/**
 * Parse a POST /api/llm/chat[/stream] request body into a ChatRequest.
 * Mirrors server.py validation: empty/absent messages → false ("messages must not be
 * empty"); a message missing role/content → false ("missing key '<k>'"). Defaults:
 * model "llama3.1:8b", temperature 0.2. Returns false + error_detail on a 400 case.
 */
bool parse_chat_request(const std::string& body,
                        algoforge::llm::ChatRequest& out,
                        std::string& error_detail);

// ── Slice 5: algo_gen routes (Python oracle: dashboard/algo_gen_routes.py) ──

/** HTTP status for an AlgoGenError.kind (Python algo_gen route mapping; default 500). */
int algo_gen_error_status(const std::string& kind);
/** Error body → {"error": <kind>, "detail": <detail>}. */
std::string algo_gen_error_json(const std::string& kind, const std::string& detail);
/** Body returned by every /api/algos route when algo_gen is disabled. */
std::string algos_disabled_json();

/** One item of GET /api/algos → {"id","name","tier","status"}.
 *  Empty name/tier are emitted as JSON null (mirrors Python None). */
std::string algo_list_item_json(const std::string& id,
                                const std::string& name,
                                const std::string& tier,
                                const std::string& status);

/** POST /api/algos/{id}/backtest → {algo_id,symbol,equity_curve,metrics}. */
std::string backtest_summary_json(const std::string& algo_id,
                                  const std::string& symbol,
                                  const algoforge::algo_gen::BacktestSummary& bt);

/** POST /api/algos/generate → {id, manifest, tier_report}. The C++ port folds
 *  Python's separate {validation, tier} into the unified TierReport (documented
 *  divergence). manifest_json / tier_report_json are pre-serialised fragments. */
std::string generate_response_json(const std::string& id,
                                   const std::string& manifest_json,
                                   const std::string& tier_report_json);

/** Parsed POST /api/algos/generate body (Python GenerateBody). */
struct GenerateReq {
    std::string brief;
    std::string effort = "fast";   ///< "fast" | "balanced" | "max"
    int         seed   = 42;       ///< Python: None → 42 in the route
};
/** Parse a generate body. brief is required (→ false + detail); effort/seed default. */
bool parse_generate_request(const std::string& body, GenerateReq& out, std::string& error_detail);

/** Parsed POST /api/algos/{id}/backtest body (Python BacktestBody). */
struct BacktestReq {
    std::string symbol = "EURUSD";
    int         bars   = -1;       ///< Python: None → all bars (sentinel -1)
    int         seed   = 42;
};
/** Parse a backtest body. All fields optional; an empty body yields the defaults. */
bool parse_backtest_request(const std::string& body, BacktestReq& out, std::string& error_detail);

} // namespace algoforge::dashboard
