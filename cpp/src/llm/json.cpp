/**
 * AlgoForge — src/llm/json.cpp
 * S2 LLM Layer — hand-rolled minimal JSON parser and emitter.
 *
 * Supports only the subset needed by the LLM layer:
 *   - Emit: objects (std::map → sorted keys), arrays, strings, numbers, bool, null
 *   - Parse: full recursive descent for Ollama API responses
 *   - Balanced-brace scanner for news_sentiment JSON extraction (§9)
 *
 * No external JSON dependency.
 */

#include "llm_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace algoforge::llm::json {

/* =========================================================================
 * Emitter
 * ========================================================================= */

static void emit_string(std::string& out, const std::string& s) {
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
}

std::string emit_object(const std::map<std::string, std::string>& kv) {
    std::string out;
    out += '{';
    bool first = true;
    for (const auto& [k, v] : kv) {  /* map is already sorted */
        if (!first) out += ',';
        first = false;
        emit_string(out, k);
        out += ':';
        out += v;   /* v is already a JSON value (number/string/etc.) */
    }
    out += '}';
    return out;
}

/* Emit a double as a JSON number string using shortest round-tripping
 * decimal (Python repr-equivalent). Iterates precision until round-trip
 * succeeds to avoid std::to_chars dependency. */
std::string emit_double(double v) {
    if (std::isinf(v) || std::isnan(v)) return "null";
    char buf[64];
    /* shortest round-trip representation */
    for (int p = 1; p <= 17; ++p) {
        snprintf(buf, sizeof(buf), "%.*g", p, v);
        if (strtod(buf, nullptr) == v) break;
    }
    std::string s(buf);
    /* %g can pick scientific notation (e.g. 250 -> "2.5e+02"), which diverges
     * from Python's json and malforms broker API payloads. For values in a
     * normal positional range, re-render without an exponent. */
    if (s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
        const double av = std::fabs(v);
        if (av >= 1e-4 && av < 1e16) {
            for (int dp = 0; dp <= 17; ++dp) {
                snprintf(buf, sizeof(buf), "%.*f", dp, v);
                if (strtod(buf, nullptr) == v) { s = buf; break; }
            }
        }
    }
    return s;
}

/* Emit a JSON string value (with quotes) */
std::string emit_string_val(const std::string& s) {
    std::string out;
    emit_string(out, s);
    return out;
}

/* Emit a boolean */
std::string emit_bool(bool b) { return b ? "true" : "false"; }

/* Emit a sorted-keys JSON object from indicator pairs
 * (sorts by key, formats values as numbers). */
std::string emit_indicators(const std::vector<std::pair<std::string,double>>& ind) {
    /* sort by key */
    std::vector<std::pair<std::string,double>> sorted = ind;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    std::string out;
    out += '{';
    bool first = true;
    for (const auto& [k, v] : sorted) {
        if (!first) out += ',';
        first = false;
        emit_string(out, k);
        out += ':';
        out += emit_double(v);
    }
    out += '}';
    return out;
}

/* =========================================================================
 * Parser — minimal recursive descent
 * ========================================================================= */

struct Parser {
    const char* src;
    size_t      len;
    size_t      pos = 0;

    explicit Parser(const std::string& s) : src(s.data()), len(s.size()) {}

    char peek() const { return pos < len ? src[pos] : '\0'; }
    char advance() {
        if (pos >= len) throw std::runtime_error("JSON: unexpected end");
        return src[pos++];
    }

    void skip_ws() {
        while (pos < len && (src[pos] == ' ' || src[pos] == '\t' ||
                             src[pos] == '\n' || src[pos] == '\r'))
            ++pos;
    }

    void expect(char c) {
        skip_ws();
        if (peek() != c) {
            char msg[64];
            snprintf(msg, sizeof(msg), "JSON: expected '%c' got '%c' at %zu", c, peek(), pos);
            throw std::runtime_error(msg);
        }
        ++pos;
    }

    std::string parse_string_raw() {
        expect('"');
        std::string out;
        while (true) {
            if (pos >= len) throw std::runtime_error("JSON: unterminated string");
            char c = src[pos++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos >= len) throw std::runtime_error("JSON: bad escape");
                char e = src[pos++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        /* parse 4 hex digits */
                        if (pos + 4 > len) throw std::runtime_error("JSON: bad \\u");
                        char hex[5] = {};
                        memcpy(hex, src + pos, 4);
                        pos += 4;
                        unsigned codepoint = 0;
                        sscanf(hex, "%4x", &codepoint);
                        /* Encode as UTF-8 (BMP only) */
                        if (codepoint < 0x80) {
                            out += static_cast<char>(codepoint);
                        } else if (codepoint < 0x800) {
                            out += static_cast<char>(0xC0 | (codepoint >> 6));
                            out += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (codepoint >> 12));
                            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    /* Forward declarations */
    JsonValue parse_value();
    JsonValue parse_object();
    JsonValue parse_array();
    JsonValue parse_number();
};

/* JsonValue — a minimal variant-like type */
JsonValue Parser::parse_number() {
    size_t start = pos;
    if (peek() == '-') ++pos;
    while (pos < len && (src[pos] >= '0' && src[pos] <= '9')) ++pos;
    if (pos < len && src[pos] == '.') {
        ++pos;
        while (pos < len && (src[pos] >= '0' && src[pos] <= '9')) ++pos;
    }
    if (pos < len && (src[pos] == 'e' || src[pos] == 'E')) {
        ++pos;
        if (pos < len && (src[pos] == '+' || src[pos] == '-')) ++pos;
        while (pos < len && (src[pos] >= '0' && src[pos] <= '9')) ++pos;
    }
    std::string num(src + start, pos - start);
    JsonValue v;
    v.type = JsonType::Number;
    v.num  = std::stod(num);
    v.str  = num;
    return v;
}

JsonValue Parser::parse_object() {
    JsonValue v;
    v.type = JsonType::Object;
    expect('{');
    skip_ws();
    if (peek() == '}') { ++pos; return v; }
    while (true) {
        skip_ws();
        std::string key = parse_string_raw();
        skip_ws();
        expect(':');
        skip_ws();
        JsonValue val = parse_value();
        v.obj[key] = std::move(val);
        skip_ws();
        if (peek() == '}') { ++pos; break; }
        expect(',');
    }
    return v;
}

JsonValue Parser::parse_array() {
    JsonValue v;
    v.type = JsonType::Array;
    expect('[');
    skip_ws();
    if (peek() == ']') { ++pos; return v; }
    while (true) {
        skip_ws();
        v.arr.push_back(parse_value());
        skip_ws();
        if (peek() == ']') { ++pos; break; }
        expect(',');
    }
    return v;
}

JsonValue Parser::parse_value() {
    skip_ws();
    char c = peek();
    if (c == '"') {
        JsonValue v;
        v.type = JsonType::String;
        v.str  = parse_string_raw();
        return v;
    }
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == 't') {
        if (pos + 4 <= len && strncmp(src + pos, "true", 4) == 0) {
            pos += 4;
            JsonValue v; v.type = JsonType::Bool; v.boolean = true; return v;
        }
    }
    if (c == 'f') {
        if (pos + 5 <= len && strncmp(src + pos, "false", 5) == 0) {
            pos += 5;
            JsonValue v; v.type = JsonType::Bool; v.boolean = false; return v;
        }
    }
    if (c == 'n') {
        if (pos + 4 <= len && strncmp(src + pos, "null", 4) == 0) {
            pos += 4;
            JsonValue v; v.type = JsonType::Null; return v;
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
    char msg[64];
    snprintf(msg, sizeof(msg), "JSON: unexpected char '%c' at %zu", c, pos);
    throw std::runtime_error(msg);
}

JsonValue parse(const std::string& s) {
    Parser p(s);
    p.skip_ws();
    return p.parse_value();
}

/* =========================================================================
 * §9  Balanced-brace JSON extractor (for news_sentiment)
 *
 * Scans for the first balanced {...} respecting strings and \-escapes.
 * Returns the extracted substring; throws LLMError(decode) if not found.
 * ========================================================================= */

std::string extract_first_json_object(const std::string& text) {
    int    depth     = 0;
    bool   in_string = false;
    bool   escape    = false;
    size_t start     = std::string::npos;

    for (size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (escape) { escape = false; continue; }
        if (ch == '\\' && in_string) { escape = true; continue; }
        if (ch == '"') { in_string = !in_string; continue; }
        if (in_string) continue;
        if (ch == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                return text.substr(start, i - start + 1);
            }
        }
    }
    throw LLMError(LLMErrorKind::decode, "no valid JSON object found in response");
}

} /* namespace algoforge::llm::json */
