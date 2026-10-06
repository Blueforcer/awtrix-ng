"""Reader test: answers, areas and checks, and the bundle a reader gets."""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import reader_test as rt  # noqa: E402

W, H = 4, 2


def frame(*lit, color=0xFFFFFF):
    pixels = [0] * (W * H)
    for x, y in lit:
        pixels[y * W + x] = color
    return pixels


class Areas(unittest.TestCase):
    def test_all_and_named_ends(self):
        self.assertEqual((0, 3, 0, 1), rt.area("all", W, H))
        self.assertEqual((1, 3, 1, 1), rt.area("1-W,H-H", W, H))
        self.assertEqual((2, 2, 0, 1), rt.area("2,0-1", W, H))


class Checks(unittest.TestCase):
    def test_moves_and_still(self):
        frames = [frame((0, 0))] * 10 + [frame((1, 0))] * 30
        self.assertTrue(rt.evaluate("moves all", frames, W, H))
        self.assertFalse(rt.evaluate("still all", frames, W, H))
        self.assertTrue(rt.evaluate("still 0-3,1-1", frames, W, H))

    def test_lit_dark_colors(self):
        frames = [frame((3, 1))]
        self.assertTrue(rt.evaluate("lit W-W,H-H", frames, W, H))
        self.assertTrue(rt.evaluate("dark 0-2,0-1", frames, W, H))
        self.assertFalse(rt.evaluate("colors 2", frames, W, H))
        two = frame((0, 0))[:4] + frame((1, 1), color=0x00FF00)[4:]
        self.assertTrue(rt.evaluate("colors 2", [two], W, H))

    def test_centered(self):
        self.assertTrue(rt.evaluate("centered all", [frame((1, 0), (2, 0))], W, H))
        self.assertFalse(rt.evaluate("centered all", [frame((0, 0))], W, H))

    def test_payload_and_source(self):
        payload = {"repeat": 1, "scroll": {"mode": "static"}, "layout": {"version": 1}}
        self.assertTrue(rt.evaluate("payload repeat>=1", [frame()], W, H, payload))
        self.assertTrue(rt.evaluate("payload scroll.mode=static", [frame()], W, H, payload))
        self.assertTrue(rt.evaluate("payload layout", [frame()], W, H, payload))
        self.assertFalse(rt.evaluate("payload hold", [frame()], W, H, payload))
        self.assertFalse(rt.evaluate("payload repeat>=1", [frame()], W, H, None))
        self.assertTrue(rt.evaluate("source !scroll_text", [frame()], W, H, source="text(1, 6, x)"))
        self.assertFalse(rt.evaluate("source !scroll_text", [frame()], W, H, source="scroll_text(x)"))

    def test_unknown_check(self):
        with self.assertRaises(ValueError):
            rt.evaluate("glows all", [frame()], W, H)


class Answers(unittest.TestCase):
    def test_one_block_per_heading(self):
        text = ("## one\n\n```berry\nreturn A()\n```\n\n"
                "## two\nno block here\n\n## three\n```bash\ncurl x\n```\n")
        answers = rt.read_answers(text)
        self.assertEqual({"one", "three"}, set(answers))
        self.assertEqual(("berry", "return A()\n"), answers["one"])


class Bundle(unittest.TestCase):
    def test_bundle_filters_by_clock_and_leaves_out_developer_pages(self):
        with tempfile.TemporaryDirectory() as docs, tempfile.TemporaryDirectory() as out:
            docs, out = Path(docs), Path(out)
            (docs / "guides").mkdir()
            (docs / "developers").mkdir()
            (docs / "guides" / "a.md").write_text(
                "# A\n<!-- only tc002 -->\nbig\n<!-- /only -->\n<!-- panel -->\n```json\n{}\n```\n")
            (docs / "guides" / "b.md").write_text("---\nonly: [tc002]\n---\n# B\n")
            (docs / "developers" / "c.md").write_text("# C\n")
            tasks = out / "tasks.json"
            tasks.write_text(json.dumps({"tasks": [
                {"id": "t1", "clocks": ["esp32"], "ask": "Do it.", "checks": []},
                {"id": "t2", "clocks": ["tc002"], "ask": "Not you.", "checks": []}]}))
            saved = rt.DOCS, rt.TASKS
            rt.DOCS, rt.TASKS = docs, tasks
            try:
                self.assertEqual(1, rt.bundle("esp32", out / "b"))
            finally:
                rt.DOCS, rt.TASKS = saved
            self.assertEqual("# A\n```json\n{}\n```\n", (out / "b" / "guides" / "a.md").read_text())
            self.assertFalse((out / "b" / "developers").exists())
            listing = (out / "b" / "TASKS.md").read_text()
            self.assertIn("## t1\n\nDo it.", listing)
            self.assertNotIn("t2", listing)


if __name__ == "__main__":
    unittest.main()
