/**
 * AlgoForge — src/dashboard/handlers.cpp
 * Pure dashboard handlers. See header. Python oracle: dashboard/server.py.
 */
#include "dashboard/handlers.hpp"
#include "llm_internal.hpp"   // algoforge::llm::json::parse (JsonValue)

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace algoforge::dashboard {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string health_json(const std::string& broker_name,
                        bool is_connected,
                        const std::string& version,
                        double uptime_seconds) {
    std::ostringstream o;
    o << "{\"broker_name\":\"" << json_escape(broker_name) << "\","
      << "\"is_connected\":" << (is_connected ? "true" : "false") << ","
      << "\"version\":\"" << json_escape(version) << "\","
      << "\"uptime_seconds\":" << uptime_seconds << "}";
    return o.str();
}

std::string symbols_json(const std::vector<std::string>& symbols) {
    std::string out = "[";
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i) out += ",";
        out += "\"" + json_escape(symbols[i]) + "\"";
    }
    out += "]";
    return out;
}

std::string logs_json(const std::vector<LogRecord>& records) {
    std::string out = "[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i) out += ",";
        const auto& r = records[i];
        out += "{\"ts\":\"" + json_escape(r.ts) + "\","
               "\"level\":\"" + json_escape(r.level) + "\","
               "\"component\":\"" + json_escape(r.component) + "\","
               "\"msg\":\"" + json_escape(r.msg) + "\"}";
    }
    out += "]";
    return out;
}

int clamp_log_count(int n) { return std::max(1, std::min(n, 500)); }

// ── Slice 2: broker-backed serialisers ──

namespace {
// Compact double formatter (avoids scientific notation; trims to JSON-friendly form).
std::string num(double v) {
    std::ostringstream o; o << v; return o.str();
}
std::string cstr(const char* s) { return json_escape(s ? std::string(s) : std::string()); }
} // namespace

std::string account_json(const AF_AccountInfo& a) {
    std::ostringstream o;
    o << "{\"balance\":" << num(a.balance)
      << ",\"equity\":" << num(a.equity)
      << ",\"margin\":" << num(a.margin)
      << ",\"free_margin\":" << num(a.free_margin)
      << ",\"profit\":" << num(a.profit)
      << ",\"leverage\":" << a.leverage
      << ",\"currency\":\"" << cstr(a.currency) << "\""
      << ",\"login\":" << a.login << "}";
    return o.str();
}

std::string position_json(const AF_Position& p) {
    std::ostringstream o;
    o << "{\"ticket\":" << p.ticket
      << ",\"symbol\":\"" << cstr(p.symbol) << "\""
      << ",\"side\":" << static_cast<int>(p.side)
      << ",\"lots\":" << num(p.lots)
      << ",\"open_price\":" << num(p.open_price)
      << ",\"current_price\":" << num(p.current_price)
      << ",\"sl\":" << num(p.sl)
      << ",\"tp\":" << num(p.tp)
      << ",\"profit\":" << num(p.profit)
      << ",\"commission\":" << num(p.commission)
      << ",\"swap\":" << num(p.swap)
      << ",\"open_time\":" << p.open_time
      << ",\"magic\":" << p.magic
      << ",\"comment\":\"" << cstr(p.comment) << "\"}";
    return o.str();
}

std::string order_json(const AF_Order& o_) {
    std::ostringstream o;
    o << "{\"ticket\":" << o_.ticket
      << ",\"symbol\":\"" << cstr(o_.symbol) << "\""
      << ",\"type\":" << static_cast<int>(o_.type)
      << ",\"lots\":" << num(o_.lots)
      << ",\"price\":" << num(o_.price)
      << ",\"sl\":" << num(o_.sl)
      << ",\"tp\":" << num(o_.tp)
      << ",\"fill_price\":" << num(o_.fill_price)
      << ",\"open_time\":" << o_.open_time
      << ",\"fill_time\":" << o_.fill_time
      << ",\"magic\":" << o_.magic
      << ",\"comment\":\"" << cstr(o_.comment) << "\""
      << ",\"client_id\":\"" << cstr(o_.client_id) << "\"}";
    return o.str();
}

std::string bar_json(const AF_Bar& b) {
    std::ostringstream o;
    o << "{\"timestamp\":" << b.timestamp
      << ",\"open\":" << num(b.open)
      << ",\"high\":" << num(b.high)
      << ",\"low\":" << num(b.low)
      << ",\"close\":" << num(b.close)
      << ",\"volume\":" << num(b.volume)
      << ",\"spread\":" << num(b.spread) << "}";
    return o.str();
}

namespace {
template <class T, class F>
std::string json_array(const std::vector<T>& xs, F one) {
    std::string out = "[";
    for (size_t i = 0; i < xs.size(); ++i) { if (i) out += ","; out += one(xs[i]); }
    out += "]";
    return out;
}
} // namespace

std::string positions_json(const std::vector<AF_Position>& ps) { return json_array(ps, position_json); }
std::string orders_json(const std::vector<AF_Order>& os)       { return json_array(os, order_json); }
std::string bars_json(const std::vector<AF_Bar>& bs)           { return json_array(bs, bar_json); }

bool parse_timeframe(const std::string& name, AF_Timeframe& out) {
    static const std::vector<std::pair<std::string, AF_Timeframe>> tfs = {
        {"S15", AF_TF_S15}, {"M1", AF_TF_M1}, {"M5", AF_TF_M5}, {"M15", AF_TF_M15},
        {"M30", AF_TF_M30}, {"H1", AF_TF_H1}, {"H4", AF_TF_H4}, {"D1", AF_TF_D1}, {"W1", AF_TF_W1},
    };
    for (const auto& [n, v] : tfs) if (n == name) { out = v; return true; }
    return false;
}

std::vector<std::string> timeframe_names() {
    return {"S15", "M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1"};
}

int clamp_bar_count(int count) { return std::max(1, std::min(count, 5000)); }

// ── Slice 3: llm routes ──

int llm_error_status(algoforge::llm::LLMErrorKind kind) {
    using K = algoforge::llm::LLMErrorKind;
    switch (kind) {
        case K::timeout:       return 504;
        case K::unreachable:   return 502;
        case K::http:          return 502;
        case K::decode:        return 500;
        case K::model_missing: return 503;
    }
    return 500;
}

std::string llm_error_kind_name(algoforge::llm::LLMErrorKind kind) {
    using K = algoforge::llm::LLMErrorKind;
    switch (kind) {
        case K::unreachable:   return "unreachable";
        case K::timeout:       return "timeout";
        case K::http:          return "http";
        case K::decode:        return "decode";
        case K::model_missing: return "model_missing";
    }
    return "internal";
}

std::string llm_error_json(const std::string& kind, const std::string& detail) {
    return "{\"error\":\"" + json_escape(kind) + "\",\"detail\":\"" + json_escape(detail) + "\"}";
}

std::string llm_health_ok_json(const algoforge::llm::HealthStatus& s, const std::string& host) {
    std::ostringstream o;
    o << "{\"status\":\"ok\",";
    o << "\"host\":" << (host.empty() ? "null" : ("\"" + json_escape(host) + "\"")) << ",";
    o << "\"model\":" << (s.model ? ("\"" + json_escape(*s.model) + "\"") : "null") << ",";
    o << "\"model_loaded\":" << (s.model_loaded ? "true" : "false") << "}";
    return o.str();
}

std::string llm_models_json(const std::vector<algoforge::llm::ModelInfo>& models) {
    std::string out = "{\"models\":[";
    for (size_t i = 0; i < models.size(); ++i) {
        if (i) out += ",";
        out += "\"" + json_escape(models[i].name) + "\"";
    }
    out += "]}";
    return out;
}

std::string llm_chat_response_json(const algoforge::llm::ChatResponse& resp) {
    std::ostringstream o;
    o << "{\"content\":\"" << json_escape(resp.message.content) << "\","
      << "\"model\":\"" << json_escape(resp.model) << "\","
      << "\"tokens\":" << resp.completion_tokens << "}";
    return o.str();
}

std::string sse_data_line(const std::string& payload) { return "data: " + payload + "\n\n"; }

bool parse_chat_request(const std::string& body,
                        algoforge::llm::ChatRequest& out,
                        std::string& error_detail) {
    namespace json = algoforge::llm::json;
    json::JsonValue root;
    try {
        root = json::parse(body);
    } catch (const std::exception& e) {
        error_detail = std::string("invalid JSON: ") + e.what();
        return false;
    }
    if (!root.has("messages") || root.get("messages").type != json::JsonType::Array ||
        root.get("messages").arr.empty()) {
        error_detail = "messages must not be empty";
        return false;
    }
    out.messages.clear();
    for (const auto& m : root.get("messages").arr) {
        if (!m.has("role"))    { error_detail = "missing key 'role'";    return false; }
        if (!m.has("content")) { error_detail = "missing key 'content'"; return false; }
        algoforge::llm::ChatMessage cm;
        cm.role = m.get("role").as_str();
        cm.content = m.get("content").as_str();
        out.messages.push_back(std::move(cm));
    }
    out.model = (root.has("model") && root.get("model").type == json::JsonType::String)
                    ? root.get("model").as_str() : "llama3.1:8b";
    out.temperature = root.has("temperature") ? static_cast<float>(root.get("temperature").as_double())
                                              : 0.2f;
    if (root.has("max_tokens")) out.max_tokens = static_cast<int>(root.get("max_tokens").as_int());
    if (root.has("seed"))       out.seed       = static_cast<int>(root.get("seed").as_int());
    return true;
}

} // namespace algoforge::dashboard
