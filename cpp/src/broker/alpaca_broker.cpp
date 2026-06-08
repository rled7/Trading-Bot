/**
 * AlgoForge — src/broker/alpaca_broker.cpp
 *
 * Alpaca REST adapter (Phase 1 / S5).
 * Python oracle: python/algoforge/brokers/alpaca.py
 *
 * Notes:
 *  - Auth via APCA-API-KEY-ID + APCA-API-SECRET-KEY headers.
 *  - Market data (get_tick/get_bars) uses a SEPARATE data host; full URLs are
 *    passed to _get(), which RestBroker forwards verbatim when they start "http".
 *  - Orders are keyed by Alpaca UUID, positions by symbol; both map to stable
 *    int tickets via _alloc_ticket (monotonic counter, idempotent per key).
 *  - lots maps 1:1 to Alpaca qty; qty/prices are emitted as Python str(float)
 *    (e.g. 10.0 -> "10.0", 7.5 -> "7.5") for byte-parity with the oracle.
 *  - Plain Alpaca orders carry no SL/TP — they are NOT transmitted and the
 *    returned Order honestly reports sl/tp = 0.
 */

#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace af {
namespace broker {

namespace {

static double json_val_double(const JsonVal& v, double def = 0.0) {
    if (v.is_num()) return v.num;
    if (v.is_str() && !v.str.empty()) { try { return std::stod(v.str); } catch (...) {} }
    return def;
}

/* Mirror Python str(float): shortest round-trip, positional (no exponent for
 * normal magnitudes), and a trailing ".0" for integral values. */
static std::string py_float_str(double v) {
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    if (std::isnan(v)) return "nan";
    char buf[64];
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof(buf), "%.*g", p, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    std::string s(buf);
    if (s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
        double av = std::fabs(v);
        if (av >= 1e-4 && av < 1e16)
            for (int dp = 0; dp <= 17; ++dp) {
                std::snprintf(buf, sizeof(buf), "%.*f", dp, v);
                if (std::strtod(buf, nullptr) == v) { s = buf; break; }
            }
    }
    if (s.find('.') == std::string::npos &&
        s.find('e') == std::string::npos && s.find('E') == std::string::npos)
        s += ".0";
    return s;
}

/* Stable int login from the account-number string. The oracle only asserts
 * login is an int (not a specific value), so a deterministic FNV-1a hash is
 * sufficient and avoids pulling in an MD5 dependency. */
static uint32_t stable_login(const std::string& acct) {
    if (acct.empty()) return 0;
    uint32_t h = 2166136261u;
    for (unsigned char c : acct) { h ^= c; h *= 16777619u; }
    return h;
}

/* ISO-8601 / RFC-3339 → unix epoch (handles trailing Z and fractional secs). */
static int64_t iso_to_epoch(const std::string& ts_str) {
    if (ts_str.empty()) return 0;
    std::string s = ts_str;
    if (s.back() == 'Z') s.pop_back();
    auto dot = s.find('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    int y=0,mo=0,d=0,h=0,mi=0,se=0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y,&mo,&d,&h,&mi,&se) < 6) return 0;
    struct tm t{};
    t.tm_year=y-1900; t.tm_mon=mo-1; t.tm_mday=d; t.tm_hour=h; t.tm_min=mi; t.tm_sec=se; t.tm_isdst=0;
#if defined(_WIN32)
    time_t e = _mkgmtime(&t);
#else
    time_t e = timegm(&t);
#endif
    return (e == (time_t)-1) ? 0 : static_cast<int64_t>(e);
}

static const char* tf_to_alpaca(int tf) {
    switch (tf) {
        case 15: return "15Sec"; case 60: return "1Min"; case 300: return "5Min";
        case 900: return "15Min"; case 1800: return "30Min"; case 3600: return "1Hour";
        case 14400: return "4Hour"; case 86400: return "1Day"; case 604800: return "1Week";
        default: return "1Min";
    }
}

static std::pair<std::string,std::string> order_type_to_alpaca(AF_OrderType t) {
    switch (t) {
        case AF_ORDER_BUY:        return {"buy",  "market"};
        case AF_ORDER_SELL:       return {"sell", "market"};
        case AF_ORDER_BUY_LIMIT:  return {"buy",  "limit"};
        case AF_ORDER_SELL_LIMIT: return {"sell", "limit"};
        case AF_ORDER_BUY_STOP:   return {"buy",  "stop"};
        case AF_ORDER_SELL_STOP:  return {"sell", "stop"};
        default:                  return {"buy",  "market"};
    }
}

static AF_OrderType alpaca_to_order_type(const std::string& side, const std::string& type) {
    bool sell = (side == "sell");
    if (type == "limit") return sell ? AF_ORDER_SELL_LIMIT : AF_ORDER_BUY_LIMIT;
    if (type == "stop")  return sell ? AF_ORDER_SELL_STOP  : AF_ORDER_BUY_STOP;
    return sell ? AF_ORDER_SELL : AF_ORDER_BUY;
}

static JsonVal jstr(const std::string& s) { JsonVal v; v.type = JsonVal::Type::String; v.str = s; return v; }

} /* anonymous namespace */

AlpacaBroker::AlpacaBroker(BrokerConfig cfg) : RestBroker(std::move(cfg)) {
    if (cfg_.name.empty()) cfg_.name = "alpaca";
}

void AlpacaBroker::_on_connect() { cfg_.require({"api_key", "api_secret"}); }

std::map<std::string, std::string> AlpacaBroker::_auth_headers() const {
    return {{"APCA-API-KEY-ID", cfg_.api_key}, {"APCA-API-SECRET-KEY", cfg_.api_secret}};
}

std::string AlpacaBroker::_data_url() const { return DEFAULT_DATA_URL; }

uint64_t AlpacaBroker::_alloc_ticket(const std::string& key) const {
    auto it = id_to_ticket_.find(key);
    if (it != id_to_ticket_.end()) return it->second;
    uint64_t ticket = next_ticket_++;
    ticket_to_id_[ticket] = key;
    id_to_ticket_[key] = ticket;
    return ticket;
}

std::string AlpacaBroker::_resolve_key(uint64_t ticket) const {
    auto it = ticket_to_id_.find(ticket);
    return it != ticket_to_id_.end() ? it->second : std::string();
}

AF_Error AlpacaBroker::get_account(AF_AccountInfo& out) const {
    try {
        JsonVal data = _get("/account");
        std::string acct_num = data["account_number"].as_str("");
        out.balance     = json_val_double(data["cash"]);
        out.equity      = json_val_double(data["equity"]);
        out.margin      = json_val_double(data["initial_margin"]);
        out.free_margin = json_val_double(data["buying_power"]);
        out.profit      = json_val_double(data["unrealized_pl"]);
        out.leverage    = 1;
        std::string ccy = data["currency"].as_str("USD");
        std::strncpy(out.currency, ccy.c_str(), sizeof(out.currency) - 1);
        out.currency[sizeof(out.currency) - 1] = '\0';
        out.login = stable_login(acct_num);
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_NOT_CONNECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error AlpacaBroker::get_tick(const char* sym, AF_Tick& out) const {
    try {
        std::string bs = _to_broker_symbol(std::string(sym));
        JsonVal data = _get(_data_url() + "/stocks/" + bs + "/quotes/latest");
        const JsonVal& q = data["quote"];
        std::strncpy(out.symbol, sym, sizeof(out.symbol) - 1);
        out.symbol[sizeof(out.symbol) - 1] = '\0';
        out.bid = json_val_double(q["bp"]);
        out.ask = json_val_double(q["ap"]);
        out.volume = json_val_double(q["bs"]);
        std::string ts = q["t"].as_str("");
        out.timestamp = ts.empty() ? static_cast<int64_t>(std::time(nullptr)) : iso_to_epoch(ts);
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error AlpacaBroker::get_symbol_info(const char* sym, AF_SymbolInfo& out) const {
    try {
        std::string bs = _to_broker_symbol(std::string(sym));
        _get("/assets/" + bs);  /* best-effort existence check */
        std::strncpy(out.name, sym, sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
        out.digits = 2; out.point = 0.01; out.contract_size = 1.0;
        out.volume_min = 1.0; out.volume_max = 10000.0; out.volume_step = 1.0;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error AlpacaBroker::get_bars(const char* sym, AF_Timeframe tf,
                                int count, AF_Bar* bars_out, int* filled) const {
    if (filled) *filled = 0;
    try {
        std::string bs = _to_broker_symbol(std::string(sym));
        JsonVal data = _get(_data_url() + "/stocks/" + bs + "/bars",
                            {{"timeframe", tf_to_alpaca(static_cast<int>(tf))},
                             {"limit", std::to_string(count)}});
        const JsonVal& bars = data["bars"];
        if (!bars.is_arr()) return AF_OK;
        int n = static_cast<int>(bars.size());
        if (n > count) n = count;
        for (int i = 0; i < n; ++i) {
            const JsonVal& b = bars[static_cast<size_t>(i)];
            AF_Bar& o = bars_out[i];
            std::memset(&o, 0, sizeof(o));
            o.timestamp = iso_to_epoch(b["t"].as_str(""));
            o.open  = json_val_double(b["o"]); o.high = json_val_double(b["h"]);
            o.low   = json_val_double(b["l"]); o.close = json_val_double(b["c"]);
            o.volume = json_val_double(b["v"]);
            af_bar_init(&o);
        }
        if (filled) *filled = n;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error AlpacaBroker::place_order(const char* sym, AF_OrderType type,
                                   double lots, double price,
                                   double sl, double tp,
                                   uint32_t magic, const char* comment,
                                   AF_Order& out) {
    try {
        auto [side, atype] = order_type_to_alpaca(type);
        std::string bs = _to_broker_symbol(std::string(sym));

        JsonVal body; body.type = JsonVal::Type::Object;
        body.obj["symbol"]        = jstr(bs);
        body.obj["qty"]           = jstr(py_float_str(lots));
        body.obj["side"]          = jstr(side);
        body.obj["type"]          = jstr(atype);
        body.obj["time_in_force"] = jstr("gtc");
        if (atype == "limit" && price != 0.0) body.obj["limit_price"] = jstr(py_float_str(price));
        if (atype == "stop"  && price != 0.0) body.obj["stop_price"]  = jstr(py_float_str(price));
        if ((comment && comment[0]) || magic) {
            std::string coid = "af-" + std::to_string(magic) + "-" + (comment ? comment : "");
            if (coid.size() > 64) coid = coid.substr(0, 64);
            body.obj["client_order_id"] = jstr(coid);
        }
        /* sl/tp deliberately NOT transmitted (plain Alpaca orders carry none). */

        JsonVal data = _post("/orders", body);
        std::string id = data["id"].as_str("");
        uint64_t ticket = _alloc_ticket(id);

        std::memset(&out, 0, sizeof(out));
        out.ticket = ticket;
        std::strncpy(out.symbol, sym, sizeof(out.symbol) - 1);
        out.type = type; out.lots = lots; out.price = price;
        out.sl = 0.0; out.tp = 0.0;  /* honest: not sent */
        out.fill_price = json_val_double(data["filled_avg_price"]);
        out.magic = magic;
        if (comment) std::strncpy(out.comment, comment, sizeof(out.comment) - 1);
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

std::vector<AF_Order> AlpacaBroker::get_orders() const {
    try {
        JsonVal data = _get("/orders", {{"status", "open"}});
        if (!data.is_arr()) return {};
        std::vector<AF_Order> result;
        for (size_t i = 0; i < data.size(); ++i) {
            const JsonVal& o = data[i];
            std::string id = o["id"].as_str("");
            AF_Order ord{};
            ord.ticket = _alloc_ticket(id);
            std::string sym = _from_broker_symbol(o["symbol"].as_str(""));
            std::strncpy(ord.symbol, sym.c_str(), sizeof(ord.symbol) - 1);
            ord.type = alpaca_to_order_type(o["side"].as_str("buy"), o["type"].as_str("market"));
            ord.lots = json_val_double(o["qty"]);
            double lp = json_val_double(o["limit_price"]);
            ord.price = lp != 0.0 ? lp : json_val_double(o["stop_price"]);
            ord.fill_price = json_val_double(o["filled_avg_price"]);
            result.push_back(ord);
        }
        return result;
    } catch (...) { return {}; }
}

AF_Error AlpacaBroker::cancel_order(uint64_t ticket) {
    std::string id = _resolve_key(ticket);
    if (id.empty()) return AF_ERR_ORDER_REJECTED;
    try { _request("DELETE", "/orders/" + id); return AF_OK; }
    catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
    catch (...) { return AF_ERR_UNKNOWN; }
}

std::vector<AF_Position> AlpacaBroker::get_positions() const {
    try {
        JsonVal data = _get("/positions");
        if (!data.is_arr()) return {};
        std::vector<AF_Position> result;
        for (size_t i = 0; i < data.size(); ++i) {
            std::string sym = _from_broker_symbol(data[i]["symbol"].as_str(""));
            uint64_t ticket = _alloc_ticket(sym);
            result.push_back(_parse_position(data[i], ticket, sym));
        }
        return result;
    } catch (...) { return {}; }
}

std::optional<AF_Position> AlpacaBroker::get_position(uint64_t ticket) const {
    std::string key = _resolve_key(ticket);
    if (key.empty()) return std::nullopt;
    try {
        std::string bs = _to_broker_symbol(key);
        JsonVal data = _get("/positions/" + bs);
        std::string sym = _from_broker_symbol(data["symbol"].as_str(key));
        return _parse_position(data, ticket, sym);
    } catch (const BrokerError&) { return std::nullopt; }
      catch (...) { return std::nullopt; }
}

AF_Position AlpacaBroker::_parse_position(const JsonVal& p, uint64_t ticket,
                                          const std::string& sym) const {
    AF_Position pos{};
    pos.ticket = ticket;
    std::strncpy(pos.symbol, sym.c_str(), sizeof(pos.symbol) - 1);
    pos.side = (p["side"].as_str("long") == "long") ? AF_DIR_LONG : AF_DIR_SHORT;
    pos.lots = json_val_double(p["qty"]);
    pos.open_price = json_val_double(p["avg_entry_price"]);
    pos.current_price = json_val_double(p["current_price"]);
    pos.sl = 0.0; pos.tp = 0.0;
    pos.profit = json_val_double(p["unrealized_pl"]);
    pos.commission = 0.0; pos.swap = 0.0; pos.magic = 0;
    return pos;
}

AF_Error AlpacaBroker::close_position(uint64_t ticket, double lots) {
    std::string key = _resolve_key(ticket);
    if (key.empty()) return AF_ERR_ORDER_REJECTED;
    try {
        std::string bs = _to_broker_symbol(key);
        std::map<std::string,std::string> params;
        if (lots > 0.0) params["qty"] = py_float_str(lots);
        _request("DELETE", "/positions/" + bs, JsonVal{}, params);
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error AlpacaBroker::modify_position(uint64_t ticket, double sl, double tp) {
    std::string key = _resolve_key(ticket);
    if (key.empty()) return AF_ERR_ORDER_REJECTED;
    /* Symbol-keyed ticket (no hyphen) = a filled position: Alpaca exposes no
     * SL/TP there. Return error WITHOUT touching the network (oracle contract). */
    if (key.find('-') == std::string::npos) return AF_ERR_ORDER_REJECTED;
    try {
        JsonVal body; body.type = JsonVal::Type::Object;
        if (sl != 0.0) body.obj["stop_price"]  = jstr(py_float_str(sl));
        if (tp != 0.0) body.obj["limit_price"] = jstr(py_float_str(tp));
        if (body.obj.empty()) return AF_OK;
        _request("PATCH", "/orders/" + key, body);
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

void register_alpaca_broker() {
    register_broker("alpaca", [](bool /*paper*/, double /*balance*/, BrokerConfig cfg)
        -> std::unique_ptr<IBroker> { return std::make_unique<AlpacaBroker>(std::move(cfg)); });
}

} /* namespace broker */
} /* namespace af */
