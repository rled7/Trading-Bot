# Trading-Bot — Next Steps & Decisions

*Last updated: 2026-06-20 (verified against the repo). Captures the **honest** remaining work and the
decision points needed to move forward.*

## Where the project actually is (verified 2026-06-20)

- **104 commits.** `main` == `cpp-port-phase1` == `origin/main` (all at `9fc9638`); clean tree, pushed.
- **C++ is the production runtime** — flipped in **Phase 6 (2026-06-12)**. `docker compose up` builds and
  runs the C++ engine (`algoforge`) + C++ dashboard (`af_dashboard_server`, :8000). Python is the parity
  oracle (compose `oracle` profile).
- **Tests green:** Python **618/618**, C++ **102/102** (ran 2026-06-20).
- **Complete:** core R1–R6 (38 indicators, 20 patterns, broker abstraction + paper broker) in all four
  languages; **S1** algo generator, **S2** local LLM, **S3** dashboard, **S4** analytics — all four;
  **S5** multi-broker REST adapters (OANDA, Alpaca, IBKR, Binance, Coinbase) in Python (191 mocked tests).

**Net: ~85–90% complete as a backtest + paper-trade + algo-generation + analytics platform.**

## The real gap to "done" — two items

### 1. Live execution is wired but never proven against a real broker (the #1 gap)
The S5 adapters pass **191 _mocked_ tests** — they have **never hit a real broker API**. Live MT5 (R12)
is Python-only and needs Windows + an MT5 terminal. So the bot fully backtests and paper-trades but has
**not placed a single real order.**

**To close it — decision needed:** which broker to validate first against its real sandbox.
- **Alpaca** — easiest: free paper-trading API, dev-friendly REST/WS, US equities + crypto. *Recommended
  first.* The adapter exists; point `AF_ALPACA_*` at the paper endpoint and exercise connect → get_account
  → get_bars → place_order → get_positions → close_position against the live sandbox.
- OANDA (FX-native, aligns with the paper broker's FX focus) · IBKR (institutional, steep) ·
  Binance / Coinbase (crypto).

### 2. S6 — background C++ algo-discovery daemon (last stretch goal, not started)
Always-on observation process that forms hypotheses from market data and emits S1-compatible manifests
without human prompting. C++-only by design. **Still blocked on 6 decisions:**
1. Hypothesis logic — statistical drift / pattern co-occurrence / regime detection / LLM-on-bar-windows?
2. Data window — rolling 1h / 24h / 7d / multi-horizon?
3. Manifest cadence + rate limit (don't flood the registry).
4. Tier targeting — Orange/Yellow first, or attempt Green/White?
5. Validator integration — submit → S1 validator → promote only survivors?
6. Resource budget — max CPU/RAM; pause during live sessions or always-on?

## Closed decisions (for the record)
- **C++ as production runtime: DECIDED** (Phase 6). The R7–R12 ladder is feature-complete in C++; C, JS
  (and Python beyond the oracle role) are reference/educational builds, **not** targeted for full parity.
  This retires the old "keep porting R7–R12 to all languages?" question.
- **S5 multi-broker: DONE** in Python (adapters + registry + env-var creds + paper/live same code path).

## To unblock the next coding session
Answer **one**: *"Validate Alpaca live first"* (I wire `AF_ALPACA_*` to the paper sandbox and exercise the
full order lifecycle), **or** give the 6 S6 decisions (defaults proposed in `docs/CPP-PORT-PLAN.md`).
Without one of these, an autonomous resume queues questions rather than codes — see [[session-limit-recovery]].

## Housekeeping note
Stray remote branch `origin/claude/upload-project-files-sRlkV` is a leftover upload branch — safe to
delete when convenient (left in place; deleting a remote branch is destructive, so it's the user's call).
