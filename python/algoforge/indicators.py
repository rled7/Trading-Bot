"""SMA / EMA / RSI / ATR / MACD / Bollinger / Stochastic / OBV / ADX /
WMA / CCI / Williams %R / ROC / MFI / VWAP / Keltner / HMA / DEMA / TEMA / TRIX
— same math as the cpp/ reference.

API:
    sma(values, period) -> list[float | None]
    ema(values, period) -> list[float | None]
    rsi(values, period) -> list[float | None]
    atr(highs, lows, closes, period) -> list[float | None]
    macd(values, fast, slow, signal) -> tuple[list[float | None], list[float | None], list[float | None]]
    bollinger(values, period, mult) -> tuple[list[float | None], list[float | None], list[float | None]]
    stochastic(highs, lows, closes, k_period, d_period) -> tuple[list[float | None], list[float | None]]
    obv(closes, volumes) -> list[float]
    adx(highs, lows, closes, period) -> tuple[list[float | None], list[float | None], list[float | None]]
    wma(values, period) -> list[float | None]
    cci(highs, lows, closes, period, constant) -> list[float | None]
    williams_r(highs, lows, closes, period) -> list[float | None]
    roc(values, period) -> list[float | None]
    mfi(highs, lows, closes, volumes, period) -> list[float | None]
    vwap(highs, lows, closes, volumes) -> list[float | None]
    keltner(highs, lows, closes, ema_period, atr_period, mult) -> tuple[list[float | None], list[float | None], list[float | None]]
    hma(values, period) -> list[float | None]
    dema(values, period) -> list[float | None]
    tema(values, period) -> list[float | None]
    trix(values, period) -> list[float | None]

Indices where the indicator is not yet defined come back as None.
Raises ValueError for invalid args (period <= 0, mismatched lengths, etc.).
"""

from __future__ import annotations

import math
from typing import Sequence


def _check_period(period: int) -> None:
    if not isinstance(period, int) or period <= 0:
        raise ValueError(f"period must be a positive integer, got {period!r}")


def sma(values: Sequence[float | None], period: int) -> list[float | None]:
    """Simple moving average.

    Handles None entries by finding the first run of *period* consecutive
    non-None values (matching cpp/ af_sma NaN-skip behaviour).
    """
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    # Find first index where we have `period` consecutive non-None values.
    seed_start = -1
    run = 0
    for i in range(n):
        if values[i] is not None:
            run += 1
            if run == period:
                seed_start = i - period + 1
                break
        else:
            run = 0
    if seed_start < 0:
        return out
    seed_end = seed_start + period - 1
    window = sum(values[seed_start : seed_end + 1])  # type: ignore[arg-type]
    out[seed_end] = window / period
    for i in range(seed_end + 1, n):
        if values[i] is None or values[i - period] is None:
            # Gap — reset is not in cpp but None elements break the sliding window;
            # propagate None for safety.
            out[i] = None
        else:
            window += values[i] - values[i - period]  # type: ignore[operator]
            out[i] = window / period
    return out


def ema(values: Sequence[float | None], period: int) -> list[float | None]:
    """Exponential moving average.

    Handles None entries by finding the first run of *period* consecutive
    non-None values (matching cpp/ af_ema NaN-skip behaviour).
    """
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    # Find first index where we have `period` consecutive non-None values.
    seed_start = -1
    run = 0
    for i in range(n):
        if values[i] is not None:
            run += 1
            if run == period:
                seed_start = i - period + 1
                break
        else:
            run = 0
    if seed_start < 0:
        return out
    alpha = 2.0 / (period + 1)
    seed_end = seed_start + period - 1
    seed = sum(values[seed_start : seed_end + 1]) / period  # type: ignore[arg-type]
    out[seed_end] = seed
    prev = seed
    for i in range(seed_end + 1, n):
        if values[i] is None:
            out[i] = None
            prev = None  # type: ignore[assignment]
        elif prev is None:
            out[i] = None
        else:
            prev = values[i] * alpha + prev * (1.0 - alpha)  # type: ignore[operator]
            out[i] = prev
    return out


def rsi(values: Sequence[float], period: int) -> list[float | None]:
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    if n <= period:
        return out

    alpha = 1.0 / period
    ag = al = 0.0
    for i in range(1, period + 1):
        d = values[i] - values[i - 1]
        if d > 0:
            ag += d
        else:
            al += -d
    ag /= period
    al /= period

    for i in range(period, n):
        if i > period:
            d = values[i] - values[i - 1]
            u = d if d > 0 else 0.0
            v = -d if d < 0 else 0.0
            ag = ag * (1.0 - alpha) + u * alpha
            al = al * (1.0 - alpha) + v * alpha
        rs = ag / al if al > 1e-12 else 100.0
        out[i] = 100.0 - 100.0 / (1.0 + rs)
    return out


def atr(highs: Sequence[float], lows: Sequence[float],
        closes: Sequence[float], period: int) -> list[float | None]:
    _check_period(period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    out: list[float | None] = [None] * n
    if n <= period:
        return out

    tr = [0.0] * n
    tr[0] = highs[0] - lows[0]
    for i in range(1, n):
        prev_close = closes[i - 1]
        tr[i] = max(
            highs[i] - lows[i],
            abs(highs[i] - prev_close),
            abs(lows[i]  - prev_close),
        )

    alpha = 1.0 / period
    seed = sum(tr[:period]) / period
    out[period - 1] = seed
    prev = seed
    for i in range(period, n):
        prev = tr[i] * alpha + prev * (1.0 - alpha)
        out[i] = prev
    return out


def macd(values: Sequence[float], fast: int = 12, slow: int = 26, signal: int = 9,
         ) -> tuple[list[float | None], list[float | None], list[float | None]]:
    """Return (macd_line, signal_line, histogram).

    macd_line  = EMA(fast) - EMA(slow); None where either EMA is None.
    signal_line = EMA(macd_line, signal); None where macd_line is None.
    histogram  = macd_line - signal_line; None where either is None.
    Raises ValueError for invalid period args.
    """
    _check_period(fast)
    _check_period(slow)
    _check_period(signal)
    n = len(values)
    ef = ema(values, fast)
    es = ema(values, slow)
    macd_line: list[float | None] = [
        None if (ef[i] is None or es[i] is None) else ef[i] - es[i]  # type: ignore[operator]
        for i in range(n)
    ]
    signal_line = ema(macd_line, signal)
    histogram: list[float | None] = [
        None if (macd_line[i] is None or signal_line[i] is None)
        else macd_line[i] - signal_line[i]  # type: ignore[operator]
        for i in range(n)
    ]
    return macd_line, signal_line, histogram


def bollinger(values: Sequence[float], period: int = 20, mult: float = 2.0,
              ) -> tuple[list[float | None], list[float | None], list[float | None]]:
    """Return (upper, middle, lower) Bollinger Bands.

    middle = SMA(period); std is population std (variance = sum((x-mean)^2)/period).
    upper  = middle + mult*std; lower = middle - mult*std.
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    middle = sma(values, period)
    upper:  list[float | None] = [None] * n
    lower:  list[float | None] = [None] * n
    for i in range(period - 1, n):
        if middle[i] is None:
            continue
        m = middle[i]
        var = sum((values[i - j] - m) ** 2 for j in range(period)) / period  # type: ignore[operator]
        sd = var ** 0.5
        upper[i] = m + mult * sd  # type: ignore[operator]
        lower[i] = m - mult * sd  # type: ignore[operator]
    return upper, middle, lower


def stochastic(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
               k_period: int = 14, d_period: int = 3,
               ) -> tuple[list[float | None], list[float | None]]:
    """Return (k, d) stochastic lines.

    raw %K = 100*(close - lowest_low) / (highest_high - lowest_low) over k_period.
    When range is zero, raw %K = 50 (matching cpp/ reference).
    k_out = SMA(raw_k, d_period); d_out = SMA(k_out, d_period).
    Raises ValueError for invalid periods or mismatched input lengths.
    """
    _check_period(k_period)
    _check_period(d_period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    # Compute raw %K (NaN-equivalent = None for indices < k_period-1)
    raw_k: list[float | None] = [None] * n
    for i in range(k_period - 1, n):
        lo = min(lows[i - j] for j in range(k_period))
        hi = max(highs[i - j] for j in range(k_period))
        r = hi - lo
        raw_k[i] = 100.0 * (closes[i] - lo) / r if r > 1e-10 else 50.0
    # k_out = SMA(raw_k, d_period); d_out = SMA(k_out, d_period)
    k_out = sma(raw_k, d_period)
    d_out = sma(k_out, d_period)
    return k_out, d_out


def obv(closes: Sequence[float], volumes: Sequence[float]) -> list[float]:
    """Return on-balance volume.

    out[0] = 0; out[i] = out[i-1] + vol[i] if close rises, -vol[i] if falls, else 0.
    Raises ValueError for mismatched input lengths.
    """
    n = len(closes)
    if len(volumes) != n:
        raise ValueError("closes and volumes must have the same length")
    out = [0.0] * n
    for i in range(1, n):
        d = closes[i] - closes[i - 1]
        out[i] = out[i - 1] + (volumes[i] if d > 0 else -volumes[i] if d < 0 else 0.0)
    return out


def wma(values: Sequence[float | None], period: int) -> list[float | None]:
    """Weighted moving average.

    Most recent bar gets weight = period; oldest in window gets weight = 1.
    Sum of weights = period*(period+1)/2.
    Handles None entries: any window containing a None yields None output.
    Matches cpp/ af_wma: defined from index period-1 onwards.
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    wsum = period * (period + 1) / 2.0
    for i in range(period - 1, n):
        s = 0.0
        valid = True
        for j in range(period):
            v = values[i - (period - 1 - j)]
            if v is None:
                valid = False
                break
            # weight of oldest element (j=0) is 1, most recent (j=period-1) is period
            s += v * (j + 1)
        if valid:
            out[i] = s / wsum
    return out


def cci(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
        period: int = 20, constant: float = 0.015) -> list[float | None]:
    """Commodity Channel Index.

    TP = (H + L + C) / 3
    CCI = (TP - SMA(TP)) / (constant * mean_abs_dev(TP))
    When mean_abs_dev == 0, returns 0 (matching cpp/).
    Defined from index period-1 onwards.
    Raises ValueError for invalid period or mismatched input lengths.
    """
    _check_period(period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    for i in range(period - 1, n):
        total = 0.0
        for j in range(period):
            total += (highs[i - j] + lows[i - j] + closes[i - j]) / 3.0
        mean = total / period
        mad = 0.0
        for j in range(period):
            mad += abs((highs[i - j] + lows[i - j] + closes[i - j]) / 3.0 - mean)
        mad /= period
        tp = (highs[i] + lows[i] + closes[i]) / 3.0
        out[i] = (tp - mean) / (constant * mad) if mad > EPSILON else 0.0
    return out


def williams_r(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
               period: int = 14) -> list[float | None]:
    """Williams %R.

    %R = -100 * (highest_high - close) / (highest_high - lowest_low)
    When range == 0, returns -50 (matching cpp/).
    Defined from index period-1 onwards; values are in [-100, 0].
    Raises ValueError for invalid period or mismatched input lengths.
    """
    _check_period(period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    for i in range(period - 1, n):
        hi = highs[i]
        lo = lows[i]
        for j in range(1, period):
            if highs[i - j] > hi:
                hi = highs[i - j]
            if lows[i - j] < lo:
                lo = lows[i - j]
        r = hi - lo
        out[i] = -100.0 * (hi - closes[i]) / r if r > EPSILON else -50.0
    return out


def roc(values: Sequence[float], period: int) -> list[float | None]:
    """Rate of Change.

    ROC = 100 * (close[i] - close[i-period]) / close[i-period]
    When close[i-period] == 0, returns 0 (matching cpp/).
    Defined from index period onwards (n > period required).
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    if n <= period:
        return out
    for i in range(period, n):
        p = values[i - period]
        out[i] = (values[i] - p) / p * 100.0 if abs(p) > EPSILON else 0.0
    return out


def mfi(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
        volumes: Sequence[float], period: int = 14) -> list[float | None]:
    """Money Flow Index.

    Typical price TP = (H + L + C) / 3
    Money flow MF = TP * volume
    Positive MF when TP >= previous TP; negative otherwise.
    MFI = 100 - 100 / (1 + positive_MF / negative_MF)
    When negative_MF == 0, returns 100 (matching cpp/).
    Defined from index period onwards (n > period required).
    Raises ValueError for invalid period or mismatched input lengths.
    """
    _check_period(period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n and len(volumes) == n):
        raise ValueError("highs, lows, closes, volumes must all have the same length")
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    if n <= period:
        return out
    for i in range(period, n):
        pmf = 0.0
        nmf = 0.0
        for j in range(period):
            k = i - j
            tp = (highs[k] + lows[k] + closes[k]) / 3.0
            mf_val = tp * volumes[k]
            # Previous TP: if k == 0, use current TP (matching cpp/ k>0 check)
            if k > 0:
                pp = (highs[k - 1] + lows[k - 1] + closes[k - 1]) / 3.0
            else:
                pp = tp
            if tp >= pp:
                pmf += mf_val
            else:
                nmf += mf_val
        r = pmf / nmf if nmf > EPSILON else 100.0
        out[i] = 100.0 - 100.0 / (1.0 + r)
    return out


def vwap(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
         volumes: Sequence[float]) -> list[float | None]:
    """Volume Weighted Average Price — cumulative (no period reset).

    VWAP[i] = cumulative(TP * vol) / cumulative(vol)
    Zero/negative volume is treated as 1 (matching cpp/).
    Always defined (no None values returned).
    Raises ValueError for mismatched input lengths.
    """
    n = len(highs)
    if not (len(lows) == n and len(closes) == n and len(volumes) == n):
        raise ValueError("highs, lows, closes, volumes must all have the same length")
    out: list[float | None] = [None] * n
    cpv = 0.0
    cv = 0.0
    for i in range(n):
        vv = volumes[i] if volumes[i] > 0 else 1.0
        tp = (highs[i] + lows[i] + closes[i]) / 3.0
        cpv += tp * vv
        cv += vv
        out[i] = cpv / cv
    return out


def keltner(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
            ema_period: int = 20, atr_period: int = 10, mult: float = 2.0,
            ) -> tuple[list[float | None], list[float | None], list[float | None]]:
    """Keltner Channels.

    middle = EMA(close, ema_period)
    upper  = middle + mult * ATR(highs, lows, closes, atr_period)
    lower  = middle - mult * ATR(highs, lows, closes, atr_period)
    Returns (upper, middle, lower).
    upper/lower are None where either EMA or ATR is None.
    Matches cpp/ af_keltner.
    Raises ValueError for invalid periods or mismatched input lengths.
    """
    _check_period(ema_period)
    _check_period(atr_period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    middle = ema(closes, ema_period)
    atr_vals = atr(highs, lows, closes, atr_period)
    upper: list[float | None] = [None] * n
    lower: list[float | None] = [None] * n
    for i in range(n):
        if middle[i] is None or atr_vals[i] is None:
            upper[i] = None
            lower[i] = None
        else:
            upper[i] = middle[i] + mult * atr_vals[i]  # type: ignore[operator]
            lower[i] = middle[i] - mult * atr_vals[i]  # type: ignore[operator]
    return upper, middle, lower


def adx(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
        period: int = 14,
        ) -> tuple[list[float | None], list[float | None], list[float | None]]:
    """Return (adx, +DI, -DI).

    Wilder smoothing (alpha = 1/period) over TR, +DM, -DM.
    +DI = 100*+DM_smoothed/ATR_smoothed; -DI = 100*-DM_smoothed/ATR_smoothed.
    DX = 100*|+DI - -DI|/(+DI + -DI); ADX = Wilder-smoothed DX.
    Output is defined starting at index period (requires n >= 2*period+1 for all
    outputs to be valid — matching the cpp/ reference guard).
    Raises ValueError for invalid period or mismatched input lengths.
    """
    _check_period(period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    none_n: list[float | None] = [None] * n
    if n < 2 * period + 1:
        return list(none_n), list(none_n), list(none_n)

    EPSILON = 1e-10
    alpha = 1.0 / period

    # Build TR, +DM, -DM (index 0 unused / 0.0, as in cpp/)
    tr   = [0.0] * n
    pdm  = [0.0] * n
    mdm  = [0.0] * n
    for i in range(1, n):
        hl  = highs[i] - lows[i]
        hpc = abs(highs[i] - closes[i - 1])
        lpc = abs(lows[i]  - closes[i - 1])
        tr[i] = max(hl, hpc, lpc)
        up = highs[i] - highs[i - 1]
        dn = lows[i - 1] - lows[i]
        pdm[i] = up if (up > dn and up > 0) else 0.0
        mdm[i] = dn if (dn > up and dn > 0) else 0.0

    out_adx: list[float | None] = [None] * n
    out_pdi: list[float | None] = [None] * n
    out_mdi: list[float | None] = [None] * n

    # Seed: sum indices 1..period (inclusive), same as cpp/
    atr_v = sum(tr[1 : period + 1])
    pdm_s = sum(pdm[1 : period + 1])
    mdm_s = sum(mdm[1 : period + 1])

    for i in range(period, n):
        if i > period:
            atr_v = atr_v * (1 - alpha) + tr[i]
            pdm_s = pdm_s * (1 - alpha) + pdm[i]
            mdm_s = mdm_s * (1 - alpha) + mdm[i]
        pdi = 100.0 * pdm_s / atr_v if atr_v > EPSILON else 0.0
        mdi = 100.0 * mdm_s / atr_v if atr_v > EPSILON else 0.0
        out_pdi[i] = pdi
        out_mdi[i] = mdi
        denom = pdi + mdi
        dx = 100.0 * abs(pdi - mdi) / denom if denom > EPSILON else 0.0
        if i == period:
            out_adx[i] = dx
        else:
            prev_adx = out_adx[i - 1]
            out_adx[i] = prev_adx * (1 - alpha) + dx * alpha  # type: ignore[operator]

    return out_adx, out_pdi, out_mdi


def hma(values: Sequence[float], period: int) -> list[float | None]:
    """Hull Moving Average.

    Matches cpp/ af_hma:
      half = int(sqrt(period / 2.0)); clamped to >= 1
      sq   = int(sqrt(period));       clamped to >= 2
      raw[i] = 2 * WMA(half)[i] - WMA(period)[i]  (None where either is None)
      HMA    = WMA(raw, sq)
    Raises ValueError for period < 2.
    """
    _check_period(period)
    if period < 2:
        raise ValueError(f"hma requires period >= 2, got {period!r}")
    n = len(values)
    half = int(math.sqrt(period / 2.0))
    if half < 1:
        half = 1
    sq = int(math.sqrt(float(period)))
    if sq < 2:
        sq = 2
    wh = wma(values, half)
    wf = wma(values, period)
    raw: list[float | None] = [
        None if (wh[i] is None or wf[i] is None) else 2.0 * wh[i] - wf[i]  # type: ignore[operator]
        for i in range(n)
    ]
    return wma(raw, sq)


def dema(values: Sequence[float], period: int) -> list[float | None]:
    """Double Exponential Moving Average.

    DEMA = 2 * EMA1 - EMA(EMA1, period).
    Matches cpp/ af_dema.
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    e1 = ema(values, period)
    e2 = ema(e1, period)
    return [
        None if (e1[i] is None or e2[i] is None) else 2.0 * e1[i] - e2[i]  # type: ignore[operator]
        for i in range(n)
    ]


def tema(values: Sequence[float], period: int) -> list[float | None]:
    """Triple Exponential Moving Average.

    TEMA = 3 * EMA1 - 3 * EMA2 + EMA3.
    Matches cpp/ af_tema.
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    e1 = ema(values, period)
    e2 = ema(e1, period)
    e3 = ema(e2, period)
    return [
        None if (e1[i] is None or e2[i] is None or e3[i] is None)
        else 3.0 * e1[i] - 3.0 * e2[i] + e3[i]  # type: ignore[operator]
        for i in range(n)
    ]


def trix(values: Sequence[float], period: int) -> list[float | None]:
    """Triple-smoothed EMA Rate of Change (TRIX line only).

    Triple-smooth the input with EMA(period) three times; then compute:
      TRIX[i] = (e3[i] - e3[i-1]) / e3[i-1] * 100
    where |e3[i-1]| > EPSILON, else None.
    Defined from index 1 onwards where e3 is valid.
    Matches cpp/ af_trix (signal line omitted; just returns the TRIX line).
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    e1 = ema(values, period)
    e2 = ema(e1, period)
    e3 = ema(e2, period)
    for i in range(1, n):
        if e3[i] is None or e3[i - 1] is None:
            out[i] = None
        elif abs(e3[i - 1]) > EPSILON:  # type: ignore[arg-type]
            out[i] = (e3[i] - e3[i - 1]) / e3[i - 1] * 100.0  # type: ignore[operator]
        # else leave as None (|prev| <= EPSILON)
    return out
