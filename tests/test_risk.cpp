/**
 * AlgoForge — tests/test_risk.cpp
 */
#include "test_helpers.hpp"
#include "risk/risk_types.h"
#include "indicators/indicators.h"
#include "core/types.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <string>

static void AF_ASSERT(bool c,const char *e,const char *f,int l){
    if(!c){char m[512];snprintf(m,512,"%s:%d: ASSERT(%s)",f,l,e);throw std::string(m);}
}
#define CHK(e)       AF_ASSERT((e),#e,__FILE__,__LINE__)
#define CHK_GT(a,b)  AF_ASSERT((a)>(b),#a ">" #b,__FILE__,__LINE__)
#define CHK_GE(a,b)  AF_ASSERT((a)>=(b),#a ">=" #b,__FILE__,__LINE__)
#define CHK_LE(a,b)  AF_ASSERT((a)<=(b),#a "<=" #b,__FILE__,__LINE__)
#define CHK_NEAR(a,b,t) AF_ASSERT(std::fabs((a)-(b))<=(t),#a "~=" #b,__FILE__,__LINE__)


static AF_SymbolInfo make_sym() {
    AF_SymbolInfo s{};
    snprintf(s.name,12,"EURUSD");
    s.digits=5; s.point=0.00001; s.contract_size=100000;
    s.volume_min=0.01; s.volume_max=100; s.volume_step=0.01;
    return s;
}

void test_risk(TestRunner T) {
    section("RISK MANAGEMENT");

    T("PositionSizer: fixed-risk gives valid lots", []{
        auto sym = make_sym();
        AF_SizeResult r{};
        CHK(af_size_position(AF_SIZE_FIXED_RISK,10000,1.1000,1.0950,
                             &sym,0.001,0.01,10.0,0.01,0.55,2.0,&r)==AF_OK);
        CHK_GE(r.lots, 0.01);
        CHK_GT(r.risk_amount, 0);
        CHK_LE(r.risk_pct, 0.02);
    });

    T("PositionSizer: ATR-risk method", []{
        auto sym = make_sym();
        AF_SizeResult r{};
        CHK(af_size_position(AF_SIZE_ATR_RISK,10000,1.1000,0,
                             &sym,0.0012,0.01,10.0,0.01,0.55,2.0,&r)==AF_OK);
        CHK_GE(r.lots, 0.01);
    });

    T("PositionSizer: Kelly method gives positive lots", []{
        auto sym = make_sym();
        AF_SizeResult r{};
        CHK(af_size_position(AF_SIZE_KELLY,10000,1.1000,1.0950,
                             &sym,0.001,0.02,10.0,0.01,0.60,2.0,&r)==AF_OK);
        CHK_GT(r.lots, 0);
    });

    T("PositionSizer: caps at max_lots", []{
        auto sym = make_sym();
        AF_SizeResult r{};
        /* Huge balance + tiny stop → would give massive lots */
        af_size_position(AF_SIZE_FIXED_RISK,10000000,1.1000,1.09999,
                         &sym,0.001,0.01,1.0,0.01,0.55,2.0,&r);
        CHK_LE(r.lots, 1.0);
        CHK(r.capped == 1);
    });

    T("PositionSizer: min_lots floor applied", []{
        auto sym = make_sym();
        AF_SizeResult r{};
        /* Very small balance + large stop → would give < min_lots */
        af_size_position(AF_SIZE_FIXED_RISK,10,1.1000,0.1,
                         &sym,0.001,0.01,10.0,0.01,0.55,2.0,&r);
        CHK_GE(r.lots, 0.01);
    });

    T("Kelly fraction: positive for edge > 0", []{
        double f = af_kelly_fraction(0.60, 2.0, 0.02);
        CHK_GT(f, 0);
        CHK_LE(f, 0.02);
    });

    T("Kelly fraction: capped at max_risk", []{
        double f = af_kelly_fraction(0.90, 5.0, 0.01); /* Very high edge */
        CHK_LE(f, 0.01);
    });

    T("Kelly fraction: floored at 0.005 when edge negative", []{
        double f = af_kelly_fraction(0.10, 2.0, 0.02); /* Edge = 0.10*2-(0.90) = -0.70 */
        CHK_GE(f, 0.005);
    });

    T("NormalizeLots: rounds to step", []{
        CHK_NEAR(af_normalize_lots(0.123, 0.01, 0.01, 100.0), 0.12, 1e-9);
        CHK_NEAR(af_normalize_lots(0.178, 0.05, 0.05, 100.0), 0.20, 1e-9);
    });

    T("NormalizeLots: clamps to [min,max]", []{
        CHK_NEAR(af_normalize_lots(0.001, 0.01, 0.01, 100.0), 0.01, 1e-9);
        CHK_NEAR(af_normalize_lots(999.0, 0.01, 0.01,   5.0),  5.0, 1e-9);
    });

    T("Pivot: R1 > P > S1 (classic)", []{
        double r3,r2,r1,p,s1,s2,s3;
        af_pivot_classic(1.200,1.000,1.100,&r3,&r2,&r1,&p,&s1,&s2,&s3);
        CHK_GT(r1,p); CHK_GT(p,s1); CHK_GT(r2,r1); CHK_GT(s1,s2);
    });

    T("Pivot: R1 > P > S1 (fibonacci)", []{
        double r3,r2,r1,p,s1,s2,s3;
        af_pivot_fibonacci(1.200,1.000,1.100,&r3,&r2,&r1,&p,&s1,&s2,&s3);
        CHK_GT(r1,p); CHK_GT(p,s1);
    });

    T("Pivot: camarilla R4 > R3 > R2 > R1 > S1 > S2", []{
        double r4,r3,r2,r1,s1,s2,s3,s4;
        af_pivot_camarilla(1.200,1.000,1.100,&r4,&r3,&r2,&r1,&s1,&s2,&s3,&s4);
        CHK_GT(r4,r3); CHK_GT(r3,r2); CHK_GT(r2,r1);
        CHK_GT(r1,s1); CHK_GT(s1,s2);
    });
}
