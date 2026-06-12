/**
 * AlgoForge — include/dashboard/server.hpp
 *
 * Phase 5 (dashboard) — public surface of the httplib binding. ServerDeps is the
 * dependency bundle a host process fills in; register_routes() wires every route
 * onto an httplib::Server. Mirrors python/algoforge/dashboard/server.py::make_app.
 *
 * The serving binary (src/dashboard/dashboard_main.cpp) constructs these deps and
 * calls register_routes(); the route bodies live in server.cpp.
 */
#pragma once

#include "httplib.h"
#include "dashboard/log_buffer.hpp"
#include "broker/broker.hpp"
#include "core/llm.hpp"
#include "core/algo_gen.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace algoforge::dashboard {

struct ServerDeps {
    std::string                                       version = "dev";
    std::vector<std::string>                          symbols;
    LogRingBuffer*                                    logs = nullptr;
    const af::IBroker*                                broker = nullptr;   // slice 2 data source
    algoforge::llm::LLMProvider*                      llm = nullptr;      // slice 3 data source
    algoforge::algo_gen::AlgoGenService*              algo_gen = nullptr; // slice 5 data source
    std::string                                       llm_host;           // for /api/llm/health
    std::function<std::pair<std::string, bool>()>     broker_status;      // (name, connected)
    std::function<double()>                           uptime;
    std::string                                       static_dir;
};

/** Register every dashboard route onto srv using the supplied dependencies. */
void register_routes(httplib::Server& srv, ServerDeps deps);

} // namespace algoforge::dashboard
