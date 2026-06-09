# S6 — Background C++ Algo Discovery Daemon (Implementation Spec)

*Created 2026-06-09. Reference-backed via existing C++ primitives + a purpose-built synthetic
oracle (S6 has no Python reference — see §7). Decisions locked by user 2026-06-08.
Downstream of CPP-PORT-PLAN.md Phase 4.*

## 0. Why this spec exists
S6 "exists in no language and has no tested reference." The project's acceptance gate is
**differential parity vs a tested oracle** (CPP-PORT-PLAN rules 1 & 3 — *no guessing on
unspecified behavior*). Building S6 blind would violate that. This spec removes the ambiguity by
(a) pinning every behavior to the 6 locked decisions, (b) reusing already-tested C++ primitives
as the only sources of nondeterminism, and (c) defining a **synthetic golden oracle** (§7) so the
daemon is verifiable without a Python reference. A worker implements strictly to this doc.

## 1. The 6 locked decisions (verbatim, user 2026-06-08)
1. **Hypothesis logic:** hybrid — regime detection + statistical drift + pattern co-occurrence,
   feeding the LLM on candidate windows.
2. **Data window:** multiple horizons in parallel (1h / 24h / 7d).
3. **Output cadence:** emit candidate manifests on threshold-crossing, rate-limited (max N/hour).
4. **Tier targeting:** target the existing S1 mid tier first.
5. **Validator integration:** route generated manifests through the existing algo_gen validator
   before promote.
6. **Resource budget:** single low-priority background thread, bounded memory.

## 2. Reused primitives (DO NOT reimplement — these are already tested)
| Need | Existing API | Header / namespace |
|---|---|---|
| Regime detection | `RegimeResult classify_regime(const AF_Bar*, size_t)`; `enum class VolRegime` | `analysis/analysis.hpp` · `algoforge::analysis` |
| Structure / trend signals | `MarketStructure::analyse`, `TrendClassifier::classify`, `ConfluenceScorer::score_timeframe` | `analysis/analysis.hpp` |
| LLM manifest generation | `generate_fast / generate_balanced / generate_max(LLMProvider&, …)` (Phase 2) | `core/algo_gen.hpp` + generator |
| Manifest type | `struct AlgoManifest` | `core/algo_gen.hpp` · `algoforge::algo_gen` |
| Validation | `TierReport validate(const AlgoManifest&, const std::vector<Bar>&, uint64_t seed)` | `core/algo_gen.hpp` |
| Tier scoring | `Tier score_to_tier(double, double, double)`; `enum Tier{Red,Orange,Yellow,Green,White}` | `core/algo_gen.hpp` |
| Promotion | `promote(const TierReport&, const AlgoManifest&, path registry_dir, bool confirm)` | `core/algo_gen.hpp` |

S6 adds **only** the orchestration layer above these. It introduces no new market math.

## 3. Public API surface (new)
New header `cpp/include/s6/discovery_daemon.hpp`, namespace `algoforge::s6`.

```cpp
namespace algoforge::s6 {

// Decision 6: bounded, low-priority.
struct DaemonConfig {
    std::vector<int>  horizons_bars   = {60, 1440, 10080}; // ~1h/24h/7d on M1 (Decision 2)
    double            drift_threshold = 2.0;   // z-score; crossing => candidate (Decision 3)
    int               max_emits_per_hour = 4;  // rate limit (Decision 3)
    Tier              target_tier_floor  = Tier::Yellow; // S1 "mid" (Decision 4)
    bool              route_through_validator = true;     // (Decision 5)
    size_t            max_bars_retained  = 10080;         // bounded memory (Decision 6)
    bool             pause_during_live   = true;          // (Decision 6)
};

// One detected opportunity before it becomes a manifest.
struct Hypothesis {
    int64_t      detected_at = 0;
    int          horizon_bars = 0;
    VolRegime    regime;
    double       drift_z = 0.0;        // statistical-drift score
    double       pattern_cooccurrence = 0.0;
    std::string  summary;              // human-readable; also seeds the LLM prompt
};

// Result of one observe() tick.
struct DiscoveryEvent {
    enum class Kind { None, HypothesisFormed, ManifestEmitted, RateLimited, ValidatorRejected };
    Kind                       kind = Kind::None;
    std::optional<Hypothesis>  hypothesis;
    std::optional<AlgoManifest> manifest;
    std::optional<TierReport>  report;
};

class DiscoveryDaemon {
public:
    DiscoveryDaemon(DaemonConfig cfg, LLMProvider& llm, std::filesystem::path registry_dir);

    // Deterministic, single-step core. Feeds one new bar; returns what (if anything) happened.
    // ALL daemon logic is reachable through this — the thread in run()/stop() is a thin loop.
    DiscoveryEvent observe(const Bar& bar, int64_t now_ms);

    // Decision 6: spawns ONE low-priority background thread that calls observe() as bars arrive.
    void run();
    void stop();

    // Introspection for tests/metrics.
    size_t emits_this_hour(int64_t now_ms) const;
};

} // namespace algoforge::s6
```

**Design rule:** `observe()` is pure given (internal state, inputs) — it is the unit of test.
`run()/stop()` are an untested-by-design thin thread wrapper (Decision 6) that only calls
`observe()`. This keeps 100% of S6 *logic* deterministically testable (§7).

## 4. Per-tick algorithm (what `observe()` does)
1. Append `bar` to a ring buffer capped at `max_bars_retained` (Decision 6 — bounded memory).
2. For each horizon in `horizons_bars` (Decision 2), over the trailing window:
   a. `classify_regime()` → regime; b. compute **statistical drift** = z-score of the window's
   mean return vs the prior window (rolling, O(1) via running sums); c. compute **pattern
   co-occurrence** via `ConfluenceScorer` confluence on the window.
3. **Threshold crossing (Decision 3):** if `drift_z >= drift_threshold` on *any* horizon AND no
   identical-regime hypothesis is already open for that horizon → form a `Hypothesis`
   (`Kind::HypothesisFormed`). Otherwise return `Kind::None`.
4. **Rate limit (Decision 3):** if `emits_this_hour(now) >= max_emits_per_hour` →
   `Kind::RateLimited`, drop the hypothesis. (Sliding 1-hour window of emit timestamps.)
5. **Generate:** build the LLM prompt from `Hypothesis::summary` + targeted tier floor
   (Decision 4 → request a Yellow-or-better mid-tier strategy); call `generate_balanced(llm, …)`
   → `AlgoManifest`.
6. **Validator gate (Decision 5):** if `route_through_validator`, call `validate(manifest,
   window_bars)`. If `!report.passed` OR `report.tier < target_tier_floor` →
   `Kind::ValidatorRejected`. Else `promote(report, manifest, registry_dir, /*confirm=*/false
   for Green/White; manifests below that are emitted but NOT auto-promoted)` and return
   `Kind::ManifestEmitted` (record the emit timestamp for the rate limiter).

## 5. Threading & resource budget (Decision 6)
- `run()` spawns exactly **one** `std::thread`, set to low priority
  (`pthread_setschedparam` SCHED_IDLE / lowest niceness; document the macOS QOS fallback
  `pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND)`).
- Memory is bounded by the ring buffer (`max_bars_retained`) + at most one open hypothesis per
  horizon. No unbounded queues.
- If `pause_during_live` and a live-session flag is set, `observe()` early-returns `Kind::None`
  (the daemon yields the machine to live trading).

## 6. Files to create
```
cpp/include/s6/discovery_daemon.hpp     # API above
cpp/src/s6/discovery_daemon.cpp         # observe() + thread wrapper
cpp/tests/test_s6.cpp                   # golden oracle suite (§7)
cpp/tests/test_s6_main.cpp              # standalone runner (mirror existing *_main.cpp)
cpp/tests/fixtures/s6/*.json            # canned bar windows + expected events (the oracle)
```
Wire into `cpp/CMakeLists.txt`: an `af_s6` lib target + a `test_s6` executable + `add_test(NAME
S6Tests …)`. Reuse `tests/llm_mock.hpp` as the `LLMProvider` so generation is deterministic.

## 7. The synthetic oracle (how greenfield S6 stays verifiable)
Because there is no Python reference, the spec **is** the oracle. Verification = golden tests with
deterministic inputs and pinned expected `DiscoveryEvent` sequences:

1. **No-drift window** → feed a flat/low-vol canned series across all 3 horizons →
   expect every tick `Kind::None` (no false positives).
2. **Single regime-shift** → a window engineered to cross `drift_threshold` exactly once →
   expect exactly one `HypothesisFormed` then one `ManifestEmitted` (mock LLM returns a known
   Yellow-tier manifest fixture; assert the emitted manifest equals the fixture).
3. **Rate-limit** → engineer 6 threshold crossings within one hour with `max_emits_per_hour=4` →
   expect exactly 4 `ManifestEmitted` and 2 `RateLimited`, and `emits_this_hour()==4`.
4. **Validator rejection** → mock LLM returns a manifest that `validate()` fails (or scores below
   `target_tier_floor`) → expect `ValidatorRejected` and **no** file written to `registry_dir`.
5. **Tier floor (Decision 4)** → a manifest validating to Orange (below Yellow floor) → rejected;
   one validating to Green → emitted + promoted.
6. **Bounded memory (Decision 6)** → feed `max_bars_retained + 1000` bars → assert the ring
   buffer size never exceeds `max_bars_retained`.
7. **Pause-during-live** → set the live flag → assert every `observe()` returns `Kind::None`.

All inputs are committed JSON fixtures under `cpp/tests/fixtures/s6/`; the mock LLM responses
reuse the existing `llm_response_*.json` style. Each test asserts the **full event sequence**, not
just an endpoint — this is the differential-parity discipline applied to a self-defined oracle.

## 8. Acceptance criteria
- `ctest` shows `S6Tests` green; all 7 oracle scenarios pass.
- No regression: full suite stays 12/12 → 13/13.
- Daemon adds no new market math (every numeric primitive comes from §2 tested code).
- `observe()` is deterministic given the mock LLM; the thread wrapper is the only untested code
  and contains no branching logic beyond the loop + pause flag.

## 9. Out of scope (explicit)
- Live market-data ingestion plumbing (the daemon consumes `Bar`s; the feed is Phase 3/6).
- Any dashboard/visualisation of discoveries (Phase 5).
- MT5 (hard-blocked, Windows-only).
