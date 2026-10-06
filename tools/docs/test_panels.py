"""Which markers the picture tool renders, and the options it resolves."""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import panel_examples  # noqa: E402
import panels  # noqa: E402

SCRIPT = "```berry\nclass A\n  def draw()\n  end\nend\nreturn A()\n```\n"


class Collect(unittest.TestCase):
    def test_one_job_per_picture_and_display_size(self):
        with tempfile.TemporaryDirectory() as docs:
            docs = Path(docs)
            (docs / "a.md").write_text("<!-- panel -->\n" + SCRIPT)
            (docs / "b.md").write_text("<!-- only tc002 -->\n<!-- panel boxes -->\n"
                                       '```json\n{"text": "X"}\n```\n<!-- /only -->\n')
            (docs / "c.md").write_text("---\nonly: [esp32]\n---\n<!-- panel -->\n" + SCRIPT)
            jobs = panels.collect(docs, ["esp32", "esp32-s3", "tc002"])
        found = sorted((job.page, job.size) for job in jobs.values())
        self.assertEqual([("a.md", (32, 8)), ("a.md", (52, 16)), ("b.md", (52, 16))], found)
        [inside] = [job for job in jobs.values() if job.page == "b.md"]
        self.assertEqual((1, 2), (inside.panel.line, inside.panel.source))

    def test_a_bad_marker_names_its_page(self):
        with tempfile.TemporaryDirectory() as docs:
            (Path(docs) / "a.md").write_text("<!-- panel style=huge -->\n" + SCRIPT)
            with self.assertRaisesRegex(panel_examples.PanelError, "a.md:1: unknown panel option"):
                panels.collect(Path(docs), ["esp32"])


class Verdict(unittest.TestCase):
    def test_same_picture(self):
        self.assertEqual("same", panels.verdict("a", "a", lambda: self.fail("no second render")))

    def test_a_steady_picture_that_changed(self):
        self.assertEqual("differs", panels.verdict("a", "b", lambda: "b"))

    def test_a_picture_that_changes_on_its_own(self):
        self.assertEqual("unsteady", panels.verdict("a", "b", lambda: "c"))

    def test_a_missing_picture(self):
        self.assertEqual("differs", panels.verdict(None, "b", lambda: "b"))

    def test_the_second_render_starts_out_of_phase(self):
        slept = []
        with mock.patch.object(panels.time, "sleep", slept.append), \
                mock.patch.object(panels, "picture", lambda host, panel: '<svg data-frame0="ab">'):
            self.assertEqual("ab", panels.render_again(None, None))
        self.assertEqual([panels.PHASE_SHIFT_S], slept)
        self.assertNotEqual(0, panels.PHASE_SHIFT_S % 1)


class Options(unittest.TestCase):
    def test_columns_resolve_the_last_one(self):
        self.assertEqual([9, 31], panels.columns([9, "W"], 32))
        self.assertEqual([9, 20], panels.columns([9, 20], 52))
        self.assertIsNone(panels.columns(None, 32))

    def test_boxes_come_from_the_layout(self):
        payload = {"layout": {"version": 1, "regions": [{"id": "a", "box": [0, 0, 52, 8]},
                                                         {"id": "b", "box": [0, 8, 52, 8]}]}}
        [panel] = panel_examples.find_panels(
            "<!-- panel boxes -->\n```json\n%s\n```\n" % json.dumps(payload), "p")
        self.assertEqual([[0, 0, 52, 8], [0, 8, 52, 8]], panels.boxes(panel))
        [plain] = panel_examples.find_panels("<!-- panel -->\n```json\n%s\n```\n"
                                             % json.dumps(payload), "p")
        self.assertEqual([], panels.boxes(plain))


if __name__ == "__main__":
    unittest.main()
