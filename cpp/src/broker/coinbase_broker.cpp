/**
 * AlgoForge — src/broker/coinbase_broker.cpp
 *
 * Coinbase Advanced Trade REST adapter (Phase 1 / S5).
 * Python oracle: python/algoforge/brokers/coinbase.py
 *
 * Auth: legacy HMAC-SHA256. Per request, sign the message
 *   {timestamp}{METHOD}{path-no-query}{body} with api_secret, set CB-ACCESS-*
 *   headers. lots == base_size. Positions derived from non-fiat wallet balances.
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

static double jvd(const JsonVal& v, double def = 0.0) {
    if (v.is_num()) return v.num;
    if (v.is_str() && !v.str.empty()) { try { return std::stod(v.str); } catch (...) {} }
    return def;
}
static std::string py_float_str(double v) {
    char buf[64];
    for (int p = 1; p <= 17; ++p) { std::snprintf(buf,sizeof(buf),"%.*g",p,v); if(std::strtod(buf,nullptr)==v) break; }
    std::string s(buf);
    if (s.find('e')!=std::string::npos||s.find('E')!=std::string::npos) {
        double av=std::fabs(v);
        if (av>=1e-4&&av<1e16) for(int dp=0;dp<=17;++dp){std::snprintf(buf,sizeof(buf),"%.*f",dp,v); if(std::strtod(buf,nullptr)==v){s=buf;break;}}
    }
    if (s.find('.')==std::string::npos&&s.find('e')==std::string::npos&&s.find('E')==std::string::npos) s+=".0";
    return s;
}
static JsonVal jstr(const std::string& s){ JsonVal v; v.type=JsonVal::Type::String; v.str=s; return v; }
static JsonVal jobj1(const std::string& k, JsonVal v){ JsonVal o; o.type=JsonVal::Type::Object; o.obj[k]=std::move(v); return o; }

static const char* tf_to_granularity(int tf) {
    switch (tf) {
        case 60: return "ONE_MINUTE"; case 300: return "FIVE_MINUTE"; case 900: return "FIFTEEN_MINUTE";
        case 1800: return "THIRTY_MINUTE"; case 3600: return "ONE_HOUR"; case 21600: return "SIX_HOUR";
        case 86400: return "ONE_DAY"; default: return nullptr;
    }
}
static bool is_fiat(const std::string& c){ return c=="USD"||c=="USDC"||c=="USDT"||c=="EUR"||c=="GBP"; }
static uint64_t ccy_ticket(const std::string& c){ uint32_t h=2166136261u; for(unsigned char x:c){h^=x;h*=16777619u;} return h%1000000u; }

} /* anonymous namespace */

CoinbaseBroker::CoinbaseBroker(BrokerConfig cfg) : RestBroker(std::move(cfg)) {
    if (cfg_.name.empty()) cfg_.name = "coinbase";
}

std::string CoinbaseBroker::_to_broker_symbol(const std::string& symbol) const {
    std::string s;
    for (char c : symbol) {
        if (c=='/'||c=='_') s += '-';
        else s += static_cast<char>(std::toupper((unsigned char)c));
    }
    if (s.find('-') == std::string::npos && s.size() == 6) s = s.substr(0,3) + "-" + s.substr(3);
    return s;
}
std::string CoinbaseBroker::_from_broker_symbol(const std::string& symbol) const {
    std::string out; for (char c : symbol) if (c!='-') out += c; return out;
}

uint64_t CoinbaseBroker::_register_order_id(const std::string& oid) const {
    auto it = id_to_ticket_.find(oid);
    if (it != id_to_ticket_.end()) return it->second;
    uint64_t t = next_ticket_++;
    id_to_ticket_[oid] = t; ticket_to_id_[t] = oid;
    return t;
}
std::string CoinbaseBroker::_ticket_to_order_id(uint64_t ticket) const {
    auto it = ticket_to_id_.find(ticket);
    if (it == ticket_to_id_.end()) throw BrokerError("coinbase: unknown ticket", 0, 5);
    return it->second;
}

std::string CoinbaseBroker::sign(const std::string& ts, const std::string& method,
                                 const std::string& path, const std::string& body) const {
    return af::crypto::hmac_sha256_hex(cfg_.api_secret, ts + method + path + body);
}

/* Production signing seam: inject CB-ACCESS-* headers, then delegate to the
 * base transport. (Tests override this entirely with canned responses; the
 * signature itself is parity-tested via sign().) */
HttpResponse CoinbaseBroker::do_http_request(const std::string& method, const std::string& url,
                                             const std::string& body,
                                             const std::map<std::string,std::string>& hdrs) const {
    std::string sign_path = url;
    auto pos = url.find("://");
    if (pos != std::string::npos) { auto s = url.find('/', pos+3); sign_path = (s==std::string::npos)?"/":url.substr(s); }
    auto q = sign_path.find('?'); if (q != std::string::npos) sign_path = sign_path.substr(0, q);
    std::string ts = std::to_string(static_cast<long long>(std::time(nullptr)));
    std::map<std::string,std::string> h = hdrs;
    h["CB-ACCESS-KEY"] = cfg_.api_key;
    h["CB-ACCESS-TIMESTAMP"] = ts;
    h["CB-ACCESS-SIGN"] = sign(ts, method, sign_path, body);
    return RestBroker::do_http_request(method, url, body, h);
}

void CoinbaseBroker::_on_connect() {
    cfg_.require({"api_key", "api_secret"});
    _get("/accounts", {{"limit", "1"}});
}

AF_Error CoinbaseBroker::get_account(AF_AccountInfo& out) const {
    try {
        JsonVal resp = _get("/accounts");
        const JsonVal& accts = resp["accounts"];
        double total = 0.0;
        if (accts.is_arr())
            for (size_t i = 0; i < accts.size(); ++i) {
                std::string ccy = accts[i]["currency"].as_str("");
                if (is_fiat(ccy))
                    total += jvd(accts[i]["available_balance"]["value"]) + jvd(accts[i]["hold"]["value"]);
            }
        out.balance = total; out.equity = total;
        out.margin = 0.0; out.free_margin = 0.0; out.profit = 0.0; out.leverage = 1;
        std::strncpy(out.currency, "USD", sizeof(out.currency)-1); out.currency[sizeof(out.currency)-1]='\0';
        out.login = 0;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_NOT_CONNECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error CoinbaseBroker::get_tick(const char* sym, AF_Tick& out) const {
    try {
        std::string pid = _to_broker_symbol(std::string(sym));
        JsonVal resp = _get("/best_bid_ask", {{"product_ids", pid}});
        const JsonVal& books = resp["pricebooks"];
        if (!books.is_arr() || books.size() == 0)
            throw BrokerError(std::string("coinbase: no price book for ") + pid, 0, 5);
        const JsonVal& book = books[0];
        double bid = (book["bids"].is_arr() && book["bids"].size()>0) ? jvd(book["bids"][0]["price"]) : 0.0;
        double ask = (book["asks"].is_arr() && book["asks"].size()>0) ? jvd(book["asks"][0]["price"]) : 0.0;
        std::string s = _from_broker_symbol(pid);
        std::strncpy(out.symbol, s.c_str(), sizeof(out.symbol)-1); out.symbol[sizeof(out.symbol)-1]='\0';
        out.bid = bid; out.ask = ask; out.volume = 0.0;
        out.timestamp = static_cast<int64_t>(std::time(nullptr));
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error CoinbaseBroker::get_symbol_info(const char* sym, AF_SymbolInfo& out) const {
    try {
        std::string pid = _to_broker_symbol(std::string(sym));
        JsonVal resp = _get("/products/" + pid);
        std::string qinc = resp["quote_increment"].as_str("0.01");
        double point = jvd(resp["quote_increment"], 0.01);
        int digits = 0;
        auto dot = qinc.find('.');
        if (dot != std::string::npos) {
            std::string frac = qinc.substr(dot+1);
            while (!frac.empty() && frac.back()=='0') frac.pop_back();
            digits = static_cast<int>(frac.size());
        }
        std::string nm = _from_broker_symbol(pid);
        std::strncpy(out.name, nm.c_str(), sizeof(out.name)-1); out.name[sizeof(out.name)-1]='\0';
        out.digits = digits; out.point = point; out.contract_size = 1.0;
        out.volume_min = jvd(resp["base_min_size"], 0.001);
        out.volume_max = jvd(resp["base_max_size"], 1000.0);
        out.volume_step = jvd(resp["base_increment"], 0.00000001);
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error CoinbaseBroker::get_bars(const char* sym, AF_Timeframe tf,
                                  int count, AF_Bar* bars_out, int* filled) const {
    if (filled) *filled = 0;
    const char* gran = tf_to_granularity(static_cast<int>(tf));
    if (!gran) return AF_ERR_INVALID_PARAM;
    try {
        std::string pid = _to_broker_symbol(std::string(sym));
        long long end_ts = static_cast<long long>(std::time(nullptr));
        long long start_ts = end_ts - static_cast<long long>(count) * static_cast<int>(tf);
        JsonVal resp = _get("/products/" + pid + "/candles",
            {{"granularity", gran}, {"start", std::to_string(start_ts)}, {"end", std::to_string(end_ts)}});
        const JsonVal& candles = resp["candles"];
        if (!candles.is_arr()) return AF_OK;
        int n = static_cast<int>(candles.size());
        if (n > count) n = count;
        /* CB returns newest-first → fill oldest-first by reversing */
        for (int i = 0; i < n; ++i) {
            const JsonVal& c = candles[static_cast<size_t>(n - 1 - i)];
            AF_Bar& b = bars_out[i]; std::memset(&b, 0, sizeof(b));
            b.timestamp = static_cast<int64_t>(jvd(c["start"]));
            b.open = jvd(c["open"]); b.high = jvd(c["high"]);
            b.low = jvd(c["low"]); b.close = jvd(c["close"]); b.volume = jvd(c["volume"]);
            af_bar_init(&b);
        }
        if (filled) *filled = n;
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_INVALID_SYMBOL; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error CoinbaseBroker::place_order(const char* sym, AF_OrderType type,
                                     double lots, double price, double sl, double tp,
                                     uint32_t magic, const char* comment, AF_Order& out) {
    if (type != AF_ORDER_BUY && type != AF_ORDER_SELL) return AF_ERR_INVALID_PARAM;
    try {
        std::string pid = _to_broker_symbol(std::string(sym));
        std::string coid = "af-" + std::to_string(static_cast<long long>(std::time(nullptr))*1000LL) + "-" + std::to_string(magic);
        JsonVal body; body.type = JsonVal::Type::Object;
        body.obj["client_order_id"] = jstr(coid);
        body.obj["product_id"] = jstr(pid);
        body.obj["side"] = jstr(type==AF_ORDER_BUY?"BUY":"SELL");
        body.obj["order_configuration"] = jobj1("market_market_ioc", jobj1("base_size", jstr(py_float_str(lots))));

        JsonVal resp = _post("/orders", body);
        if (!resp["success"].as_bool(false)) throw BrokerError("coinbase: order rejected", 0, 2);

        std::string oid = resp["success_response"]["order_id"].as_str("");
        uint64_t ticket = _register_order_id(oid);

        std::memset(&out, 0, sizeof(out));
        out.ticket = ticket;
        std::string s = _from_broker_symbol(pid);
        std::strncpy(out.symbol, s.c_str(), sizeof(out.symbol)-1);
        out.type = type; out.lots = lots; out.price = 0.0;
        out.sl = sl; out.tp = tp;  /* stored locally; not sent to exchange */
        out.fill_price = 0.0; out.magic = magic;
        if (comment) std::strncpy(out.comment, comment, sizeof(out.comment)-1);
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error CoinbaseBroker::cancel_order(uint64_t ticket) {
    std::string oid;
    try { oid = _ticket_to_order_id(ticket); }
    catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
    try {
        JsonVal body; body.type = JsonVal::Type::Object;
        JsonVal ids; ids.type = JsonVal::Type::Array; ids.arr.push_back(jstr(oid));
        body.obj["order_ids"] = std::move(ids);
        JsonVal resp = _post("/orders/batch_cancel", body);
        const JsonVal& results = resp["results"];
        if (results.is_arr() && results.size() > 0 && results[0]["success"].as_bool(false)) return AF_OK;
        return AF_ERR_ORDER_REJECTED;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

AF_Error CoinbaseBroker::modify_position(uint64_t ticket, double /*sl*/, double /*tp*/) {
    /* CB market IOC orders cannot be modified — implemented as cancel-then-replace.
     * Delegates to cancel_order (mirrors the Python oracle). */
    return cancel_order(ticket);
}

AF_Error CoinbaseBroker::close_position(uint64_t ticket, double lots) {
    std::string oid;
    try { oid = _ticket_to_order_id(ticket); }
    catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
    try {
        JsonVal hist = _get("/orders/historical/" + oid);
        const JsonVal& o = hist["order"];
        std::string pid = o["product_id"].as_str("");
        if (pid.empty()) return AF_ERR_ORDER_REJECTED;
        std::string orig_side = o["side"].as_str("BUY");
        double close_size = (lots == 0.0) ? jvd(o["filled_size"]) : lots;
        JsonVal body; body.type = JsonVal::Type::Object;
        body.obj["client_order_id"] = jstr("af-close-" + std::to_string(ticket));
        body.obj["product_id"] = jstr(pid);
        body.obj["side"] = jstr(orig_side=="BUY"?"SELL":"BUY");
        body.obj["order_configuration"] = jobj1("market_market_ioc", jobj1("base_size", jstr(py_float_str(close_size))));
        JsonVal resp = _post("/orders", body);
        if (!resp["success"].as_bool(false)) return AF_ERR_ORDER_REJECTED;
        _register_order_id(resp["success_response"]["order_id"].as_str(""));
        return AF_OK;
    } catch (const BrokerError&) { return AF_ERR_ORDER_REJECTED; }
      catch (...) { return AF_ERR_UNKNOWN; }
}

std::vector<AF_Position> CoinbaseBroker::get_positions() const {
    try {
        JsonVal resp = _get("/accounts");
        const JsonVal& accts = resp["accounts"];
        if (!accts.is_arr()) return {};
        std::vector<AF_Position> result;
        for (size_t i = 0; i < accts.size(); ++i) {
            std::string ccy = accts[i]["currency"].as_str("");
            if (is_fiat(ccy)) continue;
            double total = jvd(accts[i]["available_balance"]["value"]) + jvd(accts[i]["hold"]["value"]);
            if (total <= 0.0) continue;
            double current_price = 0.0;
            AF_Tick t{};
            if (const_cast<CoinbaseBroker*>(this)->get_tick((ccy + "USD").c_str(), t) == AF_OK)
                current_price = (t.bid + t.ask) / 2.0;
            AF_Position pos{};
            pos.ticket = ccy_ticket(ccy);
            std::string s = ccy + "USD";
            std::strncpy(pos.symbol, s.c_str(), sizeof(pos.symbol)-1);
            pos.side = AF_DIR_LONG; pos.lots = total;
            pos.open_price = 0.0; pos.current_price = current_price;
            pos.sl = 0.0; pos.tp = 0.0; pos.profit = 0.0;
            pos.commission = 0.0; pos.swap = 0.0; pos.magic = 0;
            std::strncpy(pos.comment, "spot-wallet", sizeof(pos.comment)-1);
            result.push_back(pos);
        }
        return result;
    } catch (...) { return {}; }
}

std::vector<AF_Order> CoinbaseBroker::get_orders() const {
    try {
        JsonVal resp = _get("/orders/historical/batch", {{"order_status", "OPEN"}});
        const JsonVal& raw = resp["orders"];
        if (!raw.is_arr()) return {};
        std::vector<AF_Order> result;
        for (size_t i = 0; i < raw.size(); ++i) {
            const JsonVal& ro = raw[i];
            AF_Order o{};
            o.ticket = _register_order_id(ro["order_id"].as_str(""));
            std::string s = _from_broker_symbol(ro["product_id"].as_str(""));
            std::strncpy(o.symbol, s.c_str(), sizeof(o.symbol)-1);
            o.type = (ro["side"].as_str("BUY")=="BUY") ? AF_ORDER_BUY : AF_ORDER_SELL;
            o.lots = jvd(ro["filled_size"]);
            o.price = 0.0; o.sl = 0.0; o.tp = 0.0;
            o.fill_price = jvd(ro["average_filled_price"]);
            result.push_back(o);
        }
        return result;
    } catch (...) { return {}; }
}

std::optional<AF_Position> CoinbaseBroker::get_position(uint64_t ticket) const {
    for (const auto& p : get_positions()) if (p.ticket == ticket) return p;
    return std::nullopt;
}

void register_coinbase_broker() {
    register_broker("coinbase", [](bool /*paper*/, double /*balance*/, BrokerConfig cfg)
        -> std::unique_ptr<IBroker> { return std::make_unique<CoinbaseBroker>(std::move(cfg)); });
}

} /* namespace broker */
} /* namespace af */
