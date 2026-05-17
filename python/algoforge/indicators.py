"""SMA / EMA / RSI / ATR / MACD / Bollinger / Stochastic / OBV / ADX /
WMA / CCI / Williams %R / ROC / MFI / VWAP / Keltner / HMA / DEMA / TEMA / TRIX /
Momentum / TrueRange / WilderEMA / VWMA / HistVol / CMF / AccDist / ForceIndex /
VolOsc / Donchian / PivotClassic / PivotFibonacci / PivotCamarilla / Fibonacci /
SAR / Ichimoku
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
    momentum(values, period) -> list[float | None]
    true_range(highs, lows, closes) -> list[float | None]
    wilder_ema(values, period) -> list[float | None]
    vwma(values, volumes, period) -> list[float | None]
    hist_vol(values, period, tdays) -> list[float | None]
    cmf(highs, lows, closes, volumes, period) -> list[float | None]
    acc_dist(highs, lows, closes, volumes) -> list[float]
    force_index(closes, volumes, period) -> list[float | None]
    vol_osc(volumes, fast, slow) -> list[float | None]
    donchian(highs, lows, period) -> tuple[list[float | None], list[float | None], list[float | None]]
    pivot_classic(high, low, close) -> dict[str, float]
    pivot_fibonacci(high, low, close) -> dict[str, float]
    pivot_camarilla(high, low, close) -> dict[str, float]
    fibonacci(swing_high, swing_low) -> list[float]
    sar(highs, lows, start, step, max_af) -> tuple[list[float | None], list[float | None]]
    ichimoku(highs, lows, closes, tenkan, kijun, senkou_b) -> tuple[...]

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


# ══ NEW INDICATORS ════════════════════════════════════════════════════════════

def momentum(values: Sequence[float], period: int) -> list[float | None]:
    """Momentum.

    out[i] = values[i] - values[i - period]
    Defined from index period onwards (n > period required).
    Matches cpp/ af_momentum.
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    if n <= period:
        return out
    for i in range(period, n):
        out[i] = values[i] - values[i - period]
    return out


def true_range(highs: Sequence[float], lows: Sequence[float],
               closes: Sequence[float]) -> list[float | None]:
    """True Range.

    out[0] = highs[0] - lows[0]
    out[i] = max(highs[i] - lows[i], |highs[i] - closes[i-1]|, |lows[i] - closes[i-1]|)
    Always fully defined (no None).
    Matches cpp/ af_true_range.
    Raises ValueError for mismatched lengths or empty input.
    """
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")
    if n == 0:
        return []
    out: list[float | None] = [None] * n
    out[0] = highs[0] - lows[0]
    for i in range(1, n):
        hl = highs[i] - lows[i]
        hpc = abs(highs[i] - closes[i - 1])
        lpc = abs(lows[i] - closes[i - 1])
        out[i] = max(hl, hpc, lpc)
    return out


def wilder_ema(values: Sequence[float | None], period: int) -> list[float | None]:
    """Wilder's EMA (smoothing factor alpha = 1/period).

    Seeds with SMA of first `period` consecutive non-None values.
    Matches cpp/ af_wilder_ema.
    Raises ValueError for invalid period.
    """
    _check_period(period)
    n = len(values)
    out: list[float | None] = [None] * n
    # Find first run of `period` consecutive non-None values
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
    alpha = 1.0 / period
    seed_end = seed_start + period - 1
    seed = sum(values[seed_start: seed_end + 1]) / period  # type: ignore[arg-type]
    out[seed_end] = seed
    for i in range(seed_end + 1, n):
        if values[i] is None or out[i - 1] is None:
            out[i] = None
        else:
            out[i] = values[i] * alpha + out[i - 1] * (1.0 - alpha)  # type: ignore[operator]
    return out


def vwma(values: Sequence[float], volumes: Sequence[float],
         period: int) -> list[float | None]:
    """Volume Weighted Moving Average.

    VWMA[i] = sum(price[j]*vol[j]) / sum(vol[j])  over period bars ending at i.
    Zero/negative volume is treated as 1 (matching cpp/ af_vwma).
    Defined from index period-1 onwards.
    Raises ValueError for invalid period or mismatched lengths.
    """
    _check_period(period)
    n = len(values)
    if len(volumes) != n:
        raise ValueError("values and volumes must have the same length")
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    for i in range(period - 1, n):
        pv = 0.0
        vsum = 0.0
        for j in range(period):
            vv = volumes[i - j]
            if vv <= 0:
                vv = 1.0
            pv += values[i - j] * vv
            vsum += vv
        out[i] = pv / vsum if vsum > EPSILON else values[i]
    return out


def hist_vol(values: Sequence[float], period: int,
             tdays: int = 252) -> list[float | None]:
    """Historical Volatility (annualised).

    Computes log-return std dev over a rolling `period` window, annualised by
    sqrt(tdays) and scaled to percent.
    Uses sample std dev (divides by period-1, matching cpp/ af_hist_vol).
    Defined from index period onwards (n > period required).
    Raises ValueError for period < 2 or mismatched lengths.
    """
    _check_period(period)
    if period < 2:
        raise ValueError(f"hist_vol requires period >= 2, got {period!r}")
    n = len(values)
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    if n <= period:
        return out
    for i in range(period, n):
        mean = 0.0
        count = 0
        for j in range(period):
            p = values[i - 1 - j]
            if p > EPSILON:
                mean += math.log(values[i - j] / p)
                count += 1
        mean /= period
        var = 0.0
        for j in range(period):
            p = values[i - 1 - j]
            if p > EPSILON:
                r = math.log(values[i - j] / p) - mean
                var += r * r
        sd = math.sqrt(var / (period - 1))
        out[i] = sd * math.sqrt(float(tdays)) * 100.0
    return out


def cmf(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
        volumes: Sequence[float], period: int = 20) -> list[float | None]:
    """Chaikin Money Flow.

    CLV = ((close - low) - (high - close)) / (high - low)
    CMF[i] = sum(CLV[j] * vol[j]) / sum(vol[j])  over period bars ending at i.
    When range == 0, CLV = 0.  When total volume == 0, CMF = 0.
    Defined from index period-1 onwards.
    Matches cpp/ af_cmf.
    Raises ValueError for invalid period or mismatched lengths.
    """
    _check_period(period)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n and len(volumes) == n):
        raise ValueError("highs, lows, closes, volumes must all have the same length")
    out: list[float | None] = [None] * n
    EPSILON = 1e-10
    for i in range(period - 1, n):
        mfvs = 0.0
        vs = 0.0
        for j in range(period):
            r = highs[i - j] - lows[i - j]
            clv = ((closes[i - j] - lows[i - j]) - (highs[i - j] - closes[i - j])) / r \
                  if r > EPSILON else 0.0
            mfvs += clv * volumes[i - j]
            vs += volumes[i - j]
        out[i] = mfvs / vs if vs > EPSILON else 0.0
    return out


def acc_dist(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
             volumes: Sequence[float]) -> list[float]:
    """Accumulation / Distribution Line.

    CLV = ((close - low) - (high - close)) / (high - low)
    acc_dist[0] = CLV[0] * vol[0] (not seeded at 0 as in cpp/ af_acc_dist).

    Note: cpp/ sets out[0] = 0 then adds CLV*vol in the same loop starting at i=0,
    making acc_dist[0] = CLV[0]*vol[0].  This Python version matches that exactly.
    Raises ValueError for mismatched lengths.
    """
    n = len(highs)
    if not (len(lows) == n and len(closes) == n and len(volumes) == n):
        raise ValueError("highs, lows, closes, volumes must all have the same length")
    out: list[float] = [0.0] * n
    EPSILON = 1e-10
    for i in range(n):
        r = highs[i] - lows[i]
        clv = ((closes[i] - lows[i]) - (highs[i] - closes[i])) / r if r > EPSILON else 0.0
        out[i] = (0.0 if i == 0 else out[i - 1]) + clv * volumes[i]
    return out


def force_index(closes: Sequence[float], volumes: Sequence[float],
                period: int = 13) -> list[float | None]:
    """Force Index.

    raw[0] = 0; raw[i] = (close[i] - close[i-1]) * volume[i]
    Force Index = EMA(raw, period).
    Matches cpp/ af_force_index.
    Raises ValueError for invalid period, mismatched lengths, or n < 2.
    """
    _check_period(period)
    n = len(closes)
    if len(volumes) != n:
        raise ValueError("closes and volumes must have the same length")
    if n < 2:
        raise ValueError("force_index requires at least 2 bars")
    raw: list[float | None] = [0.0] + [
        (closes[i] - closes[i - 1]) * volumes[i] for i in range(1, n)
    ]
    return ema(raw, period)


def vol_osc(volumes: Sequence[float], fast: int = 14,
            slow: int = 28) -> list[float | None]:
    """Volume Oscillator.

    vol_osc[i] = (EMA(fast)[i] - EMA(slow)[i]) / EMA(slow)[i] * 100
    None where either EMA is None or |EMA(slow)| <= EPSILON.
    Matches cpp/ af_vol_osc.
    Raises ValueError for invalid periods.
    """
    _check_period(fast)
    _check_period(slow)
    n = len(volumes)
    ef = ema(volumes, fast)
    es = ema(volumes, slow)
    EPSILON = 1e-10
    out: list[float | None] = [None] * n
    for i in range(n):
        if ef[i] is not None and es[i] is not None and abs(es[i]) > EPSILON:  # type: ignore[arg-type]
            out[i] = (ef[i] - es[i]) / es[i] * 100.0  # type: ignore[operator]
    return out


def donchian(highs: Sequence[float], lows: Sequence[float],
             period: int = 20,
             ) -> tuple[list[float | None], list[float | None], list[float | None]]:
    """Donchian Channels.

    upper  = highest high over period bars
    lower  = lowest low over period bars
    middle = (upper + lower) / 2
    Returns (upper, middle, lower).
    Defined from index period-1 onwards.
    Matches cpp/ af_donchian.
    Raises ValueError for invalid period or mismatched lengths.
    """
    _check_period(period)
    n = len(highs)
    if len(lows) != n:
        raise ValueError("highs and lows must have the same length")
    upper: list[float | None] = [None] * n
    middle: list[float | None] = [None] * n
    lower: list[float | None] = [None] * n
    for i in range(period - 1, n):
        hi = highs[i]
        lo = lows[i]
        for j in range(1, period):
            if highs[i - j] > hi:
                hi = highs[i - j]
            if lows[i - j] < lo:
                lo = lows[i - j]
        upper[i] = hi
        lower[i] = lo
        middle[i] = (hi + lo) * 0.5
    return upper, middle, lower


def pivot_classic(high: float, low: float, close: float) -> dict[str, float]:
    """Classic Pivot Points.

    p  = (high + low + close) / 3
    r1 = 2*p - low;  r2 = p + (high - low);  r3 = p + 2*(high - low)
    s1 = 2*p - high; s2 = p - (high - low);  s3 = p - 2*(high - low)
    Returns {'p', 'r1', 's1', 'r2', 's2', 'r3', 's3'}.
    Matches cpp/ af_pivot_classic.
    """
    p = (high + low + close) / 3.0
    r = high - low
    return {
        'p':  p,
        'r1': 2.0 * p - low,
        'r2': p + r,
        'r3': p + 2.0 * r,
        's1': 2.0 * p - high,
        's2': p - r,
        's3': p - 2.0 * r,
    }


def pivot_fibonacci(high: float, low: float, close: float) -> dict[str, float]:
    """Fibonacci Pivot Points.

    p  = (high + low + close) / 3;  r = high - low
    r1 = p + r*0.382; r2 = p + r*0.618; r3 = p + r*1.000
    s1 = p - r*0.382; s2 = p - r*0.618; s3 = p - r*1.000
    Returns {'p', 'r1', 's1', 'r2', 's2', 'r3', 's3'}.
    Matches cpp/ af_pivot_fibonacci.
    """
    p = (high + low + close) / 3.0
    r = high - low
    return {
        'p':  p,
        'r1': p + r * 0.382,
        'r2': p + r * 0.618,
        'r3': p + r * 1.000,
        's1': p - r * 0.382,
        's2': p - r * 0.618,
        's3': p - r * 1.000,
    }


def pivot_camarilla(high: float, low: float, close: float) -> dict[str, float]:
    """Camarilla Pivot Points.

    r = high - low
    r4 = close + r*1.1/2;   r3 = close + r*1.1/4
    r2 = close + r*1.1/6;   r1 = close + r*1.1/12
    s1 = close - r*1.1/12;  s2 = close - r*1.1/6
    s3 = close - r*1.1/4;   s4 = close - r*1.1/2  (extra level, returned as 's4')
    Returns {'p', 'r1', 's1', 'r2', 's2', 'r3', 's3'} plus 'r4'/'s4'.
    Note: cpp/ af_pivot_camarilla has r4/s4 but not p; we set p = (h+l+c)/3 for
    consistency with the other pivot functions and include r4/s4 as extra keys.
    Matches cpp/ af_pivot_camarilla.
    """
    r = high - low
    return {
        'p':  (high + low + close) / 3.0,
        'r1': close + r * 1.1 / 12.0,
        'r2': close + r * 1.1 / 6.0,
        'r3': close + r * 1.1 / 4.0,
        'r4': close + r * 1.1 / 2.0,
        's1': close - r * 1.1 / 12.0,
        's2': close - r * 1.1 / 6.0,
        's3': close - r * 1.1 / 4.0,
        's4': close - r * 1.1 / 2.0,
    }


def fibonacci(swing_high: float, swing_low: float,
              uptrend: bool = True) -> list[float]:
    """Fibonacci Retracement Levels.

    Returns 7 levels: 0%, 23.6%, 38.2%, 50%, 61.8%, 78.6%, 100%.
    If uptrend=True: levels[i] = swing_high - F[i] * (swing_high - swing_low)
    If uptrend=False: levels[i] = swing_low + F[i] * (swing_high - swing_low)
    Matches cpp/ af_fibonacci.
    """
    _FIBS = [0.0, 0.236, 0.382, 0.500, 0.618, 0.786, 1.000]
    r = swing_high - swing_low
    if uptrend:
        return [swing_high - f * r for f in _FIBS]
    else:
        return [swing_low + f * r for f in _FIBS]


def sar(highs: Sequence[float], lows: Sequence[float],
        start: float = 0.02, step: float = 0.02, max_af: float = 0.2,
        ) -> tuple[list[float | None], list[float | None]]:
    """Parabolic SAR.

    Returns (sar_values, is_bullish_flag) where is_bullish_flag[i] = 1.0 (bullish)
    or 0.0 (bearish).
    Initialises at index 0: sar[0] = lows[0], is_bullish[0] = 1.0.
    Matches cpp/ af_sar exactly.
    Raises ValueError for mismatched lengths or n < 2.
    """
    n = len(highs)
    if len(lows) != n:
        raise ValueError("highs and lows must have the same length")
    if n < 2:
        raise ValueError("sar requires at least 2 bars")
    out_sar: list[float | None] = [None] * n
    out_bull: list[float | None] = [None] * n
    out_sar[0] = lows[0]
    out_bull[0] = 1.0
    ep = highs[0]
    accel = start
    for i in range(1, n):
        bull = out_bull[i - 1] > 0.5  # type: ignore[operator]
        prev_sar = out_sar[i - 1]
        nep = ep
        if bull:
            sar_val = prev_sar + accel * (ep - prev_sar)  # type: ignore[operator]
            # Clamp SAR below prior two lows
            prev2_low = lows[i - 2] if i >= 2 else lows[i - 1]
            sar_val = min(sar_val, lows[i - 1], prev2_low)
            if lows[i] < sar_val:
                # Reversal to bearish
                sar_val = ep
                nep = lows[i]
                accel = start
                bull = False
            elif highs[i] > ep:
                nep = highs[i]
                accel = min(accel + step, max_af)
        else:
            sar_val = prev_sar - accel * (prev_sar - ep)  # type: ignore[operator]
            # Clamp SAR above prior two highs
            prev2_high = highs[i - 2] if i >= 2 else highs[i - 1]
            sar_val = max(sar_val, highs[i - 1], prev2_high)
            if highs[i] > sar_val:
                # Reversal to bullish
                sar_val = ep
                nep = highs[i]
                accel = start
                bull = True
            elif lows[i] < ep:
                nep = lows[i]
                accel = min(accel + step, max_af)
        ep = nep
        out_sar[i] = sar_val
        out_bull[i] = 1.0 if bull else 0.0
    return out_sar, out_bull


def ichimoku(highs: Sequence[float], lows: Sequence[float], closes: Sequence[float],
             tenkan: int = 9, kijun: int = 26, senkou_b: int = 52,
             ) -> tuple[list[float | None], list[float | None], list[float | None],
                        list[float | None], list[float | None]]:
    """Ichimoku Cloud.

    tenkan_sen  = (highest_high + lowest_low) / 2  over `tenkan` bars
    kijun_sen   = (highest_high + lowest_low) / 2  over `kijun` bars
    senkou_a    = (tenkan + kijun) / 2, shifted forward by `kijun` bars
    senkou_b    = (highest_high + lowest_low) / 2  over `senkou_b` bars,
                  shifted forward by `kijun` bars
    chikou      = close shifted back by `kijun` bars

    Returns (tenkan_sen, kijun_sen, senkou_a, senkou_b, chikou).
    All output arrays have length n; leading/trailing Nones where undefined.
    Matches cpp/ af_ichimoku (shift = kijun).
    Raises ValueError for invalid periods or mismatched lengths.
    """
    _check_period(tenkan)
    _check_period(kijun)
    _check_period(senkou_b)
    n = len(highs)
    if not (len(lows) == n and len(closes) == n):
        raise ValueError("highs, lows, closes must all have the same length")

    shift = kijun  # cpp/ uses kijun as the shift

    out_t: list[float | None] = [None] * n
    out_k: list[float | None] = [None] * n
    out_sa: list[float | None] = [None] * n
    out_sb: list[float | None] = [None] * n
    out_ch: list[float | None] = [None] * n

    for i in range(n):
        # Tenkan-sen
        if i >= tenkan - 1:
            hi = highs[i]
            lo = lows[i]
            for j in range(1, tenkan):
                if highs[i - j] > hi:
                    hi = highs[i - j]
                if lows[i - j] < lo:
                    lo = lows[i - j]
            out_t[i] = (hi + lo) / 2.0
        # Kijun-sen
        if i >= kijun - 1:
            hi = highs[i]
            lo = lows[i]
            for j in range(1, kijun):
                if highs[i - j] > hi:
                    hi = highs[i - j]
                if lows[i - j] < lo:
                    lo = lows[i - j]
            out_k[i] = (hi + lo) / 2.0

    # Senkou A: shifted forward by `shift`
    for i in range(n):
        dst = i + shift
        if dst < n and out_t[i] is not None and out_k[i] is not None:
            out_sa[dst] = (out_t[i] + out_k[i]) / 2.0  # type: ignore[operator]

    # Senkou B: shifted forward by `shift`
    for i in range(n):
        if i >= senkou_b - 1:
            hi = highs[i]
            lo = lows[i]
            for j in range(1, senkou_b):
                if highs[i - j] > hi:
                    hi = highs[i - j]
                if lows[i - j] < lo:
                    lo = lows[i - j]
            dst = i + shift
            if dst < n:
                out_sb[dst] = (hi + lo) / 2.0

    # Chikou: close shifted back by `shift` (close[i] goes to index i - shift)
    for i in range(shift, n):
        out_ch[i - shift] = closes[i]

    return out_t, out_k, out_sa, out_sb, out_ch
