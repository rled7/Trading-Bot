/**
 * AlgoForge — src/risk/position_sizer.cpp
 * Position sizing math (C-linkage) and DrawdownGuard C++ class.
 */
#include "risk/risk_types.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ── C implementations ── */

double af_normalize_lots(double lots, double step, double min_lots, double max_lots) {
    if (step <= 0) step = 0.01;
    double n = round(lots / step) * step;
    if (n < min_lots) n = min_lots;
    if (n > max_lots) n = max_lots;
    return n;
}

double af_kelly_fraction(double win_rate, double rr, double max_risk) {
    double edge  = win_rate * rr - (1.0 - win_rate);
    double kelly = (rr > AF_EPSILON) ? edge / rr : 0.0;
    double half  = kelly * 0.5;
    if (half < 0.005) half = 0.005;
    if (half > max_risk) half = max_risk;
    return half;
}

AF_Error af_size_position(
    AF_SizingMethod method,
    double balance,
    double entry_price,
    double stop_loss,
    const AF_SymbolInfo *sym,
    double atr,
    double risk_pct,
    double max_lots,
    double min_lots,
    double win_rate,
    double rr_ratio,
    AF_SizeResult *out)
{
    if (!sym || !out || balance <= 0) return AF_ERR_INVALID_PARAM;

    /* Derive stop distance */
    double stop_dist;
    if (stop_loss <= 0.0 && atr > 0) {
        stop_dist = atr * 1.5;
    } else {
        stop_dist = fabs(entry_price - stop_loss);
    }
    if (stop_dist < AF_EPSILON) stop_dist = entry_price * 0.005;

    /* Adjust risk_pct for Kelly */
    double eff_risk = risk_pct;
    if (method == AF_SIZE_KELLY) {
        eff_risk = af_kelly_fraction(win_rate, rr_ratio, risk_pct);
    }

    double risk_amount   = balance * eff_risk;
    double point         = sym->point > 0 ? sym->point : 0.00001;
    double contract_size = sym->contract_size > 0 ? sym->contract_size : 100000.0;
    double val_per_point = contract_size * point;
    double stop_in_pts   = stop_dist / point;

    double raw_lots = (stop_in_pts > AF_EPSILON && val_per_point > AF_EPSILON)
                      ? risk_amount / (stop_in_pts * val_per_point)
                      : min_lots;

    int capped = 0;
    double step = sym->volume_step > 0 ? sym->volume_step : 0.01;
    double ml   = max_lots  > 0 ? max_lots  : 100.0;
    double mn   = min_lots  > 0 ? min_lots  : 0.01;

    double norm = af_normalize_lots(raw_lots, step, mn, ml);
    if (norm >= ml) capped = 1;

    double actual_risk = norm * stop_in_pts * val_per_point;

    out->lots          = norm;
    out->risk_amount   = actual_risk;
    out->risk_pct      = (balance > AF_EPSILON) ? actual_risk / balance : 0.0;
    out->stop_distance = stop_dist;
    out->capped        = capped;

    return AF_OK;
}
