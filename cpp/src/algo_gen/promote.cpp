/**
 * AlgoForge — src/algo_gen/promote.cpp
 * S1 Phase 1 — Registry promotion.
 *
 * Mirrors python/algoforge/algo_gen/promote.py:
 *   - Refuses red tier.
 *   - Requires confirm=true for orange/yellow.
 *   - Writes registry_dir/<name>.json with _metadata block.
 */

#include "algo_gen_internal.hpp"
#include "core/algo_gen.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace algoforge::algo_gen {

/* =========================================================================
 * Build ISO-8601 UTC timestamp (YYYY-MM-DDTHH:MM:SSZ)
 * ========================================================================= */

static std::string utc_now_iso8601() {
    auto now   = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc;
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_utc.tm_year + 1900,
             tm_utc.tm_mon  + 1,
             tm_utc.tm_mday,
             tm_utc.tm_hour,
             tm_utc.tm_min,
             tm_utc.tm_sec);
    return buf;
}

/* =========================================================================
 * Serialise an AlgoManifest to a JSON string (manifest body only)
 * Used by promote to write the registry file.
 * ========================================================================= */

static std::string manifest_to_json(const AlgoManifest& m) {
    using namespace json;
    std::ostringstream o;
    o << "{\n";
    o << "  \"schema_version\": " << emit_string_val(m.schema_version) << ",\n";
    o << "  \"name\": "           << emit_string_val(m.name)           << ",\n";
    o << "  \"description\": "    << emit_string_val(m.description)    << ",\n";
    o << "  \"rationale\": "      << emit_string_val(m.rationale)      << ",\n";

    // timeframes
    o << "  \"timeframes\": [";
    for (size_t i = 0; i < m.timeframes.size(); ++i) {
        if (i) o << ", ";
        o << emit_string_val(m.timeframes[i]);
    }
    o << "],\n";

    // symbols
    o << "  \"symbols\": ";
    if (std::holds_alternative<std::string>(m.symbols)) {
        o << emit_string_val(std::get<std::string>(m.symbols));
    } else {
        const auto& syms = std::get<std::vector<std::string>>(m.symbols);
        o << "[";
        for (size_t i = 0; i < syms.size(); ++i) {
            if (i) o << ", ";
            o << emit_string_val(syms[i]);
        }
        o << "]";
    }
    o << ",\n";

    // indicators
    o << "  \"indicators\": [\n";
    for (size_t i = 0; i < m.indicators.size(); ++i) {
        const auto& ind = m.indicators[i];
        o << "    {\"id\": " << emit_string_val(ind.id)
          << ", \"kind\": "   << emit_string_val(ind.kind)
          << ", \"params\": {";
        bool first_p = true;
        for (const auto& [k, v] : ind.params) {
            if (!first_p) o << ", ";
            o << emit_string_val(k) << ": " << emit_double(v);
            first_p = false;
        }
        o << "}}";
        if (i + 1 < m.indicators.size()) o << ",";
        o << "\n";
    }
    o << "  ],\n";

    // entries
    o << "  \"entries\": [\n";
    for (size_t i = 0; i < m.entries.size(); ++i) {
        const auto& e = m.entries[i];
        o << "    {\"side\": " << emit_string_val(e.side)
          << ", \"when\": "     << emit_string_val(e.when) << "}";
        if (i + 1 < m.entries.size()) o << ",";
        o << "\n";
    }
    o << "  ],\n";

    // exits
    o << "  \"exits\": [\n";
    for (size_t i = 0; i < m.exits.size(); ++i) {
        const auto& x = m.exits[i];
        o << "    {\"side\": " << emit_string_val(x.side);
        if (x.when)   o << ", \"when\": "   << emit_string_val(*x.when);
        if (x.sl_atr) o << ", \"sl_atr\": " << emit_double(*x.sl_atr);
        if (x.tp_atr) o << ", \"tp_atr\": " << emit_double(*x.tp_atr);
        o << "}";
        if (i + 1 < m.exits.size()) o << ",";
        o << "\n";
    }
    o << "  ],\n";

    // risk
    o << "  \"risk\": {\n";
    o << "    \"size\": "           << emit_string_val(m.risk.size)       << ",\n";
    o << "    \"atr_mult\": "       << emit_double(m.risk.atr_mult)       << ",\n";
    o << "    \"fixed_lots\": "     << emit_double(m.risk.fixed_lots)     << ",\n";
    o << "    \"max_concurrent\": " << m.risk.max_concurrent              << ",\n";
    o << "    \"hedge\": "          << emit_bool(m.risk.hedge)            << ",\n";
    o << "    \"cool_down_bars\": " << m.risk.cool_down_bars              << "\n";
    o << "  }";

    // code (escape-hatch)
    if (m.code) {
        o << ",\n  \"code\": " << emit_string_val(*m.code);
    }

    return o.str();
}

/* =========================================================================
 * Public API: promote
 * ========================================================================= */

void promote(const TierReport& report,
             const AlgoManifest& manifest,
             const std::filesystem::path& registry_dir,
             bool confirm) {
    // Refuse red tier
    if (report.tier == Tier::Red) {
        throw std::runtime_error(
            std::string("Cannot promote algorithm '") + manifest.name +
            "': tier is red (min_score=" +
            [&]{ char b[32]; snprintf(b, sizeof(b), "%.2f", report.min_score); return std::string(b); }() +
            " < 70). Red-tier algorithms are never promoted."
        );
    }

    // Orange/Yellow require manual confirmation
    if ((report.tier == Tier::Orange || report.tier == Tier::Yellow) && !confirm) {
        throw std::runtime_error(
            std::string("Cannot promote '") + manifest.name +
            "' (tier=" + tier_name(report.tier) +
            ") without confirm=true. This tier requires manual review."
        );
    }

    // Create registry directory
    std::error_code ec;
    std::filesystem::create_directories(registry_dir, ec);
    if (ec) {
        throw std::runtime_error(
            "Cannot create registry directory: " + registry_dir.string() +
            ": " + ec.message()
        );
    }

    std::filesystem::path manifest_path = registry_dir / (manifest.name + ".json");

    // Build _metadata block
    using namespace json;
    std::string created_at = utc_now_iso8601();
    std::string tier_str   = tier_name(report.tier);
    std::string color_str  = tier_color(report.tier);

    std::ostringstream meta;
    meta << "  \"_metadata\": {\n";
    meta << "    \"source\": \"algo_gen\",\n";
    meta << "    \"schema_version\": " << emit_string_val(manifest.schema_version) << ",\n";
    meta << "    \"tier\": "           << emit_string_val(tier_str)   << ",\n";
    meta << "    \"tier_color\": "     << emit_string_val(color_str)  << ",\n";
    meta << "    \"scores\": {\n";
    meta << "      \"walk_forward\": " << emit_double(report.walk_forward) << ",\n";
    meta << "      \"mc_bootstrap\": " << emit_double(report.mc_bootstrap) << ",\n";
    meta << "      \"robustness\": "   << emit_double(report.robustness)   << "\n";
    meta << "    },\n";
    meta << "    \"min_score\": "  << emit_double(report.min_score)  << ",\n";
    meta << "    \"size_mult\": "  << emit_double(report.size_mult)  << ",\n";
    meta << "    \"experimental\": " << emit_bool(report.experimental) << ",\n";
    meta << "    \"paper_only\": "   << emit_bool(report.paper_only)   << ",\n";
    meta << "    \"created_at\": "   << emit_string_val(created_at)    << ",\n";
    meta << "    \"generator\": {},\n";
    meta << "    \"brief\": \"\",\n";
    meta << "    \"manifest_path\": " << emit_string_val(manifest_path.string()) << "\n";
    meta << "  }\n";

    // Compose full JSON: manifest body + _metadata
    std::string body = manifest_to_json(manifest);

    std::ofstream f(manifest_path);
    if (!f.is_open()) {
        throw std::runtime_error(
            "Cannot open file for writing: " + manifest_path.string()
        );
    }
    f << body << ",\n" << meta.str() << "}\n";
    f.close();
}

} /* namespace algoforge::algo_gen */
