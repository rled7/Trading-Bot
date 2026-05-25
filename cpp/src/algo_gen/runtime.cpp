/**
 * AlgoForge — src/algo_gen/runtime.cpp
 * S1 Phase 1 — ManifestAlgo runtime: indicator dispatch + simple backtest.
 *
 * Mirrors python/algoforge/algo_gen/runtime.py logic.
 * Uses a self-contained bar-by-bar engine (no C++ backtesting:: dependency)
 * to avoid including the full engine in the algo_gen library.
 */

#include "algo_gen_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

/* ── C indicator library (compiled separately) ── */
extern "C" {
#include "indicators/indicators.h"
}

namespace algoforge::algo_gen {

/* =========================================================================
 * Indicator value extraction helpers
 * ========================================================================= */

static double last_value(const double* arr, int n) {
    if (!arr || n <= 0) return std::numeric_limits<double>::quiet_NaN();
    double v = arr[n - 1];
    if (!std::isfinite(v) || v == 0.0) return v; // allow zero (e.g. OBV)
    return v;
}

/* =========================================================================
 * compute_indicator — dispatch to C indicator library
 * ========================================================================= */

double compute_indicator(const IndicatorDecl& ind, const std::vector<Bar>& bars) {
    int n = (int)bars.size();
    if (n < 2) return std::numeric_limits<double>::quiet_NaN();

    // Extract series
    std::vector<double> closes(n), opens(n), highs(n), lows(n), vols(n);
    for (int i = 0; i < n; ++i) {
        closes[i] = bars[i].close;
        opens[i]  = bars[i].open;
        highs[i]  = bars[i].high;
        lows[i]   = bars[i].low;
        vols[i]   = bars[i].volume;
    }

    const auto& p = ind.params;
    auto pi = [&](const char* k, int def) -> int {
        auto it = p.find(k);
        return it != p.end() ? std::max(1, (int)it->second) : def;
    };
    auto pf = [&](const char* k, double def) -> double {
        auto it = p.find(k);
        return it != p.end() ? it->second : def;
    };

    const std::string& kind = ind.kind;

    // Output buffer
    std::vector<double> out(n, 0.0);
    std::vector<double> out2(n, 0.0), out3(n, 0.0);

    if (kind == "sma") {
        int per = pi("period", 14);
        af_sma(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "ema") {
        int per = pi("period", 14);
        af_ema(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "rsi") {
        int per = pi("period", 14);
        af_rsi(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "atr") {
        int per = pi("period", 14);
        af_atr(highs.data(), lows.data(), closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "wma") {
        int per = pi("period", 14);
        af_wma(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "macd") {
        int fast   = pi("fast",   12);
        int slow_p = pi("slow",   26);
        int sig    = pi("signal",  9);
        af_macd(closes.data(), n, fast, slow_p, sig, out.data(), out2.data(), out3.data());
        return last_value(out.data(), n);   // MACD line
    }
    if (kind == "bollinger") {
        int    per  = pi("period", 20);
        double mult = pf("mult",    2.0);
        af_bollinger(closes.data(), n, per, mult,
                     out.data(), out2.data(), out3.data(), nullptr, nullptr);
        return last_value(out2.data(), n);   // middle band
    }
    if (kind == "stochastic") {
        int kp = pi("k_period", 14);
        int dp = pi("d_period",  3);
        af_stochastic(highs.data(), lows.data(), closes.data(), n, kp, dp, 3,
                      out.data(), out2.data());
        return last_value(out.data(), n);   // %K
    }
    if (kind == "obv") {
        af_obv(closes.data(), vols.data(), n, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "cci") {
        int    per  = pi("period",   20);
        double cons = pf("constant", 0.015);
        af_cci(highs.data(), lows.data(), closes.data(), n, per, cons, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "williams_r") {
        int per = pi("period", 14);
        af_williams_r(highs.data(), lows.data(), closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "roc") {
        int per = pi("period", 14);
        af_roc(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "mfi") {
        int per = pi("period", 14);
        af_mfi(highs.data(), lows.data(), closes.data(), vols.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "vwap") {
        std::vector<double> u1(n,0), u2(n,0), l1(n,0), l2(n,0);
        af_vwap(highs.data(), lows.data(), closes.data(), vols.data(), n,
                out.data(), u1.data(), u2.data(), l1.data(), l2.data());
        return last_value(out.data(), n);
    }
    if (kind == "adx") {
        int per = pi("period", 14);
        af_adx(highs.data(), lows.data(), closes.data(), n, per,
               out.data(), out2.data(), out3.data());
        return last_value(out.data(), n);   // ADX value
    }
    if (kind == "keltner") {
        int    ep = pi("ema_period", 20);
        int    ap = pi("atr_period", 10);
        double ml = pf("mult",        2.0);
        af_keltner(highs.data(), lows.data(), closes.data(), n, ep, ap, ml,
                   out.data(), out2.data(), out3.data());
        return last_value(out.data(), n);   // middle
    }
    if (kind == "hma") {
        int per = pi("period", 14);
        af_hma(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "dema") {
        int per = pi("period", 14);
        af_dema(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "tema") {
        int per = pi("period", 14);
        af_tema(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "trix") {
        int per = pi("period", 14);
        af_trix(closes.data(), n, per, 9, out.data(), out2.data());
        return last_value(out.data(), n);
    }
    if (kind == "momentum") {
        int per = pi("period", 14);
        af_momentum(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "wilder_ema") {
        int per = pi("period", 14);
        af_wilder_ema(closes.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "vwma") {
        int per = pi("period", 14);
        af_vwma(closes.data(), vols.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "hist_vol") {
        int per   = pi("period",  20);
        int tdays = pi("tdays",  252);
        af_hist_vol(closes.data(), n, per, tdays, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "cmf") {
        int per = pi("period", 20);
        af_cmf(highs.data(), lows.data(), closes.data(), vols.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "acc_dist") {
        af_acc_dist(highs.data(), lows.data(), closes.data(), vols.data(), n, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "force_index") {
        int per = pi("period", 13);
        af_force_index(closes.data(), vols.data(), n, per, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "vol_osc") {
        int fast   = pi("fast", 14);
        int slow_p = pi("slow", 28);
        af_vol_osc(vols.data(), n, fast, slow_p, out.data());
        return last_value(out.data(), n);
    }
    if (kind == "donchian") {
        int per = pi("period", 20);
        af_donchian(highs.data(), lows.data(), n, per,
                    out.data(), out2.data(), out3.data());
        return last_value(out.data(), n);   // upper
    }
    if (kind == "true_range") {
        af_true_range(highs.data(), lows.data(), closes.data(), n, out.data());
        return last_value(out.data(), n);
    }

    return std::numeric_limits<double>::quiet_NaN();
}

/* =========================================================================
 * Simple backtest engine (bar-by-bar, DSL-driven)
 * ========================================================================= */

namespace {

// Compute ATR for bracket sizing
static double compute_atr_val(const std::vector<Bar>& bars, int period = 14) {
    int n = (int)bars.size();
    if (n < period + 1) return 0.0;
    std::vector<double> h(n), l(n), c(n), out(n, 0.0);
    for (int i = 0; i < n; ++i) { h[i]=bars[i].high; l[i]=bars[i].low; c[i]=bars[i].close; }
    af_atr(h.data(), l.data(), c.data(), n, period, out.data());
    double v = out[n-1];
    return std::isfinite(v) ? v : 0.0;
}

} // anon namespace

SimpleBacktestResult run_manifest_backtest(
    const AlgoManifest& m,
    const std::vector<Bar>& bars,
    const BacktestConfig& cfg
) {
    SimpleBacktestResult res;
    res.initial_capital = cfg.initial_capital;

    int n = (int)bars.size();
    if (n < 2) return res;

    // Collect declared indicator IDs for DSL
    std::vector<std::string> ind_ids;
    for (const auto& id : m.indicators) ind_ids.push_back(id.id);

    // Compile entry/exit DSL expressions
    struct Rule {
        std::string side;
        dsl::Expr   expr;
    };
    struct BracketRule {
        std::string    side;
        std::optional<double> sl_atr;
        std::optional<double> tp_atr;
    };

    std::vector<Rule>        entry_rules, exit_rules;
    std::vector<BracketRule> bracket_rules;

    for (const auto& e : m.entries) {
        try {
            auto expr = dsl::compile_expr(e.when, ind_ids);
            entry_rules.push_back({e.side, expr});
        } catch (...) {}
    }
    for (const auto& ex : m.exits) {
        if (ex.when) {
            try {
                auto expr = dsl::compile_expr(*ex.when, ind_ids);
                exit_rules.push_back({ex.side, expr});
            } catch (...) {}
        }
        if (ex.sl_atr || ex.tp_atr) {
            bracket_rules.push_back({ex.side, ex.sl_atr, ex.tp_atr});
        }
    }

    // Find ATR indicator id
    std::string atr_ind_id;
    for (const auto& id : m.indicators) {
        if (id.kind == "atr") { atr_ind_id = id.id; break; }
    }

    // Position state
    struct Position {
        std::string side;      // "long" | "short"
        double      entry_price;
        double      sl;
        double      tp;
        int         open_bar;
    };

    double equity = cfg.initial_capital;
    double peak   = equity;
    double max_dd = 0.0;
    res.equity_curve.push_back(equity);

    std::optional<Position> open_pos;
    int bars_since_exit = 9999;
    int cool_down = m.risk.cool_down_bars;

    auto close_pos = [&](double price, int bar_idx) {
        if (!open_pos) return;
        double pnl = 0.0;
        if (open_pos->side == "long") {
            pnl = (price - open_pos->entry_price) / open_pos->entry_price * equity;
        } else {
            pnl = (open_pos->entry_price - price) / open_pos->entry_price * equity;
        }
        // Simple fixed lot sizing: use 1% risk equivalent
        double risk_pct = 0.01;
        pnl = pnl * risk_pct - cfg.commission_usd / equity * equity * 0.001;
        // Normalise to dollar amount
        pnl = equity * risk_pct * ((open_pos->side == "long") ?
            (price - open_pos->entry_price) / open_pos->entry_price :
            (open_pos->entry_price - price) / open_pos->entry_price);
        pnl -= cfg.commission_usd * 0.01; // commission per trade
        equity += pnl;
        res.trade_pnls.push_back(pnl);
        res.total_trades++;
        open_pos.reset();
        bars_since_exit = 0;
        if (equity > peak) peak = equity;
        double dd = (peak > 0) ? (peak - equity) / peak * 100.0 : 0.0;
        if (dd > max_dd) max_dd = dd;
    };

    for (int i = cfg.warmup_bars; i < n; ++i) {
        bars_since_exit++;

        // Build sub-slice [0..i]
        std::vector<Bar> sub(bars.begin(), bars.begin() + i + 1);
        int sub_n = (int)sub.size();

        // Compute all indicators
        dsl::Env env;
        const Bar& cur = sub[sub_n - 1];
        env["close"]  = cur.close;
        env["open"]   = cur.open;
        env["high"]   = cur.high;
        env["low"]    = cur.low;
        env["volume"] = cur.volume;
        env["spread"] = cur.spread;

        for (const auto& ind_decl : m.indicators) {
            double v = compute_indicator(ind_decl, sub);
            env[ind_decl.id] = std::isfinite(v) ? v : std::numeric_limits<double>::quiet_NaN();
        }

        // ATR value for bracket sizing
        double atr_val = 0.0;
        if (!atr_ind_id.empty()) {
            auto it = env.find(atr_ind_id);
            if (it != env.end() && std::isfinite(it->second))
                atr_val = it->second;
        }
        if (atr_val == 0.0) atr_val = compute_atr_val(sub);

        double price = cur.close;

        // Check bracket exits
        if (open_pos) {
            bool hit_sl = false, hit_tp = false;
            if (open_pos->side == "long") {
                if (open_pos->sl > 0 && price <= open_pos->sl) hit_sl = true;
                if (open_pos->tp > 0 && price >= open_pos->tp) hit_tp = true;
            } else {
                if (open_pos->sl > 0 && price >= open_pos->sl) hit_sl = true;
                if (open_pos->tp > 0 && price <= open_pos->tp) hit_tp = true;
            }
            if (hit_sl || hit_tp) {
                double exit_price = hit_tp ? open_pos->tp : open_pos->sl;
                close_pos(exit_price, i);
            }
        }

        // Check DSL exit rules
        if (open_pos) {
            for (const auto& rule : exit_rules) {
                if (rule.side == open_pos->side) {
                    if (dsl::eval_expr(rule.expr, env)) {
                        close_pos(price, i);
                        break;
                    }
                }
            }
        }

        // Check entry rules
        if (!open_pos && bars_since_exit >= cool_down) {
            for (const auto& rule : entry_rules) {
                if (dsl::eval_expr(rule.expr, env)) {
                    // Compute SL/TP from bracket rules
                    double sl = 0.0, tp = 0.0;
                    for (const auto& br : bracket_rules) {
                        if (br.side == rule.side) {
                            if (br.sl_atr && atr_val > 0)
                                sl = (rule.side == "long") ?
                                    price - *br.sl_atr * atr_val :
                                    price + *br.sl_atr * atr_val;
                            if (br.tp_atr && atr_val > 0)
                                tp = (rule.side == "long") ?
                                    price + *br.tp_atr * atr_val :
                                    price - *br.tp_atr * atr_val;
                            break;
                        }
                    }
                    open_pos = Position{rule.side, price, sl, tp, i};
                    break;
                }
            }
        }

        // Update equity curve
        double cur_equity = equity;
        if (open_pos) {
            // Unrealised P&L (approximate)
            double upnl = 0.0;
            if (open_pos->side == "long")
                upnl = (price - open_pos->entry_price) / open_pos->entry_price * equity * 0.01;
            else
                upnl = (open_pos->entry_price - price) / open_pos->entry_price * equity * 0.01;
            cur_equity = equity + upnl;
        }
        res.equity_curve.push_back(cur_equity);
        if (!std::isfinite(cur_equity)) { res.has_nan_inf = true; }
    }

    // Close any open position at end
    if (open_pos) {
        close_pos(bars.back().close, n - 1);
    }

    // Compute max drawdown
    double pk = res.equity_curve[0];
    for (double eq : res.equity_curve) {
        if (eq > pk) pk = eq;
        double dd = (pk > 0) ? (pk - eq) / pk * 100.0 : 0.0;
        if (dd > res.max_dd_pct) res.max_dd_pct = dd;
        if (!std::isfinite(eq)) res.has_nan_inf = true;
    }

    // Compute Sharpe ratio
    if (res.equity_curve.size() >= 2) {
        std::vector<double> rets;
        for (size_t j = 1; j < res.equity_curve.size(); ++j) {
            if (res.equity_curve[j-1] > 1e-10) {
                rets.push_back((res.equity_curve[j] - res.equity_curve[j-1]) / res.equity_curve[j-1]);
            }
        }
        if (!rets.empty()) {
            double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size();
            double var  = 0.0;
            for (double r : rets) { double d = r - mean; var += d * d; }
            double sd = std::sqrt(var / rets.size());
            res.sharpe = (sd > 1e-10) ? mean / sd * std::sqrt(252.0) : 0.0;
        }
    }

    res.net_profit = equity - cfg.initial_capital;

    return res;
}

} /* namespace algoforge::algo_gen */
