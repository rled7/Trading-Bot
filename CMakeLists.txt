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

add_library(af_engine STATIC ${C_SOURCES} ${CPP_SOURCES})
target_include_directories(af_engine PUBLIC include third_party/sqlite3)
target_link_libraries(af_engine PUBLIC sqlite3 Threads::Threads)

add_executable(algoforge   src/main.cpp)
add_executable(af_backtest src/backtest_main.cpp)
target_link_libraries(algoforge   PRIVATE af_engine)
target_link_libraries(af_backtest PRIVATE af_engine)

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

message(STATUS "AlgoForge ${PROJECT_VERSION} | ${CMAKE_BUILD_TYPE} | ${CMAKE_SYSTEM_NAME}")
