/**
 * AlgoForge — src/dashboard/handlers.cpp
 * Pure dashboard handlers. See header. Python oracle: dashboard/server.py.
 */
#include "dashboard/handlers.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

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

} // namespace algoforge::dashboard
