/**
 * AlgoForge — src/risk/drawdown_guard.cpp
 */
#include "risk/risk_types.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

namespace af {

class DrawdownGuard {
public:
    DrawdownGuard(double daily_limit=0.05, double total_limit=0.15)
        : daily_limit_(daily_limit), total_limit_(total_limit) {}

    void initialise(double balance, double equity) {
        peak_equity_       = equity;
        daily_start_equity_= equity;
        initial_balance_   = balance;
        state_             = AF_GUARD_GREEN;
        reset_day_         = current_day();
        halt_count_        = 0;
        printf("[DrawdownGuard] Init | equity=%.2f daily_lim=%.1f%% total_lim=%.1f%%\n",
               equity, daily_limit_*100, total_limit_*100);
    }

    AF_GuardStatus check(double equity, double balance) {
        /* Midnight reset */
        int today = current_day();
        if (today != reset_day_) {
            daily_start_equity_ = equity;
            reset_day_          = today;
            if (state_ == AF_GUARD_RED) {
                state_ = AF_GUARD_GREEN;
                halt_reason_[0] = '\0';
                printf("[DrawdownGuard] Daily reset — trading re-enabled.\n");
            }
        }

        if (equity > peak_equity_) peak_equity_ = equity;

        double daily_pnl     = equity - daily_start_equity_;
        double daily_pct     = daily_pnl / (daily_start_equity_ + 1e-10);
        double total_dd      = (peak_equity_ - equity) / (peak_equity_ + 1e-10);
        double warn_thresh   = 0.75;

        AF_GuardStatus st{};
        st.daily_pnl       = daily_pnl;
        st.daily_pnl_pct   = daily_pct;
        st.total_dd_pct    = total_dd;
        st.peak_equity     = peak_equity_;
        st.current_equity  = equity;

        if (daily_pct <= -daily_limit_ || total_dd >= total_limit_) {
            state_ = AF_GUARD_RED;
            if (daily_pct <= -daily_limit_)
                snprintf(halt_reason_, sizeof(halt_reason_),
                         "Daily loss %.1f%% >= limit %.1f%%",
                         -daily_pct*100, daily_limit_*100);
            else
                snprintf(halt_reason_, sizeof(halt_reason_),
                         "Total drawdown %.1f%% >= limit %.1f%%",
                         total_dd*100, total_limit_*100);
            halt_count_++;
            printf("[CRITICAL] TRADING HALTED: %s\n", halt_reason_);
        } else if (daily_pct <= -(daily_limit_*warn_thresh) ||
                   total_dd  >=  total_limit_*warn_thresh) {
            if (state_ == AF_GUARD_GREEN) {
                state_ = AF_GUARD_YELLOW;
                printf("[WARN] DrawdownGuard YELLOW daily=%.2f%% dd=%.2f%%\n",
                       -daily_pct*100, total_dd*100);
            }
        } else {
            if (state_ == AF_GUARD_YELLOW) state_ = AF_GUARD_GREEN;
        }

        st.state           = state_;
        st.trading_allowed = (state_ != AF_GUARD_RED) ? 1 : 0;
        snprintf(st.reason, sizeof(st.reason), "%s",
                 state_ == AF_GUARD_RED ? halt_reason_ : "");
        return st;
    }

    AF_GuardState state()       const { return state_; }
    bool trading_allowed()      const { return state_ != AF_GUARD_RED; }
    int  halt_count()           const { return halt_count_; }
    const char* halt_reason()   const { return halt_reason_; }

private:
    double daily_limit_, total_limit_;
    double peak_equity_{0}, daily_start_equity_{0}, initial_balance_{0};
    AF_GuardState state_{AF_GUARD_GREEN};
    int    reset_day_{0}, halt_count_{0};
    char   halt_reason_[256]{};

    static int current_day() {
        time_t t = time(nullptr);
        struct tm *tm = gmtime(&t);
        return tm ? tm->tm_yday + tm->tm_year*1000 : 0;
    }
};

} /* namespace af */
