/**
 * AlgoForge — tests/llm_mock.cpp
 * S2 LLM Layer — MockTransport implementation.
 */

#include "llm_mock.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/* We need the internal JSON parser.  Rather than depend on the private
 * llm_internal.hpp (which lives in src/llm/), we replicate only what we
 * need: a way to load and navigate the fixture JSON.
 *
 * The simplest approach: re-use the json:: types by linking against the
 * compiled llm sources, but the internal header is not installed.
 * Instead we forward-include it via a relative path from tests/. */
#include "../src/llm/llm_internal.hpp"

using namespace algoforge::llm;
using namespace algoforge::llm::json;

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Locate the fixture directory.  We try several candidate paths so that
 * the test binary can be run from different working directories. */
static std::string find_fixture_dir(const std::string& override) {
    if (!override.empty()) return override;

    const char* candidates[] = {
        "tests/fixtures/llm",
        "../tests/fixtures/llm",
        "../../tests/fixtures/llm",
    };
    for (const char* c : candidates) {
        /* Try to open a known fixture to verify the directory */
        std::string probe = std::string(c) + "/complete_basic.json";
        std::ifstream f(probe);
        if (f.good()) return c;
    }
    /* Fall back — will fail gracefully when loading */
    return "tests/fixtures/llm";
}

/* Read entire file as string */
static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("MockTransport: cannot open fixture: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* Serialize a JsonValue back to a JSON string (for response_body_str_) */
static std::string serialize(const JsonValue& v);

static std::string serialize_string(const std::string& s) {
    std::string out;
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

static std::string serialize(const JsonValue& v) {
    switch (v.type) {
        case JsonType::Null:    return "null";
        case JsonType::Bool:    return v.boolean ? "true" : "false";
        case JsonType::Number: {
            char buf[64];
            double d = v.num;
            long long i = static_cast<long long>(d);
            if (static_cast<double>(i) == d) {
                snprintf(buf, sizeof(buf), "%lld", i);
            } else {
                snprintf(buf, sizeof(buf), "%.17g", d);
            }
            return buf;
        }
        case JsonType::String:  return serialize_string(v.str);
        case JsonType::Array: {
            std::string out = "[";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i > 0) out += ",";
                out += serialize(v.arr[i]);
            }
            out += "]";
            return out;
        }
        case JsonType::Object: {
            std::string out = "{";
            bool first = true;
            for (const auto& [k, val] : v.obj) {
                if (!first) out += ",";
                first = false;
                out += serialize_string(k);
                out += ":";
                out += serialize(val);
            }
            out += "}";
            return out;
        }
    }
    return "null";
}

/* =========================================================================
 * MockTransport
 * ========================================================================= */

MockTransport::MockTransport(const std::string& fixture_name,
                             const std::string& fixture_dir)
    : fixture_name_(fixture_name)
    , fixture_dir_(find_fixture_dir(fixture_dir))
{
    /* Special synthetic fixtures */
    if (fixture_name == "mock_unreachable") {
        is_unreachable_ = true;
        return;
    }
    if (fixture_name == "mock_timeout") {
        is_timeout_ = true;
        return;
    }
    load_fixture();
}

void MockTransport::load_fixture() {
    std::string path = fixture_dir_ + "/" + fixture_name_ + ".json";
    std::string raw  = read_file(path);

    JsonValue root = parse(raw);
    if (root.type != JsonType::Object) {
        throw std::runtime_error("MockTransport: fixture root is not an object");
    }

    /* request */
    if (root.has("request")) {
        const auto& req = root.get("request");
        if (req.has("method")) expected_method_ = req.get("method").as_str();
        if (req.has("path"))   expected_path_   = req.get("path").as_str();
    }

    /* response */
    if (root.has("response")) {
        const auto& resp = root.get("response");
        if (resp.has("status")) {
            response_status_ = static_cast<int>(resp.get("status").as_int());
        }
        if (resp.has("body")) {
            response_body_str_ = serialize(resp.get("body"));
        }
    }
}

TransportResult MockTransport::request(const std::string& method,
                                        const std::string& path,
                                        const std::string& body) {
    last_method_ = method;
    last_path_   = path;
    last_body_   = body;

    if (is_unreachable_) {
        throw LLMError(LLMErrorKind::unreachable,
                       "MockTransport: simulated unreachable");
    }
    if (is_timeout_) {
        throw LLMError(LLMErrorKind::timeout,
                       "MockTransport: simulated timeout");
    }

    /* Verify method and path match fixture */
    if (!expected_method_.empty() && method != expected_method_) {
        throw std::runtime_error(
            "MockTransport: method mismatch: expected " + expected_method_ +
            " got " + method);
    }
    if (!expected_path_.empty() && path != expected_path_) {
        throw std::runtime_error(
            "MockTransport: path mismatch: expected " + expected_path_ +
            " got " + path);
    }

    TransportResult tr;
    tr.status = response_status_;
    tr.body   = response_body_str_;
    tr.ok     = (response_status_ >= 200 && response_status_ < 300);
    return tr;
}
