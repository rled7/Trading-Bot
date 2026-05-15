#pragma once
#include "core/types.h"
#include <string>
#include <vector>

namespace af {

struct CsvLoadResult {
    std::vector<AF_Bar> bars;
    std::string         error;          /* empty on success */
    int                 rows_parsed{0};
    int                 rows_skipped{0}; /* header / blank / malformed */
};

/**
 * Load OHLCV bars from a CSV/TSV file.
 *
 * Expected columns (in order): timestamp, open, high, low, close, volume [, spread]
 * Delimiter: auto-detected from the first non-blank line (',' '\t' ';').
 * Header: first row is skipped automatically if its first field isn't numeric.
 * Timestamp: epoch seconds (integer) OR "YYYY[-./]MM[-./]DD[ Tt]HH:MM[:SS]".
 *   Datetime strings are interpreted as UTC.
 *
 * Bars are returned in file order; the caller is responsible for ensuring
 * they're chronological (oldest first) before feeding them to the engine.
 */
CsvLoadResult load_csv_bars(const std::string &path);

} /* namespace af */
