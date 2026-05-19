/**
 * AlgoForge — c/src/risk.c
 * Round 10: Position sizing implementation.
 *
 * Ports the C++ RiskManager::normalize_lots / kelly_fraction /
 * size_position logic to pure C.
 */

#include <math.h>
#include <stddef.h>
#include "../include/af_risk.h"

/* ──────────────────────────────────────────────────────────────────────────
 * af_normalize_lots
 * ────────────────────────────────────────────────────────────────────────── */
double af_normalize_lots(double lots,
                          double step,
                          double min_lots,
                          double max_lots)
{
    if (step <= 0.0) step = 0.01;

    /* Round to nearest step */
    double n = round(lots / step) * step;

    /* Clamp to broker limits */
    if (n < min_lots) n = min_lots;
    if (n > max_lots) n = max_lots;

    return n;
}

/* ──────────────────────────────────────────────────────────────────────────
 * af_kelly_fraction
 * ────────────────────────────────────────────────────────────────────────── */
double af_kelly_fraction(double win_rate, double rr, double max_risk)
{
    double edge  = win_rate * rr - (1.0 - win_rate);
    double kelly = (rr > 1e-10) ? edge / rr : 0.0;
    double half  = kelly * 0.5;

    /* Floor at 0.005 (half-percent minimum meaningful stake) */
    if (half < 0.005) half = 0.005;

    /* Cap at caller-supplied max risk */
    if (half > max_risk) half = max_risk;

    return half;
}

/* ──────────────────────────────────────────────────────────────────────────
 * af_size_position
 * ────────────────────────────────────────────────────────────────────────── */
af_error_t af_size_position(af_sizing_method_t method,
                             double balance,
                             double entry_price,
                             double stop_loss,
                             const af_symbol_info_t *sym,
                             double atr,
                             double risk_pct,
                             double max_lots,
                             double min_lots,
                             double win_rate,
                             double rr_ratio,
                             af_size_result_t *out)
{
    if (!sym || !out || balance <= 0.0)
        return AF_ERR_INVALID_PARAM;

    /* ── Determine stop distance ── */
    double stop_dist;
    if (stop_loss <= 0.0 && atr > 0.0) {
        stop_dist = atr * 1.5;
    } else {
        stop_dist = fabs(entry_price - stop_loss);
    }
    /* Fallback: 0.5 % of entry price */
    if (stop_dist < 1e-10) {
        stop_dist = entry_price * 0.005;
    }

    /* ── Effective risk fraction ── */
    double eff_risk;
    if (method == AF_SIZE_KELLY) {
        eff_risk = af_kelly_fraction(win_rate, rr_ratio, risk_pct);
    } else {
        eff_risk = risk_pct;
    }

    double risk_amount = balance * eff_risk;

    /* ── Symbol parameters ── */
    double point         = (sym->point > 0.0)         ? sym->point         : 0.00001;
    double contract_size = (sym->contract_size > 0.0) ? sym->contract_size : 100000.0;

    double val_per_point = contract_size * point;   /* value of 1 point per 1 lot */
    double stop_in_pts   = stop_dist / point;

    /* ── Raw lot calculation ── */
    double raw_lots;
    if (stop_in_pts > 1e-10 && val_per_point > 1e-10) {
        /*
         * risk_amount = lots * stop_in_pts * val_per_point
         * => lots = risk_amount / (stop_in_pts * val_per_point)
         */
        raw_lots = risk_amount / (stop_in_pts * val_per_point);
    } else {
        raw_lots = min_lots;
    }

    /* ── Normalise ── */
    double step = (sym->volume_step > 0.0) ? sym->volume_step : 0.01;
    double vmin = (min_lots > 0.0)         ? min_lots         : sym->volume_min;
    double vmax = (max_lots > 0.0)         ? max_lots         : sym->volume_max;

    /* Ensure vmin/vmax are sane */
    if (vmin <= 0.0) vmin = 0.01;
    if (vmax <= 0.0) vmax = 100.0;

    int capped = 0;
    if (raw_lots < vmin || raw_lots > vmax) capped = 1;

    double final_lots = af_normalize_lots(raw_lots, step, vmin, vmax);

    /* Recalculate actual risk at the final lot size */
    double actual_risk_amount = final_lots * stop_in_pts * val_per_point;
    double actual_risk_pct    = (balance > 1e-10) ? actual_risk_amount / balance : 0.0;

    /* ── Fill result ── */
    out->lots          = final_lots;
    out->risk_amount   = actual_risk_amount;
    out->risk_pct      = actual_risk_pct;
    out->stop_distance = stop_dist;
    out->capped        = capped;

    return AF_OK;
}
