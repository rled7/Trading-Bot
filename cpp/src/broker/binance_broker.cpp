/**
 * AlgoForge — src/broker/binance_broker.cpp
 *
 * Binance Spot REST adapter (Phase 1 / S5).
 * Python oracle: python/algoforge/brokers/binance.py
 *
 * Auth: X-MBX-APIKEY header + HMAC-SHA256 signature over the urlencoded query
 * string on SIGNED endpoints. lots == base-asset quantity. Spot has no positions
 * server-side, so get_positions derives LONG positions from non-zero balances.
 */

#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"
#include "core/hmac_sha256.hpp"

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

/* Python str(float): shortest round-trip, positional, trailing .0 if integral. */
static std::string py_float_str(double v) {
    char buf[64];
    for (int p = 1; p <= 17; ++p) { std::snprintf(buf, sizeof(buf), "%.*g", p, v); if (std::strtod(buf,nullptr)==v) break; }
    std::string s(buf);
    if (s.find('e')!=std::string::npos || s.find('E')!=std::string::npos) {
        double av=std::fabs(v);
        if (av>=1e-4 && av<1e16) for (int dp=0; dp<=17; ++dp){ std::snprintf(buf,sizeof(buf),"%.*f",dp,v); if(std::strtod(buf,nullptr)==v){s=buf;break;} }
    }
    if (s.find('.')==std::string::npos && s.find('e')==std::string::npos && s.find('E')==std::string::npos) s += ".0";
    return s;
}

/* urlencode a value (quote_plus-style: unreserved A-Za-z0-9 _.-~ pass through). */
static std::string urlenc(const std::string& s) {
    static const char* hx = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'||c=='~')
            out += static_cast<char>(c);
        else if (c==' ') out += '+';
        else { out += '%'; out += hx[c>>4]; out += hx[c&0xf]; }
    }
    return out;
}

static const char* tf_to_interval(int tf) {
    switch (tf) {
        case 15: return "15s"; case 60: return "1m"; case 300: return "5m";
        case 900: return "15m"; case 1800: return "30m"; case 3600: return "1h";
        case 14400: return "4h"; case 86400: return "1d"; case 604800: return "1w";
        default: return nullptr;
    }
}

static bool is_stable(const std::string& a, bool incl_bnb) {
    if (a=="USDT"||a=="BUSD"||a=="USDC"||a=="USD"||a=="FDUSD") return true;
    return incl_bnb && a=="BNB";
}

static uint64_t pseudo_ticket(const std::string& asset) {
    uint32_t h = 2166136261u;
    for (unsigned char c : asset) { h ^= c; h *= 16777619u; }
    return static_cast<uint64_t>(h % 1000000000u);
}

} /* anonymous namespace */

BinanceBroker::BinanceBroker(BrokerConfig cfg) : RestBroker(std::move(cfg)) {
    if (cfg_.name.empty()) cfg_.name = "binance";
}

std::map<std::string,std::string> BinanceBroker::_auth_headers() const {
    return {{"X-MBX-APIKEY", cfg_.api_key}};
}

std::string BinanceBroker::_to_broker_symbol(const std::string& s) const {
    std::string out;
    for (char c : s) { if (c=='/'||c=='_') continue; out += static_cast<char>(std::toupper((unsigned char)c)); }
    return out;
}

std::string BinanceBroker::hmac_sha256_hex(const std::string& key, const std::string& msg) const {
    return af::crypto::hmac_sha256_hex(key, msg);
}

std::map<std::string,std::string>
BinanceBroker::signed_params(std::map<std::string,std::string> params, long long timestamp_ms) const {
    if (timestamp_ms == 0)
        timestamp_ms = static_cast<long long>(std::time(nullptr)) * 1000LL;
    params["timestamp"]  = std::to_string(timestamp_ms);
    params["recvWindow"] = "5000";
    /* query over the params in (std::map) key order — the exact bytes we sign+send */
    std::string query;
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (it != params.begin()) query += "&";
        query += it->first + "=" + urlenc(it->second);
    }
    params["signature"] = af::crypto::hmac_sha256_hex(cfg_.api_secret, query);
    return params;
}

void BinanceBroker::_on_connect() {
    cfg_.require({"api_key", "api_secret"});
    _get("/api/v3/ping");
    _get("/api/v3/account", signed_params({}));
}

AF_Error BinanceBroker::get_account(AF_AccountInfo& out) const {
    try {
        JsonVal data = _get("/api/v3/account", signed_params({}));
        const JsonVal& bals = data["balances"];
        double total = 0.0;
        if (bals.is_arr())
            for (size_t i = 0; i < bals.size(); ++i) {
                std::string a = bals[i]["asset"].as_str("");
                if (is_stable(a, false))
                    total += json_val_double(bals[i]["free"]) + json_val_double(bals[i]["locked"]);
            }
        out.balance = total; out.equity = total;
        out.margin = 0.0; out.free_margin = 0.0; out.profit = 0.0; out.leverage = 1;
        std::strncpy(out.currency, "USDT", sizeof(out.currency)-1);
        out.currency[sizeof(out.currency)-1] = '\0';
        out.login = 0;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_NOT_CONNECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error BinanceBroker::get_tick(const char* sym, AF_Tick& out) const {
    try {
        std::string bs = _to_broker_symbol(std::string(sym));
        JsonVal data = _get("/api/v3/ticker/bookTicker", {{"symbol", bs}});
        std::strncpy(out.symbol, sym, sizeof(out.symbol)-1); out.symbol[sizeof(out.symbol)-1]='\0';
        out.bid = json_val_double(data["bidPrice"]);
        out.ask = json_val_double(data["askPrice"]);
        out.volume = 0.0;
        out.timestamp = static_cast<int64_t>(std::time(nullptr)) * 1000LL;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error BinanceBroker::get_symbol_info(const char* sym, AF_SymbolInfo& out) const {
    try {
        std::string bs = _to_broker_symbol(std::string(sym));
        JsonVal data = _get("/api/v3/exchangeInfo", {{"symbol", bs}});
        const JsonVal& symbols = data["symbols"];
        if (!symbols.is_arr() || symbols.size() == 0)
            throw BrokerError(std::string("binance: symbol ") + bs + " not found");
        const JsonVal& info = symbols[0];
        int digits = static_cast<int>(info["quotePrecision"].as_int(8));
        double point = std::pow(10.0, -digits);
        double vmin=0, vmax=0, vstep=0;
        const JsonVal& filters = info["filters"];
        if (filters.is_arr())
            for (size_t i = 0; i < filters.size(); ++i) {
                std::string ft = filters[i]["filterType"].as_str("");
                if (ft == "LOT_SIZE") {
                    vmin = json_val_double(filters[i]["minQty"]);
                    vmax = json_val_double(filters[i]["maxQty"]);
                    vstep = json_val_double(filters[i]["stepSize"]);
                } else if (ft == "PRICE_FILTER") {
                    double tick = json_val_double(filters[i]["tickSize"]);
                    if (tick > 0) { digits = std::max(0, (int)std::lround(-std::log10(tick))); point = tick; }
                }
            }
        std::strncpy(out.name, sym, sizeof(out.name)-1); out.name[sizeof(out.name)-1]='\0';
        out.digits = digits; out.point = point; out.contract_size = 1.0;
        out.volume_min = vmin; out.volume_max = vmax; out.volume_step = vstep;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error BinanceBroker::get_bars(const char* sym, AF_Timeframe tf,
                                 int count, AF_Bar* bars_out, int* filled) const {
    if (filled) *filled = 0;
    const char* interval = tf_to_interval(static_cast<int>(tf));
    if (!interval) return AF_ERR_INVALID_PARAM;
    try {
        std::string bs = _to_broker_symbol(std::string(sym));
        JsonVal data = _get("/api/v3/klines",
            {{"symbol", bs}, {"interval", interval}, {"limit", std::to_string(count)}});
        if (!data.is_arr()) return AF_OK;
        int n = static_cast<int>(data.size());
        if (n > count) n = count;
        for (int i = 0; i < n; ++i) {
            const JsonVal& k = data[static_cast<size_t>(i)];
            AF_Bar& b = bars_out[i]; std::memset(&b, 0, sizeof(b));
            b.timestamp = static_cast<int64_t>(json_val_double(k[0]));
            b.open = json_val_double(k[1]); b.high = json_val_double(k[2]);
            b.low  = json_val_double(k[3]); b.close = json_val_double(k[4]);
            b.volume = json_val_double(k[5]);
            af_bar_init(&b);
        }
        if (filled) *filled = n;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error BinanceBroker::place_order(const char* sym, AF_OrderType type,
                                    double lots, double price, double sl, double tp,
                                    uint32_t magic, const char* comment, AF_Order& out) {
    if (type != AF_ORDER_BUY && type != AF_ORDER_SELL) return AF_ERR_INVALID_PARAM;
    try {
        std::string bs = _to_broker_symbol(std::string(sym));
        std::map<std::string,std::string> p = {
            {"symbol", bs}, {"side", type==AF_ORDER_BUY?"BUY":"SELL"},
            {"type", "MARKET"}, {"quantity", py_float_str(lots)},
            {"newOrderRespType", "RESULT"}};
        JsonVal data = _request("POST", "/api/v3/order", JsonVal{}, signed_params(p));

        double fill_price = 0.0;
        const JsonVal& fills = data["fills"];
        if (fills.is_arr() && fills.size() > 0) fill_price = json_val_double(fills[0]["price"]);
        if (fill_price == 0.0) {
            double executed = json_val_double(data["executedQty"]);
            double cumQuote = json_val_double(data["cummulativeQuoteQty"]);
            if (executed > 0.0) fill_price = cumQuote / executed;
        }
        std::memset(&out, 0, sizeof(out));
        out.ticket = static_cast<uint64_t>(data["orderId"].as_int(0));
        std::strncpy(out.symbol, sym, sizeof(out.symbol)-1);
        out.type = type;
        out.lots = data.has("executedQty") ? json_val_double(data["executedQty"]) : lots;
        out.price = price; out.sl = 0.0; out.tp = 0.0;
        out.fill_price = fill_price; out.magic = magic;
        if (comment) std::strncpy(out.comment, comment, sizeof(out.comment)-1);
        (void)sl; (void)tp;  /* Spot MARKET orders carry no native SL/TP */
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error BinanceBroker::cancel_order(uint64_t ticket) {
    try {
        JsonVal data = _get("/api/v3/openOrders", signed_params({}));
        if (data.is_arr())
            for (size_t i = 0; i < data.size(); ++i)
                if (static_cast<uint64_t>(data[i]["orderId"].as_int(-1)) == ticket) {
                    std::string symbol = data[i]["symbol"].as_str("");
                    _request("DELETE", "/api/v3/order", JsonVal{},
                             signed_params({{"symbol", symbol}, {"orderId", std::to_string(ticket)}}));
                    return AF_OK;
                }
        return AF_ERR_ORDER_REJECTED;  /* not found */
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error BinanceBroker::close_position(uint64_t ticket, double lots) {
    if (lots <= 0.0) return AF_ERR_INVALID_PARAM;
    try {
        JsonVal data = _get("/api/v3/openOrders", signed_params({}));
        if (data.is_arr())
            for (size_t i = 0; i < data.size(); ++i)
                if (static_cast<uint64_t>(data[i]["orderId"].as_int(-1)) == ticket) {
                    std::string symbol = data[i]["symbol"].as_str("");
                    _request("DELETE", "/api/v3/order", JsonVal{},
                             signed_params({{"symbol", symbol}, {"orderId", std::to_string(ticket)}}));
                    return AF_OK;
                }
        return AF_ERR_ORDER_REJECTED;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error BinanceBroker::modify_position(uint64_t /*ticket*/, double /*sl*/, double /*tp*/) {
    /* Spot has no modify endpoint; SL/TP not supported on filled MARKET orders.
     * No-op returning success (mirrors the Python oracle). */
    return AF_OK;
}

std::vector<AF_Position> BinanceBroker::get_positions() const {
    try {
        JsonVal data = _get("/api/v3/account", signed_params({}));
        const JsonVal& bals = data["balances"];
        if (!bals.is_arr()) return {};
        std::vector<AF_Position> result;
        for (size_t i = 0; i < bals.size(); ++i) {
            std::string asset = bals[i]["asset"].as_str("");
            if (is_stable(asset, true)) continue;
            double qty = json_val_double(bals[i]["free"]) + json_val_double(bals[i]["locked"]);
            if (qty <= 0.0) continue;
            AF_Position pos{};
            pos.ticket = pseudo_ticket(asset);
            std::string sym = asset + "USDT";
            std::strncpy(pos.symbol, sym.c_str(), sizeof(pos.symbol)-1);
            pos.side = AF_DIR_LONG; pos.lots = qty;
            pos.open_price = 0.0; pos.current_price = 0.0;
            pos.sl = 0.0; pos.tp = 0.0; pos.profit = 0.0;
            pos.commission = 0.0; pos.swap = 0.0; pos.magic = 0;
            result.push_back(pos);
        }
        return result;
    } catch (...) { return {}; }
}

std::vector<AF_Order> BinanceBroker::get_orders() const {
    try {
        JsonVal data = _get("/api/v3/openOrders", signed_params({}));
        if (!data.is_arr()) return {};
        std::vector<AF_Order> result;
        for (size_t i = 0; i < data.size(); ++i) {
            const JsonVal& o = data[i];
            AF_Order ord{};
            ord.ticket = static_cast<uint64_t>(o["orderId"].as_int(0));
            std::string sym = _from_broker_symbol(o["symbol"].as_str(""));
            std::strncpy(ord.symbol, sym.c_str(), sizeof(ord.symbol)-1);
            ord.type = (o["side"].as_str("")=="BUY") ? AF_ORDER_BUY : AF_ORDER_SELL;
            ord.lots = json_val_double(o["origQty"]);
            ord.price = json_val_double(o["price"]);
            ord.sl = 0.0; ord.tp = 0.0;
            ord.fill_price = json_val_double(o["cummulativeQuoteQty"]);
            result.push_back(ord);
        }
        return result;
    } catch (...) { return {}; }
}

std::optional<AF_Position> BinanceBroker::get_position(uint64_t ticket) const {
    for (const auto& p : get_positions()) if (p.ticket == ticket) return p;
    return std::nullopt;
}

void register_binance_broker() {
    register_broker("binance", [](bool /*paper*/, double /*balance*/, BrokerConfig cfg)
        -> std::unique_ptr<IBroker> { return std::make_unique<BinanceBroker>(std::move(cfg)); });
}

} /* namespace broker */
} /* namespace af */
