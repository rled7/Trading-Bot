import math
import unittest

from algoforge import Bar
from algoforge.patterns import (
    is_doji, is_hammer, is_engulfing, is_marubozu, is_pin_bar,
    is_morning_star, is_evening_star, is_three_white_soldiers, is_three_black_crows,
    is_bullish_flag, is_bearish_flag, is_head_and_shoulders, is_inverse_head_and_shoulders,
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


class MorningStarTests(unittest.TestCase):
    def test_positive_match(self):
        # b0: bearish big body (open=110, close=100, high=111, low=99): body=10, range=12, body>range*0.60=7.2 ✓
        # b1: small body (open=99, close=99.5, high=100, low=98): body=0.5, b0.body*0.30=3 → 0.5<3 ✓
        # b2: bullish closing above midpoint of b0 body: midpoint=(110+100)/2=105; close=108 > 105 ✓
        b0 = b(110, 111, 99, 100)   # bearish: close(100)<open(110), body=10, range=12
        b1 = b(99, 100, 98, 99.5)   # small body=0.5
        b2 = b(100, 112, 99, 108)   # bullish, close=108 > 105
        self.assertEqual(is_morning_star(b0, b1, b2), 1)

    def test_no_match_b0_not_bearish(self):
        # b0 bullish → no match
        b0 = b(100, 111, 99, 110)   # bullish
        b1 = b(99, 100, 98, 99.5)
        b2 = b(100, 112, 99, 108)
        self.assertEqual(is_morning_star(b0, b1, b2), 0)

    def test_no_match_b0_small_body(self):
        # b0 bearish but body not large enough (body <= range*0.60)
        b0 = b(105, 111, 99, 100)   # body=5, range=12, 5 <= 12*0.60=7.2 → fails
        b1 = b(99, 100, 98, 99.5)
        b2 = b(100, 112, 99, 108)
        self.assertEqual(is_morning_star(b0, b1, b2), 0)

    def test_no_match_b1_large_body(self):
        # b1 body too large relative to b0.body
        b0 = b(110, 111, 99, 100)
        b1 = b(99, 105, 98, 104)    # body=5 > 10*0.30=3
        b2 = b(100, 112, 99, 108)
        self.assertEqual(is_morning_star(b0, b1, b2), 0)

    def test_no_match_b2_close_below_midpoint(self):
        # b2 close below midpoint of b0 body
        b0 = b(110, 111, 99, 100)
        b1 = b(99, 100, 98, 99.5)
        b2 = b(100, 106, 99, 104)   # midpoint=105, close=104 < 105 → no match
        self.assertEqual(is_morning_star(b0, b1, b2), 0)

    def test_zero_range_b0_no_match(self):
        b0 = b(105, 105, 105, 105)
        b1 = b(99, 100, 98, 99.5)
        b2 = b(100, 112, 99, 108)
        self.assertEqual(is_morning_star(b0, b1, b2), 0)


class EveningStarTests(unittest.TestCase):
    def test_positive_match(self):
        # b0: bullish big body (open=100, close=110, high=111, low=99): body=10, range=12 ✓
        # b1: small body (body=0.5 < 10*0.30=3) ✓
        # b2: bearish closing below midpoint (midpoint=105, close=102) ✓
        b0 = b(100, 111, 99, 110)   # bullish
        b1 = b(110, 112, 110, 110.5)  # small body=0.5
        b2 = b(110, 111, 99, 102)   # bearish, close=102 < 105
        self.assertEqual(is_evening_star(b0, b1, b2), -1)

    def test_no_match_b0_not_bullish(self):
        b0 = b(110, 111, 99, 100)   # bearish
        b1 = b(110, 112, 110, 110.5)
        b2 = b(110, 111, 99, 102)
        self.assertEqual(is_evening_star(b0, b1, b2), 0)

    def test_no_match_b2_not_bearish(self):
        b0 = b(100, 111, 99, 110)
        b1 = b(110, 112, 110, 110.5)
        b2 = b(99, 111, 99, 108)    # bullish, close=108 > 105 → wrong direction
        self.assertEqual(is_evening_star(b0, b1, b2), 0)

    def test_no_match_b2_close_above_midpoint(self):
        b0 = b(100, 111, 99, 110)
        b1 = b(110, 112, 110, 110.5)
        b2 = b(110, 111, 99, 106)   # bearish, but close=106 > midpoint=105
        self.assertEqual(is_evening_star(b0, b1, b2), 0)

    def test_zero_range_b0_no_match(self):
        b0 = b(105, 105, 105, 105)
        b1 = b(110, 112, 110, 110.5)
        b2 = b(110, 111, 99, 102)
        self.assertEqual(is_evening_star(b0, b1, b2), 0)


class ThreeWhiteSoldiersTests(unittest.TestCase):
    def _soldier(self, open_, close_, slack=0.05):
        """Bullish bar with body_pct ~ 0.80 and negligible upper shadow."""
        # high = close + slack (tiny upper shadow), low = open - slack (tiny lower shadow)
        return b(open_, close_ + slack, open_ - slack, close_)

    def test_positive_match(self):
        b0 = self._soldier(100.0, 102.0)
        b1 = self._soldier(101.5, 104.0)
        b2 = self._soldier(103.5, 106.0)
        self.assertEqual(is_three_white_soldiers(b0, b1, b2), 1)

    def test_no_match_one_bearish(self):
        b0 = self._soldier(100.0, 102.0)
        b1 = b(104.0, 104.5, 100.5, 101.0)  # bearish
        b2 = self._soldier(103.5, 106.0)
        self.assertEqual(is_three_white_soldiers(b0, b1, b2), 0)

    def test_no_match_body_pct_too_small(self):
        # Body is only 20% of range — won't pass 0.60 threshold
        b0 = b(100.0, 110.0, 90.0, 102.0)  # body=2, range=20, body_pct=0.10
        b1 = self._soldier(101.5, 104.0)
        b2 = self._soldier(103.5, 106.0)
        self.assertEqual(is_three_white_soldiers(b0, b1, b2), 0)

    def test_no_match_closes_not_higher(self):
        b0 = self._soldier(100.0, 104.0)
        b1 = self._soldier(103.5, 104.0)  # close not higher than b0
        b2 = self._soldier(103.5, 106.0)
        self.assertEqual(is_three_white_soldiers(b0, b1, b2), 0)

    def test_zero_range_no_match(self):
        b0 = b(100, 100, 100, 100)
        b1 = self._soldier(101.5, 104.0)
        b2 = self._soldier(103.5, 106.0)
        self.assertEqual(is_three_white_soldiers(b0, b1, b2), 0)

    def test_no_match_large_upper_shadow(self):
        # upper_shadow (2.0) >= body (2.0)*0.30=0.6 → fails
        b0 = b(100.0, 104.0, 99.5, 102.0)  # body=2, range=4.5, upper=2
        b1 = self._soldier(101.5, 104.0)
        b2 = self._soldier(103.5, 106.0)
        self.assertEqual(is_three_white_soldiers(b0, b1, b2), 0)


class ThreeBlackCrowsTests(unittest.TestCase):
    def _crow(self, open_, close_, slack=0.05):
        """Bearish bar with body_pct ~ 0.80 and negligible lower shadow."""
        return b(open_, open_ + slack, close_ - slack, close_)

    def test_positive_match(self):
        b0 = self._crow(106.0, 104.0)
        b1 = self._crow(104.5, 102.0)
        b2 = self._crow(102.5, 100.0)
        self.assertEqual(is_three_black_crows(b0, b1, b2), -1)

    def test_no_match_one_bullish(self):
        b0 = self._crow(106.0, 104.0)
        b1 = b(102.0, 105.0, 101.5, 104.5)  # bullish
        b2 = self._crow(102.5, 100.0)
        self.assertEqual(is_three_black_crows(b0, b1, b2), 0)

    def test_no_match_closes_not_lower(self):
        b0 = self._crow(106.0, 100.0)
        b1 = self._crow(104.5, 100.0)  # close not lower than b0
        b2 = self._crow(102.5, 99.0)
        self.assertEqual(is_three_black_crows(b0, b1, b2), 0)

    def test_zero_range_no_match(self):
        b0 = b(100, 100, 100, 100)
        b1 = self._crow(104.5, 102.0)
        b2 = self._crow(102.5, 100.0)
        self.assertEqual(is_three_black_crows(b0, b1, b2), 0)

    def test_no_match_large_lower_shadow(self):
        # lower_shadow (2.0) >= body (2.0)*0.30 → fails
        b0 = b(106.0, 106.5, 102.0, 104.0)  # body=2, range=4.5, lower=2
        b1 = self._crow(104.5, 102.0)
        b2 = self._crow(102.5, 100.0)
        self.assertEqual(is_three_black_crows(b0, b1, b2), 0)


class BullishFlagTests(unittest.TestCase):
    def _make_bullish_flag_bars(self):
        """Create 20 bars that form a clear bullish flag.

        Pole (bars 0–9): strong upward move (close[9] >> close[0]).
        Flag (bars 10–19): tight sideways consolidation.
        """
        bars = []
        # Pole: rise from 100 to 120 (10 bars)
        for i in range(10):
            price = 100.0 + i * 2.0
            bars.append(Bar(timestamp=i, open=price - 0.5, high=price + 0.5,
                            low=price - 0.6, close=price, volume=1))
        # Flag: tight consolidation around 120 (10 bars, range ~0.4)
        for i in range(10):
            price = 120.0 + (i % 2) * 0.2
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 0.2,
                            low=price - 0.2, close=price, volume=1))
        return bars

    def test_positive_match(self):
        bars = self._make_bullish_flag_bars()
        self.assertEqual(is_bullish_flag(bars), 1)

    def test_below_min_bars_returns_zero(self):
        bars = self._make_bullish_flag_bars()
        self.assertEqual(is_bullish_flag(bars[:19]), 0)
        self.assertEqual(is_bullish_flag([]), 0)

    def test_no_match_bearish_pole(self):
        """Pole goes down, so pole_bull = False → no match."""
        bars = []
        # Falling pole
        for i in range(10):
            price = 120.0 - i * 2.0
            bars.append(Bar(timestamp=i, open=price + 0.5, high=price + 0.6,
                            low=price - 0.5, close=price, volume=1))
        # Tight flag
        for i in range(10):
            price = 100.0 + (i % 2) * 0.2
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 0.2,
                            low=price - 0.2, close=price, volume=1))
        self.assertEqual(is_bullish_flag(bars), 0)

    def test_no_match_wide_flag(self):
        """Flag range >= pole_rng * 0.45 → no match."""
        bars = []
        # Pole: 10 bars, rise 100->120, pole_rng ~20
        for i in range(10):
            price = 100.0 + i * 2.0
            bars.append(Bar(timestamp=i, open=price - 0.5, high=price + 0.5,
                            low=price - 0.6, close=price, volume=1))
        # Wide flag: range = 15 which is > 20*0.45=9
        for i in range(10):
            price = 115.0 + (i % 2) * 15.0
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 1.0,
                            low=price - 1.0, close=price, volume=1))
        self.assertEqual(is_bullish_flag(bars), 0)


class BearishFlagTests(unittest.TestCase):
    def _make_bearish_flag_bars(self):
        """Create 20 bars that form a clear bearish flag.

        Pole (bars 0–9): strong downward move (close[9] << close[0]).
        Flag (bars 10–19): tight sideways consolidation.
        """
        bars = []
        # Pole: fall from 120 to 100 (10 bars)
        for i in range(10):
            price = 120.0 - i * 2.0
            bars.append(Bar(timestamp=i, open=price + 0.5, high=price + 0.6,
                            low=price - 0.5, close=price, volume=1))
        # Flag: tight consolidation around 100 (10 bars)
        for i in range(10):
            price = 100.0 + (i % 2) * 0.2
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 0.2,
                            low=price - 0.2, close=price, volume=1))
        return bars

    def test_positive_match(self):
        bars = self._make_bearish_flag_bars()
        self.assertEqual(is_bearish_flag(bars), -1)

    def test_below_min_bars_returns_zero(self):
        bars = self._make_bearish_flag_bars()
        self.assertEqual(is_bearish_flag(bars[:19]), 0)
        self.assertEqual(is_bearish_flag([]), 0)

    def test_no_match_bullish_pole(self):
        """Pole goes up, so pole_bear = False → no match."""
        bars = []
        for i in range(10):
            price = 100.0 + i * 2.0
            bars.append(Bar(timestamp=i, open=price - 0.5, high=price + 0.5,
                            low=price - 0.6, close=price, volume=1))
        for i in range(10):
            price = 120.0 + (i % 2) * 0.2
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 0.2,
                            low=price - 0.2, close=price, volume=1))
        self.assertEqual(is_bearish_flag(bars), 0)

    def test_no_match_wide_flag(self):
        """Flag range >= pole_rng * 0.45 → no match."""
        bars = []
        # Pole: fall 120->100, pole_rng ~20
        for i in range(10):
            price = 120.0 - i * 2.0
            bars.append(Bar(timestamp=i, open=price + 0.5, high=price + 0.6,
                            low=price - 0.5, close=price, volume=1))
        # Wide flag: range = 15 > 20*0.45=9
        for i in range(10):
            price = 100.0 + (i % 2) * 15.0
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 1.0,
                            low=price - 1.0, close=price, volume=1))
        self.assertEqual(is_bearish_flag(bars), 0)


class HeadAndShouldersTests(unittest.TestCase):
    def _make_has_bars(self):
        """Create 40 bars with a clear H&S pattern.

        Layout:
          - Left shoulder  [0–12]:  highs peak at ~105
          - Head           [13–26]: highs peak at ~115 (>105*1.01)
          - Right shoulder [27–39]: highs peak at ~104 (close to left shoulder)
          - Neckline: lows around 98 (below shoulders)
          - Last close: 96 (below neckline)
        """
        bars = []
        # Left shoulder: peak ~105
        for i in range(13):
            frac = i / 12.0
            peak_h = 100.0 + 5.0 * math.sin(frac * math.pi)
            bars.append(Bar(timestamp=i, open=peak_h - 1.0, high=peak_h,
                            low=98.0, close=peak_h - 0.5, volume=1))
        # Head: peak ~115
        for i in range(14):
            frac = i / 13.0
            peak_h = 100.0 + 15.0 * math.sin(frac * math.pi)
            bars.append(Bar(timestamp=13 + i, open=peak_h - 1.0, high=peak_h,
                            low=98.0, close=peak_h - 0.5, volume=1))
        # Right shoulder: peak ~104.5, last close 96 (below neckline 98)
        for i in range(13):
            frac = i / 12.0
            peak_h = 100.0 + 4.5 * math.sin(frac * math.pi)
            close_val = 96.0 if i == 12 else peak_h - 0.5
            bars.append(Bar(timestamp=27 + i, open=peak_h - 1.0, high=peak_h,
                            low=98.0, close=close_val, volume=1))
        return bars

    def test_positive_match(self):
        bars = self._make_has_bars()
        self.assertEqual(len(bars), 40)
        self.assertEqual(is_head_and_shoulders(bars), -1)

    def test_below_min_bars_returns_zero(self):
        bars = self._make_has_bars()
        self.assertEqual(is_head_and_shoulders(bars[:39]), 0)
        self.assertEqual(is_head_and_shoulders([]), 0)

    def test_no_match_head_not_higher(self):
        """Head equal to shoulders → h <= ls*1.01 → no match."""
        bars = []
        # All three sections have same max high ~105
        for section in range(3):
            start_idx = [0, 13, 27][section]
            count = [13, 14, 13][section]
            for i in range(count):
                frac = i / (count - 1) if count > 1 else 0.5
                peak_h = 100.0 + 5.0 * math.sin(frac * math.pi)
                close_val = 96.0 if (section == 2 and i == count - 1) else peak_h - 0.5
                bars.append(Bar(timestamp=start_idx + i, open=peak_h - 1.0, high=peak_h,
                                low=98.0, close=close_val, volume=1))
        self.assertEqual(is_head_and_shoulders(bars), 0)

    def test_no_match_close_above_neckline(self):
        """Last close above neckline → no match."""
        bars = self._make_has_bars()
        # Replace last bar with close above neckline (98)
        last = bars[-1]
        bars[-1] = Bar(timestamp=last.timestamp, open=last.open, high=last.high,
                       low=last.low, close=99.0, volume=1)
        self.assertEqual(is_head_and_shoulders(bars), 0)


class InverseHeadAndShouldersTests(unittest.TestCase):
    def _make_ihas_bars(self):
        """Create 40 bars with a clear Inverse H&S pattern.

        Layout:
          - Left shoulder  [0–12]:  lows trough at ~95
          - Head           [13–26]: lows trough at ~85 (< 95*0.99)
          - Right shoulder [27–39]: lows trough at ~94.5
          - Neckline: highs around 102
          - Last close: 104 (above neckline)
        """
        bars = []
        # Left shoulder: trough ~95
        for i in range(13):
            frac = i / 12.0
            trough_l = 100.0 - 5.0 * math.sin(frac * math.pi)
            bars.append(Bar(timestamp=i, open=trough_l + 1.0, high=102.0,
                            low=trough_l, close=trough_l + 0.5, volume=1))
        # Head: trough ~85
        for i in range(14):
            frac = i / 13.0
            trough_l = 100.0 - 15.0 * math.sin(frac * math.pi)
            bars.append(Bar(timestamp=13 + i, open=trough_l + 1.0, high=102.0,
                            low=trough_l, close=trough_l + 0.5, volume=1))
        # Right shoulder: trough ~94.5, last close 104 (above neckline 102)
        for i in range(13):
            frac = i / 12.0
            trough_l = 100.0 - 4.5 * math.sin(frac * math.pi)
            close_val = 104.0 if i == 12 else trough_l + 0.5
            bars.append(Bar(timestamp=27 + i, open=trough_l + 1.0, high=102.0,
                            low=trough_l, close=close_val, volume=1))
        return bars

    def test_positive_match(self):
        bars = self._make_ihas_bars()
        self.assertEqual(len(bars), 40)
        self.assertEqual(is_inverse_head_and_shoulders(bars), 1)

    def test_below_min_bars_returns_zero(self):
        bars = self._make_ihas_bars()
        self.assertEqual(is_inverse_head_and_shoulders(bars[:39]), 0)
        self.assertEqual(is_inverse_head_and_shoulders([]), 0)

    def test_no_match_head_not_lower(self):
        """Head equal to shoulders → h >= ls*0.99 → no match."""
        bars = []
        # All three sections have same min low ~95
        for section in range(3):
            start_idx = [0, 13, 27][section]
            count = [13, 14, 13][section]
            for i in range(count):
                frac = i / (count - 1) if count > 1 else 0.5
                trough_l = 100.0 - 5.0 * math.sin(frac * math.pi)
                close_val = 104.0 if (section == 2 and i == count - 1) else trough_l + 0.5
                bars.append(Bar(timestamp=start_idx + i, open=trough_l + 1.0, high=102.0,
                                low=trough_l, close=close_val, volume=1))
        self.assertEqual(is_inverse_head_and_shoulders(bars), 0)

    def test_no_match_close_below_neckline(self):
        """Last close below neckline → no match."""
        bars = self._make_ihas_bars()
        last = bars[-1]
        bars[-1] = Bar(timestamp=last.timestamp, open=last.open, high=last.high,
                       low=last.low, close=100.0, volume=1)
        self.assertEqual(is_inverse_head_and_shoulders(bars), 0)


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

    def test_scan_detects_morning_star(self):
        # Craft a morning star: b0 bearish big, b1 small, b2 bullish closing above midpoint
        b0 = b(110, 111, 99, 100)    # bearish, body=10, range=12 > 12*0.60=7.2
        b1 = b(99, 100, 98, 99.5)   # small body=0.5 < 10*0.30=3
        b2 = b(100, 112, 99, 108)   # bullish, close=108 > midpoint=105
        matches = scan_patterns([b0, b1, b2])
        names = {(m.bar_index, m.name) for m in matches}
        self.assertIn((2, "morning_star"), names)

    def test_scan_detects_three_white_soldiers(self):
        def soldier(o, c):
            return b(o, c + 0.05, o - 0.05, c)
        bars = [soldier(100, 102), soldier(101.5, 104), soldier(103.5, 106)]
        matches = scan_patterns(bars)
        names = {(m.bar_index, m.name) for m in matches}
        self.assertIn((2, "three_white_soldiers"), names)

    def test_scan_detects_bullish_flag(self):
        bars = []
        for i in range(10):
            price = 100.0 + i * 2.0
            bars.append(Bar(timestamp=i, open=price - 0.5, high=price + 0.5,
                            low=price - 0.6, close=price, volume=1))
        for i in range(10):
            price = 120.0 + (i % 2) * 0.2
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 0.2,
                            low=price - 0.2, close=price, volume=1))
        matches = scan_patterns(bars)
        names = {m.name for m in matches}
        self.assertIn("bullish_flag", names)

    def test_scan_detects_bearish_flag(self):
        bars = []
        for i in range(10):
            price = 120.0 - i * 2.0
            bars.append(Bar(timestamp=i, open=price + 0.5, high=price + 0.6,
                            low=price - 0.5, close=price, volume=1))
        for i in range(10):
            price = 100.0 + (i % 2) * 0.2
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 0.2,
                            low=price - 0.2, close=price, volume=1))
        matches = scan_patterns(bars)
        names = {m.name for m in matches}
        self.assertIn("bearish_flag", names)

    def test_scan_new_chart_patterns_at_last_bar(self):
        """New chart patterns should only fire at the last bar index."""
        bars = []
        for i in range(10):
            price = 100.0 + i * 2.0
            bars.append(Bar(timestamp=i, open=price - 0.5, high=price + 0.5,
                            low=price - 0.6, close=price, volume=1))
        for i in range(10):
            price = 120.0 + (i % 2) * 0.2
            bars.append(Bar(timestamp=10 + i, open=price, high=price + 0.2,
                            low=price - 0.2, close=price, volume=1))
        matches = scan_patterns(bars)
        last = len(bars) - 1
        for m in matches:
            if m.name in ("bullish_flag", "bearish_flag",
                          "head_and_shoulders", "inverse_head_and_shoulders"):
                self.assertEqual(m.bar_index, last)


if __name__ == "__main__":
    unittest.main()
