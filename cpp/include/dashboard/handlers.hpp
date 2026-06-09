/**
 * AlgoForge — include/dashboard/handlers.hpp
 *
 * Phase 5 (dashboard) — PURE request handlers: (inputs) -> JSON string.
 * Python oracle: python/algoforge/dashboard/server.py route bodies.
 *
 * The handlers hold all the logic and are the unit of parity test (mirrors the
 * broker MockTransport / S6 injected-deps discipline). The httplib socket layer in
 * server.cpp is a thin, untested-by-design binding that only calls these.
 */
#pragma once

#include <string>
#include <vector>

#include "dashboard/log_buffer.hpp"
#include "core/types.h"   // AF_AccountInfo, AF_Position, AF_Order, AF_Bar, AF_Timeframe

namespace algoforge::dashboard {

/** RFC-8259 string escaping for embedding in JSON. */
std::string json_escape(const std::string& s);

/** GET /api/health → {broker_name,is_connected,version,uptime_seconds}. */
std::string health_json(const std::string& broker_name,
                        bool is_connected,
                        const std::string& version,
                        double uptime_seconds);

/** GET /api/symbols → ["EURUSD", ...]. */
std::string symbols_json(const std::vector<std::string>& symbols);

/** GET /api/logs → [{ts,level,component,msg}, ...]. */
std::string logs_json(const std::vector<LogRecord>& records);

/** Server clamp for the ?n= query of /api/logs: max(1, min(n, 500)). */
int clamp_log_count(int n);

// ── Slice 2: broker-backed routes (Python oracle: server.py account/positions/orders/bars) ──
// Serialise the C++ broker's AF_* structs. side/type are coerced to ints to mirror the Python
// _position_dict/_order_dict convention. (Field sets follow the C++ broker types, which a C++
// dashboard serves; these differ slightly from the Python dataclasses by design.)

/** GET /api/account → {balance,equity,margin,free_margin,profit,leverage,currency,login}. */
std::string account_json(const AF_AccountInfo& a);

/** One position object with side as int. */
std::string position_json(const AF_Position& p);
/** GET /api/positions → [position, ...]. */
std::string positions_json(const std::vector<AF_Position>& ps);

/** One order object with type as int. */
std::string order_json(const AF_Order& o);
/** GET /api/orders → [order, ...]. */
std::string orders_json(const std::vector<AF_Order>& os);

/** One bar object → {timestamp,open,high,low,close,volume,spread} (Python Bar fields). */
std::string bar_json(const AF_Bar& b);
/** GET /api/bars/{symbol} → [bar, ...]. */
std::string bars_json(const std::vector<AF_Bar>& bs);

/** Parse a timeframe name ("M5", "H1", ...) → AF_Timeframe. Returns false if unknown. */
bool parse_timeframe(const std::string& name, AF_Timeframe& out);
/** Valid timeframe names, in enum order (for the 422 error detail). */
std::vector<std::string> timeframe_names();
/** Server clamp for /api/bars ?count=: max(1, min(count, 5000)). */
int clamp_bar_count(int count);

} // namespace algoforge::dashboard
