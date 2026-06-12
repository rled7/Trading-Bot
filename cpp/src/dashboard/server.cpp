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
#include "core/algo_gen.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace algoforge::dashboard {

struct ServerDeps {
    std::string                                       version = "dev";
    std::vector<std::string>                          symbols;
    LogRingBuffer*                                    logs = nullptr;
    const af::IBroker*                                broker = nullptr;  // slice 2 data source
    algoforge::llm::LLMProvider*                      llm = nullptr;     // slice 3 data source
    algoforge::algo_gen::AlgoGenService*              algo_gen = nullptr; // slice 5 data source
    std::string                                       llm_host;          // for /api/llm/health
    std::function<std::pair<std::string, bool>()>     broker_status;  // (name, connected)
    std::function<double()>                           uptime;
    std::string                                       static_dir;
};

// ── Slice 5: in-memory algo store (mirrors the Python module-level _STORE) ──
// Thread-safe: httplib serves requests on multiple threads. The store mutex is
// held only around map access, never across a (possibly slow) service call.
namespace {
struct AlgoRecord {
    algoforge::algo_gen::AlgoManifest manifest;
    algoforge::algo_gen::TierReport   tier;
    std::string                       status = "generated";
};
struct AlgoStore {
    std::mutex                          mu;
    std::map<std::string, AlgoRecord>   recs;
};

std::string new_algo_id() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::mutex rng_mu;
    std::lock_guard<std::mutex> lk(rng_mu);
    std::uniform_int_distribution<uint64_t> d;
    char buf[33];
    std::snprintf(buf, sizeof buf, "%016llx%016llx",
                  static_cast<unsigned long long>(d(rng)),
                  static_cast<unsigned long long>(d(rng)));
    return std::string(buf);
}
} // namespace

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

    // ── Slice 4: broker tick SSE (server.py stream_ticks) ──
    srv.Get("/api/stream/ticks", [deps](const httplib::Request& req, httplib::Response& res) {
        const double interval = clamp_stream_interval(
            req.has_param("interval") ? std::atof(req.get_param_value("interval").c_str()) : 1.0);
        const std::string csv = req.has_param("symbols") ? req.get_param_value("symbols") : "";
        const std::vector<std::string> syms = parse_stream_symbols(csv, deps.symbols);
        const af::IBroker* broker = deps.broker;
        res.set_chunked_content_provider(
            "text/event-stream",
            [broker, syms, interval](size_t, httplib::DataSink& sink) {
                if (!broker) { sink.done(); return false; }
                while (sink.is_writable()) {
                    const std::string ka = ": keepalive\n\n";
                    if (!sink.write(ka.data(), ka.size())) break;
                    for (const auto& sym : syms) {
                        AF_Tick t{};
                        if (broker->get_tick(sym.c_str(), t) != AF_OK) continue;
                        const std::string line = sse_data_line(tick_json(t));
                        if (!sink.write(line.data(), line.size())) { sink.done(); return true; }
                    }
                    std::this_thread::sleep_for(
                        std::chrono::duration<double>(interval));
                }
                sink.done();
                return true;
            });
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

    // ── Slice 5: algo_gen routes (server.py /api/algos via algo_gen_routes.py) ──
    // Thin binding over AlgoGenService + an in-memory AlgoStore. When algo_gen is
    // null every route returns 503 {"error":"algo_gen_disabled"} (Python parity).
    {
        namespace ag = algoforge::algo_gen;
        auto store = std::make_shared<AlgoStore>();

        // GET /api/algos — list generated manifests
        srv.Get("/api/algos", [deps, store](const httplib::Request&, httplib::Response& res) {
            if (!deps.algo_gen) { res.status = 503; res.set_content(algos_disabled_json(), "application/json"); return; }
            std::string out = "[";
            {
                std::lock_guard<std::mutex> lk(store->mu);
                bool first = true;
                for (auto& [id, rec] : store->recs) {
                    if (!first) out += ",";
                    first = false;
                    const std::string tier = rec.tier.has_tier ? ag::tier_name(rec.tier.tier) : "";
                    out += algo_list_item_json(id, rec.manifest.name, tier, rec.status);
                }
            }
            out += "]";
            res.set_content(out, "application/json");
        });

        // POST /api/algos/generate — generate + validate, store, return {id,manifest,tier_report}
        srv.Post("/api/algos/generate", [deps, store](const httplib::Request& req, httplib::Response& res) {
            if (!deps.algo_gen) { res.status = 503; res.set_content(algos_disabled_json(), "application/json"); return; }
            GenerateReq g; std::string err;
            if (!parse_generate_request(req.body, g, err)) {
                res.status = 422; res.set_content(algo_gen_error_json("generation", err), "application/json"); return;
            }
            try {
                ag::AlgoManifest m  = deps.algo_gen->generate(g.brief, g.effort, static_cast<uint64_t>(g.seed));
                ag::TierReport   tr = deps.algo_gen->validate(m, static_cast<uint64_t>(g.seed));
                const std::string id = new_algo_id();
                { std::lock_guard<std::mutex> lk(store->mu); store->recs.insert_or_assign(id, AlgoRecord{m, tr, "generated"}); }
                res.set_content(generate_response_json(id, ag::manifest_to_json(m), ag::tier_report_to_json(tr)),
                                "application/json");
            } catch (const ag::AlgoGenError& e) {
                res.status = algo_gen_error_status(e.kind);
                res.set_content(algo_gen_error_json(e.kind, e.what()), "application/json");
            } catch (const std::exception& e) {
                res.status = 500; res.set_content(algo_gen_error_json("internal", e.what()), "application/json");
            }
        });

        // POST /api/algos/generate/stream — SSE staged generation
        srv.Post("/api/algos/generate/stream", [deps, store](const httplib::Request& req, httplib::Response& res) {
            if (!deps.algo_gen) { res.status = 503; res.set_content(algos_disabled_json(), "application/json"); return; }
            GenerateReq g; std::string err;
            if (!parse_generate_request(req.body, g, err)) {
                res.status = 422; res.set_content(algo_gen_error_json("generation", err), "application/json"); return;
            }
            auto* svc = deps.algo_gen;
            res.set_chunked_content_provider(
                "text/event-stream",
                [svc, store, g](size_t, httplib::DataSink& sink) {
                    auto emit = [&](const std::string& payload) {
                        const std::string l = sse_data_line(payload);
                        sink.write(l.data(), l.size());
                    };
                    emit("{\"stage\":\"prompt\"}");
                    ag::AlgoManifest m;
                    try {
                        m = svc->generate(g.brief, g.effort, static_cast<uint64_t>(g.seed));
                    } catch (const ag::AlgoGenError& e) {
                        emit("{\"stage\":\"error\",\"error\":\"" + json_escape(e.kind) +
                             "\",\"detail\":\"" + json_escape(e.what()) + "\"}");
                        sink.done();
                        return true;
                    }
                    emit("{\"stage\":\"manifest\",\"manifest\":" + ag::manifest_to_json(m) + "}");
                    ag::TierReport tr = svc->validate(m, static_cast<uint64_t>(g.seed));
                    emit("{\"stage\":\"tier\",\"tier\":" + ag::tier_report_to_json(tr) + "}");
                    const std::string id = new_algo_id();
                    { std::lock_guard<std::mutex> lk(store->mu); store->recs.insert_or_assign(id, AlgoRecord{m, tr, "generated"}); }
                    emit("[DONE]");
                    sink.done();
                    return true;
                });
        });

        // POST /api/algos/{id}/backtest
        srv.Post(R"(/api/algos/([^/]+)/backtest)", [deps, store](const httplib::Request& req, httplib::Response& res) {
            if (!deps.algo_gen) { res.status = 503; res.set_content(algos_disabled_json(), "application/json"); return; }
            const std::string id = req.matches[1];
            ag::AlgoManifest m; bool found = false;
            { std::lock_guard<std::mutex> lk(store->mu); auto it = store->recs.find(id); if (it != store->recs.end()) { m = it->second.manifest; found = true; } }
            if (!found) { res.status = 404; res.set_content(algo_gen_error_json("not_found", "algo_id=" + id), "application/json"); return; }
            BacktestReq b; std::string err;
            if (!parse_backtest_request(req.body, b, err)) {
                res.status = 422; res.set_content(algo_gen_error_json("backtest", err), "application/json"); return;
            }
            try {
                ag::BacktestSummary bt = deps.algo_gen->backtest(m, b.symbol, b.bars, static_cast<uint64_t>(b.seed));
                res.set_content(backtest_summary_json(id, b.symbol, bt), "application/json");
            } catch (const ag::AlgoGenError& e) {
                res.status = algo_gen_error_status(e.kind);
                res.set_content(algo_gen_error_json(e.kind, e.what()), "application/json");
            } catch (const std::exception& e) {
                res.status = 500; res.set_content(algo_gen_error_json("internal", e.what()), "application/json");
            }
        });

        // GET /api/algos/{id}/manifest
        srv.Get(R"(/api/algos/([^/]+)/manifest)", [deps, store](const httplib::Request& req, httplib::Response& res) {
            if (!deps.algo_gen) { res.status = 503; res.set_content(algos_disabled_json(), "application/json"); return; }
            const std::string id = req.matches[1];
            std::lock_guard<std::mutex> lk(store->mu);
            auto it = store->recs.find(id);
            if (it == store->recs.end()) { res.status = 404; res.set_content(algo_gen_error_json("not_found", "algo_id=" + id), "application/json"); return; }
            res.set_content(ag::manifest_to_json(it->second.manifest), "application/json");
        });

        // POST /api/algos/{id}/promote
        srv.Post(R"(/api/algos/([^/]+)/promote)", [deps, store](const httplib::Request& req, httplib::Response& res) {
            if (!deps.algo_gen) { res.status = 503; res.set_content(algos_disabled_json(), "application/json"); return; }
            const std::string id = req.matches[1];
            ag::AlgoManifest m; ag::TierReport tr; bool found = false;
            { std::lock_guard<std::mutex> lk(store->mu); auto it = store->recs.find(id); if (it != store->recs.end()) { m = it->second.manifest; tr = it->second.tier; found = true; } }
            if (!found) { res.status = 404; res.set_content(algo_gen_error_json("not_found", "algo_id=" + id), "application/json"); return; }
            if (!tr.has_tier || !tr.passed) {
                res.status = 422; res.set_content(algo_gen_error_json("generation", "No passing tier report; run generate first"), "application/json"); return;
            }
            try {
                deps.algo_gen->promote(tr, m);
            } catch (const ag::AlgoGenError& e) {
                res.status = algo_gen_error_status(e.kind);
                res.set_content(algo_gen_error_json(e.kind, e.what()), "application/json"); return;
            } catch (const std::exception& e) {
                res.status = 500; res.set_content(algo_gen_error_json("internal", e.what()), "application/json"); return;
            }
            { std::lock_guard<std::mutex> lk(store->mu); auto it = store->recs.find(id); if (it != store->recs.end()) it->second.status = "promoted"; }
            res.set_content("{\"algo_id\":\"" + json_escape(id) + "\",\"status\":\"promoted\"}", "application/json");
        });

        // POST /api/algos/{id}/retire
        srv.Post(R"(/api/algos/([^/]+)/retire)", [deps, store](const httplib::Request& req, httplib::Response& res) {
            if (!deps.algo_gen) { res.status = 503; res.set_content(algos_disabled_json(), "application/json"); return; }
            const std::string id = req.matches[1];
            ag::AlgoManifest m; bool found = false;
            { std::lock_guard<std::mutex> lk(store->mu); auto it = store->recs.find(id); if (it != store->recs.end()) { m = it->second.manifest; found = true; } }
            if (!found) { res.status = 404; res.set_content(algo_gen_error_json("not_found", "algo_id=" + id), "application/json"); return; }
            try {
                deps.algo_gen->retire(m);
            } catch (const std::exception& e) {
                res.status = 500; res.set_content(algo_gen_error_json("internal", e.what()), "application/json"); return;
            }
            { std::lock_guard<std::mutex> lk(store->mu); auto it = store->recs.find(id); if (it != store->recs.end()) it->second.status = "retired"; }
            res.set_content("{\"algo_id\":\"" + json_escape(id) + "\",\"status\":\"retired\"}", "application/json");
        });
    }

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
