import math
import unittest

from algoforge.indicators import sma, ema, rsi, atr


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


if __name__ == "__main__":
    unittest.main()
