#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════
#   AlgoForge C/C++ Build Script
# ═══════════════════════════════════════════════════════════
set -e

BUILD_TYPE=${1:-Release}
BUILD_DIR="build/${BUILD_TYPE}"
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "  AlgoForge C/C++ Build"
echo "  ─────────────────────────────────────────────"
echo "  Build type : ${BUILD_TYPE}"
echo "  Build dir  : ${BUILD_DIR}"
echo "  Cores      : ${CORES}"
echo "  ─────────────────────────────────────────────"
echo ""

# ── Check for real SQLite3 amalgamation ──
if [ ! -s "third_party/sqlite3/sqlite3.c" ] || [ $(wc -l < "third_party/sqlite3/sqlite3.c") -lt 100 ]; then
    echo "  [WARN] SQLite3 amalgamation not found in third_party/sqlite3/"
    echo "  Download from https://sqlite.org/download.html"
    echo "  Extract sqlite3.c and sqlite3.h into third_party/sqlite3/"
    echo ""
fi

# ── Configure ──
cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "$@"

# ── Build ──
cmake --build "${BUILD_DIR}" --parallel "${CORES}"

# ── Report ──
echo ""
echo "  ─────────────────────────────────────────────"
echo "  Build complete:"
if [ -f "${BUILD_DIR}/algoforge" ]; then
    echo "    ./algoforge   — live/paper trading bot"
fi
if [ -f "${BUILD_DIR}/af_backtest" ]; then
    echo "    ./af_backtest — standalone backtester"
fi
if [ -f "${BUILD_DIR}/af_tests" ]; then
    echo "    ./af_tests    — test suite"
fi
echo "  ─────────────────────────────────────────────"
echo ""
