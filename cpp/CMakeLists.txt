cmake_minimum_required(VERSION 3.20)
project(AlgoForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD   17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W3 /O2 /MP)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra)
endif()

if(WIN32)
  add_compile_definitions(AF_PLATFORM_WINDOWS AF_MT5_AVAILABLE)
elseif(APPLE)
  add_compile_definitions(AF_PLATFORM_MACOS)
else()
  add_compile_definitions(AF_PLATFORM_LINUX)
endif()

find_package(Threads REQUIRED)

# ── Bundled SQLite3 ──
add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)

# ── Include dirs ──
include_directories(include)
include_directories(third_party/sqlite3)

# ── C math library ──
set(C_SOURCES
  src/indicators/indicators.c
  src/patterns/pattern_math.c
  src/risk/position_sizer.cpp
)

# ── C++ engine sources ──
set(CPP_SOURCES
  src/core/engine.cpp
  src/core/config.cpp
  src/broker/paper_broker.cpp
  src/broker/mt5_broker.cpp
  src/broker/rest_broker.cpp
  src/broker/ibkr_broker.cpp
  src/broker/oanda_broker.cpp
  src/broker/alpaca_broker.cpp
  src/broker/binance_broker.cpp
  src/broker/coinbase_broker.cpp
  src/data/bar_store.cpp
  src/data/trade_journal.cpp
  src/data/csv_bars.cpp
  src/indicators/indicator_engine.cpp
  src/patterns/pattern_engine.cpp
  src/patterns/candlestick_patterns.cpp
  src/patterns/chart_patterns.cpp
  src/analysis/market_structure.cpp
  src/analysis/confluence_scorer.cpp
  src/risk/drawdown_guard.cpp
  src/algorithms/trend_follower.cpp
  src/algorithms/mean_reversion.cpp
  src/algorithms/algorithm_registry.cpp
  src/backtesting/backtest_engine.cpp
  src/execution/signal_processor.cpp
  src/monitoring/logger.cpp
  src/learning/error_registry.cpp
  src/learning/learned_block_store.cpp
)

# ── Analytics sources (S4) ──
set(ANALYTICS_SOURCES
  src/analytics/metrics.cpp
  src/analytics/drawdown.cpp
  src/analytics/distributions.cpp
  src/analytics/correlations.cpp
  src/analytics/montecarlo.cpp
  src/analytics/walkforward.cpp
  src/analytics/ml_attribution.cpp
  src/analytics/factors.cpp
  src/analytics/streaming.cpp
  src/analytics/html_report.cpp
)

add_library(af_lib STATIC ${ANALYTICS_SOURCES})
target_include_directories(af_lib PUBLIC include)
set_target_properties(af_lib PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

add_library(af_engine STATIC ${C_SOURCES} ${CPP_SOURCES})
target_include_directories(af_engine PUBLIC include third_party/sqlite3)
target_link_libraries(af_engine PUBLIC sqlite3 Threads::Threads)

add_executable(algoforge   src/main.cpp)
add_executable(af_backtest src/backtest_main.cpp)
add_executable(af_bench    src/bench_main.cpp)
target_link_libraries(algoforge   PRIVATE af_engine)
target_link_libraries(af_backtest PRIVATE af_engine)
target_link_libraries(af_bench    PRIVATE af_engine)

enable_testing()
add_executable(af_tests
  tests/test_main.cpp
  tests/test_indicators.cpp
  tests/test_patterns.cpp
  tests/test_risk.cpp
  tests/test_broker.cpp
  tests/test_backtest.cpp
  tests/test_learning.cpp)
target_link_libraries(af_tests PRIVATE af_engine)
add_test(NAME AlgoForgeTests COMMAND af_tests)

# ── Analytics test suite (S4, standalone) ──
add_executable(test_analytics
  tests/test_analytics_main.cpp
  tests/test_analytics.cpp)
target_include_directories(test_analytics PRIVATE include tests)
target_link_libraries(test_analytics PRIVATE af_lib)
set_target_properties(test_analytics PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME AnalyticsTests COMMAND test_analytics)

# ── LLM sources (S2) ──
set(LLM_SOURCES
  src/llm/json.cpp
  src/llm/ollama.cpp
  src/llm/prompts.cpp
  src/llm/use_cases.cpp
)

add_library(af_llm STATIC ${LLM_SOURCES})
target_include_directories(af_llm PUBLIC include PRIVATE src/llm)
set_target_properties(af_llm PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# rest_broker.cpp (in af_engine) calls algoforge::llm::json::parse from af_llm.
target_link_libraries(af_engine PUBLIC af_llm)

# ── LLM test suite (S2, standalone) ──
add_executable(test_llm
  tests/test_llm_main.cpp
  tests/test_llm.cpp
  tests/llm_mock.cpp)
target_include_directories(test_llm PRIVATE include tests src/llm)
target_link_libraries(test_llm PRIVATE af_llm)
set_target_properties(test_llm PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME LLMTests COMMAND test_llm
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── AlgoGen sources (S1 Phase 1) ──
set(ALGO_GEN_SOURCES
  src/algo_gen/dsl.cpp
  src/algo_gen/json_helpers.cpp
  src/algo_gen/prompts.cpp
  src/algo_gen/generator.cpp
  src/algo_gen/promote.cpp
  src/algo_gen/robustness.cpp
  src/algo_gen/runtime.cpp
  src/algo_gen/schema.cpp
  src/algo_gen/serialise.cpp
  src/algo_gen/service.cpp
  src/algo_gen/tier.cpp
  src/algo_gen/validator.cpp
)

add_library(af_algo_gen STATIC ${ALGO_GEN_SOURCES})
target_include_directories(af_algo_gen PUBLIC include PRIVATE src/algo_gen)
# af_llm: generator.cpp + service.cpp drive the LLM provider for manifest generation.
target_link_libraries(af_algo_gen PUBLIC af_lib af_llm)
# indicators.c is already compiled into af_engine; for standalone use we add it directly
target_sources(af_algo_gen PRIVATE src/indicators/indicators.c)
set_target_properties(af_algo_gen PROPERTIES
  C_STANDARD 17
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON)

# ── AlgoGen test suite (S1 Phase 1, standalone) ──
add_executable(test_algo_gen
  tests/test_algo_gen_main.cpp
  tests/test_algo_gen.cpp)
target_include_directories(test_algo_gen PRIVATE include tests src/algo_gen)
target_link_libraries(test_algo_gen PRIVATE af_algo_gen)
target_compile_definitions(test_algo_gen PRIVATE
  # Canonical fixtures live at the repo-root tests/ tree (shared with the Python
  # oracle); CMAKE_SOURCE_DIR is the cpp/ subdir, so step up one level. Using the
  # same fixtures as Python is the truest parity gate (no duplicated copies to drift).
  FIXTURE_DIR="${CMAKE_SOURCE_DIR}/../tests/fixtures")
set_target_properties(test_algo_gen PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME AlgoGenTests COMMAND test_algo_gen
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── REST broker test suite (Phase 1 / S5, standalone) ──
add_executable(test_rest_broker
  tests/test_rest_broker_main.cpp
  tests/test_rest_broker.cpp)
target_include_directories(test_rest_broker PRIVATE include tests src/llm)
target_link_libraries(test_rest_broker PRIVATE af_engine af_llm)
set_target_properties(test_rest_broker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME RestBrokerTests COMMAND test_rest_broker)

# ── IBKR broker test suite (Phase 1 / S5, standalone) ──
add_executable(test_ibkr_broker
  tests/test_ibkr_broker_main.cpp
  tests/test_ibkr_broker.cpp)
target_include_directories(test_ibkr_broker PRIVATE include tests src/llm)
target_link_libraries(test_ibkr_broker PRIVATE af_engine af_llm)
set_target_properties(test_ibkr_broker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME IbkrBrokerTests COMMAND test_ibkr_broker)

# ── OANDA broker test suite (Phase 1 / S5, standalone) ──
add_executable(test_oanda_broker
  tests/test_oanda_broker_main.cpp
  tests/test_oanda_broker.cpp)
target_include_directories(test_oanda_broker PRIVATE include tests src/llm)
target_link_libraries(test_oanda_broker PRIVATE af_engine af_llm)
set_target_properties(test_oanda_broker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME OandaBrokerTests COMMAND test_oanda_broker)

# ── Alpaca broker test suite (Phase 1 / S5, standalone) ──
add_executable(test_alpaca_broker
  tests/test_alpaca_broker_main.cpp
  tests/test_alpaca_broker.cpp)
target_include_directories(test_alpaca_broker PRIVATE include tests src/llm)
target_link_libraries(test_alpaca_broker PRIVATE af_engine af_llm)
set_target_properties(test_alpaca_broker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME AlpacaBrokerTests COMMAND test_alpaca_broker)

# ── Binance broker test suite (Phase 1 / S5, standalone) ──
add_executable(test_binance_broker
  tests/test_binance_broker_main.cpp
  tests/test_binance_broker.cpp)
target_include_directories(test_binance_broker PRIVATE include tests src/llm)
target_link_libraries(test_binance_broker PRIVATE af_engine af_llm)
set_target_properties(test_binance_broker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME BinanceBrokerTests COMMAND test_binance_broker)

# ── Coinbase broker test suite (Phase 1 / S5, standalone) ──
add_executable(test_coinbase_broker
  tests/test_coinbase_broker_main.cpp
  tests/test_coinbase_broker.cpp)
target_include_directories(test_coinbase_broker PRIVATE include tests src/llm)
target_link_libraries(test_coinbase_broker PRIVATE af_engine af_llm)
set_target_properties(test_coinbase_broker PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME CoinbaseBrokerTests COMMAND test_coinbase_broker)

# ── algo_gen prompts test suite (Phase 2, standalone) ──
add_executable(test_prompts
  tests/test_prompts_main.cpp
  tests/test_prompts.cpp)
target_include_directories(test_prompts PRIVATE include tests src src/algo_gen)
target_link_libraries(test_prompts PRIVATE af_algo_gen)
set_target_properties(test_prompts PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME PromptsTests COMMAND test_prompts)

# ── algo_gen generator test suite (Phase 2, standalone) ──
add_executable(test_generator
  tests/test_generator_main.cpp
  tests/test_generator.cpp)
target_include_directories(test_generator PRIVATE include tests src src/algo_gen)
target_link_libraries(test_generator PRIVATE af_algo_gen af_llm)
set_target_properties(test_generator PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME GeneratorTests COMMAND test_generator)

# ── S6 discovery daemon (Phase 4) ──
# Orchestration layer above af_algo_gen (generate/validate/promote) + af_engine
# (af::classify_regime). Links both; static archives resolve the shared indicators.c
# symbols lazily (af_algo_gen pulled first), so no duplicate-symbol clash.
add_library(af_s6 STATIC src/s6/discovery_daemon.cpp)
target_include_directories(af_s6 PUBLIC include PRIVATE src/algo_gen)
target_link_libraries(af_s6 PUBLIC af_algo_gen af_engine)
set_target_properties(af_s6 PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

add_executable(test_s6
  tests/test_s6_main.cpp
  tests/test_s6.cpp)
target_include_directories(test_s6 PRIVATE include tests src src/algo_gen)
target_link_libraries(test_s6 PRIVATE af_s6)
set_target_properties(test_s6 PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME S6Tests COMMAND test_s6 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── Dashboard (Phase 5, slice 1) ──
# Pure handlers + log buffer (all logic, parity-tested) + a thin httplib socket
# binding (server.cpp, untested by design). httplib is header-only (third_party).
add_library(af_dashboard STATIC
  src/dashboard/handlers.cpp
  src/dashboard/server.cpp)
target_include_directories(af_dashboard PUBLIC include third_party/httplib PRIVATE src/llm src/algo_gen)
set_target_properties(af_dashboard PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
# af_algo_gen: the /api/algos routes drive AlgoGenService + manifest/tier serialisers.
target_link_libraries(af_dashboard PUBLIC Threads::Threads af_llm af_algo_gen)

add_executable(test_dashboard
  tests/test_dashboard_main.cpp
  tests/test_dashboard.cpp)
target_include_directories(test_dashboard PRIVATE include tests)
target_link_libraries(test_dashboard PRIVATE af_dashboard)
set_target_properties(test_dashboard PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME DashboardTests COMMAND test_dashboard WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── Dashboard serving binary (the C++ analogue of uvicorn make_app) ──
# Wires a PaperBroker (+ optional OllamaProvider/AlgoGenService) into the routes
# and runs httplib::Server. af_engine supplies make_paper_broker.
add_executable(af_dashboard_server src/dashboard/dashboard_main.cpp)
target_include_directories(af_dashboard_server PRIVATE include third_party/httplib)
target_link_libraries(af_dashboard_server PRIVATE af_dashboard af_engine)
set_target_properties(af_dashboard_server PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(af_dashboard_server PRIVATE AF_VERSION="${PROJECT_VERSION}")

message(STATUS "AlgoForge ${PROJECT_VERSION} | ${CMAKE_BUILD_TYPE} | ${CMAKE_SYSTEM_NAME}")
