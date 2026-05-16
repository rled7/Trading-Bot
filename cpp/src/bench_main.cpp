/* Cross-language benchmark — cpp/.
 * Same series generator, same indicator calls as the c/, python/, js/ benches.
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" {
#include "indicators/indicators.h"
#include "patterns/pattern_types.h"
}
#include "patterns/pattern_engine.hpp"

static std::vector<double> gen_series(size_t n) {
    std::vector<double> out(n);
    double p = 100.0;
    for (size_t i = 0; i < n; ++i) {
        p += static_cast<double>((static_cast<int>(i) * 9301 + 49297) % 233 - 116) * 0.01;
        out[i] = p;
    }
    return out;
}

template <typename F>
static std::pair<long long, long long> timed(F fn, int iters) {
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < iters; ++r) fn();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    return {ns, ns / iters};
}

int main(int argc, char **argv) {
    size_t n     = (argc > 1) ? static_cast<size_t>(std::atoi(argv[1])) : 100000;
    int    iters = (argc > 2) ? std::atoi(argv[2])                       : 10;

    auto src = gen_series(n);
    std::vector<double> hi(n), lo(n), out(n);
    for (size_t i = 0; i < n; ++i) { hi[i] = src[i] + 0.5; lo[i] = src[i] - 0.5; }
    std::vector<AF_Bar> bars(n);
    for (size_t i = 0; i < n; ++i) {
        bars[i] = {};
        bars[i].timestamp = static_cast<int64_t>(i);
        bars[i].open  = src[i];
        bars[i].close = src[i] + 0.001 * (static_cast<int>(i % 7) - 3);
        bars[i].high  = hi[i]; bars[i].low = lo[i];
        bars[i].volume = 1.0;
        af_bar_init(&bars[i]);
    }
    af::PatternEngine eng;
    eng.register_all();

    std::printf("# language\tcpp\n");
    std::printf("# bars\t%zu\n", n);
    std::printf("# iterations\t%d\n", iters);
    std::printf("indicator\ttotal_ns\tns_per_iter\n");

    auto [s_total, s_per] = timed([&]{ af_sma(src.data(), n, 20, out.data()); }, iters);
    std::printf("sma20\t%lld\t%lld\n", s_total, s_per);
    auto [e_total, e_per] = timed([&]{ af_ema(src.data(), n, 50, out.data()); }, iters);
    std::printf("ema50\t%lld\t%lld\n", e_total, e_per);
    auto [r_total, r_per] = timed([&]{ af_rsi(src.data(), n, 14, out.data()); }, iters);
    std::printf("rsi14\t%lld\t%lld\n", r_total, r_per);
    auto [a_total, a_per] = timed([&]{ af_atr(hi.data(), lo.data(), src.data(), n, 14, out.data()); }, iters);
    std::printf("atr14\t%lld\t%lld\n", a_total, a_per);
    auto [p_total, p_per] = timed([&]{ (void)eng.scan(bars.data(), n); }, iters);
    std::printf("pscan\t%lld\t%lld\n", p_total, p_per);

    return 0;
}
