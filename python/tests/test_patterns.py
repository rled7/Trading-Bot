import unittest

from algoforge import Bar
from algoforge.patterns import (
    is_doji, is_hammer, is_engulfing, is_marubozu, is_pin_bar,
    scan_patterns,
)


def b(o, h, l, c):
    return Bar(timestamp=0, open=o, high=h, low=l, close=c, volume=1)


class DojiTests(unittest.TestCase):
    def test_match_tiny_body(self):
        self.assertEqual(is_doji(b(1.0, 1.5, 0.5, 1.005)), 1)

    def test_no_match_big_body(self):
        self.assertEqual(is_doji(b(1.0, 1.5, 0.5, 1.5)), 0)

    def test_zero_range(self):
        self.assertEqual(is_doji(b(1.0, 1.0, 1.0, 1.0)), 0)

    def test_threshold(self):
        self.assertEqual(is_doji(b(1.0, 1.5, 0.5, 1.09)), 1)  # 9% body
        self.assertEqual(is_doji(b(1.0, 1.5, 0.5, 1.11)), 0)  # 11% body


class HammerTests(unittest.TestCase):
    def test_bullish_hammer(self):
        self.assertEqual(is_hammer(b(1.0, 1.06, 0.40, 1.05)), 1)

    def test_shooting_star(self):
        self.assertEqual(is_hammer(b(1.05, 1.70, 0.99, 1.0)), -1)

    def test_zero_range(self):
        self.assertEqual(is_hammer(b(1.0, 1.0, 1.0, 1.0)), 0)

    def test_balanced_candle(self):
        self.assertEqual(is_hammer(b(1.0, 1.10, 0.90, 1.05)), 0)


class EngulfingTests(unittest.TestCase):
    def test_bullish(self):
        self.assertEqual(is_engulfing(b(100, 100.5, 94.5, 95),
                                       b(94, 101, 94, 101)), 1)

    def test_bearish(self):
        self.assertEqual(is_engulfing(b(95, 100, 95, 100),
                                       b(101, 101, 94, 94)), -1)

    def test_same_direction_no_match(self):
        self.assertEqual(is_engulfing(b(95, 100, 95, 100),
                                       b(96, 101, 96, 101)), 0)

    def test_inside_bar_no_match(self):
        self.assertEqual(is_engulfing(b(95, 100, 95, 100),
                                       b(99, 99.5, 96, 96)), 0)


class MarubozuTests(unittest.TestCase):
    def test_bullish(self):
        self.assertEqual(is_marubozu(b(1.00, 1.02, 1.00, 1.95)), 1)

    def test_bearish(self):
        self.assertEqual(is_marubozu(b(1.95, 1.95, 0.97, 1.00)), -1)

    def test_long_shadow_no_match(self):
        self.assertEqual(is_marubozu(b(1.00, 1.95, 0.50, 1.95)), 0)

    def test_zero_range(self):
        self.assertEqual(is_marubozu(b(1.0, 1.0, 1.0, 1.0)), 0)


class PinBarTests(unittest.TestCase):
    def test_bullish(self):
        self.assertEqual(is_pin_bar(b(1.0, 1.06, 0.40, 1.05)), 1)

    def test_bearish(self):
        self.assertEqual(is_pin_bar(b(1.05, 1.70, 0.99, 1.0)), -1)

    def test_tall_body_no_match(self):
        self.assertEqual(is_pin_bar(b(1.0, 1.95, 1.0, 1.95)), 0)

    def test_zero_range(self):
        self.assertEqual(is_pin_bar(b(1.0, 1.0, 1.0, 1.0)), 0)


class ScanTests(unittest.TestCase):
    def test_collects_matches(self):
        bars = [
            b(100, 100.5, 94.5, 95),     # bearish big body
            b(94, 101, 94, 101),         # bullish engulfing + marubozu shape
            b(101, 102, 100, 101.05),    # doji
        ]
        matches = scan_patterns(bars)
        names = {(m.bar_index, m.name) for m in matches}
        self.assertIn((1, "engulfing"), names)
        self.assertIn((2, "doji"), names)

    def test_empty_input(self):
        self.assertEqual(scan_patterns([]), [])


if __name__ == "__main__":
    unittest.main()
