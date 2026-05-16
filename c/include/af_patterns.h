#ifndef AF_PATTERNS_H
#define AF_PATTERNS_H

#include <stddef.h>
#include "af_types.h"

/* Per-bar detectors. All return +1 (bullish), -1 (bearish), or 0 (no match).
   Bars with range == 0 (open==high==low==close) never match.

   Defaults match the cpp/ reference:
     doji      doji_pct      = 0.10
     marubozu  body_min_pct  = 0.90
     pin_bar   min_wick_mult = 2.0
*/

int af_is_doji      (double open, double high, double low, double close, double doji_pct);
int af_is_hammer    (double open, double high, double low, double close);
int af_is_engulfing (double prev_open,  double prev_close,
                     double curr_open,  double curr_close);
int af_is_marubozu  (double open, double high, double low, double close, double body_min_pct);
int af_is_pin_bar   (double open, double high, double low, double close, double min_wick_mult);

/* Match record produced by af_scan_patterns. */
typedef struct {
    int         bar_index;
    const char *name;
    int         signal;     /* +1 bullish, -1 bearish */
} af_pattern_match_t;

/* Walk the bar array and emit pattern matches into `out` (up to `out_cap`).
   Returns the total number of matches FOUND (which may exceed out_cap; in
   that case only the first out_cap are written, but the count is honest). */
size_t af_scan_patterns(const af_bar_t *bars, size_t n,
                         af_pattern_match_t *out, size_t out_cap);

#endif /* AF_PATTERNS_H */
