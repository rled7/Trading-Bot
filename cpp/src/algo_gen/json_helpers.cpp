/**
 * AlgoForge — src/algo_gen/json_helpers.cpp
 * Hand-rolled JSON parser + emitter for algo_gen module.
 *
 * Based on the same pattern as src/llm/json.cpp.
 * emit_double uses shortest round-trip repr (Python json.dumps compatible).
 */

#include "algo_gen_internal.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace algoforge::algo_gen::json {

/* =========================================================================
 * Emitter helpers
 * ========================================================================= */

static void emit_string_raw(std::string& out, const std::string& s) {
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

/**
 * Shortest round-trip float repr — matches Python json.dumps.
 * Iterates precision 1..17, checks strtod round-trip.
 */
std::string emit_double(double v) {
    if (std::isinf(v) || std::isnan(v)) return "null";
    char buf[64];
    for (int p = 1; p <= 17; ++p) {
        snprintf(buf, sizeof(buf), "%.*g", p, v);
        double rt = strtod(buf, nullptr);
        if (rt == v) return buf;
    }
    snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

std::string emit_string_val(const std::string& s) {
    std::string out;
    emit_string_raw(out, s);
    return out;
}

std::string emit_bool(bool b) { return b ? "true" : "false"; }

/* =========================================================================
 * Parser — recursive descent
 * ========================================================================= */

struct Parser {
    const char* src;
    size_t      len;
    size_t      pos = 0;

    explicit Parser(const std::string& s) : src(s.data()), len(s.size()) {}

    char peek() const { return pos < len ? src[pos] : '\0'; }
    char advance() {
        if (pos >= len) throw std::runtime_error("JSON: unexpected end of input");
        return src[pos++];
    }
    void skip_ws() {
        while (pos < len && (src[pos]==' '||src[pos]=='\t'||src[pos]=='\n'||src[pos]=='\r'))
            ++pos;
    }
    void expect_char(char c) {
        skip_ws();
        if (peek() != c) {
            char msg[80];
            snprintf(msg, sizeof(msg), "JSON: expected '%c' got '%c' at %zu", c, peek(), pos);
            throw std::runtime_error(msg);
        }
        ++pos;
    }

    std::string parse_string_raw() {
        expect_char('"');
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
                        if (pos + 4 > len) throw std::runtime_error("JSON: bad \\u");
                        char hex[5] = {};
                        memcpy(hex, src + pos, 4);
                        pos += 4;
                        unsigned cp = 0;
                        sscanf(hex, "%4x", &cp);
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
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
    std::string numstr(src + start, pos - start);
    JsonValue v;
    v.type = JsonType::Number;
    v.num  = std::stod(numstr);
    v.str  = numstr;
    return v;
}

JsonValue Parser::parse_object() {
    JsonValue v;
    v.type = JsonType::Object;
    expect_char('{');
    skip_ws();
    if (peek() == '}') { ++pos; return v; }
    while (true) {
        skip_ws();
        std::string key = parse_string_raw();
        skip_ws();
        expect_char(':');
        skip_ws();
        JsonValue val = parse_value();
        v.obj[key] = std::move(val);
        skip_ws();
        if (peek() == '}') { ++pos; break; }
        expect_char(',');
    }
    return v;
}

JsonValue Parser::parse_array() {
    JsonValue v;
    v.type = JsonType::Array;
    expect_char('[');
    skip_ws();
    if (peek() == ']') { ++pos; return v; }
    while (true) {
        skip_ws();
        v.arr.push_back(parse_value());
        skip_ws();
        if (peek() == ']') { ++pos; break; }
        expect_char(',');
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
    char msg[80];
    snprintf(msg, sizeof(msg), "JSON: unexpected char '%c' at %zu", c, pos);
    throw std::runtime_error(msg);
}

JsonValue parse(const std::string& s) {
    Parser p(s);
    p.skip_ws();
    return p.parse_value();
}

} /* namespace algoforge::algo_gen::json */
