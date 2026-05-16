# AlgoForge — C

Pure C17 implementation of the AlgoForge blueprint.

**Status:** Phase 0 scaffold. Builds, tests pass, one trivial smoke check.

## Build

```bash
make            # builds build/algoforge
make test       # builds build/af_tests and runs them
make clean
```

## Layout

```
c/
├── Makefile
├── include/     — public headers (af_types.h, ...)
├── src/         — implementation, main.c is the bot entry
├── tests/       — test_smoke.c is a zero-dep test runner
└── build/       — generated (gitignored)
```

## Roadmap

Following the blueprint phases. Live MT5 path will use a DLL loader against
the MetaTrader 5 terminal's API on Windows (`MT5APIClient64.dll`).
