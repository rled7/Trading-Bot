/**
 * AlgoForge — src/broker/rest_broker.cpp
 *
 * REST broker infrastructure (Phase 1 / S5):
 *   - BrokerConfig::from_env, ::require, ::_getenv
 *   - json_parse, json_emit, json_from_map  (af::broker::JsonVal helpers)
 *   - RestBroker: connect, _resolve_base_url, _get, _post, _request,
 *                 _urlencode, do_http_request (no-op stub), _on_connect (no-op)
 *   - available_brokers() and make_broker()
 *   - Self-registration map: broker_registry() + register_broker()
 *
 * The five concrete adapters (OANDA, Alpaca, IBKR, Binance, Coinbase) are
 * declared in rest_broker.hpp but NOT implemented here — each lives in its
 * own translation unit and self-registers via:
 *
 *     static bool _reg = register_broker("name", [](...){ return ...; });
 *
 * Python reference oracle: python/algoforge/brokers/{config,base,registry}.py
 *
 * JSON: bridges algoforge::llm::json::parse (from src/llm/json.cpp) into
 * af::broker::JsonVal. The llm JSON emitter helpers are reused for leaf
 * serialisation so numbers round-trip identically to the rest of the codebase.
 * No new third-party dependencies.
 */

#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"   /* IBroker, make_paper_broker */

/* Re-use the hand-rolled llm JSON parser. af_engine now links af_llm (see
 * CMakeLists.txt) so the symbol is available at link time. */
#include "../llm/llm_internal.hpp"   /* algoforge::llm::json::parse, JsonValue */

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/* ── Factory declaration from paper_broker.cpp ── */
namespace af {
    std::unique_ptr<IBroker> make_paper_broker(double balance);
#ifdef AF_MT5_AVAILABLE
    std::unique_ptr<IBroker> make_mt5_broker();
#endif
}

namespace af {
namespace broker {

/* =========================================================================
 * Internal helpers
 * =========================================================================*/

namespace {

/* Convert a string to lowercase in-place. */
std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/* Trim leading/trailing whitespace. */
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/* Convert algoforge::llm::json::JsonValue → af::broker::JsonVal recursively. */
JsonVal llm_to_broker_json(const algoforge::llm::json::JsonValue& v) {
    using JT = algoforge::llm::json::JsonType;
    JsonVal out;
    switch (v.type) {
        case JT::Null:
            out.type = JsonVal::Type::Null;
            break;
        case JT::Bool:
            out.type    = JsonVal::Type::Bool;
            out.boolean = v.boolean;
            break;
        case JT::Number:
            out.type = JsonVal::Type::Number;
            out.num  = v.num;
            break;
        case JT::String:
            out.type = JsonVal::Type::String;
            out.str  = v.str;
            break;
        case JT::Array:
            out.type = JsonVal::Type::Array;
            out.arr.reserve(v.arr.size());
            for (const auto& elem : v.arr)
                out.arr.push_back(llm_to_broker_json(elem));
            break;
        case JT::Object:
            out.type = JsonVal::Type::Object;
            for (const auto& [k, val] : v.obj)
                out.obj[k] = llm_to_broker_json(val);
            break;
    }
    return out;
}

/* Forward declaration for recursive emit. */
void emit_val(std::string& out, const JsonVal& v);

void emit_string_escaped(std::string& out, const std::string& s) {
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

void emit_val(std::string& out, const JsonVal& v) {
    switch (v.type) {
        case JsonVal::Type::Null:
            out += "null";
            break;
        case JsonVal::Type::Bool:
            out += (v.boolean ? "true" : "false");
            break;
        case JsonVal::Type::Number:
            /* Reuse llm emitter for identical double formatting. */
            out += algoforge::llm::json::emit_double(v.num);
            break;
        case JsonVal::Type::String:
            emit_string_escaped(out, v.str);
            break;
        case JsonVal::Type::Array:
            out += '[';
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i > 0) out += ',';
                emit_val(out, v.arr[i]);
            }
            out += ']';
            break;
        case JsonVal::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [k, val] : v.obj) {  /* map = sorted keys */
                if (!first) out += ',';
                first = false;
                emit_string_escaped(out, k);
                out += ':';
                emit_val(out, val);
            }
            out += '}';
            break;
        }
    }
}

/* =========================================================================
 * Broker registry (self-registration map)
 * =========================================================================*/

/**
 * Function-local-static map: name → factory lambda.
 * Adapter .cpp files call register_broker("name", factory) via a
 * static-initialiser trick; this returns the shared map.
 *
 * NOTE: Static-library linker stripping caveat — if an adapter TU is in a
 * static lib and nothing else references it, its static initialiser may not
 * run. Callers building with static libs may need --whole-archive or an
 * explicit reference per adapter. See report for details.
 */
std::map<std::string, factory_fn>& broker_registry() {
    static std::map<std::string, factory_fn> reg;
    return reg;
}

} /* anonymous namespace */

/* =========================================================================
 * JsonVal helpers — public API
 * =========================================================================*/

JsonVal json_parse(const std::string& text) {
    /* Delegate to the llm JSON parser, then bridge the result type. */
    algoforge::llm::json::JsonValue llm_val = algoforge::llm::json::parse(text);
    return llm_to_broker_json(llm_val);
}

std::string json_emit(const JsonVal& v) {
    std::string out;
    emit_val(out, v);
    return out;
}

JsonVal json_from_map(const std::map<std::string, std::string>& kv) {
    JsonVal obj;
    obj.type = JsonVal::Type::Object;
    for (const auto& [k, v] : kv) {
        JsonVal sv;
        sv.type = JsonVal::Type::String;
        sv.str  = v;
        obj.obj[k] = std::move(sv);
    }
    return obj;
}

/* =========================================================================
 * Registration API (public, declared in rest_broker.hpp)
 * =========================================================================*/

bool register_broker(const std::string& name, factory_fn fn) {
    broker_registry()[name] = std::move(fn);
    return true;
}

/* =========================================================================
 * BrokerConfig — mirrors Python config.py
 * =========================================================================*/

/* static */
std::string BrokerConfig::_getenv(const std::string& var) {
    const char* val = std::getenv(var.c_str());
    return (val != nullptr) ? std::string(val) : std::string{};
}

/* static */
BrokerConfig BrokerConfig::from_env(const std::string& name, bool paper) {
    /* Mirrors Python BrokerConfig.from_env:
     *   prefix = f"AF_{name.upper()}_"
     *   env_paper = _env_bool(env.get(prefix + "PAPER"))
     *   resolved_paper = env_paper if env_paper is not None else paper
     */
    std::string upper_name = name;
    for (char& c : upper_name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::string prefix = "AF_" + upper_name + "_";

    /* Resolve paper flag: env var overrides the default. */
    std::string paper_env = _getenv(prefix + "PAPER");
    bool resolved_paper = paper;
    if (!paper_env.empty()) {
        std::string lower = to_lower(trim(paper_env));
        resolved_paper = (lower == "1" || lower == "true" || lower == "yes" || lower == "on");
    }

    BrokerConfig cfg(name);
    cfg.paper      = resolved_paper;
    cfg.base_url   = _getenv(prefix + "BASE_URL");
    cfg.api_key    = _getenv(prefix + "API_KEY");
    cfg.api_secret = _getenv(prefix + "API_SECRET");
    cfg.account_id = _getenv(prefix + "ACCOUNT");
    return cfg;
}

void BrokerConfig::require(std::initializer_list<const char*> fields) const {
    /* Mirrors Python BrokerConfig.require():
     *   missing = [f for f in fields if not getattr(self, f, "")]
     *   raise ValueError(f"{self.name}: missing required credential(s): {pretty}")
     *
     * C++ equivalent: raise BrokerError with the same message format.
     * Field names map directly to struct members.
     */
    std::vector<std::string> missing;
    for (const char* field : fields) {
        std::string fname(field);
        bool empty = false;
        if (fname == "api_key")    empty = api_key.empty();
        else if (fname == "api_secret") empty = api_secret.empty();
        else if (fname == "account_id") empty = account_id.empty();
        else if (fname == "base_url")   empty = base_url.empty();
        else if (fname == "name")       empty = name.empty();
        else {
            /* Check extra map for unknown fields. */
            auto it = extra.find(fname);
            empty = (it == extra.end() || it->second.empty());
        }
        if (empty) missing.push_back(fname);
    }
    if (!missing.empty()) {
        std::string upper_name = name;
        for (char& c : upper_name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        std::string pretty;
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) pretty += ", ";
            /* Mirror Python: f"AF_{self.name.upper()}_{f.upper()}" */
            std::string fu = missing[i];
            for (char& c : fu)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            pretty += "AF_" + upper_name + "_" + fu;
        }
        throw BrokerError(name + ": missing required credential(s): " + pretty);
    }
}

/* =========================================================================
 * RestBroker — connect and HTTP helpers
 * =========================================================================*/

AF_Error RestBroker::connect() {
    /* Mirrors Python RestBroker.connect():
     *   if self._connected: return 0
     *   self._base_url = self._resolve_base_url()
     *   if not self._base_url: log error; return 1
     *   try: self._on_connect()
     *   except (BrokerError, ValueError): log error; return code or 1
     *   self._connected = True
     *   return 0
     */
    if (connected_) return AF_OK;

    base_url_ = _resolve_base_url();
    if (base_url_.empty()) {
        return AF_ERR_NOT_CONNECTED;  /* no URL configured */
    }

    try {
        _on_connect();
    } catch (const BrokerError& exc) {
        /* Map broker error code to AF_Error: code 2=HTTP error, 3=network, 4=JSON */
        if (exc.http_status == -2 || exc.code == 2)  return AF_ERR_AUTH_FAILED;
        return AF_ERR_NOT_CONNECTED;
    } catch (const std::invalid_argument&) {
        return AF_ERR_NOT_CONNECTED;
    } catch (const std::runtime_error&) {
        return AF_ERR_NOT_CONNECTED;
    }

    connected_ = true;
    return AF_OK;
}

/* private */
std::string RestBroker::_resolve_base_url() const {
    /* Mirrors Python:
     *   if self._config.base_url: return self._config.base_url.rstrip("/")
     *   url = self.PAPER_URL if self._config.paper else self.LIVE_URL
     *   return url.rstrip("/")
     */
    if (!cfg_.base_url.empty()) {
        std::string url = cfg_.base_url;
        while (!url.empty() && url.back() == '/') url.pop_back();
        return url;
    }
    std::string url = cfg_.paper ? std::string(paper_url()) : std::string(live_url());
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

/* static private */
std::string RestBroker::_urlencode(const std::map<std::string, std::string>& params) {
    /* Percent-encode key=value pairs, joined by '&'. */
    static auto hex_nibble = [](unsigned char c) -> char {
        return c < 10 ? ('0' + c) : ('A' + c - 10);
    };
    static auto encode = [&](const std::string& s) -> std::string {
        std::string out;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += static_cast<char>(c);
            } else {
                out += '%';
                out += hex_nibble(c >> 4);
                out += hex_nibble(c & 0xF);
            }
        }
        return out;
    };

    std::string result;
    bool first = true;
    for (const auto& [k, v] : params) {
        if (!first) result += '&';
        first = false;
        result += encode(k);
        result += '=';
        result += encode(v);
    }
    return result;
}

HttpResponse RestBroker::do_http_request(
        const std::string& /*method*/,
        const std::string& /*url*/,
        const std::string& /*body*/,
        const std::map<std::string, std::string>& /*hdrs*/) const {
    /* Production code should override this.  The default returns status=0
     * (unimplemented) so callers (and tests) can detect the no-op. */
    return HttpResponse{0, ""};
}

JsonVal RestBroker::_get(
        const std::string& path,
        const std::map<std::string, std::string>& params) const {
    return _request("GET", path, JsonVal{}, params, {});
}

JsonVal RestBroker::_post(
        const std::string& path,
        const JsonVal& body,
        const std::map<std::string, std::string>& params) const {
    return _request("POST", path, body, params, {});
}

JsonVal RestBroker::_request(
        const std::string& method,
        const std::string& path,
        const JsonVal&     body,
        const std::map<std::string, std::string>& params,
        const std::map<std::string, std::string>& extra_headers) const {
    /* Mirrors Python RestBroker._request():
     *   url = path if path.startswith("http") else f"{self._base_url}/{path.lstrip('/')}"
     *   if params: url += "?" + urlencode(params)
     *   req_headers = {"Accept": "application/json"}
     *   req_headers.update(self._auth_headers())
     *   if headers: req_headers.update(headers)
     *   if body: data = json.dumps(body); req_headers["Content-Type"] = "application/json"
     *   → dispatch; raise BrokerError on HTTP>=400, network error, invalid JSON
     *   if not raw: return {}
     */

    /* Build URL. */
    std::string url;
    if (path.size() >= 4 && path.substr(0, 4) == "http") {
        url = path;
    } else {
        std::string stripped = path;
        while (!stripped.empty() && stripped.front() == '/')
            stripped.erase(stripped.begin());
        url = base_url_ + "/" + stripped;
    }
    if (!params.empty()) {
        url += '?';
        url += _urlencode(params);
    }

    /* Build headers. */
    std::map<std::string, std::string> hdrs;
    hdrs["Accept"] = "application/json";
    for (const auto& [k, v] : _auth_headers()) hdrs[k] = v;
    for (const auto& [k, v] : extra_headers)   hdrs[k] = v;

    /* Serialise body. */
    std::string body_str;
    if (!body.is_null()) {
        body_str = json_emit(body);
        hdrs.emplace("Content-Type", "application/json");
    }

    HttpResponse resp = do_http_request(method, url, body_str, hdrs);

    /* status=0 means transport not implemented (default stub). */
    if (resp.status == 0) {
        throw BrokerError(
            cfg_.name + " " + method + " " + path +
            " -> transport unimplemented (override do_http_request)",
            0, 3);
    }

    /* HTTP >= 400 → BrokerError(code=2). */
    if (resp.status >= 400) {
        throw BrokerError(
            cfg_.name + " " + method + " " + path +
            " -> HTTP " + std::to_string(resp.status) + ": " +
            resp.body.substr(0, 300),
            resp.status, 2);
    }

    /* Empty body → {} (mirrors Python's `if not raw: return {}`). */
    if (resp.body.empty()) {
        return JsonVal{};
    }

    /* Parse JSON; bad JSON → BrokerError(code=4). */
    try {
        return json_parse(resp.body);
    } catch (const std::exception& exc) {
        throw BrokerError(
            cfg_.name + " " + method + " " + path +
            " -> invalid JSON: " + std::string(exc.what()).substr(0, 200),
            resp.status, 4);
    }
}

/* =========================================================================
 * Registry — available_brokers() and make_broker()
 * =========================================================================*/

std::vector<std::string> available_brokers() {
    /* Returns "paper" + "mt5" + any registered adapters, sorted+deduped. */
    std::vector<std::string> result = {"paper", "mt5"};
    for (const auto& [name, _] : broker_registry()) {
        result.push_back(name);
    }
    /* Sort and remove duplicates (paper/mt5 always come first here but
     * sort() makes the output deterministic regardless of insertion order). */
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::unique_ptr<IBroker> make_broker(const std::string& name,
                                      bool paper,
                                      double balance,
                                      BrokerConfig cfg) {
    /* Mirrors Python make_broker():
     *   key = name.strip().lower()
     *   if key == "paper": return PaperBroker
     *   if key == "mt5": return MT5
     *   if key not in registry: raise ValueError/invalid_argument
     *   cfg = config or BrokerConfig.from_env(key, paper=paper)
     *   return cls(cfg)
     */
    std::string key = to_lower(trim(name));

    if (key == "paper") {
        return af::make_paper_broker(balance);
    }

    if (key == "mt5") {
#ifdef AF_MT5_AVAILABLE
        return af::make_mt5_broker();
#else
        throw std::invalid_argument(
            "mt5 broker not available on this platform "
            "(AF_MT5_AVAILABLE not defined; use PAPER mode)");
#endif
    }

    /* Check self-registration map. */
    auto& reg = broker_registry();
    auto  it  = reg.find(key);
    if (it == reg.end()) {
        throw std::invalid_argument("unknown or not-yet-implemented broker: " + name);
    }

    /* Resolve config from environment if the caller did not supply one. */
    if (cfg.name.empty()) {
        cfg = BrokerConfig::from_env(key, paper);
    }

    return it->second(paper, balance, std::move(cfg));
}

} /* namespace broker */
} /* namespace af */
