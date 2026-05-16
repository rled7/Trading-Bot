#!/usr/bin/env bash
# Run the same benchmark across all four language projects and aggregate.
#
# Each language's bench follows the same TSV format:
#   # language\t<name>
#   # bars\t<n>
#   # iterations\t<i>
#   indicator\ttotal_ns\tns_per_iter
#   <name>\t<total>\t<per>
#   ...
#
# Usage:
#   benchmarks/run_all.sh [bars] [iterations]
set -euo pipefail

BARS=${1:-100000}
ITERS=${2:-10}
ROOT=$(cd "$(dirname "$0")/.." && pwd)

run_one() {
    local label="$1"; shift
    echo "── $label ───────────────────────────────────────"
    "$@" "$BARS" "$ITERS" 2>&1 | tee "/tmp/af_bench_$label.tsv"
    echo
}

echo "AlgoForge cross-language benchmark"
echo "  bars       : $BARS"
echo "  iterations : $ITERS"
echo

# c
make -C "$ROOT/c" bench >/dev/null
run_one c "$ROOT/c/build/af_bench"

# cpp
cmake --build "$ROOT/cpp/build/Release" --target af_bench >/dev/null
run_one cpp "$ROOT/cpp/build/Release/af_bench"

# python
( cd "$ROOT/python" && PYTHONPATH=. run_label=python python3 -m algoforge.bench "$BARS" "$ITERS" ) \
    | tee /tmp/af_bench_python.tsv >/dev/null
echo "── python ───────────────────────────────────────"
cat /tmp/af_bench_python.tsv
echo

# js
( cd "$ROOT/js" && node src/bench.js "$BARS" "$ITERS" ) | tee /tmp/af_bench_js.tsv >/dev/null
echo "── js ───────────────────────────────────────────"
cat /tmp/af_bench_js.tsv
echo

# Aggregate: per-iteration nanoseconds per indicator, per language.
echo "─────────────────────────────────────────────────"
echo "Summary — ns / iteration / indicator"
echo "─────────────────────────────────────────────────"
printf "%-10s %12s %12s %12s %12s\n" "indicator" "c" "cpp" "python" "js"

for ind in sma20 ema50 rsi14 atr14 pscan; do
    cval=$(  awk -v i="$ind" '$1==i {print $3}' /tmp/af_bench_c.tsv)
    cppval=$(awk -v i="$ind" '$1==i {print $3}' /tmp/af_bench_cpp.tsv)
    pyval=$( awk -v i="$ind" '$1==i {print $3}' /tmp/af_bench_python.tsv)
    jsval=$( awk -v i="$ind" '$1==i {print $3}' /tmp/af_bench_js.tsv)
    printf "%-10s %12s %12s %12s %12s\n" "$ind" "$cval" "$cppval" "$pyval" "$jsval"
done
