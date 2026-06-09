/**
 * AlgoForge — src/dashboard/server.cpp
 *
 * Phase 5 (dashboard) — THIN httplib binding. Untested-by-design: it only wires
 * sockets to the pure handlers in handlers.cpp (which carry all logic + parity tests).
 * Mirrors python/algoforge/dashboard/server.py route registration.
 *
 * SSE note: server.py uses FastAPI async StreamingResponse (1s poll, emit-on-change).
 * The C++ analogue is httplib's synchronous chunked content provider — proven by the
 * /api/stream/demo route below (the one real feasibility risk; it works).
 */
#include "httplib.h"
#include "dashboard/handlers.hpp"
#include "dashboard/log_buffer.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>

namespace algoforge::dashboard {

struct ServerDeps {
    std::string                                       version = "dev";
    std::vector<std::string>                          symbols;
    LogRingBuffer*                                    logs = nullptr;
    std::function<std::pair<std::string, bool>()>     broker_status;  // (name, connected)
    std::function<double()>                           uptime;
    std::string                                       static_dir;
};

void register_routes(httplib::Server& srv, ServerDeps deps) {
    srv.Get("/api/health", [deps](const httplib::Request&, httplib::Response& res) {
        auto st = deps.broker_status ? deps.broker_status()
                                     : std::make_pair(std::string("paper"), true);
        const double up = deps.uptime ? deps.uptime() : 0.0;
        res.set_content(health_json(st.first, st.second, deps.version, up), "application/json");
    });

    srv.Get("/api/symbols", [deps](const httplib::Request&, httplib::Response& res) {
        res.set_content(symbols_json(deps.symbols), "application/json");
    });

    srv.Get("/api/logs", [deps](const httplib::Request& req, httplib::Response& res) {
        int n = 50;
        if (req.has_param("n")) n = std::atoi(req.get_param_value("n").c_str());
        n = clamp_log_count(n);
        std::vector<LogRecord> recs = deps.logs ? deps.logs->tail(n) : std::vector<LogRecord>{};
        res.set_content(logs_json(recs), "application/json");
    });

    if (!deps.static_dir.empty()) {
        srv.set_mount_point("/static", deps.static_dir);
        srv.Get("/", [deps](const httplib::Request&, httplib::Response& res) {
            std::ifstream f(deps.static_dir + "/index.html");
            if (!f.is_open()) { res.status = 404; return; }
            std::stringstream ss; ss << f.rdbuf();
            res.set_content(ss.str(), "text/html");
        });
    }

    // SSE feasibility prototype — chunked content provider (emit-on-change shape).
    srv.Get("/api/stream/demo", [](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider(
            "text/event-stream",
            [](size_t /*offset*/, httplib::DataSink& sink) {
                const std::string ev = "data: {\"tick\":1}\n\n";
                sink.write(ev.data(), ev.size());
                sink.done();
                return true;
            });
    });
}

} // namespace algoforge::dashboard
