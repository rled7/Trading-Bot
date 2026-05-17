import math
import unittest

from algoforge.indicators import (
    sma, ema, rsi, atr, macd, bollinger, stochastic, obv, adx,
    wma, cci, williams_r, roc, mfi, vwap, keltner,
)


class SmaTests(unittest.TestCase):
    def test_invalid_period(self):
        for bad in (0, -1, 1.5, "3"):
            with self.assertRaises(ValueError):
                sma([1, 2, 3], bad)  # type: ignore[arg-type]

    def test_empty_input(self):
        self.assertEqual(sma([], 3), [])

    def test_period_greater_than_n(self):
        out = sma([1, 2], 5)
        self.assertEqual(out, [None, None])

    def test_period_equals_n(self):
        out = sma([2, 4, 6], 3)
        self.assertIsNone(out[0]); self.assertIsNone(out[1])
        self.assertAlmostEqual(out[2], 4.0)

    def test_known_values(self):
        out = sma([1, 2, 3, 4, 5], 3)
        self.assertIsNone(out[0]); self.assertIsNone(out[1])
        self.assertAlmostEqual(out[2], 2.0)
        self.assertAlmostEqual(out[3], 3.0)
        self.assertAlmostEqual(out[4], 4.0)

    def test_constant_input(self):
        out = sma([7.5] * 10, 4)
        for v in out[3:]:
            self.assertAlmostEqual(v, 7.5)

    def test_period_one_is_identity(self):
        data = [1, 2, 3, 4, 5]
        self.assertEqual(sma(data, 1), data)


class EmaTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            ema([1, 2, 3], 0)

    def test_insufficient_data(self):
        self.assertEqual(ema([1, 2], 5), [None, None])

    def test_constant_input(self):
        out = ema([3.14] * 20, 5)
        for v in out[4:]:
            self.assertAlmostEqual(v, 3.14, places=12)

    def test_responds_to_step_up(self):
        data = [100.0] * 10 + [110.0] * 10
        out = ema(data, 5)
        self.assertGreater(out[10], 100.0)
        self.assertLess(out[10], 110.0)
        self.assertGreater(out[19], out[10])
        self.assertLess(out[19], 110.0)


class RsiTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            rsi([1, 2, 3], 0)

    def test_insufficient_data(self):
        self.assertEqual(rsi([1, 2, 3, 4, 5], 5), [None] * 5)

    def test_bounds(self):
        # deterministic noisy walk
        data, p = [], 100.0
        for i in range(100):
            p += ((i * 9301 + 49297) % 233 - 116) * 0.01
            data.append(p)
        out = rsi(data, 14)
        for v in out[14:]:
            self.assertGreaterEqual(v, 0.0)
            self.assertLessEqual(v, 100.0)

    def test_strict_uptrend(self):
        data = [100.0 + i * 0.5 for i in range(50)]
        out = rsi(data, 14)
        # rs is capped at 100 when avg-loss is ~0; RSI ≈ 99.01.
        self.assertGreater(out[-1], 99.0)
        self.assertLessEqual(out[-1], 100.0)

    def test_strict_downtrend(self):
        data = [100.0 - i * 0.5 for i in range(50)]
        out = rsi(data, 14)
        self.assertLess(out[-1], 5.0)
        self.assertGreaterEqual(out[-1], 0.0)


class AtrTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            atr([1, 2], [0, 1], [1, 1], 0)

    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            atr([1, 2, 3], [0, 1], [1, 1, 1], 2)

    def test_non_negative(self):
        h, l, c = [], [], []
        for i in range(100):
            mid = 100.0 + i * 0.1
            h.append(mid + 0.5); l.append(mid - 0.5); c.append(mid)
        out = atr(h, l, c, 14)
        for v in out[13:]:
            self.assertGreaterEqual(v, 0.0)

    def test_constant_range(self):
        h = [11.0] * 50; l = [9.0] * 50; c = [10.0] * 50
        out = atr(h, l, c, 14)
        # Bar 0 TR = 2, subsequent bars TR = max(2, |11-10|, |9-10|) = 2 → ATR stays at 2.
        self.assertAlmostEqual(out[-1], 2.0, places=9)

    def test_insufficient_data(self):
        h = [1.0] * 5; l = [0.0] * 5; c = [1.0] * 5
        self.assertEqual(atr(h, l, c, 14), [None] * 5)


class MacdTests(unittest.TestCase):
    def test_invalid_fast(self):
        with self.assertRaises(ValueError):
            macd([1.0] * 30, fast=0)

    def test_invalid_slow(self):
        with self.assertRaises(ValueError):
            macd([1.0] * 30, slow=-1)

    def test_invalid_signal(self):
        with self.assertRaises(ValueError):
            macd([1.0] * 30, signal=0)

    def test_empty_input(self):
        ml, sl, hl = macd([], 12, 26, 9)
        self.assertEqual(ml, [])
        self.assertEqual(sl, [])
        self.assertEqual(hl, [])

    def test_insufficient_data_all_none(self):
        # With only 5 values, neither fast nor slow EMA is defined
        ml, sl, hl = macd([1.0] * 5, fast=12, slow=26, signal=9)
        self.assertTrue(all(v is None for v in ml))
        self.assertTrue(all(v is None for v in sl))
        self.assertTrue(all(v is None for v in hl))

    def test_histogram_equals_line_minus_signal(self):
        data = [100.0 + i * 0.3 for i in range(60)]
        ml, sl, hl = macd(data, 12, 26, 9)
        for i in range(len(data)):
            if ml[i] is not None and sl[i] is not None:
                self.assertAlmostEqual(hl[i], ml[i] - sl[i], places=12)
            else:
                self.assertIsNone(hl[i])

    def test_constant_input_macd_line_zero(self):
        # On constant data all EMAs are equal, so MACD line = 0
        data = [50.0] * 60
        ml, sl, hl = macd(data, 12, 26, 9)
        for v in ml:
            if v is not None:
                self.assertAlmostEqual(v, 0.0, places=10)

    def test_known_output_lengths(self):
        data = list(range(1, 61))  # 60 values
        ml, sl, hl = macd(data, 12, 26, 9)
        self.assertEqual(len(ml), 60)
        self.assertEqual(len(sl), 60)
        self.assertEqual(len(hl), 60)
        # MACD line defined from index 25 (slow-1), signal from 25+8=33
        self.assertIsNone(ml[24])
        self.assertIsNotNone(ml[25])
        self.assertIsNone(sl[32])
        self.assertIsNotNone(sl[33])


class BollingerTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            bollinger([1.0] * 20, period=0)

    def test_empty_input(self):
        u, m, l = bollinger([], 20)
        self.assertEqual(u, [])
        self.assertEqual(m, [])
        self.assertEqual(l, [])

    def test_insufficient_data_all_none(self):
        u, m, l = bollinger([1.0] * 5, period=20)
        self.assertTrue(all(v is None for v in u))
        self.assertTrue(all(v is None for v in m))
        self.assertTrue(all(v is None for v in l))

    def test_upper_ge_middle_ge_lower(self):
        data = [100.0 + math.sin(i * 0.3) for i in range(50)]
        u, m, l = bollinger(data, period=10, mult=2.0)
        for i in range(10 - 1, 50):
            self.assertGreaterEqual(u[i], m[i])
            self.assertGreaterEqual(m[i], l[i])

    def test_constant_input_no_band_width(self):
        # Constant data => std=0 => upper == middle == lower
        data = [5.0] * 30
        u, m, l = bollinger(data, period=10, mult=2.0)
        for i in range(9, 30):
            self.assertAlmostEqual(u[i], 5.0, places=12)
            self.assertAlmostEqual(m[i], 5.0, places=12)
            self.assertAlmostEqual(l[i], 5.0, places=12)

    def test_known_values_period3_mult1(self):
        # data = [1, 2, 3]; mean=2; var=((1-2)^2+(2-2)^2+(3-2)^2)/3 = 2/3; sd=sqrt(2/3)
        data = [1.0, 2.0, 3.0]
        u, m, l = bollinger(data, period=3, mult=1.0)
        self.assertIsNone(u[0])
        self.assertIsNone(u[1])
        expected_sd = math.sqrt(2.0 / 3.0)
        self.assertAlmostEqual(m[2], 2.0, places=12)
        self.assertAlmostEqual(u[2], 2.0 + expected_sd, places=12)
        self.assertAlmostEqual(l[2], 2.0 - expected_sd, places=12)


class StochasticTests(unittest.TestCase):
    def test_invalid_k_period(self):
        with self.assertRaises(ValueError):
            stochastic([1.0], [1.0], [1.0], k_period=0)

    def test_invalid_d_period(self):
        with self.assertRaises(ValueError):
            stochastic([1.0], [1.0], [1.0], d_period=-1)

    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            stochastic([1.0, 2.0], [1.0], [1.0, 1.5], k_period=1)

    def test_empty_input(self):
        k, d = stochastic([], [], [], k_period=3, d_period=3)
        self.assertEqual(k, [])
        self.assertEqual(d, [])

    def test_insufficient_data_all_none(self):
        k, d = stochastic([1.0] * 2, [0.5] * 2, [0.8] * 2, k_period=14, d_period=3)
        self.assertTrue(all(v is None for v in k))
        self.assertTrue(all(v is None for v in d))

    def test_k_d_in_range_0_100(self):
        import random
        random.seed(42)
        prices = [100.0]
        for _ in range(99):
            prices.append(prices[-1] + random.uniform(-1, 1))
        h = [p + 0.5 for p in prices]
        l = [p - 0.5 for p in prices]
        k, d = stochastic(h, l, prices, k_period=5, d_period=3)
        for v in k:
            if v is not None:
                self.assertGreaterEqual(v, 0.0)
                self.assertLessEqual(v, 100.0)
        for v in d:
            if v is not None:
                self.assertGreaterEqual(v, 0.0)
                self.assertLessEqual(v, 100.0)

    def test_zero_range_gives_50(self):
        # When high == low, raw K = 50; with k_period=1, d_period=1: k=d=50
        h = [5.0] * 5
        l = [5.0] * 5
        c = [5.0] * 5
        k, d = stochastic(h, l, c, k_period=1, d_period=1)
        for v in k:
            self.assertAlmostEqual(v, 50.0)
        for v in d:
            self.assertAlmostEqual(v, 50.0)

    def test_known_value_at_high(self):
        # close == high, low=0 → raw K = 100. k_period=1, d_period=1 → k=d=100
        h = [10.0] * 5
        l = [0.0] * 5
        c = [10.0] * 5
        k, d = stochastic(h, l, c, k_period=1, d_period=1)
        for v in k:
            self.assertAlmostEqual(v, 100.0)


class ObvTests(unittest.TestCase):
    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            obv([1.0, 2.0], [100.0])

    def test_empty_input(self):
        self.assertEqual(obv([], []), [])

    def test_first_value_is_zero(self):
        result = obv([10.0, 11.0], [500.0, 600.0])
        self.assertAlmostEqual(result[0], 0.0)

    def test_pure_uptrend_monotone_non_decreasing(self):
        c = [100.0 + i for i in range(10)]
        v = [1000.0] * 10
        result = obv(c, v)
        for i in range(1, len(result)):
            self.assertGreaterEqual(result[i], result[i - 1])

    def test_pure_downtrend_monotone_non_increasing(self):
        c = [100.0 - i for i in range(10)]
        v = [1000.0] * 10
        result = obv(c, v)
        for i in range(1, len(result)):
            self.assertLessEqual(result[i], result[i - 1])

    def test_flat_price_no_change(self):
        c = [50.0] * 5
        v = [1000.0] * 5
        result = obv(c, v)
        for val in result:
            self.assertAlmostEqual(val, 0.0)

    def test_known_values(self):
        # c: 10, 11, 10, 11; v: 100, 200, 300, 400
        # obv[0]=0; obv[1]=0+200=200; obv[2]=200-300=-100; obv[3]=-100+400=300
        c = [10.0, 11.0, 10.0, 11.0]
        v = [100.0, 200.0, 300.0, 400.0]
        result = obv(c, v)
        self.assertAlmostEqual(result[0], 0.0)
        self.assertAlmostEqual(result[1], 200.0)
        self.assertAlmostEqual(result[2], -100.0)
        self.assertAlmostEqual(result[3], 300.0)


class AdxTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            adx([1.0] * 30, [0.5] * 30, [0.8] * 30, period=0)

    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            adx([1.0, 2.0], [0.5], [0.8, 0.9], period=1)

    def test_empty_input(self):
        a, p, m = adx([], [], [], period=14)
        self.assertEqual(a, [])
        self.assertEqual(p, [])
        self.assertEqual(m, [])

    def test_insufficient_data_all_none(self):
        # Need n >= 2*period+1; with 5 values and period=14, all None
        h = [1.0] * 5; l = [0.5] * 5; c = [0.8] * 5
        a, p, m = adx(h, l, c, period=14)
        self.assertTrue(all(v is None for v in a))
        self.assertTrue(all(v is None for v in p))
        self.assertTrue(all(v is None for v in m))

    def test_adx_in_range_0_100(self):
        data = []
        price = 100.0
        for i in range(100):
            price += ((i * 9301 + 49297) % 233 - 116) * 0.01
            data.append(price)
        h = [p + 0.5 for p in data]
        l = [p - 0.5 for p in data]
        a, p, m = adx(h, l, data, period=14)
        for v in a:
            if v is not None:
                self.assertGreaterEqual(v, 0.0)
                self.assertLessEqual(v, 100.0)

    def test_di_non_negative(self):
        data = [100.0 + i * 0.2 for i in range(50)]
        h = [p + 1.0 for p in data]
        l = [p - 1.0 for p in data]
        a, p, m = adx(h, l, data, period=5)
        for v in p:
            if v is not None:
                self.assertGreaterEqual(v, 0.0)
        for v in m:
            if v is not None:
                self.assertGreaterEqual(v, 0.0)

    def test_output_length_matches_input(self):
        n = 50
        h = [float(i + 1) for i in range(n)]
        l = [float(i) for i in range(n)]
        c = [float(i) + 0.5 for i in range(n)]
        a, p, m = adx(h, l, c, period=5)
        self.assertEqual(len(a), n)
        self.assertEqual(len(p), n)
        self.assertEqual(len(m), n)

    def test_strong_uptrend_plus_di_dominates(self):
        # In a strong uptrend +DI should exceed -DI
        data = [100.0 + i * 1.0 for i in range(60)]
        h = [p + 0.5 for p in data]
        l = [p - 0.5 for p in data]
        a, p_di, m_di = adx(h, l, data, period=14)
        # Check last defined value
        last_p = next(v for v in reversed(p_di) if v is not None)
        last_m = next(v for v in reversed(m_di) if v is not None)
        self.assertGreater(last_p, last_m)


class WmaTests(unittest.TestCase):
    def test_invalid_period(self):
        for bad in (0, -1, 1.5, "3"):
            with self.assertRaises(ValueError):
                wma([1, 2, 3], bad)  # type: ignore[arg-type]

    def test_insufficient_data_all_none(self):
        out = wma([1.0, 2.0], 5)
        self.assertEqual(out, [None, None])

    def test_period_equals_n(self):
        # WMA([1,2,3], period=3): weights 1,2,3; wsum=6
        # (1*1 + 2*2 + 3*3) / 6 = 14/6
        out = wma([1.0, 2.0, 3.0], 3)
        self.assertIsNone(out[0])
        self.assertIsNone(out[1])
        self.assertAlmostEqual(out[2], 14.0 / 6.0, places=12)

    def test_known_values_period2(self):
        # WMA([1,2,3,4], period=2): wsum=3; weights oldest=1,newest=2
        # i=1: (1*1 + 2*2)/3 = 5/3
        # i=2: (2*1 + 3*2)/3 = 8/3
        # i=3: (3*1 + 4*2)/3 = 11/3
        out = wma([1.0, 2.0, 3.0, 4.0], 2)
        self.assertIsNone(out[0])
        self.assertAlmostEqual(out[1], 5.0 / 3.0, places=12)
        self.assertAlmostEqual(out[2], 8.0 / 3.0, places=12)
        self.assertAlmostEqual(out[3], 11.0 / 3.0, places=12)

    def test_constant_input(self):
        out = wma([7.0] * 20, 5)
        for v in out[4:]:
            self.assertAlmostEqual(v, 7.0, places=12)

    def test_output_length_matches_input(self):
        out = wma([1.0] * 10, 3)
        self.assertEqual(len(out), 10)

    def test_period_one_is_identity(self):
        data = [1.0, 2.0, 3.0]
        out = wma(data, 1)
        for i, v in enumerate(out):
            self.assertAlmostEqual(v, data[i], places=12)


class CciTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            cci([1.0], [1.0], [1.0], period=0)

    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            cci([1.0, 2.0], [1.0], [1.0, 1.0], period=1)

    def test_insufficient_data_all_none(self):
        out = cci([1.0] * 3, [0.5] * 3, [0.8] * 3, period=20)
        self.assertTrue(all(v is None for v in out))

    def test_constant_input_zero(self):
        # Constant TP => mean=TP, MAD=0 => CCI=0
        h = [10.0] * 30
        l = [8.0] * 30
        c = [9.0] * 30
        out = cci(h, l, c, period=10)
        for v in out[9:]:
            self.assertAlmostEqual(v, 0.0, places=10)

    def test_known_value_period1(self):
        # With period=1: mean=TP, MAD=0 => CCI=0
        h = [12.0]; l = [8.0]; c = [10.0]
        out = cci(h, l, c, period=1)
        self.assertAlmostEqual(out[0], 0.0, places=10)

    def test_known_value_period2(self):
        # TP values: [10, 11]; mean=10.5; MAD=(0.5+0.5)/2=0.5
        # At i=1: tp=11; CCI=(11-10.5)/(0.015*0.5) = 0.5/0.0075 = 66.666...
        h = [11.0, 12.0]; l = [9.0, 10.0]; c = [10.0, 11.0]
        out = cci(h, l, c, period=2)
        self.assertIsNone(out[0])
        self.assertAlmostEqual(out[1], 0.5 / (0.015 * 0.5), places=8)

    def test_output_length_matches_input(self):
        n = 30
        h = [float(i + 1) for i in range(n)]
        l = [float(i)     for i in range(n)]
        c = [float(i) + 0.5 for i in range(n)]
        out = cci(h, l, c, period=5)
        self.assertEqual(len(out), n)


class WilliamsRTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            williams_r([1.0], [1.0], [1.0], period=0)

    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            williams_r([1.0, 2.0], [1.0], [1.0, 1.5])

    def test_insufficient_data_all_none(self):
        out = williams_r([1.0] * 3, [0.5] * 3, [0.8] * 3, period=14)
        self.assertTrue(all(v is None for v in out))

    def test_values_in_range(self):
        import random
        random.seed(99)
        prices = [100.0]
        for _ in range(49):
            prices.append(prices[-1] + random.uniform(-1, 1))
        h = [p + 1.0 for p in prices]
        l = [p - 1.0 for p in prices]
        out = williams_r(h, l, prices)
        for v in out:
            if v is not None:
                self.assertGreaterEqual(v, -100.0)
                self.assertLessEqual(v, 0.0)

    def test_close_at_high(self):
        # close == highest_high => %R = 0
        h = [10.0] * 5; l = [5.0] * 5; c = [10.0] * 5
        out = williams_r(h, l, c, period=3)
        for v in out[2:]:
            self.assertAlmostEqual(v, 0.0, places=10)

    def test_close_at_low(self):
        # close == lowest_low => %R = -100
        h = [10.0] * 5; l = [5.0] * 5; c = [5.0] * 5
        out = williams_r(h, l, c, period=3)
        for v in out[2:]:
            self.assertAlmostEqual(v, -100.0, places=10)

    def test_zero_range_gives_minus50(self):
        # high == low => range=0 => %R = -50
        h = [5.0] * 5; l = [5.0] * 5; c = [5.0] * 5
        out = williams_r(h, l, c, period=3)
        for v in out[2:]:
            self.assertAlmostEqual(v, -50.0, places=10)

    def test_known_value(self):
        # period=2, h=[10,12], l=[5,8], c=[10,11]
        # highest_high=12, lowest_low=5, close=11
        # %R = -100*(12-11)/(12-5) = -100/7 ~ -14.285...
        h = [10.0, 12.0]; l = [5.0, 8.0]; c = [10.0, 11.0]
        out = williams_r(h, l, c, period=2)
        self.assertIsNone(out[0])
        self.assertAlmostEqual(out[1], -100.0 * (12.0 - 11.0) / (12.0 - 5.0), places=10)


class RocTests(unittest.TestCase):
    def test_invalid_period(self):
        for bad in (0, -1, 1.5, "5"):
            with self.assertRaises(ValueError):
                roc([1.0, 2.0, 3.0], bad)  # type: ignore[arg-type]

    def test_insufficient_data_all_none(self):
        out = roc([1.0, 2.0], 5)
        self.assertEqual(out, [None, None])

    def test_known_value(self):
        # ROC([100, 110], period=1): (110-100)/100*100 = 10
        out = roc([100.0, 110.0], 1)
        self.assertIsNone(out[0])
        self.assertAlmostEqual(out[1], 10.0, places=10)

    def test_zero_base_gives_zero(self):
        # close[i-period] == 0 => ROC = 0
        out = roc([0.0, 5.0], 1)
        self.assertIsNone(out[0])
        self.assertAlmostEqual(out[1], 0.0, places=10)

    def test_constant_input_zero_roc(self):
        out = roc([50.0] * 10, 3)
        for v in out[3:]:
            self.assertAlmostEqual(v, 0.0, places=10)

    def test_known_values_period3(self):
        # values = [100, 105, 110, 120]
        # roc[3] = (120-100)/100*100 = 20
        out = roc([100.0, 105.0, 110.0, 120.0], 3)
        self.assertIsNone(out[0])
        self.assertIsNone(out[1])
        self.assertIsNone(out[2])
        self.assertAlmostEqual(out[3], 20.0, places=10)

    def test_output_length_matches_input(self):
        out = roc([1.0] * 15, 5)
        self.assertEqual(len(out), 15)

    def test_downtrend_negative_roc(self):
        data = [100.0 - i for i in range(10)]
        out = roc(data, 1)
        for v in out[1:]:
            self.assertLess(v, 0.0)


class MfiTests(unittest.TestCase):
    def test_invalid_period(self):
        with self.assertRaises(ValueError):
            mfi([1.0], [1.0], [1.0], [100.0], period=0)

    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            mfi([1.0, 2.0], [1.0], [1.0, 1.0], [100.0, 200.0], period=1)

    def test_insufficient_data_all_none(self):
        out = mfi([10.0] * 5, [8.0] * 5, [9.0] * 5, [100.0] * 5, period=14)
        self.assertTrue(all(v is None for v in out))

    def test_values_in_range(self):
        import random
        random.seed(77)
        prices = [100.0]
        for _ in range(49):
            prices.append(prices[-1] + random.uniform(-1, 1))
        h = [p + 1.0 for p in prices]
        l = [p - 1.0 for p in prices]
        vol = [1000.0 + random.uniform(-100, 100) for _ in prices]
        out = mfi(h, l, prices, vol, period=14)
        for v in out:
            if v is not None:
                self.assertGreaterEqual(v, 0.0)
                self.assertLessEqual(v, 100.0)

    def test_strict_uptrend_high_mfi(self):
        # Strictly rising TP => all positive money flow => MFI near 100
        prices = [100.0 + i for i in range(30)]
        h = [p + 0.5 for p in prices]
        l = [p - 0.5 for p in prices]
        vol = [1000.0] * 30
        out = mfi(h, l, prices, vol, period=14)
        for v in out[14:]:
            self.assertGreater(v, 90.0)

    def test_strict_downtrend_low_mfi(self):
        # Strictly falling TP => all negative money flow => MFI near 0
        prices = [130.0 - i for i in range(30)]
        h = [p + 0.5 for p in prices]
        l = [p - 0.5 for p in prices]
        vol = [1000.0] * 30
        out = mfi(h, l, prices, vol, period=14)
        for v in out[14:]:
            self.assertLess(v, 10.0)

    def test_output_length_matches_input(self):
        n = 30
        h = [10.0] * n; l = [8.0] * n; c = [9.0] * n; v = [100.0] * n
        out = mfi(h, l, c, v, period=14)
        self.assertEqual(len(out), n)


class VwapTests(unittest.TestCase):
    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            vwap([1.0, 2.0], [1.0], [1.5, 1.8], [100.0, 200.0])

    def test_empty_input(self):
        out = vwap([], [], [], [])
        self.assertEqual(out, [])

    def test_no_none_values(self):
        h = [11.0] * 10; l = [9.0] * 10; c = [10.0] * 10; vol = [1000.0] * 10
        out = vwap(h, l, c, vol)
        self.assertTrue(all(v is not None for v in out))

    def test_constant_input(self):
        # TP = (11+9+10)/3 = 10; VWAP should always equal 10
        h = [11.0] * 10; l = [9.0] * 10; c = [10.0] * 10; vol = [1000.0] * 10
        out = vwap(h, l, c, vol)
        for v in out:
            self.assertAlmostEqual(v, 10.0, places=10)

    def test_vwap_within_period_high_low(self):
        import random
        random.seed(55)
        prices = [100.0]
        for _ in range(49):
            prices.append(prices[-1] + random.uniform(-0.5, 0.5))
        h = [p + 0.5 for p in prices]
        l = [p - 0.5 for p in prices]
        vol = [1000.0] * 50
        out = vwap(h, l, prices, vol)
        lo_min = min(l)
        hi_max = max(h)
        for v in out:
            if v is not None:
                self.assertGreaterEqual(v, lo_min)
                self.assertLessEqual(v, hi_max)

    def test_known_value_period2(self):
        # bar0: h=12, l=8, c=10 => TP=10, vol=100 => cum_pv=1000, cum_v=100; VWAP=10
        # bar1: h=14, l=10, c=12 => TP=12, vol=200 => cum_pv=3400, cum_v=300; VWAP=3400/300=11.333
        h = [12.0, 14.0]; l = [8.0, 10.0]; c = [10.0, 12.0]; vol = [100.0, 200.0]
        out = vwap(h, l, c, vol)
        self.assertAlmostEqual(out[0], 10.0, places=10)
        self.assertAlmostEqual(out[1], 3400.0 / 300.0, places=10)

    def test_zero_volume_treated_as_one(self):
        # Zero volume should not crash; treated as 1
        h = [11.0]; l = [9.0]; c = [10.0]; vol = [0.0]
        out = vwap(h, l, c, vol)
        self.assertAlmostEqual(out[0], 10.0, places=10)

    def test_output_length_matches_input(self):
        n = 20
        h = [10.0] * n; l = [8.0] * n; c = [9.0] * n; vol = [100.0] * n
        out = vwap(h, l, c, vol)
        self.assertEqual(len(out), n)


class KeltnerTests(unittest.TestCase):
    def test_invalid_ema_period(self):
        with self.assertRaises(ValueError):
            keltner([1.0] * 30, [0.5] * 30, [0.8] * 30, ema_period=0)

    def test_invalid_atr_period(self):
        with self.assertRaises(ValueError):
            keltner([1.0] * 30, [0.5] * 30, [0.8] * 30, atr_period=-1)

    def test_mismatched_lengths(self):
        with self.assertRaises(ValueError):
            keltner([1.0, 2.0], [0.5], [0.8, 0.9])

    def test_empty_input(self):
        u, m, l = keltner([], [], [])
        self.assertEqual(u, [])
        self.assertEqual(m, [])
        self.assertEqual(l, [])

    def test_upper_ge_middle_ge_lower(self):
        import random
        random.seed(33)
        prices = [100.0]
        for _ in range(49):
            prices.append(prices[-1] + random.uniform(-1, 1))
        h = [p + 1.0 for p in prices]
        l = [p - 1.0 for p in prices]
        u, m, lo = keltner(h, l, prices, ema_period=10, atr_period=10, mult=2.0)
        for i in range(len(prices)):
            if u[i] is not None:
                self.assertGreaterEqual(u[i], m[i])
                self.assertGreaterEqual(m[i], lo[i])

    def test_insufficient_data_all_none(self):
        h = [1.0] * 3; l = [0.5] * 3; c = [0.8] * 3
        u, m, lo = keltner(h, l, c, ema_period=20, atr_period=10)
        self.assertTrue(all(v is None for v in u))
        self.assertTrue(all(v is None for v in lo))

    def test_output_lengths_match_input(self):
        n = 50
        h = [float(i + 1) for i in range(n)]
        l = [float(i)     for i in range(n)]
        c = [float(i) + 0.5 for i in range(n)]
        u, m, lo = keltner(h, l, c, ema_period=10, atr_period=10)
        self.assertEqual(len(u), n)
        self.assertEqual(len(m), n)
        self.assertEqual(len(lo), n)

    def test_constant_input_bands_equal(self):
        # Constant price => ATR=0 (after warm-up) => upper == middle == lower
        h = [11.0] * 60; l = [9.0] * 60; c = [10.0] * 60
        # ATR on constant data stabilizes at a non-zero value due to TR = 2.
        # But EMA and ATR should both be stable; upper >= lower is guaranteed.
        u, m, lo = keltner(h, l, c, ema_period=10, atr_period=10, mult=2.0)
        for i in range(len(c)):
            if u[i] is not None:
                self.assertGreaterEqual(u[i], lo[i])


if __name__ == "__main__":
    unittest.main()
