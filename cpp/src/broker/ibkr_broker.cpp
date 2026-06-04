/**
 * AlgoForge — src/broker/ibkr_broker.cpp
 *
 * Interactive Brokers adapter — Client Portal Web API.
 *
 * Python oracle: python/algoforge/brokers/ibkr.py
 *
 * Transport: a locally-running Client Portal Gateway proxies an authenticated
 * session; requests hit https://localhost:5000/v1/api. No API-key header —
 * the gateway holds the session. AF_IBKR_ACCOUNT env var selects the account.
 *
 * IBKR identifies instruments by conid (contract id), not ticker. The helper
 * _resolve_conid(symbol) calls GET /iserver/secdef/search and caches results
 * in conid_cache_.
 *
 * Lots → quantity: lots parameter is used as quantity directly (100 = 100 shares).
 * Position ticket: conid is used as ticket for positions (no order-ticket in IBKR).
 * Order-confirm reply loop: POST may return confirmation prompts; this adapter
 * iterates up to MAX_CONFIRM_ROUNDS replying with {"confirmed":true}.
 */

#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace af {
namespace broker {

/* =========================================================================
 * Constants (mirror Python ibkr.py module-level constants)
 * =========================================================================*/

static constexpr int MAX_CONFIRM_ROUNDS = 5;

/* IBKR market data field IDs */
static constexpr const char* FIELD_LAST = "31";
static constexpr const char* FIELD_BID  = "84";
static constexpr const char* FIELD_ASK  = "86";

/* =========================================================================
 * Timeframe map — mirrors Python _TF_TO_IBKR
 * AF_Timeframe (seconds) -> {bar_str, period_str}
 * =========================================================================*/

struct IbkrTfEntry { const char* bar; const char* period; };

static const IbkrTfEntry* tf_to_ibkr(AF_Timeframe tf) {
    static const std::map<int, IbkrTfEntry> TF_MAP = {
        { AF_TF_S15,  { "15secs", "1d"  } },
        { AF_TF_M1,   { "1min",   "1d"  } },
        { AF_TF_M5,   { "5mins",  "5d"  } },
        { AF_TF_M15,  { "15mins", "5d"  } },
        { AF_TF_M30,  { "30mins", "5d"  } },
        { AF_TF_H1,   { "1h",     "30d" } },
        { AF_TF_H4,   { "4h",     "30d" } },
        { AF_TF_D1,   { "1d",     "1y"  } },
        { AF_TF_W1,   { "1w",     "5y"  } },
    };
    static const IbkrTfEntry fallback = { "1d", "1y" };
    auto it = TF_MAP.find(static_cast<int>(tf));
    return (it != TF_MAP.end()) ? &it->second : &fallback;
}

/* =========================================================================
 * Internal helpers (mirror Python module-level functions)
 * =========================================================================*/

/* Convert IBKR account ID (e.g. "U1234567") to integer login field.
 * Mirrors Python _parse_account_login. */
static uint64_t parse_account_login(const std::string& acct) {
    /* Strip leading non-digit characters (U, DU, FF, EE prefixes) */
    size_t i = 0;
    while (i < acct.size() &&
           (acct[i] == 'U' || acct[i] == 'u' ||
            acct[i] == 'D' || acct[i] == 'd' ||
            acct[i] == 'F' || acct[i] == 'f' ||
            acct[i] == 'E' || acct[i] == 'e')) {
        ++i;
    }
    std::string digits = acct.substr(i);
    if (digits.empty()) return 0;
    for (char c : digits) {
        if (c < '0' || c > '9') return 0;
    }
    return static_cast<uint64_t>(std::stoull(digits));
}

/* Helper: make a JsonVal number */
static JsonVal jnum(double v) {
    JsonVal j;
    j.type = JsonVal::Type::Number;
    j.num  = v;
    return j;
}

/* Helper: make a JsonVal string */
static JsonVal jstr(const std::string& s) {
    JsonVal j;
    j.type = JsonVal::Type::String;
    j.str  = s;
    return j;
}

/* Helper: make a JsonVal bool */
static JsonVal jbool(bool v) {
    JsonVal j;
    j.type    = JsonVal::Type::Bool;
    j.boolean = v;
    return j;
}

/* Helper: make an empty JsonVal object */
static JsonVal jobj() {
    JsonVal j;
    j.type = JsonVal::Type::Object;
    return j;
}

/* Helper: make an empty JsonVal array */
static JsonVal jarr() {
    JsonVal j;
    j.type = JsonVal::Type::Array;
    return j;
}

/* =========================================================================
 * IbkrBroker constructor
 * =========================================================================*/

IbkrBroker::IbkrBroker(BrokerConfig cfg)
    : RestBroker(std::move(cfg)) {}

/* =========================================================================
 * _safe_float — mirrors Python _safe_float(val, default=0.0)
 * Parse a JsonVal that may be string/number, return default on failure.
 * =========================================================================*/
/* static */
double IbkrBroker::_safe_float(const JsonVal& v, double def) {
    if (v.is_null()) return def;
    if (v.is_num())  return v.as_double(def);
    if (v.is_str()) {
        /* Strip commas and whitespace, then parse */
        std::string s = v.str;
        s.erase(std::remove(s.begin(), s.end(), ','), s.end());
        /* Trim whitespace */
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return def;
        size_t end = s.find_last_not_of(" \t\r\n");
        s = s.substr(start, end - start + 1);
        if (s.empty()) return def;
        try {
            size_t pos = 0;
            double result = std::stod(s, &pos);
            /* Require full parse */
            if (pos == s.size()) return result;
        } catch (...) {}
        return def;
    }
    return def;
}

/* =========================================================================
 * _summary_amount — mirrors Python _summary_amount(data, key)
 * Extract .amount from a portfolio summary field (nested dict).
 * =========================================================================*/
/* static */
double IbkrBroker::_summary_amount(const JsonVal& data, const std::string& key) {
    if (!data.is_obj()) return 0.0;
    const JsonVal& raw = data[key];
    if (raw.is_obj()) {
        return _safe_float(raw["amount"]);
    }
    return _safe_float(raw);
}

/* =========================================================================
 * _resolve_conid — mirrors Python IbkrBroker._resolve_conid(symbol)
 * Return the IBKR conid for symbol, with in-process cache.
 * =========================================================================*/
int64_t IbkrBroker::_resolve_conid(const std::string& sym) const {
    /* Uppercase key for cache */
    std::string key = sym;
    for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    auto it = conid_cache_.find(key);
    if (it != conid_cache_.end()) return it->second;

    JsonVal results = _get("/iserver/secdef/search", {{"symbol", sym}});

    if (!results.is_arr() || results.size() == 0) {
        throw BrokerError("ibkr: no conid found for symbol '" + sym + "'", 0, 6);
    }

    /* Prefer exact ticker match; fall back to first entry */
    int64_t conid = -1;
    for (size_t i = 0; i < results.size(); ++i) {
        const JsonVal& entry = results[i];
        std::string ticker = entry["ticker"].as_str();
        for (char& c : ticker) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (ticker == key) {
            conid = entry["conid"].as_int(0);
            break;
        }
    }
    if (conid < 0) {
        conid = results[0]["conid"].as_int(0);
    }

    conid_cache_[key] = conid;
    return conid;
}

/* =========================================================================
 * _on_connect — mirrors Python IbkrBroker._on_connect()
 * Verify account_id is configured and gateway session is active.
 * =========================================================================*/
void IbkrBroker::_on_connect() {
    /* Require account_id */
    cfg_.require({"account_id"});

    JsonVal status;
    try {
        status = _get("/iserver/auth/status");
    } catch (const BrokerError& exc) {
        throw BrokerError(
            std::string("ibkr: could not reach gateway auth/status: ") + exc.what(),
            exc.http_status, 3);
    }

    bool authenticated = status["authenticated"].as_bool(false);
    if (!authenticated) {
        throw BrokerError(
            "ibkr: Client Portal Gateway session is not authenticated. "
            "Start the gateway and complete SSO login first.",
            0, 5);
    }
}

/* =========================================================================
 * get_account — mirrors Python IbkrBroker.get_account()
 * =========================================================================*/
AF_Error IbkrBroker::get_account(AF_AccountInfo& out) const {
    const std::string& acct = cfg_.account_id;
    JsonVal data;
    try {
        data = _get("/portfolio/" + acct + "/summary");
    } catch (const BrokerError&) {
        throw;  /* propagate */
    }

    double equity      = _summary_amount(data, "netliquidation");
    double free_margin = _summary_amount(data, "availablefunds");
    double balance     = _summary_amount(data, "totalcashvalue");
    if (balance == 0.0) balance = equity;  /* fallback */
    double profit      = _summary_amount(data, "unrealizedpnl");
    double margin      = _summary_amount(data, "grosspositionvalue");

    /* currency from any nested field */
    std::string currency = "USD";
    for (const std::string& field_key : {"netliquidation", "availablefunds"}) {
        const JsonVal& raw = data[field_key];
        if (raw.is_obj()) {
            const JsonVal& cur = raw["currency"];
            if (cur.is_str() && !cur.str.empty()) {
                currency = cur.str;
                break;
            }
        }
    }

    out.balance     = balance;
    out.equity      = equity;
    out.margin      = margin;
    out.free_margin = free_margin;
    out.profit      = profit;
    out.leverage    = 1;   /* IBKR does not expose a simple leverage ratio */
    out.login       = parse_account_login(acct);

    /* currency: copy up to 7 chars */
    std::strncpy(out.currency, currency.c_str(), sizeof(out.currency) - 1);
    out.currency[sizeof(out.currency) - 1] = '\0';

    return AF_OK;
}

/* =========================================================================
 * get_tick — mirrors Python IbkrBroker.get_tick(symbol)
 * Fetch last/bid/ask snapshot via /iserver/marketdata/snapshot.
 * =========================================================================*/
AF_Error IbkrBroker::get_tick(const char* sym, AF_Tick& out) const {
    std::string symbol(sym ? sym : "");
    int64_t conid = _resolve_conid(symbol);

    std::string fields = std::string(FIELD_LAST) + "," + FIELD_BID + "," + FIELD_ASK;
    JsonVal results = _get("/iserver/marketdata/snapshot",
                           {{"conids", std::to_string(conid)}, {"fields", fields}});

    if (!results.is_arr() || results.size() == 0) {
        throw BrokerError("ibkr: empty snapshot for " + symbol, 0, 7);
    }

    const JsonVal& snap = results[0];
    double last = _safe_float(snap[FIELD_LAST]);
    double bid  = _safe_float(snap[FIELD_BID]);
    double ask  = _safe_float(snap[FIELD_ASK]);

    /* If bid/ask missing but last available, use last for both. */
    if (bid == 0.0 && last > 0.0) bid = last;
    if (ask == 0.0 && last > 0.0) ask = last;

    out.bid    = bid;
    out.ask    = ask;
    out.volume = 0.0;
    out.timestamp = static_cast<int64_t>(std::time(nullptr));

    std::strncpy(out.symbol, sym, AF_MAX_SYMBOL_LEN - 1);
    out.symbol[AF_MAX_SYMBOL_LEN - 1] = '\0';

    return AF_OK;
}

/* =========================================================================
 * get_symbol_info — mirrors Python IbkrBroker.get_symbol_info(symbol)
 * Return symbol metadata derived from secdef search results.
 * =========================================================================*/
AF_Error IbkrBroker::get_symbol_info(const char* sym, AF_SymbolInfo& out) const {
    std::string symbol(sym ? sym : "");
    std::string key = symbol;
    for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    JsonVal results = _get("/iserver/secdef/search", {{"symbol", symbol}});
    if (!results.is_arr() || results.size() == 0) {
        throw BrokerError("ibkr: no secdef for " + symbol, 0, 6);
    }

    /* Prefer exact ticker match; fall back to first entry */
    /* (We don't actually use entry data since IBKR doesn't return min lot
     * in secdef search — use sensible defaults, matching Python oracle.) */

    std::strncpy(out.name, sym, AF_MAX_SYMBOL_LEN - 1);
    out.name[AF_MAX_SYMBOL_LEN - 1] = '\0';
    out.digits        = 2;
    out.point         = 0.01;
    out.contract_size = 1.0;
    out.volume_min    = 1.0;
    out.volume_max    = 10'000'000.0;
    out.volume_step   = 1.0;

    return AF_OK;
}

/* =========================================================================
 * get_bars — mirrors Python IbkrBroker.get_bars(symbol, tf, count)
 * Fetch OHLCV bars via /iserver/marketdata/history.
 * =========================================================================*/
AF_Error IbkrBroker::get_bars(const char* sym, AF_Timeframe tf,
                               int count, AF_Bar* bars_out, int* filled) const {
    if (filled) *filled = 0;
    if (!bars_out || count <= 0) return AF_ERR_INVALID_PARAM;

    std::string symbol(sym ? sym : "");
    int64_t conid = _resolve_conid(symbol);

    const IbkrTfEntry* entry = tf_to_ibkr(tf);

    JsonVal data = _get("/iserver/marketdata/history",
                        {{"conid",      std::to_string(conid)},
                         {"period",     entry->period},
                         {"bar",        entry->bar},
                         {"outsideRth", "false"}});

    /* data["data"] is the bars array */
    const JsonVal& raw_bars = data.is_obj() ? data["data"] : JsonVal{};

    if (!raw_bars.is_arr()) {
        return AF_OK;  /* empty result — not an error per Python oracle */
    }

    size_t total = raw_bars.size();

    /* Return up to count most recent bars (tail trim) — mirrors Python [-count:] */
    size_t start = (static_cast<int>(total) > count)
                   ? (total - static_cast<size_t>(count))
                   : 0;
    size_t n = total - start;
    if (static_cast<int>(n) > count) n = static_cast<size_t>(count);

    for (size_t i = 0; i < n; ++i) {
        const JsonVal& rb = raw_bars[start + i];
        AF_Bar& b = bars_out[i];
        b.timestamp = rb["t"].as_int(0) / 1000;  /* ms -> s */
        b.open      = _safe_float(rb["o"]);
        b.high      = _safe_float(rb["h"]);
        b.low       = _safe_float(rb["l"]);
        b.close     = _safe_float(rb["c"]);
        b.volume    = _safe_float(rb["v"]);
        b.spread    = 0.0;
        af_bar_init(&b);
    }

    if (filled) *filled = static_cast<int>(n);
    return AF_OK;
}

/* =========================================================================
 * _map_order_type — mirrors Python IbkrBroker._map_order_type(order_type)
 * Returns (side_str, ibkr_type_str) for an AF_OrderType value.
 * =========================================================================*/
static std::pair<std::string, std::string> map_order_type(AF_OrderType ot) {
    switch (ot) {
        case AF_ORDER_BUY:        return {"BUY",  "MKT"};
        case AF_ORDER_SELL:       return {"SELL", "MKT"};
        case AF_ORDER_BUY_LIMIT:  return {"BUY",  "LMT"};
        case AF_ORDER_SELL_LIMIT: return {"SELL", "LMT"};
        case AF_ORDER_BUY_STOP:   return {"BUY",  "STP"};
        case AF_ORDER_SELL_STOP:  return {"SELL", "STP"};
        default:                  return {"BUY",  "MKT"};
    }
}

/* =========================================================================
 * place_order — mirrors Python IbkrBroker.place_order(...)
 * Place an order; handles confirmation reply loop.
 * =========================================================================*/
AF_Error IbkrBroker::place_order(const char* sym, AF_OrderType type,
                                  double lots, double price,
                                  double sl, double tp,
                                  uint32_t magic, const char* comment,
                                  AF_Order& out) {
    std::string symbol(sym ? sym : "");
    int64_t conid = _resolve_conid(symbol);

    auto [side, ibkr_type] = map_order_type(type);

    /* quantity: integer if whole number, otherwise fractional */
    double quantity = lots;
    if (lots == static_cast<double>(static_cast<long long>(lots))) {
        quantity = static_cast<double>(static_cast<long long>(lots));
    }

    /* Build order body */
    JsonVal order_body = jobj();
    order_body.obj["conid"]     = jnum(static_cast<double>(conid));
    order_body.obj["orderType"] = jstr(ibkr_type);
    order_body.obj["side"]      = jstr(side);
    order_body.obj["quantity"]  = jnum(quantity);
    order_body.obj["tif"]       = jstr("GTC");

    if (ibkr_type == "LMT" && price != 0.0) {
        order_body.obj["price"] = jnum(price);
    }
    if (ibkr_type == "STP" && price != 0.0) {
        order_body.obj["auxPrice"] = jnum(price);
    }

    /* Wrap in {"orders": [order_body]} */
    JsonVal orders_arr = jarr();
    orders_arr.arr.push_back(std::move(order_body));

    JsonVal req_body = jobj();
    req_body.obj["orders"] = std::move(orders_arr);

    const std::string& acct = cfg_.account_id;
    JsonVal resp = _post("/iserver/account/" + acct + "/orders", req_body);

    /* Normalise resp to array */
    if (resp.is_obj()) {
        JsonVal arr = jarr();
        arr.arr.push_back(std::move(resp));
        resp = std::move(arr);
    }

    int64_t order_id = -1;

    for (int round = 0; round < MAX_CONFIRM_ROUNDS; ++round) {
        if (!resp.is_arr() || resp.size() == 0) break;

        const JsonVal& item = resp[0];

        /* Confirmation prompt: has "id" and "message" but no "order_id" */
        bool has_id      = item.has("id");
        bool has_message = item.has("message");
        bool has_order_id = item.has("order_id") || item.has("orderId");

        if (has_id && has_message && !has_order_id) {
            std::string reply_id = item["id"].as_str();
            JsonVal confirm_body = jobj();
            confirm_body.obj["confirmed"] = jbool(true);
            resp = _post("/iserver/reply/" + reply_id, confirm_body);
            if (resp.is_obj()) {
                JsonVal arr = jarr();
                arr.arr.push_back(std::move(resp));
                resp = std::move(arr);
            }
            continue;
        }

        /* Order result */
        if (item.has("order_id")) {
            order_id = item["order_id"].as_int(-1);
            break;
        }
        if (item.has("orderId")) {
            order_id = item["orderId"].as_int(-1);
            break;
        }
        /* Unexpected shape — break */
        break;
    }

    if (order_id < 0) {
        throw BrokerError(
            "ibkr: could not extract order_id after " +
            std::to_string(MAX_CONFIRM_ROUNDS) +
            " confirmation rounds",
            0, 8);
    }

    /* Populate output order */
    out.ticket     = static_cast<uint64_t>(order_id);
    out.type       = type;
    out.lots       = quantity;
    out.price      = price;
    out.sl         = sl;
    out.tp         = tp;
    out.fill_price = 0.0;
    out.magic      = magic;
    out.open_time  = 0;
    out.fill_time  = 0;
    out.client_id[0] = '\0';

    std::strncpy(out.symbol, sym, AF_MAX_SYMBOL_LEN - 1);
    out.symbol[AF_MAX_SYMBOL_LEN - 1] = '\0';

    if (comment) {
        std::strncpy(out.comment, comment, sizeof(out.comment) - 1);
        out.comment[sizeof(out.comment) - 1] = '\0';
    } else {
        out.comment[0] = '\0';
    }

    return AF_OK;
}

/* =========================================================================
 * cancel_order — mirrors Python IbkrBroker.cancel_order(ticket)
 * Returns AF_OK on success, error code on failure.
 * =========================================================================*/
AF_Error IbkrBroker::cancel_order(uint64_t ticket) {
    const std::string& acct = cfg_.account_id;
    try {
        _request("DELETE",
                 "/iserver/account/" + acct + "/order/" + std::to_string(ticket),
                 JsonVal{}, {}, {});
        return AF_OK;
    } catch (const BrokerError& exc) {
        (void)exc;
        return AF_ERR_ORDER_REJECTED;
    }
}

/* =========================================================================
 * modify_position — mirrors Python IbkrBroker.modify_position(ticket, sl, tp)
 * Modify stop-loss/take-profit via POST /iserver/account/{accountId}/order/{orderId}.
 * =========================================================================*/
AF_Error IbkrBroker::modify_position(uint64_t ticket, double sl, double tp) {
    if (sl == 0.0 && tp == 0.0) return AF_OK;  /* nothing to do */

    const std::string& acct = cfg_.account_id;
    JsonVal body = jobj();
    if (sl != 0.0) body.obj["auxPrice"] = jnum(sl);
    if (tp != 0.0) body.obj["price"]    = jnum(tp);

    try {
        _post("/iserver/account/" + acct + "/order/" + std::to_string(ticket), body);
        return AF_OK;
    } catch (const BrokerError& exc) {
        (void)exc;
        return AF_ERR_ORDER_REJECTED;
    }
}

/* =========================================================================
 * _parse_position — mirrors Python IbkrBroker._parse_position(raw)
 * Convert a raw portfolio position JsonVal to AF_Position.
 * =========================================================================*/
AF_Position IbkrBroker::_parse_position(const JsonVal& raw) const {
    AF_Position pos{};

    double qty = _safe_float(raw["position"]);
    pos.ticket       = static_cast<uint64_t>(raw["conid"].as_int(0));
    pos.side         = (qty > 0.0) ? AF_DIR_LONG
                     : (qty < 0.0) ? AF_DIR_SHORT
                     : AF_DIR_NONE;
    pos.lots         = std::abs(qty);
    pos.open_price   = _safe_float(raw["avgCost"]);
    pos.current_price= _safe_float(raw["mktPrice"]);
    pos.profit       = _safe_float(raw["unrealizedPnl"]);
    pos.sl           = 0.0;
    pos.tp           = 0.0;
    pos.commission   = 0.0;
    pos.swap         = 0.0;
    pos.magic        = 0;
    pos.comment[0]   = '\0';
    pos.open_time    = 0;

    /* Symbol: prefer "ticker", fall back to "contractDesc" */
    std::string sym;
    if (raw.has("ticker") && raw["ticker"].is_str() && !raw["ticker"].str.empty()) {
        sym = raw["ticker"].str;
    } else if (raw.has("contractDesc") && raw["contractDesc"].is_str()) {
        sym = raw["contractDesc"].str;
    }
    std::strncpy(pos.symbol, sym.c_str(), AF_MAX_SYMBOL_LEN - 1);
    pos.symbol[AF_MAX_SYMBOL_LEN - 1] = '\0';

    return pos;
}

/* =========================================================================
 * get_positions — mirrors Python IbkrBroker.get_positions()
 * Return all open positions from /portfolio/{accountId}/positions/0.
 * =========================================================================*/
std::vector<AF_Position> IbkrBroker::get_positions() const {
    const std::string& acct = cfg_.account_id;
    JsonVal data;
    try {
        data = _get("/portfolio/" + acct + "/positions/0");
    } catch (...) {
        return {};
    }

    if (!data.is_arr()) return {};

    std::vector<AF_Position> result;
    for (size_t i = 0; i < data.size(); ++i) {
        const JsonVal& p = data[i];
        double qty = _safe_float(p["position"]);
        if (qty == 0.0) continue;  /* filter zero-quantity */
        result.push_back(_parse_position(p));
    }
    return result;
}

/* =========================================================================
 * get_position — mirrors Python IbkrBroker.get_position(ticket)
 * Return the position whose conid == ticket, or nullopt.
 * =========================================================================*/
std::optional<AF_Position> IbkrBroker::get_position(uint64_t ticket) const {
    auto positions = get_positions();
    for (const auto& pos : positions) {
        if (pos.ticket == ticket) return pos;
    }
    return std::nullopt;
}

/* =========================================================================
 * close_position — mirrors Python IbkrBroker.close_position(ticket, lots)
 * Close a position identified by conid (= ticket).
 * =========================================================================*/
AF_Error IbkrBroker::close_position(uint64_t ticket, double lots) {
    auto maybe_pos = get_position(ticket);
    if (!maybe_pos.has_value()) {
        return AF_ERR_POSITION_NOT_FOUND;
    }
    const AF_Position& pos = maybe_pos.value();

    double close_qty = (lots > 0.0) ? lots : pos.lots;
    AF_OrderType close_type = (pos.side == AF_DIR_LONG) ? AF_ORDER_SELL : AF_ORDER_BUY;

    /* Use conid directly since we already know it */
    std::string symbol(pos.symbol);
    std::string key = symbol;
    for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    conid_cache_[key] = static_cast<int64_t>(ticket);

    AF_Order out{};
    try {
        return place_order(pos.symbol, close_type, close_qty, 0.0, 0.0, 0.0, 0, "", out);
    } catch (const BrokerError& exc) {
        (void)exc;
        return AF_ERR_ORDER_REJECTED;
    }
}

/* =========================================================================
 * _parse_order — mirrors Python IbkrBroker._parse_order(raw)
 * Convert a raw order JsonVal to AF_Order.
 * =========================================================================*/
AF_Order IbkrBroker::_parse_order(const JsonVal& raw) const {
    AF_Order order{};

    std::string side_str = raw["side"].as_str();
    for (char& c : side_str) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    bool is_buy = (side_str.find("BUY") != std::string::npos);

    /* ticket: prefer orderId (camelCase), then order_id (snake_case) */
    int64_t tid = 0;
    if (raw.has("orderId")) {
        tid = raw["orderId"].as_int(0);
    } else if (raw.has("order_id")) {
        tid = raw["order_id"].as_int(0);
    }
    order.ticket = static_cast<uint64_t>(tid);
    order.type   = is_buy ? AF_ORDER_BUY : AF_ORDER_SELL;

    /* lots: prefer remainingQuantity, fallback totalSize */
    double lots_val = 0.0;
    if (raw.has("remainingQuantity")) {
        lots_val = _safe_float(raw["remainingQuantity"]);
    } else if (raw.has("totalSize")) {
        lots_val = _safe_float(raw["totalSize"]);
    }
    order.lots       = lots_val;
    order.price      = _safe_float(raw["price"]);
    order.fill_price = _safe_float(raw["avgPrice"]);
    order.sl         = 0.0;
    order.tp         = 0.0;
    order.magic      = 0;
    order.open_time  = 0;
    order.fill_time  = 0;
    order.client_id[0] = '\0';

    /* symbol: prefer "ticker", fallback "symbol" */
    std::string sym;
    if (raw.has("ticker") && raw["ticker"].is_str()) {
        sym = raw["ticker"].str;
    } else if (raw.has("symbol") && raw["symbol"].is_str()) {
        sym = raw["symbol"].str;
    }
    std::strncpy(order.symbol, sym.c_str(), AF_MAX_SYMBOL_LEN - 1);
    order.symbol[AF_MAX_SYMBOL_LEN - 1] = '\0';

    /* comment from description */
    std::string desc = raw["description"].as_str();
    std::strncpy(order.comment, desc.c_str(), sizeof(order.comment) - 1);
    order.comment[sizeof(order.comment) - 1] = '\0';

    return order;
}

/* =========================================================================
 * get_orders — mirrors Python IbkrBroker.get_orders()
 * Return live/pending orders from /iserver/account/orders.
 * =========================================================================*/
std::vector<AF_Order> IbkrBroker::get_orders() const {
    JsonVal data;
    try {
        data = _get("/iserver/account/orders");
    } catch (...) {
        return {};
    }

    /* data may be dict with "orders" key, or array directly */
    const JsonVal* raw_list = nullptr;
    JsonVal temp_arr;
    if (data.is_obj() && data.has("orders")) {
        raw_list = &data.obj.at("orders");
    } else if (data.is_arr()) {
        raw_list = &data;
    }

    if (!raw_list || !raw_list->is_arr()) return {};

    std::vector<AF_Order> result;
    result.reserve(raw_list->size());
    for (size_t i = 0; i < raw_list->size(); ++i) {
        result.push_back(_parse_order((*raw_list)[i]));
    }
    return result;
}

/* =========================================================================
 * register_ibkr_broker — self-registration
 * =========================================================================*/
void register_ibkr_broker() {
    register_broker("ibkr",
        [](bool /*paper*/, double /*balance*/, BrokerConfig cfg)
            -> std::unique_ptr<IBroker> {
            return std::make_unique<IbkrBroker>(std::move(cfg));
        });
}

} /* namespace broker */
} /* namespace af */
