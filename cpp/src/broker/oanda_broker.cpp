/**
 * AlgoForge — src/broker/oanda_broker.cpp
 *
 * OANDA v20 REST adapter (Phase 1 / S5).
 * Python oracle: python/algoforge/brokers/oanda.py
 *
 * Design notes:
 *  - OANDA returns nearly all numerics as QUOTED JSON strings ("1.10990", "10000").
 *    json_val_double()/json_val_int() helpers transparently parse both String and
 *    Number JsonVal — analogous to Python's float()/int() which accept both.
 *  - Nested JsonVal bodies (order spec with stopLossOnFill etc.) are built with
 *    jstr()/jobj() helpers since json_from_map() is flat string→string only.
 *  - All trading methods return AF_Error (AF_OK on success, non-OK on error);
 *    exceptions from _get/_post/_request are caught and mapped.
 */

#include "broker/rest_broker.hpp"
#include "broker/broker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace af {
namespace broker {

/* =========================================================================
 * Internal helpers
 * =========================================================================*/

namespace {

/* ---------------------------------------------------------------------------
 * Numeric coercion helpers
 *
 * OANDA sends most numbers as quoted strings ("10000.00", "1.10990", etc.).
 * JsonVal::as_double/as_int return the default on a String type, so we must
 * parse strings ourselves — mirroring Python's transparent float()/int().
 * --------------------------------------------------------------------------- */
static double json_val_double(const JsonVal& v, double def = 0.0) {
    if (v.is_num()) return v.num;
    if (v.is_str() && !v.str.empty()) {
        try { return std::stod(v.str); } catch (...) {}
    }
    return def;
}

static long long json_val_int(const JsonVal& v, long long def = 0LL) {
    if (v.is_num()) return static_cast<long long>(v.num);
    if (v.is_str() && !v.str.empty()) {
        try { return std::stoll(v.str); } catch (...) {}
    }
    return def;
}

/* ---------------------------------------------------------------------------
 * JsonVal builders for nested request bodies
 * --------------------------------------------------------------------------- */

/* Build a JsonVal string node. */
static JsonVal jstr(const std::string& s) {
    JsonVal v;
    v.type = JsonVal::Type::String;
    v.str  = s;
    return v;
}

/* Build a JsonVal object from a flat initialiser list of key/value JsonVal pairs. */
static JsonVal jobj(std::initializer_list<std::pair<std::string, JsonVal>> kvs) {
    JsonVal v;
    v.type = JsonVal::Type::Object;
    for (auto& [k, val] : kvs) v.obj[k] = val;
    return v;
}

/* ---------------------------------------------------------------------------
 * Format a double with exactly 5 decimal places (for OANDA price fields).
 * Mirrors Python f"{price:.5f}".
 * --------------------------------------------------------------------------- */
static std::string fmt5(double d) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.5f", d);
    return std::string(buf);
}

/* ---------------------------------------------------------------------------
 * Timeframe map: seconds → OANDA granularity string.
 * Mirrors python/algoforge/brokers/oanda.py :: _TF_MAP
 * --------------------------------------------------------------------------- */
static const char* tf_to_granularity(int tf_seconds) {
    switch (tf_seconds) {
        case 5:       return "S5";
        case 10:      return "S10";
        case 15:      return "S15";
        case 30:      return "S30";
        case 60:      return "M1";
        case 120:     return "M2";
        case 180:     return "M4";
        case 300:     return "M5";
        case 600:     return "M10";
        case 900:     return "M15";
        case 1200:    return "M30";
        case 1800:    return "M30";
        case 3600:    return "H1";
        case 7200:    return "H2";
        case 10800:   return "H3";
        case 14400:   return "H4";
        case 21600:   return "H6";
        case 28800:   return "H8";
        case 43200:   return "H12";
        case 86400:   return "D";
        case 604800:  return "W";
        case 2592000: return "M";
        default:      return "M1";
    }
}

/* FX default contract size (units per lot). */
static constexpr double FX_CONTRACT_SIZE = 100000.0;

} /* anonymous namespace */

/* =========================================================================
 * oanda_parse_ts — parse OANDA RFC3339 nanosecond timestamp → unix epoch.
 *
 * Mirrors Python _parse_ts() in oanda.py. Handles:
 *   "2016-10-05T14:14:00.000000000Z"
 *   "2024-01-15T10:30:00.123456Z"
 *   "2024-01-15T10:30:00Z"
 *
 * External linkage so test file can forward-declare and call it directly
 * to port the TestParseTs class.
 * =========================================================================*/
int64_t oanda_parse_ts(const std::string& ts_str) {
    if (ts_str.empty()) return 0;

    /* Strip trailing 'Z' then truncate fractional seconds to at most 6 digits.
     * We only need whole-second precision for tests (and the parsed int value). */
    std::string s = ts_str;
    if (!s.empty() && s.back() == 'Z') s.pop_back();

    /* Find and trim fractional seconds */
    auto dot_pos = s.find('.');
    if (dot_pos != std::string::npos) {
        /* Keep only integer seconds part for simplicity — tests only require >0 */
        s = s.substr(0, dot_pos);
    }

    /* Parse: YYYY-MM-DDTHH:MM:SS */
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d",
                    &year, &month, &day, &hour, &min, &sec) < 6) {
        return 0;
    }

    /* Build struct tm and convert to UTC epoch using timegm (POSIX, darwin). */
    struct tm t{};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;
    t.tm_isdst = 0;

#if defined(_WIN32)
    /* _mkgmtime is the Windows equivalent of timegm */
    time_t epoch = _mkgmtime(&t);
#else
    time_t epoch = timegm(&t);
#endif
    return (epoch == (time_t)-1) ? 0 : static_cast<int64_t>(epoch);
}

/* =========================================================================
 * OandaBroker constructor
 * =========================================================================*/
OandaBroker::OandaBroker(BrokerConfig cfg)
    : RestBroker(std::move(cfg)) {
    /* Ensure the config name is set so BrokerError messages are informative. */
    if (cfg_.name.empty()) cfg_.name = "oanda";
}

/* =========================================================================
 * Protected overrides
 * =========================================================================*/

void OandaBroker::_on_connect() {
    /* Mirrors Python OandaBroker._on_connect():
     *   self._config.require("api_key", "account_id")
     */
    cfg_.require({"api_key", "account_id"});
}

std::map<std::string, std::string> OandaBroker::_auth_headers() const {
    /* Mirrors Python:
     *   return {"Authorization": f"Bearer {self._config.api_key}"}
     */
    return {{"Authorization", "Bearer " + cfg_.api_key}};
}

std::string OandaBroker::_to_broker_symbol(const std::string& s) const {
    /* Mirrors Python:
     *   s = symbol.replace("/", "").replace("_", "")
     *   return f"{s[:3]}_{s[3:]}" if len(s) == 6 else symbol
     * Note: fallback uses original symbol, not the stripped form.
     */
    std::string cleaned;
    cleaned.reserve(s.size());
    for (char c : s) {
        if (c != '/' && c != '_') cleaned += c;
    }
    if (cleaned.size() == 6) {
        return cleaned.substr(0, 3) + "_" + cleaned.substr(3);
    }
    return s;  /* original symbol, not cleaned */
}

std::string OandaBroker::_from_broker_symbol(const std::string& s) const {
    /* Mirrors Python:
     *   return symbol.replace("_", "")
     */
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != '_') out += c;
    }
    return out;
}

/* =========================================================================
 * Private helpers
 * =========================================================================*/

std::string OandaBroker::_acct_path(const std::string& sub) const {
    /* Mirrors Python:
     *   base = f"accounts/{self._config.account_id}"
     *   return f"{base}/{path.lstrip('/')}" if path else base
     */
    std::string base = "accounts/" + cfg_.account_id;
    if (sub.empty()) return base;
    /* Strip leading slashes from sub */
    size_t start = sub.find_first_not_of('/');
    if (start == std::string::npos) return base;
    return base + "/" + sub.substr(start);
}

/* =========================================================================
 * get_account
 * =========================================================================*/
AF_Error OandaBroker::get_account(AF_AccountInfo& out) const {
    /* Mirrors Python OandaBroker.get_account():
     *   data = self._get(self._acct_path("summary"))
     *   acct = data["account"]
     *   margin_rate = float(acct.get("marginRate", "0.02") or "0.02")
     *   leverage = round(1.0 / margin_rate) if margin_rate else 100
     */
    try {
        JsonVal data = _get(_acct_path("summary"));
        const JsonVal& acct = data["account"];

        double margin_rate = json_val_double(acct["marginRate"], 0.02);
        if (margin_rate == 0.0) margin_rate = 0.02;  /* guard div-by-zero */

        long long leverage = static_cast<long long>(std::llround(1.0 / margin_rate));

        out.balance     = json_val_double(acct["balance"]);
        /* NAV defaults to balance if missing */
        out.equity      = acct.has("NAV")
                          ? json_val_double(acct["NAV"])
                          : json_val_double(acct["balance"]);
        out.margin      = json_val_double(acct["marginUsed"]);
        out.free_margin = json_val_double(acct["marginAvailable"]);
        out.profit      = json_val_double(acct["unrealizedPL"]);
        out.leverage    = static_cast<uint32_t>(leverage);

        /* currency — string field, real string in JSON */
        std::string ccy = acct["currency"].as_str("USD");
        std::strncpy(out.currency, ccy.c_str(), sizeof(out.currency) - 1);
        out.currency[sizeof(out.currency) - 1] = '\0';

        out.login = 0;  /* OANDA account id is a dashed string; use 0 as numeric login */

        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_NOT_CONNECTED;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * get_tick
 * =========================================================================*/
AF_Error OandaBroker::get_tick(const char* sym, AF_Tick& out) const {
    /* Mirrors Python OandaBroker.get_tick() */
    try {
        std::string instrument = _to_broker_symbol(std::string(sym));
        JsonVal data = _get(_acct_path("pricing"),
                            {{"instruments", instrument}});

        const JsonVal& prices = data["prices"];
        if (!prices.is_arr() || prices.size() == 0) {
            throw BrokerError(std::string("oanda: no pricing data for ") + instrument);
        }
        const JsonVal& price = prices[0];

        /* bids/asks are arrays of objects with "price" string fields */
        const JsonVal& bids = price["bids"];
        const JsonVal& asks = price["asks"];

        double bid = 0.0, ask = 0.0;
        if (bids.is_arr() && bids.size() > 0) {
            bid = json_val_double(bids[0]["price"]);
        }
        if (asks.is_arr() && asks.size() > 0) {
            ask = json_val_double(asks[0]["price"]);
        }

        std::string ts_str = price["time"].as_str("");
        int64_t timestamp  = ts_str.empty() ? 0 : oanda_parse_ts(ts_str);

        /* symbol field: use original input symbol, not broker symbol */
        std::strncpy(out.symbol, sym, sizeof(out.symbol) - 1);
        out.symbol[sizeof(out.symbol) - 1] = '\0';
        out.bid       = bid;
        out.ask       = ask;
        out.volume    = 0.0;   /* OANDA pricing endpoint does not provide volume */
        out.timestamp = timestamp;

        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_INVALID_SYMBOL;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * get_symbol_info
 * =========================================================================*/
AF_Error OandaBroker::get_symbol_info(const char* sym, AF_SymbolInfo& out) const {
    /* Mirrors Python OandaBroker.get_symbol_info() */
    try {
        std::string instrument = _to_broker_symbol(std::string(sym));
        JsonVal data = _get(_acct_path("instruments"),
                            {{"instruments", instrument}});

        const JsonVal& instruments = data["instruments"];
        if (!instruments.is_arr() || instruments.size() == 0) {
            throw BrokerError(std::string("oanda: no instrument data for ") + instrument);
        }
        const JsonVal& inst = instruments[0];

        /* displayPrecision is a number in the JSON response */
        int digits = static_cast<int>(inst["displayPrecision"].as_int(5));
        double point = std::pow(10.0, -digits);

        /* minimumTradeSize / maximumOrderUnits are strings */
        double min_units = json_val_double(inst["minimumTradeSize"], 1.0);
        double max_units = json_val_double(inst["maximumOrderUnits"], 100000000.0);

        double contract_size = FX_CONTRACT_SIZE;
        double volume_min  = min_units / contract_size;
        double volume_max  = max_units / contract_size;
        double volume_step = volume_min;  /* smallest increment == minimum */

        std::strncpy(out.name, sym, sizeof(out.name) - 1);
        out.name[sizeof(out.name) - 1] = '\0';
        out.digits        = digits;
        out.point         = point;
        out.contract_size = contract_size;
        out.volume_min    = volume_min;
        out.volume_max    = volume_max;
        out.volume_step   = volume_step;

        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_INVALID_SYMBOL;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * get_bars
 * =========================================================================*/
AF_Error OandaBroker::get_bars(const char* sym, AF_Timeframe tf,
                                int count, AF_Bar* bars_out, int* filled) const {
    /* Mirrors Python OandaBroker.get_bars() */
    if (filled) *filled = 0;
    try {
        std::string instrument  = _to_broker_symbol(std::string(sym));
        const char* granularity = tf_to_granularity(static_cast<int>(tf));
        std::string count_str   = std::to_string(count);

        /* Note: path is NOT account-scoped (instruments/{sym}/candles is top-level) */
        JsonVal data = _get("instruments/" + instrument + "/candles",
                            {{"granularity", granularity},
                             {"count",       count_str},
                             {"price",       "M"}});   /* midpoint candles */

        const JsonVal& candles = data["candles"];
        if (!candles.is_arr()) {
            return AF_OK;  /* empty result */
        }

        int n = static_cast<int>(candles.size());
        if (n > count) n = count;

        for (int i = 0; i < n; ++i) {
            const JsonVal& c   = candles[static_cast<size_t>(i)];
            const JsonVal& mid = c["mid"];

            AF_Bar& b   = bars_out[i];
            std::memset(&b, 0, sizeof(b));
            b.timestamp = oanda_parse_ts(c["time"].as_str(""));
            b.open      = json_val_double(mid["o"]);
            b.high      = json_val_double(mid["h"]);
            b.low       = json_val_double(mid["l"]);
            b.close     = json_val_double(mid["c"]);
            b.volume    = json_val_double(c["volume"]);
            af_bar_init(&b);
        }

        if (filled) *filled = n;
        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_INVALID_SYMBOL;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * place_order
 * =========================================================================*/
AF_Error OandaBroker::place_order(const char* sym, AF_OrderType type,
                                   double lots, double price,
                                   double sl, double tp,
                                   uint32_t magic, const char* comment,
                                   AF_Order& out) {
    /* Mirrors Python OandaBroker.place_order() */
    try {
        std::string instrument = _to_broker_symbol(std::string(sym));

        /* Positive units = buy, negative = sell.
         * Mirrors: units_int = round(lots * _FX_CONTRACT_SIZE) */
        long long units_int = static_cast<long long>(
            std::llround(lots * FX_CONTRACT_SIZE));
        if (type == AF_ORDER_SELL || type == AF_ORDER_SELL_LIMIT ||
            type == AF_ORDER_SELL_STOP) {
            units_int = -units_int;
        }
        std::string units_str = std::to_string(units_int);

        /* Build the order spec object */
        JsonVal order_spec;
        order_spec.type = JsonVal::Type::Object;

        auto set_str = [&](const std::string& k, const std::string& v) {
            order_spec.obj[k] = jstr(v);
        };

        set_str("instrument",    instrument);
        set_str("units",         units_str);
        set_str("positionFill",  "DEFAULT");

        if (type == AF_ORDER_BUY || type == AF_ORDER_SELL) {
            set_str("type",        "MARKET");
            set_str("timeInForce", "FOK");
        } else if (type == AF_ORDER_BUY_LIMIT || type == AF_ORDER_SELL_LIMIT) {
            set_str("type",        "LIMIT");
            set_str("price",       fmt5(price));
            set_str("timeInForce", "GTC");
        } else if (type == AF_ORDER_BUY_STOP || type == AF_ORDER_SELL_STOP) {
            set_str("type",        "STOP");
            set_str("price",       fmt5(price));
            set_str("timeInForce", "GTC");
        } else {
            throw BrokerError(std::string("oanda: unsupported order type"));
        }

        /* Attach SL/TP if nonzero */
        if (sl > 0.0) {
            order_spec.obj["stopLossOnFill"] =
                jobj({{"price", jstr(fmt5(sl))}, {"timeInForce", jstr("GTC")}});
        }
        if (tp > 0.0) {
            order_spec.obj["takeProfitOnFill"] =
                jobj({{"price", jstr(fmt5(tp))}, {"timeInForce", jstr("GTC")}});
        }

        /* Wrap in {"order": ...} */
        JsonVal body;
        body.type = JsonVal::Type::Object;
        body.obj["order"] = std::move(order_spec);

        JsonVal data = _post(_acct_path("orders"), body);

        /* Parse response */
        uint64_t ticket     = 0;
        double   fill_price = 0.0;

        const JsonVal& fill_tx   = data["orderFillTransaction"];
        const JsonVal& create_tx = data["orderCreateTransaction"];

        if (!fill_tx.is_null()) {
            const JsonVal& trade_opened = fill_tx["tradeOpened"];
            ticket     = static_cast<uint64_t>(json_val_int(trade_opened["tradeID"]));
            fill_price = json_val_double(fill_tx["price"]);
        } else if (!create_tx.is_null()) {
            ticket     = static_cast<uint64_t>(json_val_int(create_tx["id"]));
            fill_price = 0.0;
        } else {
            throw BrokerError("oanda: unexpected order response", 0, 2);
        }

        /* Populate output */
        std::memset(&out, 0, sizeof(out));
        out.ticket     = ticket;
        std::strncpy(out.symbol, sym, sizeof(out.symbol) - 1);
        out.type       = type;
        out.lots       = lots;
        out.price      = price;
        out.sl         = sl;
        out.tp         = tp;
        out.fill_price = fill_price;
        out.magic      = magic;
        if (comment) {
            std::strncpy(out.comment, comment, sizeof(out.comment) - 1);
        }

        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_ORDER_REJECTED;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * close_position
 * =========================================================================*/
AF_Error OandaBroker::close_position(uint64_t ticket, double lots) {
    /* Mirrors Python OandaBroker.close_position():
     *   path = self._acct_path(f"trades/{ticket}/close")
     *   body = {"units": str(units)} if lots > 0 else None
     *   try: self._request("PUT", path, body=body); return 0
     *   except BrokerError: return 1
     */
    try {
        std::string path = _acct_path("trades/" + std::to_string(ticket) + "/close");
        JsonVal body;
        if (lots > 0.0) {
            long long units = static_cast<long long>(std::llround(lots * FX_CONTRACT_SIZE));
            body = json_from_map({{"units", std::to_string(units)}});
        }
        _request("PUT", path, body);
        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_ORDER_REJECTED;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * modify_position
 * =========================================================================*/
AF_Error OandaBroker::modify_position(uint64_t ticket, double sl, double tp) {
    /* Mirrors Python OandaBroker.modify_position():
     *   path = self._acct_path(f"trades/{ticket}/orders")
     *   body = {}
     *   if sl > 0: body["stopLoss"] = {"price": f"{sl:.5f}", "timeInForce": "GTC"}
     *   if tp > 0: body["takeProfit"] = {"price": f"{tp:.5f}", "timeInForce": "GTC"}
     *   try: self._request("PUT", path, body=body); return 0
     *   except BrokerError: return 1
     */
    try {
        std::string path = _acct_path("trades/" + std::to_string(ticket) + "/orders");
        JsonVal body;
        body.type = JsonVal::Type::Object;
        if (sl > 0.0) {
            body.obj["stopLoss"] =
                jobj({{"price", jstr(fmt5(sl))}, {"timeInForce", jstr("GTC")}});
        }
        if (tp > 0.0) {
            body.obj["takeProfit"] =
                jobj({{"price", jstr(fmt5(tp))}, {"timeInForce", jstr("GTC")}});
        }
        _request("PUT", path, body);
        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_ORDER_REJECTED;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * cancel_order
 * =========================================================================*/
AF_Error OandaBroker::cancel_order(uint64_t ticket) {
    /* Mirrors Python OandaBroker.cancel_order():
     *   path = self._acct_path(f"orders/{ticket}/cancel")
     *   try: self._request("PUT", path); return 0
     *   except BrokerError: return 1
     */
    try {
        std::string path = _acct_path("orders/" + std::to_string(ticket) + "/cancel");
        _request("PUT", path);
        return AF_OK;
    } catch (const BrokerError&) {
        return AF_ERR_ORDER_REJECTED;
    } catch (...) {
        return AF_ERR_UNKNOWN;
    }
}

/* =========================================================================
 * get_positions
 * =========================================================================*/
std::vector<AF_Position> OandaBroker::get_positions() const {
    /* Mirrors Python OandaBroker.get_positions():
     *   data = self._get(self._acct_path("openTrades"))
     *   trades = data.get("trades", [])
     *   return [self._trade_to_position(t) for t in trades]
     */
    try {
        JsonVal data = _get(_acct_path("openTrades"));
        const JsonVal& trades = data["trades"];
        if (!trades.is_arr()) return {};

        std::vector<AF_Position> result;
        result.reserve(trades.size());
        for (size_t i = 0; i < trades.size(); ++i) {
            result.push_back(_trade_to_position(trades[i]));
        }
        return result;
    } catch (...) {
        return {};
    }
}

/* =========================================================================
 * get_orders
 * =========================================================================*/
std::vector<AF_Order> OandaBroker::get_orders() const {
    /* Mirrors Python OandaBroker.get_orders():
     *   data = self._get(self._acct_path("pendingOrders"))
     *   orders_raw = data.get("orders", [])
     *   return [self._raw_order_to_order(o) for o in orders_raw]
     */
    try {
        JsonVal data = _get(_acct_path("pendingOrders"));
        const JsonVal& orders_raw = data["orders"];
        if (!orders_raw.is_arr()) return {};

        std::vector<AF_Order> result;
        result.reserve(orders_raw.size());
        for (size_t i = 0; i < orders_raw.size(); ++i) {
            result.push_back(_raw_order_to_order(orders_raw[i]));
        }
        return result;
    } catch (...) {
        return {};
    }
}

/* =========================================================================
 * get_position
 * =========================================================================*/
std::optional<AF_Position> OandaBroker::get_position(uint64_t ticket) const {
    /* Mirrors Python OandaBroker.get_position():
     *   for pos in self.get_positions():
     *       if pos.ticket == ticket: return pos
     *   return None
     */
    for (const auto& pos : get_positions()) {
        if (pos.ticket == ticket) return pos;
    }
    return std::nullopt;
}

/* =========================================================================
 * _trade_to_position (static)
 * =========================================================================*/
/* static */
AF_Position OandaBroker::_trade_to_position(const JsonVal& trade) {
    /* Mirrors Python OandaBroker._trade_to_position() */
    double current_units = json_val_double(trade["currentUnits"]);
    AF_Direction side    = (current_units > 0.0) ? AF_DIR_LONG : AF_DIR_SHORT;
    double lots          = std::fabs(current_units) / FX_CONTRACT_SIZE;

    std::string instrument = trade["instrument"].as_str("");
    /* symbol = instrument minus underscore */
    std::string symbol;
    symbol.reserve(instrument.size());
    for (char c : instrument) {
        if (c != '_') symbol += c;
    }

    double open_price    = json_val_double(trade["price"]);
    /* currentPrice falls back to open_price when absent */
    double current_price = trade.has("currentPrice")
                           ? json_val_double(trade["currentPrice"])
                           : open_price;

    /* SL/TP from nested order objects */
    double sl_price = 0.0, tp_price = 0.0;
    const JsonVal& sl_order = trade["stopLossOrder"];
    const JsonVal& tp_order = trade["takeProfitOrder"];
    if (sl_order.is_obj()) sl_price = json_val_double(sl_order["price"]);
    if (tp_order.is_obj()) tp_price = json_val_double(tp_order["price"]);

    double profit     = json_val_double(trade["unrealizedPL"]);
    double commission = json_val_double(trade["financing"]);

    /* comment from clientExtensions (if present) */
    std::string comment_str;
    const JsonVal& ext = trade["clientExtensions"];
    if (ext.is_obj()) {
        comment_str = ext["comment"].as_str("");
    }

    AF_Position pos{};
    pos.ticket        = static_cast<uint64_t>(json_val_int(trade["id"]));
    std::strncpy(pos.symbol, symbol.c_str(), sizeof(pos.symbol) - 1);
    pos.side          = side;
    pos.lots          = lots;
    pos.open_price    = open_price;
    pos.current_price = current_price;
    pos.sl            = sl_price;
    pos.tp            = tp_price;
    pos.profit        = profit;
    pos.commission    = commission;
    pos.swap          = 0.0;
    pos.magic         = 0;
    std::strncpy(pos.comment, comment_str.c_str(), sizeof(pos.comment) - 1);
    return pos;
}

/* =========================================================================
 * _raw_order_to_order (static)
 * =========================================================================*/
/* static */
AF_Order OandaBroker::_raw_order_to_order(const JsonVal& o) {
    /* Mirrors Python OandaBroker._raw_order_to_order() */
    std::string oanda_type = o["type"].as_str("MARKET");
    double units     = json_val_double(o["units"]);
    bool   is_sell   = units < 0.0;

    AF_OrderType order_type;
    if (oanda_type == "LIMIT") {
        order_type = is_sell ? AF_ORDER_SELL_LIMIT : AF_ORDER_BUY_LIMIT;
    } else if (oanda_type == "STOP") {
        order_type = is_sell ? AF_ORDER_SELL_STOP : AF_ORDER_BUY_STOP;
    } else {
        order_type = is_sell ? AF_ORDER_SELL : AF_ORDER_BUY;
    }

    std::string instrument = o["instrument"].as_str("");
    std::string symbol;
    symbol.reserve(instrument.size());
    for (char c : instrument) {
        if (c != '_') symbol += c;
    }

    double sl_price = 0.0, tp_price = 0.0;
    const JsonVal& sl_obj = o["stopLossOnFill"];
    const JsonVal& tp_obj = o["takeProfitOnFill"];
    if (sl_obj.is_obj()) sl_price = json_val_double(sl_obj["price"]);
    if (tp_obj.is_obj()) tp_price = json_val_double(tp_obj["price"]);

    std::string comment_str;
    const JsonVal& ext = o["clientExtensions"];
    if (ext.is_obj()) {
        comment_str = ext["comment"].as_str("");
    }

    AF_Order order{};
    order.ticket     = static_cast<uint64_t>(json_val_int(o["id"]));
    std::strncpy(order.symbol, symbol.c_str(), sizeof(order.symbol) - 1);
    order.type       = order_type;
    order.lots       = std::fabs(units) / FX_CONTRACT_SIZE;
    order.price      = json_val_double(o["price"]);
    order.sl         = sl_price;
    order.tp         = tp_price;
    order.fill_price = 0.0;
    order.magic      = 0;
    std::strncpy(order.comment, comment_str.c_str(), sizeof(order.comment) - 1);
    return order;
}

/* =========================================================================
 * register_oanda_broker — free function (exact shape from task spec)
 * =========================================================================*/
void register_oanda_broker() {
    register_broker("oanda", [](bool /*paper*/, double /*balance*/, BrokerConfig cfg)
        -> std::unique_ptr<IBroker> { return std::make_unique<OandaBroker>(std::move(cfg)); });
}

} /* namespace broker */
} /* namespace af */
