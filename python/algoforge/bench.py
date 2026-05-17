"""Cross-language benchmark — python/.

Run standalone:
    python -m algoforge.bench [bars] [iterations]
"""

from __future__ import annotations

import sys
import time

from .indicators import (
    sma, ema, rsi, atr,
    macd, bollinger, stochastic, obv, adx,
    wma, cci, williams_r, roc, mfi, vwap, keltner,
)
from .patterns   import scan_patterns
from .types      import Bar


def gen_series(n: int) -> list[float]:
    """Deterministic walk matched bit-for-bit across all four language benches."""
    data: list[float] = []
    p = 100.0
    for i in range(n):
        p += ((i * 9301 + 49297) % 233 - 116) * 0.01
        data.append(p)
    return data


def _time(fn, iters: int) -> tuple[int, int]:
    t0 = time.perf_counter_ns()
    for _ in range(iters):
        fn()
    total = time.perf_counter_ns() - t0
    return total, total // iters


def main(argv: list[str]) -> int:
    n     = int(argv[1]) if len(argv) > 1 else 100_000
    iters = int(argv[2]) if len(argv) > 2 else 10

    src = gen_series(n)
    hi  = [v + 0.5 for v in src]
    lo  = [v - 0.5 for v in src]
    vol = [1000.0 + (i % 500) for i in range(n)]

    print(f"# language\tpython")
    print(f"# bars\t{n}")
    print(f"# iterations\t{iters}")
    print("indicator\ttotal_ns\tns_per_iter")

    bars = [
        Bar(timestamp=i, open=src[i], high=hi[i], low=lo[i],
            close=src[i] + 0.001 * (i % 7 - 3), volume=1.0)
        for i in range(n)
    ]

    for label, fn in (
        ("sma20",     lambda: sma(src, 20)),
        ("ema50",     lambda: ema(src, 50)),
        ("rsi14",     lambda: rsi(src, 14)),
        ("atr14",     lambda: atr(hi, lo, src, 14)),
        ("macd",      lambda: macd(src, 12, 26, 9)),
        ("bollinger", lambda: bollinger(src, 20, 2.0)),
        ("stoch",     lambda: stochastic(hi, lo, src, 14, 3)),
        ("obv",       lambda: obv(src, vol)),
        ("adx",       lambda: adx(hi, lo, src, 14)),
        ("wma",       lambda: wma(src, 20)),
        ("cci",       lambda: cci(hi, lo, src, 20, 0.015)),
        ("williamsR", lambda: williams_r(hi, lo, src, 14)),
        ("roc",       lambda: roc(src, 10)),
        ("mfi",       lambda: mfi(hi, lo, src, vol, 14)),
        ("vwap",      lambda: vwap(hi, lo, src, vol)),
        ("keltner",   lambda: keltner(hi, lo, src, 20, 10, 2.0)),
        ("pscan",     lambda: scan_patterns(bars)),
    ):
        total, per = _time(fn, iters)
        print(f"{label}\t{total}\t{per}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
