/**
 * AlgoForge — src/algo_gen/serialise.cpp
 * S1 Phase 1 — TierReport JSON round-trip serialisation.
 *
 * tier_report_to_json:  TierReport → JSON string (matches fixture format).
 * tier_report_from_json: JSON string → TierReport (for cross-language round-trip).
 */

#include "algo_gen_internal.hpp"
#include "core/algo_gen.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace algoforge::algo_gen {

/* =========================================================================
 * tier_report_to_json
 * ========================================================================= */

std::string tier_report_to_json(const TierReport& r) {
    using namespace json;
    std::ostringstream o;
    o << "{\n";
    o << "  \"manifest_name\": " << emit_string_val(r.manifest_name) << ",\n";
    o << "  \"passed\": "        << emit_bool(r.passed)              << ",\n";

    // tier — null when has_tier is false (validation failed before tier is assigned)
    if (r.has_tier) {
        o << "  \"tier\": " << emit_string_val(tier_name(r.tier)) << ",\n";
        o << "  \"min_score\": "    << emit_double(r.min_score)    << ",\n";
        o << "  \"walk_forward\": " << emit_double(r.walk_forward) << ",\n";
        o << "  \"mc_bootstrap\": " << emit_double(r.mc_bootstrap) << ",\n";
        o << "  \"robustness\": "   << emit_double(r.robustness)   << ",\n";
        o << "  \"size_mult\": "    << emit_double(r.size_mult)    << ",\n";
        o << "  \"experimental\": " << emit_bool(r.experimental)   << ",\n";
        o << "  \"paper_only\": "   << emit_bool(r.paper_only)     << ",\n";
    } else {
        o << "  \"tier\": null,\n";
    }

    // stages
    o << "  \"stages\": [\n";
    for (size_t i = 0; i < r.stages.size(); ++i) {
        const auto& s = r.stages[i];
        o << "    {\n";
        o << "      \"stage\": "  << s.stage                        << ",\n";
        o << "      \"name\": "   << emit_string_val(s.name)        << ",\n";
        o << "      \"passed\": " << emit_bool(s.passed)            << ",\n";
        o << "      \"reason\": " << emit_string_val(s.reason)      << ",\n";
        o << "      \"metrics\": {";
        bool first = true;
        for (const auto& [k, v] : s.metrics) {
            if (!first) o << ", ";
            // metric values are stored as strings but may be numeric-looking;
            // to match fixture format, emit them as numbers when they parse cleanly.
            double dv;
            char* end;
            // Try to parse as double to emit without quotes (matching Python fixture)
            dv = strtod(v.c_str(), &end);
            if (end != v.c_str() && *end == '\0') {
                o << emit_string_val(k) << ": " << emit_double(dv);
            } else {
                o << emit_string_val(k) << ": " << emit_string_val(v);
            }
            first = false;
        }
        o << "}";
        o << "\n    }";
        if (i + 1 < r.stages.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n";
    o << "}";
    return o.str();
}

/* =========================================================================
 * tier_report_from_json
 * ========================================================================= */

static Tier tier_from_string(const std::string& s) {
    if (s == "red")    return Tier::Red;
    if (s == "orange") return Tier::Orange;
    if (s == "yellow") return Tier::Yellow;
    if (s == "green")  return Tier::Green;
    if (s == "white")  return Tier::White;
    throw std::invalid_argument("unknown tier: " + s);
}

TierReport tier_report_from_json(const std::string& src) {
    auto root = json::parse(src);
    if (!root.is_object()) {
        throw std::invalid_argument("tier_report_from_json: expected JSON object");
    }

    TierReport r;

    if (root.has("manifest_name"))
        r.manifest_name = root.get("manifest_name").as_str();

    if (root.has("passed"))
        r.passed = root.get("passed").as_bool();

    // tier may be null
    if (root.has("tier") && !root.get("tier").is_null()) {
        r.has_tier = true;
        r.tier     = tier_from_string(root.get("tier").as_str());
    } else {
        r.has_tier = false;
    }

    if (root.has("min_score"))    r.min_score    = root.get("min_score").as_double();
    if (root.has("walk_forward")) r.walk_forward = root.get("walk_forward").as_double();
    if (root.has("mc_bootstrap")) r.mc_bootstrap = root.get("mc_bootstrap").as_double();
    if (root.has("robustness"))   r.robustness   = root.get("robustness").as_double();
    if (root.has("size_mult"))    r.size_mult    = root.get("size_mult").as_double();
    if (root.has("experimental")) r.experimental = root.get("experimental").as_bool();
    if (root.has("paper_only"))   r.paper_only   = root.get("paper_only").as_bool();

    // stages
    if (root.has("stages") && root.get("stages").is_array()) {
        for (const auto& sj : root.get("stages").arr) {
            StageResult s;
            if (sj.has("stage"))  s.stage  = (int)sj.get("stage").as_int();
            if (sj.has("name"))   s.name   = sj.get("name").as_str();
            if (sj.has("passed")) s.passed = sj.get("passed").as_bool();
            if (sj.has("reason")) s.reason = sj.get("reason").as_str();
            if (sj.has("metrics") && sj.get("metrics").is_object()) {
                for (const auto& [k, v] : sj.get("metrics").obj) {
                    if (v.is_number()) {
                        // Store as the emit_double representation
                        s.metrics[k] = json::emit_double(v.as_double());
                    } else if (v.is_string()) {
                        s.metrics[k] = v.as_str();
                    }
                }
            }
            r.stages.push_back(s);
        }
    }

    return r;
}

} /* namespace algoforge::algo_gen */
