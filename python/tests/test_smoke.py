import unittest

from algoforge import Bar, Direction, Timeframe, __version__


class SmokeTests(unittest.TestCase):
    def test_version_present(self):
        self.assertIsInstance(__version__, str)
        self.assertTrue(__version__)

    def test_timeframes_seconds(self):
        self.assertEqual(Timeframe.H1, 3600)
        self.assertEqual(Timeframe.W1, 604800)

    def test_direction_values(self):
        self.assertEqual(Direction.LONG, 1)
        self.assertEqual(Direction.SHORT, -1)
        self.assertEqual(Direction.NONE, 0)

    def test_bar_derived(self):
        b = Bar(timestamp=1, open=1.10, high=1.12, low=1.09, close=1.11, volume=100)
        self.assertAlmostEqual(b.body, 0.01, places=10)
        self.assertAlmostEqual(b.range, 0.03, places=10)
        self.assertTrue(b.is_bullish)


if __name__ == "__main__":
    unittest.main()
