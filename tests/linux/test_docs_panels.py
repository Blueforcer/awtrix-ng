"""Docs tools against the real runtime: examples render into what the display shows."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "docs"))

import panel_examples  # noqa: E402
import panel_host  # noqa: E402

OPTIONS = None

SCRIPT = """class Hi
  def draw()
    text(1, 6, "Hi", 0xFFFFFF)
  end
end
return Hi()
"""


def host(width=32, height=8):
    return panel_host.PanelHost(OPTIONS.binary, width, height, OPTIONS.webui)


def lit_rows(pixels, width):
    return {i // width for i, pixel in enumerate(pixels) if pixel}


class PanelHostContract(unittest.TestCase):
    def test_script_text_on_baseline_six_fills_rows_one_to_five(self):
        with host() as runtime:
            [pixels] = runtime.frames(panel_examples.Example("script", SCRIPT))
        self.assertEqual({1, 2, 3, 4, 5}, lit_rows(pixels, 32))

    def test_draw_text_with_top_one_fills_the_same_rows(self):
        draw = {"draw": [["text", 1, 1, "Hi", "#FFFFFF"]]}
        with host() as runtime:
            [pixels] = runtime.frames(panel_examples.Example("pushed", draw))
        self.assertEqual({1, 2, 3, 4, 5}, lit_rows(pixels, 32))

    def test_long_pushed_text_moves(self):
        long_text = {"text": "THIS TEXT IS FAR TOO LONG TO FIT"}
        with host() as runtime:
            frames = runtime.frames(panel_examples.Example("pushed", long_text), motion_s=2.5)
        self.assertGreater(len(frames), 30)
        self.assertNotEqual(frames[0], frames[-1])

    def test_notification_shows(self):
        with host(52, 16) as runtime:
            [pixels] = runtime.frames(panel_examples.Example("notification", {"text": "HI"}))
        self.assertTrue(any(pixels))

    def test_broken_script_is_reported(self):
        with host() as runtime:
            with self.assertRaisesRegex(panel_host.HostError, "does not run"):
                runtime.frames(panel_examples.Example("script", "class A\nend\nreturn B()\n"))


import reader_test  # noqa: E402


class ReaderReference(unittest.TestCase):
    def test_reference_answers_pass_on_both_display_sizes(self):
        for clock in ("esp32", "tc002"):
            with self.subTest(clock=clock):
                results = reader_test.check(clock, reader_test.HERE / "reader_reference.md",
                                            OPTIONS.binary, OPTIONS.webui)
                self.assertEqual([], [r for r in results if not r[1]])


import panels  # noqa: E402


class Pictures(unittest.TestCase):
    def test_marked_pushed_app_with_the_sun_icon(self):
        [panel] = panel_examples.find_panels(
            '<!-- panel style=diagram cols=9-W -->\n```json\n{"icon": "sun", "text": "21"}\n```\n',
            "t.md")
        with host() as runtime:
            svg = panels.picture(runtime, panel)
        self.assertIn("data-frame0=", svg)
        self.assertIn('fill="#FFD000"', svg)
        self.assertIn('fill="%s"' % panels.panel_svg.COLS, svg)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.binary = str(Path(OPTIONS.binary).resolve())
    OPTIONS.webui = str(Path(OPTIONS.webui).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
