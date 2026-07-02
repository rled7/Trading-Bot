# Trading-Bot — Next Steps & Decisions

*Last updated: 2026-07-02 (verified against the repo, corrected — the previous
version incorrectly claimed S6 was "not started"; it was already built on
2026-06-09 and just never had a runnable entrypoint, which is now fixed).*

## Where the project actually is (verified 2026-07-02)

- **C++ is the production runtime** (Phase 6, 2026-06-12). `docker compose up` runs the C++ engine
  (`algoforge`) + C++ dashboard (`af_dashboard_server`, :8000). Python is the parity oracle
  (`--profile oracle`).
- **Tests green:** C++ **16/16 suites** including `S6Tests` (ran 2026-07-02 via `ctest`).
- **Complete:** core R1–R6 in all four languages; **S1** algo generator, **S2** local LLM, **S3**
  dashboard, **S4** analytics — all four; **S5** multi-broker REST adapters (OANDA, Alpaca, IBKR,
  Binance, Coinbase) in Python + C++ (191 mocked tests); **S6** background discovery daemon — built,
  tested (`S6Tests` golden oracle), **and now has a real entrypoint** (`af_s6_daemon`, see below).
- **libcurl was never wired into the C++ build** — found while wiring S6. `af_dashboard_server
  --llm-host` and the S6 daemon both depend on it to reach a real LLM; without it every LLM call threw
  `LLMError(unreachable)` by design (so tests with a mock transport still passed), but nothing actually
  worked end-to-end. Fixed: `cpp/CMakeLists.txt` now does `find_package(CURL)` and links it into
  `af_llm` when present; `Dockerfile.cpp-lang` installs `libcurl4-openssl-dev`/`libcurl4`. Verified
  locally: curl found, daemon makes real HTTP attempts (confirmed via a real "connection refused"
  against a not-running Ollama, not the old "unreachable" fallback).
- **Found + fixed a real crash bug while verifying S6 end-to-end:** `DiscoveryDaemon::observe()` had no
  exception handling around the LLM `generate_fn`/`validate_fn` calls (unlike the `promote()` call two
  lines below it, which was already defensively wrapped) — a single LLM failure crashed the entire
  daemon process. Added a `Kind::GenerationFailed` event + try/catch; verified the daemon now survives
  100+ consecutive LLM failures without dying (ran it live against an unreachable Ollama host).
- **Found + fixed a real, pre-existing Linux/GCC portability bug** while doing the first actual
  `docker build` of this codebase in a long time (installed colima to get a working Docker daemon and
  ran it for real, not just `docker compose config`): `cpp/include/core/analytics.hpp` used
  `std::function` (for `BacktestFn`) without including `<functional>`. Silently compiled on macOS
  (AppleClang's libc++ header graph transitively pulls it in) but fails outright on Ubuntu 22.04/GCC —
  meaning `af_dashboard_server`'s Linux container build (pre-existing, not something this session
  introduced) had likely never actually succeeded. One-line fix; confirmed the full `docker build`
  succeeds afterward.

**Net: S1–S6 are all built, tested, runnable, and Docker-verified. The one substantive gap left is
live-broker proof — see below — and it needs the user's own Alpaca credentials to close.**

## The real gap to "done" — one item now

### Live execution is wired but never proven against a real broker (the #1 gap)
The S5 adapters pass **191 _mocked_ tests** — they have **never hit a real broker API**. Live MT5 (R12)
is Python-only and needs Windows + an MT5 terminal. So the bot fully backtests and paper-trades but has
**not placed a single real order.**

**Status: in progress.** Alpaca was picked (free paper-trading sandbox, no funding/KYC needed). Waiting
on the user to generate an `AF_ALPACA_API_KEY` / `AF_ALPACA_API_SECRET` pair from a **Paper Trading**
account at alpaca.markets and export them locally — this requires the user's own Alpaca account and
cannot be done from here. Once set, the validation itself (connect → get_account → get_bars →
place_order → get_positions → close_position against the real paper sandbox) is ready to run.

## S6 — background C++ algo-discovery daemon: now wired in

Was mistakenly listed here as "not started, blocked on 6 decisions" — both were wrong. The 6 decisions
were locked 2026-06-08 (see `docs/CPP-PORT-PLAN.md` progress log) and the daemon itself was built and
golden-oracle-tested 2026-06-09. What was actually missing — a runnable entrypoint — is fixed:

- New `cpp/src/s6/s6_main.cpp` → `af_s6_daemon` binary. Drives `DiscoveryDaemon::observe()` directly
  (not the class's optional internal-thread wrapper) from its own poll loop, so its behavior matches
  exactly what the golden-oracle suite already proves.
- Bootstraps a full historical window (`max_bars_retained` bars, ~1 week at M1) in one shot at startup
  instead of needing >14h of individual poll ticks to fill the buffer from cold.
- Refuses to start without `--llm-host` (fails loudly, matching this codebase's existing
  misconfiguration convention) rather than silently idling.
- `docker-compose.yml`: new `algoforge-cpp-s6` service, gated behind `--profile s6` (opt-in — it
  autonomously proposes/promotes manifests into the shared registry, so it shouldn't start silently
  alongside the trading engine by default). `docker compose --profile s6 up algoforge-cpp-s6`.
- **Verified for real, not just typechecked/config-validated.** Installed colima (a Docker daemon
  backend) since Docker wasn't running in the working environment, then: `docker build -f
  Dockerfile.cpp-lang .` — succeeds, all three binaries (`algoforge`, `af_dashboard_server`,
  `af_s6_daemon`) present in the image; `docker compose --profile s6 up -d algoforge-cpp-s6` — container
  starts, connects, bootstraps against the synthetic paper feed, and (with no Ollama actually reachable
  at `host.docker.internal:11434` in this environment) logs `GenerationFailed` repeatedly while staying
  `Up` — i.e. the exception-safety fix holds inside the real container, not just on macOS. Cleaned up
  the test containers/images afterward.

## Closed decisions (for the record)
- **C++ as production runtime: DECIDED** (Phase 6).
- **S5 multi-broker: DONE** in Python + C++.
- **S6: DECIDED + BUILT + WIRED** (this session). No longer blocked on anything.

## To unblock the next session
One item remains, and it isn't "more building" — it needs your Alpaca account:
1. **You:** generate the Alpaca paper key pair and export `AF_ALPACA_API_KEY`/`AF_ALPACA_API_SECRET`
   (`AF_ALPACA_PAPER=1`) — tell me once it's set and I'll run the live validation immediately.

(Docker build/run is no longer an open item — verified for real this session, see the S6 section above.
The stray `origin/claude/upload-project-files-sRlkV` branch mentioned in earlier revisions of this doc
has been deleted, per the user.)
