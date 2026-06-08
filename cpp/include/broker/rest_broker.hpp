/**
 * AlgoForge — include/broker/rest_broker.hpp
 *
 * REST broker infrastructure (Phase 1 / S5):
 *   BrokerConfig   — credential + endpoint configuration (mirrors Python S5 config.py)
 *   JsonVal        — minimal JSON variant (thin wrapper around the llm JSON parser idiom)
 *   RestBroker     — abstract HTTP base that every live REST adapter inherits from
 *
 * Concrete adapters (OandaBroker, AlpacaBroker, IbkrBroker, BinanceBroker,
 * CoinbaseBroker) are declared here and defined in rest_broker.cpp.
 *
 * Design mirrors Python S5 base.py / config.py / registry.py exactly:
 *   - Credentials via AF_<BROKER>_API_KEY / API_SECRET / ACCOUNT / BASE_URL / PAPER
 *   - paper/live URL resolution via LIVE_URL / PAPER_URL class constants
 *   - _to_broker_symbol / _from_broker_symbol per-adapter overrides
 *   - HTTP seam: protected virtual do_request() so tests inject canned JSON
 *     without making real network calls — mirrors Python's broker._request = mock
 *
 * No new third-party dependencies.  The JSON parser re-uses the hand-rolled
 * parser from src/llm/json.cpp; the parser is re-exposed here as a lightweight
 * JsonVal type (separate from the llm namespace so broker code stays clean).
 */
#pragma once

#include "broker/broker.hpp"     /* IBroker, AF_Error, AF_* types */

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace af {
namespace broker {

/* =========================================================================
 * BrokerError — transport / API error (mirrors Python BrokerError)
 * =========================================================================*/

struct BrokerError : std::runtime_error {
    int http_status{0};   /* HTTP status code if available */
    int code{1};          /* connect() / order return code semantic */

    explicit BrokerError(const std::string& msg, int http_status = 0, int code = 1)
        : std::runtime_error(msg), http_status(http_status), code(code) {}
};

/* =========================================================================
 * JsonVal — minimal JSON variant used by broker adapters
 *
 * Intentionally thin. We handle only the shapes that broker API responses
 * actually produce (objects, arrays, strings, numbers, booleans, null).
 * =========================================================================*/

struct JsonVal;
using JsonObj = std::map<std::string, JsonVal>;
using JsonArr = std::vector<JsonVal>;

struct JsonVal {
    enum class Type { Null, Bool, Number, String, Array, Object } type{Type::Null};

    bool        boolean{false};
    double      num{0.0};
    std::string str;
    JsonArr     arr;
    JsonObj     obj;

    /* Accessors — return sensible defaults on type mismatch */
    bool        is_null()   const { return type == Type::Null;   }
    bool        is_obj()    const { return type == Type::Object;  }
    bool        is_arr()    const { return type == Type::Array;   }
    bool        is_str()    const { return type == Type::String;  }
    bool        is_num()    const { return type == Type::Number;  }

    double      as_double(double def = 0.0)    const { return is_num() ? num : def; }
    long long   as_int   (long long def = 0LL) const { return is_num() ? static_cast<long long>(num) : def; }
    std::string as_str   (const std::string& def = "") const { return is_str() ? str : def; }
    bool        as_bool  (bool def = false)    const { return type==Type::Bool ? boolean : def; }

    bool has(const std::string& k) const {
        return is_obj() && obj.count(k) > 0;
    }
    const JsonVal& get(const std::string& k) const {
        static const JsonVal null_val{};
        if (!is_obj()) return null_val;
        auto it = obj.find(k);
        return (it != obj.end()) ? it->second : null_val;
    }
    const JsonVal& operator[](const std::string& k) const { return get(k); }
    const JsonVal& operator[](size_t idx) const {
        static const JsonVal null_val{};
        if (!is_arr() || idx >= arr.size()) return null_val;
        return arr[idx];
    }
    size_t size() const {
        if (is_arr()) return arr.size();
        if (is_obj()) return obj.size();
        return 0;
    }
};

/* Parse a JSON string into a JsonVal. Throws std::runtime_error on malformed input. */
JsonVal json_parse(const std::string& text);

/* Emit a JsonVal back to compact JSON text (for signing / request bodies). */
std::string json_emit(const JsonVal& v);

/* Build a JsonVal from a flat string→string map (request bodies). */
JsonVal json_from_map(const std::map<std::string, std::string>& kv);

/* =========================================================================
 * BrokerConfig — mirrors Python BrokerConfig in config.py
 * =========================================================================*/

struct BrokerConfig {
    std::string name;
    bool        paper      {true};
    std::string base_url;       /* explicit override — empty = adapter picks */
    std::string api_key;
    std::string api_secret;
    std::string account_id;
    std::map<std::string, std::string> extra;

    BrokerConfig() = default;
    explicit BrokerConfig(std::string name) : name(std::move(name)) {}

    /**
     * Load config from AF_<NAME>_* environment variables.
     * Mirrors Python BrokerConfig.from_env().
     */
    static BrokerConfig from_env(const std::string& name, bool paper = true);

    /**
     * Raise BrokerError if any of the named fields are empty.
     * Mirrors Python BrokerConfig.require().
     */
    void require(std::initializer_list<const char*> fields) const;

private:
    static std::string _getenv(const std::string& var);
};

/* =========================================================================
 * HttpResponse — returned by do_http_request()
 * =========================================================================*/

struct HttpResponse {
    int         status{200};
    std::string body;
};

/* =========================================================================
 * RestBroker — abstract HTTP base (mirrors Python RestBroker in base.py)
 *
 * Subclasses must:
 *   1. Set BROKER_NAME, LIVE_URL, PAPER_URL (static string members).
 *   2. Override broker_name(), _on_connect(), _auth_headers(),
 *      optionally _to_broker_symbol() / _from_broker_symbol().
 *   3. Implement all IBroker trading methods.
 *
 * The HTTP seam is do_http_request() — it is declared protected virtual so
 * test subclasses can override it to return canned responses without making
 * real network calls. Production code leaves it at the default (no-op that
 * returns an error, since real HTTP requires platform socket code outside
 * this tree's zero-dep constraint). The same pattern is used in the Python
 * tests: broker._request = mock.
 * =========================================================================*/

class RestBroker : public IBroker {
public:
    static constexpr float TIMEOUT_SEC = 15.0f;

    explicit RestBroker(BrokerConfig cfg = BrokerConfig{})
        : cfg_(std::move(cfg)) {}

    ~RestBroker() override = default;

    /* ── IBroker connection interface ── */
    AF_Error    connect()          override;
    void        disconnect()       override { connected_ = false; }
    bool        is_connected()     const override { return connected_; }
    const char* broker_name()      const override { return cfg_.name.c_str(); }

    /* ── Default no-op implementations of optional IBroker methods ── */
    std::vector<AF_Position>   get_positions() const override { return {}; }
    std::vector<AF_Order>      get_orders()    const override { return {}; }
    std::optional<AF_Position> get_position(uint64_t) const override { return std::nullopt; }

    /* ── Accessors for tests ── */
    const BrokerConfig& config() const { return cfg_; }
    const std::string&  base_url() const { return base_url_; }

protected:
    BrokerConfig cfg_;
    std::string  base_url_;
    bool         connected_{false};

    /* ── Hooks for subclasses ── */
    virtual const char* live_url()  const { return ""; }
    virtual const char* paper_url() const { return ""; }

    virtual void _on_connect() {}  /* validate creds / ping gateway */

    virtual std::map<std::string, std::string> _auth_headers() const { return {}; }

    virtual std::string _to_broker_symbol  (const std::string& s) const { return s; }
    virtual std::string _from_broker_symbol(const std::string& s) const { return s; }

    /* ── HTTP helpers — build on top of do_http_request() ── */
    JsonVal _get (const std::string& path,
                  const std::map<std::string,std::string>& params = {}) const;

    JsonVal _post(const std::string& path,
                  const JsonVal& body = JsonVal{},
                  const std::map<std::string,std::string>& params = {}) const;

    JsonVal _request(const std::string& method,
                     const std::string& path,
                     const JsonVal&     body   = JsonVal{},
                     const std::map<std::string,std::string>& params  = {},
                     const std::map<std::string,std::string>& headers = {}) const;

    /* ── Transport seam — override in test subclasses to return canned JSON ──
     *
     * Production code should provide a real TCP/TLS implementation here;
     * the provided default always returns status=0 (unimplemented) so unit
     * tests know they must override it.
     *
     * Method: "GET", "POST", "PUT", "DELETE", "PATCH"
     * url:    full URL including query string
     * body:   raw request body (JSON string), may be empty
     * hdrs:   merged auth + content-type headers
     */
    virtual HttpResponse do_http_request(const std::string& method,
                                         const std::string& url,
                                         const std::string& body,
                                         const std::map<std::string,std::string>& hdrs) const;

private:
    std::string _resolve_base_url() const;
    static std::string _urlencode(const std::map<std::string,std::string>& params);
};

/* =========================================================================
 * Concrete adapters
 * =========================================================================*/

/* ── OANDA v20 REST ── */
class OandaBroker : public RestBroker {
public:
    static constexpr const char* BROKER_NAME = "oanda";
    static constexpr const char* LIVE_URL    = "https://api-fxtrade.oanda.com/v3";
    static constexpr const char* PAPER_URL   = "https://api-fxpractice.oanda.com/v3";

    explicit OandaBroker(BrokerConfig cfg = BrokerConfig{"oanda"});

    const char* broker_name() const override { return BROKER_NAME; }

    /* IBroker trading surface */
    AF_Error get_account   (AF_AccountInfo& out)   const override;
    AF_Error get_tick      (const char* sym, AF_Tick& out) const override;
    AF_Error get_symbol_info(const char* sym, AF_SymbolInfo& out) const override;
    AF_Error get_bars      (const char* sym, AF_Timeframe tf,
                             int count, AF_Bar* bars_out, int* filled) const override;
    AF_Error place_order   (const char* sym, AF_OrderType type,
                             double lots, double price, double sl, double tp,
                             uint32_t magic, const char* comment,
                             AF_Order& out) override;
    AF_Error close_position (uint64_t ticket, double lots = 0.0) override;
    AF_Error modify_position(uint64_t ticket, double sl, double tp) override;
    AF_Error cancel_order   (uint64_t ticket) override;
    std::vector<AF_Position>   get_positions() const override;
    std::vector<AF_Order>      get_orders()    const override;
    std::optional<AF_Position> get_position(uint64_t ticket) const override;

    /* Exposed for tests */
    std::string to_broker_symbol(const std::string& s) const { return _to_broker_symbol(s); }
    std::string from_broker_symbol(const std::string& s) const { return _from_broker_symbol(s); }

protected:
    const char* live_url()  const override { return LIVE_URL;  }
    const char* paper_url() const override { return PAPER_URL; }
    void _on_connect() override;
    std::map<std::string,std::string> _auth_headers() const override;
    std::string _to_broker_symbol  (const std::string& s) const override;
    std::string _from_broker_symbol(const std::string& s) const override;

private:
    std::string _acct_path(const std::string& sub = "") const;
    static AF_Position _trade_to_position(const JsonVal& trade);
    static AF_Order    _raw_order_to_order(const JsonVal& o);
};

/* ── Alpaca REST ── */
class AlpacaBroker : public RestBroker {
public:
    static constexpr const char* BROKER_NAME = "alpaca";
    static constexpr const char* LIVE_URL    = "https://api.alpaca.markets/v2";
    static constexpr const char* PAPER_URL   = "https://paper-api.alpaca.markets/v2";
    static constexpr const char* DEFAULT_DATA_URL = "https://data.alpaca.markets/v2";

    explicit AlpacaBroker(BrokerConfig cfg = BrokerConfig{"alpaca"});

    const char* broker_name() const override { return BROKER_NAME; }

    AF_Error get_account   (AF_AccountInfo& out)   const override;
    AF_Error get_tick      (const char* sym, AF_Tick& out) const override;
    AF_Error get_symbol_info(const char* sym, AF_SymbolInfo& out) const override;
    AF_Error get_bars      (const char* sym, AF_Timeframe tf,
                             int count, AF_Bar* bars_out, int* filled) const override;
    AF_Error place_order   (const char* sym, AF_OrderType type,
                             double lots, double price, double sl, double tp,
                             uint32_t magic, const char* comment,
                             AF_Order& out) override;
    AF_Error close_position (uint64_t ticket, double lots = 0.0) override;
    AF_Error modify_position(uint64_t ticket, double sl, double tp) override;
    AF_Error cancel_order   (uint64_t ticket) override;
    std::vector<AF_Position>   get_positions() const override;
    std::vector<AF_Order>      get_orders()    const override;
    std::optional<AF_Position> get_position(uint64_t ticket) const override;

protected:
    const char* live_url()  const override { return LIVE_URL;  }
    const char* paper_url() const override { return PAPER_URL; }
    void _on_connect() override;
    std::map<std::string,std::string> _auth_headers() const override;

    /* protected (not private) so parity-test subclasses can drive ticket
     * allocation — see test_alpaca_broker.cpp. */
    /* Ticket management — mirrors Python Alpaca's _alloc_ticket / _resolve_key */
    mutable std::unordered_map<std::string, uint64_t> id_to_ticket_;
    mutable std::unordered_map<uint64_t, std::string> ticket_to_id_;
    mutable uint64_t next_ticket_{1};

    uint64_t    _alloc_ticket(const std::string& key) const;
    std::string _resolve_key (uint64_t ticket) const;

    std::string _data_url() const;

    AF_Position _parse_position(const JsonVal& p, uint64_t ticket,
                                 const std::string& sym) const;
};

/* ── Interactive Brokers REST (Client Portal Web API) ── */
class IbkrBroker : public RestBroker {
public:
    static constexpr const char* BROKER_NAME = "ibkr";
    static constexpr const char* LIVE_URL    = "https://localhost:5000/v1/api";
    static constexpr const char* PAPER_URL   = "https://localhost:5000/v1/api";

    explicit IbkrBroker(BrokerConfig cfg = BrokerConfig{"ibkr"});

    const char* broker_name() const override { return BROKER_NAME; }

    AF_Error get_account   (AF_AccountInfo& out)   const override;
    AF_Error get_tick      (const char* sym, AF_Tick& out) const override;
    AF_Error get_symbol_info(const char* sym, AF_SymbolInfo& out) const override;
    AF_Error get_bars      (const char* sym, AF_Timeframe tf,
                             int count, AF_Bar* bars_out, int* filled) const override;
    AF_Error place_order   (const char* sym, AF_OrderType type,
                             double lots, double price, double sl, double tp,
                             uint32_t magic, const char* comment,
                             AF_Order& out) override;
    AF_Error close_position (uint64_t ticket, double lots = 0.0) override;
    AF_Error modify_position(uint64_t ticket, double sl, double tp) override;
    AF_Error cancel_order   (uint64_t ticket) override;
    std::vector<AF_Position>   get_positions() const override;
    std::vector<AF_Order>      get_orders()    const override;
    std::optional<AF_Position> get_position(uint64_t ticket) const override;

protected:
    const char* live_url()  const override { return LIVE_URL;  }
    const char* paper_url() const override { return PAPER_URL; }
    void _on_connect() override;

protected:   /* protected (not private) so parity-test subclasses can mock/inspect — see test_ibkr_broker.cpp */
    mutable std::unordered_map<std::string, int64_t> conid_cache_;

    int64_t    _resolve_conid(const std::string& sym) const;
    AF_Position _parse_position(const JsonVal& raw) const;
    AF_Order    _parse_order   (const JsonVal& raw) const;
    static double _safe_float(const JsonVal& v, double def = 0.0);
    static double _summary_amount(const JsonVal& data, const std::string& key);
};

/* ── Binance Spot REST ── */
class BinanceBroker : public RestBroker {
public:
    static constexpr const char* BROKER_NAME = "binance";
    static constexpr const char* LIVE_URL    = "https://api.binance.com";
    static constexpr const char* PAPER_URL   = "https://testnet.binance.vision";

    explicit BinanceBroker(BrokerConfig cfg = BrokerConfig{"binance"});

    const char* broker_name() const override { return BROKER_NAME; }

    AF_Error get_account   (AF_AccountInfo& out)   const override;
    AF_Error get_tick      (const char* sym, AF_Tick& out) const override;
    AF_Error get_symbol_info(const char* sym, AF_SymbolInfo& out) const override;
    AF_Error get_bars      (const char* sym, AF_Timeframe tf,
                             int count, AF_Bar* bars_out, int* filled) const override;
    AF_Error place_order   (const char* sym, AF_OrderType type,
                             double lots, double price, double sl, double tp,
                             uint32_t magic, const char* comment,
                             AF_Order& out) override;
    AF_Error close_position (uint64_t ticket, double lots = 0.0) override;
    AF_Error modify_position(uint64_t ticket, double sl, double tp) override;
    AF_Error cancel_order   (uint64_t ticket) override;
    std::vector<AF_Position>   get_positions() const override;
    std::vector<AF_Order>      get_orders()    const override;
    std::optional<AF_Position> get_position(uint64_t ticket) const override;

    /* HMAC-SHA256 signing — public for parity test */
    std::map<std::string,std::string>
        signed_params(std::map<std::string,std::string> params,
                      long long timestamp_ms = 0) const;

    /* Sign helper exposed for tests */
    std::string hmac_sha256_hex(const std::string& key, const std::string& msg) const;

protected:
    const char* live_url()  const override { return LIVE_URL;  }
    const char* paper_url() const override { return PAPER_URL; }
    void _on_connect() override;
    std::map<std::string,std::string> _auth_headers() const override;
    std::string _to_broker_symbol  (const std::string& s) const override;
};

/* ── Coinbase Advanced Trade REST ── */
class CoinbaseBroker : public RestBroker {
public:
    static constexpr const char* BROKER_NAME = "coinbase";
    static constexpr const char* LIVE_URL    = "https://api.coinbase.com/api/v3/brokerage";
    static constexpr const char* PAPER_URL   = "https://api.coinbase.com/api/v3/brokerage";

    explicit CoinbaseBroker(BrokerConfig cfg = BrokerConfig{"coinbase"});

    const char* broker_name() const override { return BROKER_NAME; }

    AF_Error get_account   (AF_AccountInfo& out)   const override;
    AF_Error get_tick      (const char* sym, AF_Tick& out) const override;
    AF_Error get_symbol_info(const char* sym, AF_SymbolInfo& out) const override;
    AF_Error get_bars      (const char* sym, AF_Timeframe tf,
                             int count, AF_Bar* bars_out, int* filled) const override;
    AF_Error place_order   (const char* sym, AF_OrderType type,
                             double lots, double price, double sl, double tp,
                             uint32_t magic, const char* comment,
                             AF_Order& out) override;
    AF_Error close_position (uint64_t ticket, double lots = 0.0) override;
    AF_Error modify_position(uint64_t ticket, double sl, double tp) override;
    AF_Error cancel_order   (uint64_t ticket) override;
    std::vector<AF_Position>   get_positions() const override;
    std::vector<AF_Order>      get_orders()    const override;
    std::optional<AF_Position> get_position(uint64_t ticket) const override;

    /* HMAC-SHA256 signing — public for parity test */
    std::string sign(const std::string& ts, const std::string& method,
                     const std::string& path, const std::string& body) const;

protected:
    const char* live_url()  const override { return LIVE_URL;  }
    const char* paper_url() const override { return PAPER_URL; }
    void _on_connect() override;
    std::string _to_broker_symbol  (const std::string& s) const override;
    std::string _from_broker_symbol(const std::string& s) const override;

    /* Override _request to inject HMAC headers — mirrors Python CoinbaseBroker._request */
    HttpResponse do_http_request(const std::string& method,
                                  const std::string& url,
                                  const std::string& body,
                                  const std::map<std::string,std::string>& hdrs) const override;

protected:   /* protected so parity-test subclasses can register tickets — see test_coinbase_broker.cpp */
    mutable std::unordered_map<std::string, uint64_t> id_to_ticket_;
    mutable std::unordered_map<uint64_t, std::string> ticket_to_id_;
    mutable uint64_t next_ticket_{1};

    uint64_t    _register_order_id(const std::string& oid) const;
    std::string _ticket_to_order_id(uint64_t ticket) const;
};

/* =========================================================================
 * Registry — mirrors Python registry.py
 * =========================================================================*/

/**
 * Factory function type used by the self-registration map.
 * Each concrete adapter .cpp registers itself with:
 *
 *     static bool _reg = register_broker("name",
 *         [](bool paper, double balance, BrokerConfig cfg) {
 *             return std::make_unique<MyBroker>(std::move(cfg));
 *         });
 *
 * WARNING (static-library linker stripping): if an adapter TU is compiled
 * into a static library and nothing else references it, the linker may drop
 * its .o and the static-initialiser will not run.  Use --whole-archive (GCC/
 * Clang) or FORCE_LOAD (Apple ld) when linking from a static archive.
 */
using factory_fn = std::function<std::unique_ptr<IBroker>(bool paper,
                                                           double balance,
                                                           BrokerConfig cfg)>;

/**
 * Register a broker factory under the given name (lower-case canonical).
 * Returns true (so it can be used as a static initialiser value).
 * Thread-safe only if called before any concurrent make_broker() calls.
 */
bool register_broker(const std::string& name, factory_fn fn);

/* ── Per-adapter registration hooks ──
 * Each <name>_broker.cpp defines its own register_<name>_broker(), which calls
 * register_broker("<name>", factory). ensure_brokers_registered() invokes all of
 * them exactly once and is called first by make_broker()/available_brokers().
 * Explicit calls (NOT static-init) are used so an adapter's translation unit can
 * never be silently stripped by the static-library linker. Defined at integration
 * once all five adapters exist. */
void register_oanda_broker();
void register_alpaca_broker();
void register_ibkr_broker();
void register_binance_broker();
void register_coinbase_broker();
void ensure_brokers_registered();

/**
 * Returns ["paper","mt5"] plus all self-registered adapter names, sorted.
 */
std::vector<std::string> available_brokers();

/**
 * Construct a broker by name (case-insensitive).
 * For REST adapters: loads credentials from AF_<NAME>_* env vars.
 * For "paper": returns a PaperBroker.
 * Throws std::invalid_argument for unknown names.
 */
std::unique_ptr<IBroker> make_broker(const std::string& name,
                                      bool paper = true,
                                      double balance = 10'000.0,
                                      BrokerConfig cfg = BrokerConfig{});

} /* namespace broker */
} /* namespace af */
