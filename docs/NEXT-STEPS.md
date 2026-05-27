# Trading-Bot — Next Steps & Decisions

*Last updated: 2026-05-27. Captures remaining work and the decision points needed to unblock autonomous resume.*

## Stretch goals remaining (2 phases)

Per the README's Round tracker, all primary stretch goals (S1–S4) are complete. Two remain:

### S5 — Multi-broker support

**What it is:** Extend the broker abstraction beyond paper + MT5 to support additional brokers. The `IBroker` contract is already defined and exercised by `PaperBroker` across all four languages.

**Decisions needed:**

1. **Which brokers, in what priority order?** Candidate list (pick 2–3 for initial support):
   - **Interactive Brokers (IBKR)** — TWS API or REST gateway. Most institutional. Steep ramp.
   - **Alpaca** — REST + WebSocket. US equities + crypto. Very dev-friendly.
   - **OANDA** — REST. FX-native. Aligns with existing FX-pair focus.
   - **Binance** — REST + WS. Crypto. Big symbol surface.
   - **Coinbase** — REST + WS. Crypto. US-regulated.
   - **MetaTrader 5 (live)** — already partially wired in Python; finish vs broaden?
2. **Symbol mapping policy.** Different brokers use different ticker conventions (`EURUSD` vs `EUR/USD` vs `EUR_USD`). Centralized mapping table, or per-broker adapter handles it locally?
3. **Live vs paper toggle per broker.** Should each broker adapter support both modes via the same code path, or are paper-mode interactions always routed through `PaperBroker` regardless?
4. **Credential storage.** Env vars only (matches existing `MT5_*` pattern), or also support a `~/.algoforge/credentials.toml`-style config file?
5. **Language coverage.** Does S5 ship in all four languages (matching the broker abstraction's cross-language design), or initially in C++ + Python only with C and JS deferred?

### S6 — Background C++ algo discovery daemon

**What it is:** Always-on observation process that watches market data, forms hypotheses, generates S1-compatible algorithm manifests automatically without human prompting. C++-only by design.

**Decisions needed:**

1. **Hypothesis-formation logic.** Statistical drift detection? Pattern co-occurrence mining? Regime detection? LLM-driven on bar windows? Some combination?
2. **Data window per hypothesis.** Rolling 1h / 24h / 7d / multiple horizons in parallel?
3. **Manifest output cadence.** One per market-regime-shift, one per N bars, opportunistic? What's the rate limit so it doesn't flood the manifest registry?
4. **Tier targeting.** Should generated manifests target a specific tier (Orange / Yellow) initially, or attempt Green/White from the start?
5. **Integration with S1's validator pipeline.** Daemon submits manifest → run through validator → only promote those that survive? Or accept raw and let the user prune?
6. **Resource budget.** Max CPU/RAM the daemon may use; does it pause during live trading sessions vs. always-on?

## Core ladder (R-rounds) — separate decision

The R7–R12 ladder ships full features in **C++** but is largely unported to **C, Python, JS**. Decisions:

1. **Keep porting?** If yes, which language is next priority — C (for the bare-metal/MT5 DLL path) or Python (for the dashboard-hosted live runtime)?
2. **Or accept C++ as the production runtime?** Treat C, Python, JS as reference/educational/utility builds, not feature-complete trading runtimes.

The honest read: the dashboard runs on Python (S3), live MT5 is Python-only (R12¹), and the algo generator is Python-only by design. The C++ reference is intellectually cleanest but isn't where the user-facing path lives. Worth discussing whether full parity is still the goal.

## Existing uncommitted WIP (as of 2026-05-27)

Three JS files have uncommitted changes (~7 lines total) adding `sizeMult` support to the JS backtest and risk modules, plus extending the JS test runner to include `algo_gen/*.test.js`. These look like the tail-end of S1 — not committed yet. Decide whether to commit or revert before any new branch work.

## Autonomous-resume note

When the weekly limit resets and the wakeup-prompt's Tier 3 runs against this repo, the immediate next action is to read this file and identify a decision the user has answered (e.g. "S5 first, prioritize Alpaca + OANDA"). Without a user-answered decision in this file, the daemon-mode resume will queue questions rather than auto-execute — see [[session-limit-recovery]] safety boundaries.

To unblock autonomous resume, the user should fill in **one section** above with concrete decisions before going AFK. Even partial answers (e.g. "S5 brokers: Alpaca + OANDA, paper-mode same-code-path, env-var credentials") let the next session start coding.
