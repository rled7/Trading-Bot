/**
 * AlgoForge — include/risk/risk_types.h
 * Risk calculation result types. Pure C.
 */
#ifndef AF_RISK_TYPES_H
#define AF_RISK_TYPES_H

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AF_SIZE_FIXED_RISK = 0,  /* risk fixed % of balance */
    AF_SIZE_ATR_RISK,         /* risk based on ATR stop distance */
    AF_SIZE_KELLY,            /* half-Kelly fraction */
} AF_SizingMethod;

typedef struct {
    double lots;
    double risk_amount;      /* USD at risk */
    double risk_pct;         /* fraction of account */
    double stop_distance;    /* price units */
    int    capped;           /* 1 if lots were capped at max */
} AF_SizeResult;

typedef enum {
    AF_SL_ATR        = 0,
    AF_SL_SWING      = 1,
    AF_SL_STRUCTURE  = 2,
    AF_SL_FIXED_PTS  = 3,
} AF_SLMethod;

typedef struct {
    double price;
    double distance;
    AF_SLMethod method;
} AF_SLResult;

typedef struct {
    double tp1, tp2, tp3;
    double rr1, rr2, rr3;    /* R:R ratios */
} AF_TPResult;

typedef enum {
    AF_GUARD_GREEN  = 0,
    AF_GUARD_YELLOW = 1,
    AF_GUARD_RED    = 2,
} AF_GuardState;

typedef struct {
    AF_GuardState state;
    double daily_pnl;
    double daily_pnl_pct;
    double total_dd_pct;
    double peak_equity;
    double current_equity;
    int    trading_allowed;  /* 1=yes, 0=halted */
    char   reason[128];
} AF_GuardStatus;

/* ── Position sizing C function ── */
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
    double win_rate,   /* for Kelly */
    double rr_ratio,   /* for Kelly */
    AF_SizeResult *out
);

/* Normalise lots to broker step */
double af_normalize_lots(double lots, double step, double min_lots, double max_lots);

/* Kelly fraction (half-Kelly, capped at max_risk) */
double af_kelly_fraction(double win_rate, double rr, double max_risk);

#ifdef __cplusplus
}
#endif
#endif /* AF_RISK_TYPES_H */
