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
#include "broker/broker.hpp"
#include "core/llm.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace algoforge::dashboard {

struct ServerDeps {
    std::string                                       version = "dev";
    std::vector<std::string>                          symbols;
    LogRingBuffer*                                    logs = nullptr;
    const af::IBroker*                                broker = nullptr;  // slice 2 data source
    algoforge::llm::LLMProvider*                      llm = nullptr;     // slice 3 data source
    std::string                                       llm_host;          // for /api/llm/health
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

    // ── Slice 2: broker-backed routes (server.py account/positions/orders/bars) ──
    srv.Get("/api/account", [deps](const httplib::Request&, httplib::Response& res) {
        if (!deps.broker) { res.status = 503; return; }
        AF_AccountInfo a{};
        deps.broker->get_account(a);
        res.set_content(account_json(a), "application/json");
    });

    srv.Get("/api/positions", [deps](const httplib::Request&, httplib::Response& res) {
        if (!deps.broker) { res.status = 503; return; }
        res.set_content(positions_json(deps.broker->get_positions()), "application/json");
    });

    srv.Get("/api/orders", [deps](const httplib::Request&, httplib::Response& res) {
        if (!deps.broker) { res.status = 503; return; }
        res.set_content(orders_json(deps.broker->get_orders()), "application/json");
    });

    srv.Get(R"(/api/bars/([^/]+))", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.broker) { res.status = 503; return; }
        const std::string symbol = req.matches[1];
        const std::string tf = req.has_param("tf") ? req.get_param_value("tf") : "M5";
        AF_Timeframe tf_enum;
        if (!parse_timeframe(tf, tf_enum)) {
            std::string valid;
            for (const auto& n : timeframe_names()) { if (!valid.empty()) valid += ", "; valid += n; }
            res.status = 422;
            res.set_content("{\"detail\":\"Invalid tf '" + json_escape(tf) +
                            "'. Must be one of: [" + valid + "]\"}", "application/json");
            return;
        }
        int count = req.has_param("count") ? std::atoi(req.get_param_value("count").c_str()) : 200;
        count = clamp_bar_count(count);
        std::vector<AF_Bar> bars(static_cast<size_t>(count));
        int filled = 0;
        deps.broker->get_bars(symbol.c_str(), tf_enum, count, bars.data(), &filled);
        bars.resize(filled > 0 ? static_cast<size_t>(filled) : 0);
        res.set_content(bars_json(bars), "application/json");
    });

    // ── Slice 3: llm routes (server.py llm_health/models/chat[/stream]) ──
    srv.Get("/api/llm/health", [deps](const httplib::Request&, httplib::Response& res) {
        if (!deps.llm) { res.status = 503; res.set_content(llm_error_json("llm_disabled", "LLM not configured"), "application/json"); return; }
        try {
            auto st = deps.llm->health();
            if (!st.ok) {
                res.status = 503;
                res.set_content(llm_error_json("down", st.error ? *st.error : "unhealthy"), "application/json");
                return;
            }
            res.set_content(llm_health_ok_json(st, deps.llm_host), "application/json");
        } catch (const algoforge::llm::LLMError& e) {
            res.status = 503; res.set_content(llm_error_json("down", e.what()), "application/json");
        } catch (const std::exception& e) {
            res.status = 503; res.set_content(llm_error_json("down", e.what()), "application/json");
        }
    });

    srv.Get("/api/llm/models", [deps](const httplib::Request&, httplib::Response& res) {
        if (!deps.llm) { res.status = 503; res.set_content(llm_error_json("llm_disabled", "LLM not configured"), "application/json"); return; }
        try {
            res.set_content(llm_models_json(deps.llm->list_models()), "application/json");
        } catch (const algoforge::llm::LLMError& e) {
            res.status = llm_error_status(e.kind);
            res.set_content(llm_error_json(llm_error_kind_name(e.kind), e.what()), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(llm_error_json("internal", e.what()), "application/json");
        }
    });

    srv.Post("/api/llm/chat", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.llm) { res.status = 503; res.set_content(llm_error_json("llm_disabled", "LLM not configured"), "application/json"); return; }
        algoforge::llm::ChatRequest creq; std::string err;
        if (!parse_chat_request(req.body, creq, err)) {
            res.status = 400; res.set_content(llm_error_json("validation", err), "application/json"); return;
        }
        try {
            res.set_content(llm_chat_response_json(deps.llm->chat(creq)), "application/json");
        } catch (const algoforge::llm::LLMError& e) {
            res.status = llm_error_status(e.kind);
            res.set_content(llm_error_json(llm_error_kind_name(e.kind), e.what()), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(llm_error_json("internal", e.what()), "application/json");
        }
    });

    srv.Post("/api/llm/chat/stream", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!deps.llm) { res.status = 503; res.set_content(llm_error_json("llm_disabled", "LLM not configured"), "application/json"); return; }
        algoforge::llm::ChatRequest creq; std::string err;
        if (!parse_chat_request(req.body, creq, err)) {
            res.status = 400; res.set_content(llm_error_json("validation", err), "application/json"); return;
        }
        auto* llm = deps.llm;
        res.set_chunked_content_provider(
            "text/event-stream",
            [llm, creq](size_t, httplib::DataSink& sink) {
                try {
                    for (const auto& chunk : llm->chat_stream(creq)) {
                        const std::string line = sse_data_line(chunk.delta);
                        sink.write(line.data(), line.size());
                        if (chunk.done) break;
                    }
                    const std::string done = sse_data_line("[DONE]");
                    sink.write(done.data(), done.size());
                } catch (const algoforge::llm::LLMError& e) {
                    const std::string ep = sse_data_line(llm_error_json(llm_error_kind_name(e.kind), e.what()));
                    sink.write(ep.data(), ep.size());
                }
                sink.done();
                return true;
            });
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
