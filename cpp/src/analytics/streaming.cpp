/**
 * AlgoForge — src/analytics/streaming.cpp
 * OnlineMetrics: incremental trade processing with Welford's algorithm.
 */

#include "core/analytics.hpp"

#include <cmath>
#include <limits>

namespace algoforge::analytics {

void OnlineMetrics::reset() noexcept {
    n_trades_            = 0;
    n_wins_              = 0;
    n_losses_            = 0;
    total_bars_held_     = 0;
    welford_mean_        = 0.0;
    welford_m2_          = 0.0;
    sum_winning_pnl_     = 0.0;
    sum_losing_pnl_abs_  = 0.0;
    current_streak_      = 0;
    longest_win_streak_  = 0;
    longest_loss_streak_ = 0;
    running_equity_      = initial_equity_;
    peak_equity_         = initial_equity_;
    max_dd_abs_          = 0.0;
    max_dd_pct_          = 0.0;
}

void OnlineMetrics::update(const Trade& trade) noexcept {
    ++n_trades_;
    total_bars_held_ += trade.bars_held;

    /* Welford's online mean/variance for PnL */
    double delta      = trade.net_pnl - welford_mean_;
    welford_mean_    += delta / n_trades_;
    double delta2     = trade.net_pnl - welford_mean_;
    welford_m2_      += delta * delta2;

    /* Win/loss tracking */
    if (trade.net_pnl > 0.0) {
        ++n_wins_;
        sum_winning_pnl_ += trade.net_pnl;
        current_streak_   = (current_streak_ > 0) ? current_streak_ + 1 : 1;
    } else {
        ++n_losses_;
        sum_losing_pnl_abs_ += std::abs(trade.net_pnl);
        current_streak_      = (current_streak_ < 0) ? current_streak_ - 1 : -1;
    }

    if (current_streak_ > longest_win_streak_)
        longest_win_streak_ = current_streak_;
    if (-current_streak_ > longest_loss_streak_)
        longest_loss_streak_ = -current_streak_;

    /* Equity and drawdown */
    running_equity_ += trade.net_pnl;
    if (running_equity_ > peak_equity_)
        peak_equity_ = running_equity_;

    double dd_abs = running_equity_ - peak_equity_;
    double dd_pct = (peak_equity_ > 1e-15) ? (dd_abs / peak_equity_ * 100.0) : 0.0;

    if (dd_abs < max_dd_abs_) max_dd_abs_ = dd_abs;
    if (dd_pct < max_dd_pct_) max_dd_pct_ = dd_pct;
}

Metrics OnlineMetrics::snapshot() const noexcept {
    Metrics m;
    if (n_trades_ == 0) return m;

    /* Variance from Welford */
    double var_sample = (n_trades_ > 1) ? welford_m2_ / (n_trades_ - 1) : 0.0;
    (void)var_sample;   /* std_pnl available for future use in Sharpe/Sortino streaming extensions */

    m.expectancy          = welford_mean_;
    m.win_rate            = static_cast<double>(n_wins_) / n_trades_;
    m.avg_win             = (n_wins_   > 0) ? sum_winning_pnl_ / n_wins_ : 0.0;
    m.avg_loss            = (n_losses_ > 0) ? -(sum_losing_pnl_abs_ / n_losses_) : 0.0;
    m.profit_factor       = (sum_losing_pnl_abs_ > 1e-15 && sum_winning_pnl_ > 0.0)
                                ? sum_winning_pnl_ / sum_losing_pnl_abs_
                                : (sum_losing_pnl_abs_ < 1e-15 && sum_winning_pnl_ > 0.0
                                       ? std::numeric_limits<double>::infinity()
                                       : 0.0);
    m.payoff_ratio        = (n_losses_ > 0 && std::abs(m.avg_loss) > 1e-15)
                                ? m.avg_win / std::abs(m.avg_loss)
                                : std::numeric_limits<double>::infinity();
    m.longest_win_streak  = longest_win_streak_;
    m.longest_loss_streak = longest_loss_streak_;

    double total_pnl = running_equity_ - initial_equity_;
    m.recovery_factor = (max_dd_abs_ < -1e-12)
                            ? total_pnl / std::abs(max_dd_abs_)
                            : 0.0;

    /* Note: Sharpe/Sortino/Calmar/Omega require the full bar-return series
       which is not maintained by OnlineMetrics (trades != bars).
       These remain 0 in streaming mode. Calmar needs max_dd_pct. */
    m.calmar = 0.0;   /* bar returns not available in streaming mode */
    m.sharpe = 0.0;
    m.sortino = 0.0;
    m.omega  = 0.0;

    return m;
}

} /* namespace algoforge::analytics */
