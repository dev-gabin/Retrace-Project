import os
import unittest
from unittest.mock import patch

from vision.aim_mapper import AFFINE_NAMES, AimMapper, fit_affine
from vision.object_tracker import StableObjectTracker


class CalibrationTest(unittest.TestCase):
    def test_fit_cross_axis_and_map(self):
        points = [(0.5, 0.5), (0.4, 0.52), (0.6, 0.48), (0.52, 0.4), (0.48, 0.6)]
        pan = fit_affine([(x, y, 80*x + 12*y + 30) for x, y in points])
        tilt = fit_affine([(x, y, -10*x + 60*y + 50) for x, y in points])
        mapper = AimMapper(100, 100, 0, 0, 0, 0, (*pan, *tilt))
        self.assertEqual(mapper.map(25, 75), (59, 93))
        self.assertNotEqual(mapper.map(25, 25)[0], mapper.map(25, 75)[0])

    def test_legacy_and_environment_priority(self):
        legacy = dict(zip(("RETRACE_PAN_LEFT", "RETRACE_PAN_RIGHT", "RETRACE_TILT_TOP", "RETRACE_TILT_BOTTOM"), ("135", "45", "60", "120")))
        with patch.dict(os.environ, legacy, clear=True):
            self.assertEqual(AimMapper.from_env(640, 480).map(320, 240), (90, 90))
        env = {**legacy, **dict(zip(AFFINE_NAMES, map(str, (80, 12, 30, -10, 60, 50))))}
        with patch.dict(os.environ, env, clear=True):
            self.assertEqual(AimMapper.from_env(100, 100).map(25, 75), (59, 93))
        with patch.dict(os.environ, {AFFINE_NAMES[0]: "80"}, clear=True):
            with self.assertRaises(ValueError):
                AimMapper.from_env(100, 100)

    def test_remote_defaults_preserved(self):
        with patch.dict(os.environ, {}, clear=True):
            mapper = AimMapper.from_env(640, 480)
            self.assertEqual((mapper.pan_left, mapper.pan_right, mapper.tilt_top, mapper.tilt_bottom), (98, 71, 54, 30))
            self.assertEqual(mapper.map(320, 240), (85, 42))

    def test_degenerate_and_out_of_range(self):
        with self.assertRaises(ValueError):
            fit_affine([(0, 0, 20), (0.5, 0.5, 30), (1, 1, 40)])
        with self.assertRaises(ValueError):
            AimMapper(100, 100, 0, 0, 0, 0, (200, 0, 0, 0, 90, 0)).map(99, 50)

    def test_small_movement_updates_after_stability(self):
        tracker = StableObjectTracker()
        def detection(x):
            return [{"name": "wallet", "item_id": "wallet", "confidence": 0.9, "center": (x, 100)}]
        with patch("vision.object_tracker.time.monotonic", return_value=100):
            for _ in range(3):
                ready = tracker.update(detection(100))
            self.assertEqual(len(ready), 1)
            tracker.mark_saved("wallet", (100, 100))
        with patch("vision.object_tracker.time.monotonic", return_value=106):
            self.assertEqual(tracker.update(detection(109)), [])
            self.assertEqual(len(tracker.update(detection(110))), 1)


if __name__ == "__main__":
    unittest.main()
