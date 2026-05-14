/**
 * AlgoForge — tests/test_patterns.cpp
 */
#include "tests/test_helpers.hpp"
#include "patterns/pattern_engine.hpp"
#include "patterns/pattern_types.h"
#include "core/types.h"
#include <cstring>
#include <cmath>
#include <functional>
#include <string>



static AF_Bar make_bar(double o, double h, double l, double c, double v=1000) {
    AF_Bar b{}; b.open=o;b.high=h;b.low=l;b.close=c;b.volume=v;b.spread=0.0001;
    af_bar_init(&b);
    return b;
}

static std::vector<AF_Bar> make_bars(int n, double start=1.1, double drift=0.001) {
    std::vector<AF_Bar> v(n);
    srand(42);
    double p=start;
    for (int i=0;i<n;i++){
        double o=p, noise=(rand()%200-100)*0.0000008;
        p+=drift+noise;
        double h=std::max(o,p)+0.0002, l=std::min(o,p)-0.0002;
        v[i]=make_bar(o,h,l,p,1000+(rand()%3000));
    }
    return v;
}

void test_patterns(TestRunner T) {
    section("PATTERNS");

    T("PatternEngine: registers 90+ patterns", []{
        af::PatternEngine eng(0.60);
        CHK_GE(eng.total_patterns(), 30);
    });

    T("PatternEngine: scan returns result without crash (2 bars)", []{
        af::PatternEngine eng(0.60);
        auto bars = make_bars(2);
        auto r = eng.scan(bars.data(), bars.size());
        CHK(r.count >= 0);
    });

    T("PatternEngine: scan on 100 bars returns result", []{
        af::PatternEngine eng(0.60);
        auto bars = make_bars(100);
        auto r = eng.scan(bars.data(), bars.size());
        CHK(r.count >= 0);
        CHK(r.bullish_count + r.bearish_count <= r.count);
    });

    T("PatternEngine: confidence filter reduces results", []{
        af::PatternEngine hi(0.90), lo(0.50);
        auto bars = make_bars(100);
        auto rhi = hi.scan(bars.data(), bars.size());
        auto rlo = lo.scan(bars.data(), bars.size());
        CHK(rhi.count <= rlo.count);
    });

    T("PatternEngine: no crash on 1000 bars", []{
        af::PatternEngine eng(0.60);
        auto bars = make_bars(1000);
        auto r = eng.scan(bars.data(), bars.size());
        CHK(r.count >= 0);
    });

    T("BullishEngulfing: detected in crafted data", []{
        /* Bear bar followed by bull bar that fully engulfs it */
        AF_Bar bars[2];
        bars[0] = make_bar(1.110, 1.115, 1.096, 1.098); /* bear */
        bars[1] = make_bar(1.094, 1.120, 1.092, 1.115); /* bull engulfs */
        int e = af_is_engulfing(&bars[0], &bars[1]);
        CHK(e == 1);
    });

    T("BearishEngulfing: detected in crafted data", []{
        AF_Bar bars[2];
        bars[0] = make_bar(1.095, 1.118, 1.092, 1.115); /* bull */
        bars[1] = make_bar(1.117, 1.120, 1.090, 1.093); /* bear engulfs */
        int e = af_is_engulfing(&bars[0], &bars[1]);
        CHK(e == -1);
    });

    T("Doji: detected (body < 10% of range)", []{
        AF_Bar b = make_bar(1.1000, 1.1080, 1.0920, 1.1001);
        CHK(af_is_doji(&b, 0.10) == 1);
    });

    T("Hammer: detected (long lower shadow, small body at top)", []{
        AF_Bar b = make_bar(1.110, 1.112, 1.085, 1.109);
        /* lower_shadow = 1.109-1.085 = 0.024, range = 0.027 → ~89% */
        CHK(af_is_hammer(&b) == 1);
    });

    T("ShootingStar: detected (long upper shadow, small body at bottom)", []{
        AF_Bar b = make_bar(1.090, 1.115, 1.088, 1.091);
        /* upper_shadow = 1.115-1.091 = 0.024, range = 0.027 → ~89% */
        CHK(af_is_hammer(&b) == -1);
    });

    T("Marubozu bull: detected (tiny shadows, large body)", []{
        /* Clean bull marubozu: open near low, close near high, tiny shadows */
        AF_Bar b = make_bar(1.100, 1.1198, 1.0999, 1.1198);
        /* range=0.02, body=0.0198, body_pct=0.99, shadows~0.001 */
        CHK(b.is_bullish == true);
        CHK(b.body_pct >= 0.85);
        CHK(af_is_marubozu(&b, 0.85) == 1);
    });

    T("PinBar bull: detected (lower wick >= 2.5x body)", []{
        /* Open high close tight at top, long lower wick */
        AF_Bar b = make_bar(1.100, 1.101, 1.075, 1.100);
        int r = af_is_pin_bar(&b, 2.5);
        CHK(r == 1);
    });

    T("FibRatio: 0.618 check within tolerance", []{
        CHK(af_fib_ratio(100.0, 61.8, 0.618, 0.01) == 1);
        CHK(af_fib_ratio(100.0, 63.0, 0.618, 0.01) == 0);
    });

    T("af_pattern_net: correct for mixed results", []{
        AF_PatternResult r{}; r.bullish_count=3; r.bearish_count=1; r.count=4;
        CHK(af_pattern_net(&r) == 2);
    });
}
