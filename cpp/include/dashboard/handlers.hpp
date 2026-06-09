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

} // namespace algoforge::dashboard
