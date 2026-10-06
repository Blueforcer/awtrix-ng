"""SVG pictures of the display."""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import panel_svg as ps  # noqa: E402

W, H = 4, 2


def frame(*lit, color=0xFF0000):
    pixels = [0] * (W * H)
    for x, y in lit:
        pixels[y * W + x] = color
    return pixels


class Collapse(unittest.TestCase):
    def test_runs_of_equal_frames_merge(self):
        a, b = frame((0, 0)), frame((1, 0))
        self.assertEqual([(a, 140), (b, 70), (a, 70)], ps.collapse([a, a, b, a], 70))


class Still(unittest.TestCase):
    def test_led_picture(self):
        pixels = frame((0, 0), (1, 0))
        svg = ps.render([pixels], W, H)
        self.assertTrue(svg.startswith(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 52 32" width="52" height="32"'))
        self.assertIn('data-frame0="%s"' % ps.frame_hash(pixels), svg)
        self.assertIn('stroke="#FF0000"', svg)
        self.assertNotIn("<style>", svg)

    def test_diagram_with_rulers_marks_and_boxes(self):
        svg = ps.render([frame((0, 0))], W, H, "diagram", mark=["row", 1], cols=[2, 3],
                        boxes=[[0, 0, 2, 2], [2, 0, 2, 2]])
        self.assertIn('viewBox="0 0 60 36"', svg)
        self.assertIn('fill="#FF0000"', svg)
        self.assertEqual(["0", "2", "3", "0", "1"], re.findall(r">(\d+)</text>", svg))
        self.assertEqual(2, svg.count('stroke-dasharray="4 2"'))
        self.assertIn('fill="%s"' % ps.MARK, svg)
        self.assertIn('fill="%s"' % ps.COLS, svg)


class Motion(unittest.TestCase):
    def test_frames_become_an_animation_that_respects_reduced_motion(self):
        a, b = frame((0, 0)), frame((1, 0))
        svg = ps.render([a, a, b], W, H, frame_ms=70)
        self.assertEqual(2, svg.count('<g class="f f'))
        self.assertIn("@keyframes k0{0%{visibility:visible}66.667%{visibility:hidden}}", svg)
        self.assertIn(".f0{animation:k0 0.210s 0.000s infinite}", svg)
        self.assertIn(".f1{animation:k1 0.210s 0.140s infinite}", svg)
        self.assertIn("@media (prefers-reduced-motion:reduce){.f{animation:none}"
                      ".f0{visibility:visible}}", svg)


if __name__ == "__main__":
    unittest.main()
