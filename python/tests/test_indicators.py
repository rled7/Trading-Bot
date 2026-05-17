import math
import unittest

from algoforge.indicators import sma, ema, rsi, atr, macd, bollinger, stochastic, obv, adx


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


if __name__ == "__main__":
    unittest.main()
