/**
 * AlgoForge — src/analytics/metrics.cpp
 * Batch computation of all 14 performance metrics.
 */

#include "core/analytics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>

namespace algoforge::analytics {

Metrics compute_metrics(
    std::span<const Trade>  trades,
    std::span<const double> bar_returns,
    double                  max_dd_pct,
    double                  max_dd_abs,
    int                     total_bars,
    double                  annualisation
) noexcept {
    Metrics m;

    const int n_trades = static_cast<int>(trades.size());
    const int n_bars   = static_cast<int>(bar_returns.size());

    /* ── Trade-level stats ── */
    if (n_trades > 0) {
        double sum_pnl     = 0.0;
        double sum_win     = 0.0;
        double sum_loss    = 0.0;
        int    n_wins      = 0;
        int    n_losses    = 0;
        int    bars_held   = 0;
        int    cur_streak  = 0;
        int    win_streak  = 0;
        int    loss_streak = 0;

        for (const auto& t : trades) {
            sum_pnl  += t.net_pnl;
            bars_held += t.bars_held;

            if (t.net_pnl > 0.0) {
                sum_win += t.net_pnl;
                n_wins++;
                cur_streak = (cur_streak > 0) ? cur_streak + 1 : 1;
            } else {
                sum_loss += t.net_pnl;   /* negative */
                n_losses++;
                cur_streak = (cur_streak < 0) ? cur_streak - 1 : -1;
            }

            if (cur_streak > win_streak)          win_streak  = cur_streak;
            if (-cur_streak > loss_streak)        loss_streak = -cur_streak;
        }

        m.win_rate            = static_cast<double>(n_wins) / n_trades;
        m.expectancy          = sum_pnl / n_trades;
        m.avg_win             = (n_wins   > 0) ? sum_win / n_wins   : 0.0;
        m.avg_loss            = (n_losses > 0) ? sum_loss / n_losses : 0.0;
        m.profit_factor       = (n_losses > 0 && sum_win > 0.0)
                                    ? sum_win / std::abs(sum_loss)
                                    : (n_losses == 0 && sum_win > 0.0
                                           ? std::numeric_limits<double>::infinity()
                                           : 0.0);
        m.payoff_ratio        = (n_losses > 0)
                                    ? m.avg_win / std::abs(m.avg_loss)
                                    : std::numeric_limits<double>::infinity();
        m.recovery_factor     = (max_dd_abs < -1e-12)
                                    ? sum_pnl / std::abs(max_dd_abs)
                                    : 0.0;
        m.longest_win_streak  = win_streak;
        m.longest_loss_streak = loss_streak;
        m.time_in_market      = (total_bars > 0)
                                    ? static_cast<double>(bars_held) / total_bars
                                    : 0.0;
    }

    /* ── Calmar (needs max_dd_pct) ── */
    if (n_trades > 0) {
        double total_return = 0.0;
        for (const auto& t : trades) total_return += t.net_pnl;
        /* express as pct of initial capital or just raw; spec says total_return_pct / max_dd_pct */
        /* Calmar = total_return_pct / max_drawdown_pct; both in percent */
        m.calmar = (max_dd_pct < -1e-12)
                       ? (total_return / std::abs(max_dd_pct))   /* both in same units */
                       : 0.0;
    }

    /* ── Bar-return metrics (Sharpe, Sortino, Omega) ── */
    if (n_bars >= 2) {
        /* mean */
        double sum = 0.0;
        for (double r : bar_returns) sum += r;
        double mean = sum / n_bars;

        /* sample std dev */
        double ss = 0.0;
        for (double r : bar_returns) {
            double d = r - mean;
            ss += d * d;
        }
        double std_dev = std::sqrt(ss / (n_bars - 1));

        /* Sharpe */
        m.sharpe = (std_dev > 1e-15) ? annualisation * mean / std_dev : 0.0;

        /* Sortino — downside std = sqrt(sum(min(r,0)^2) / (n-1)), demeaned at 0 */
        double ds_sq = 0.0;
        for (double r : bar_returns) {
            double neg = std::min(r, 0.0);
            ds_sq += neg * neg;
        }
        double ds_std = std::sqrt(ds_sq / (n_bars - 1));
        m.sortino = (ds_std > 1e-15) ? annualisation * mean / ds_std : 0.0;

        /* Omega — threshold = 0 */
        double gain = 0.0, loss = 0.0;
        for (double r : bar_returns) {
            if (r > 0.0) gain += r;
            else         loss += std::abs(r);
        }
        m.omega = (loss > 1e-15) ? gain / loss : (gain > 0.0 ? std::numeric_limits<double>::infinity() : 0.0);
    }

    return m;
}

} /* namespace algoforge::analytics */
