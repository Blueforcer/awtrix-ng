"""Panel markers, examples, picture names and figures."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import panel_examples as pe  # noqa: E402

SCRIPT = "```berry\nclass Hi\n  def draw()\n    text(1, 6, \"Hi\")\n  end\nend\nreturn Hi()\n```\n"
CURL = ("```bash\ncurl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hi \\\n"
        "  -H 'Content-Type: application/json' \\\n  -d '{\"text\":\"HI #1\"}'\n```\n")


class Options(unittest.TestCase):
    def test_defaults(self):
        self.assertEqual({"style": "led"}, pe.parse_options("", "p"))

    def test_every_option(self):
        options = pe.parse_options(
            'style=diagram mark=row:6 cols=9-W boxes at=1500 motion=3.5 alt="Two lines"', "p")
        self.assertEqual({"style": "diagram", "mark": ["row", 6], "cols": [9, "W"],
                          "boxes": True, "at": 1500, "motion": 3.5, "alt": "Two lines"}, options)

    def test_unknown_option(self):
        with self.assertRaisesRegex(pe.PanelError, "p: unknown panel option 'style=big'"):
            pe.parse_options("style=big", "p")

    def test_backwards_columns(self):
        with self.assertRaisesRegex(pe.PanelError, "cols=9-3 runs backwards"):
            pe.parse_options("cols=9-3", "p")


class Examples(unittest.TestCase):
    def test_script(self):
        example = pe.example("berry", "class A\nend\nreturn A()\n", "p")
        self.assertEqual("script", example.kind)

    def test_incomplete_script(self):
        with self.assertRaisesRegex(pe.PanelError, "must be complete"):
            pe.example("berry", "def draw()\nend\n", "p")

    def test_json_payload(self):
        self.assertEqual(pe.Example("pushed", {"text": "HI"}),
                         pe.example("json", '{"text":"HI"}', "p"))

    def test_curl_pushed_app(self):
        code = CURL.split("\n", 1)[1].rsplit("```", 1)[0]
        self.assertEqual(pe.Example("pushed", {"text": "HI #1"}), pe.example("bash", code, "p"))

    def test_curl_notification(self):
        code = ("# a comment\n"
                "curl -X POST http://<awtrix-ip>/api/v1/notifications -d '{\"text\":\"A\"}'\n")
        self.assertEqual(pe.Example("notification", {"text": "A"}), pe.example("bash", code, "p"))

    def test_curl_with_two_requests(self):
        code = ("curl -X PUT http://x/api/v1/apps/pushed/a -d '{}'\n"
                "curl -X PUT http://x/api/v1/apps/pushed/b -d '{}'\n")
        with self.assertRaisesRegex(pe.PanelError, "one -d body"):
            pe.example("bash", code, "p")

    def test_other_language(self):
        with self.assertRaisesRegex(pe.PanelError, "not 'yaml'"):
            pe.example("yaml", "a: 1", "p")


class FindPanels(unittest.TestCase):
    def test_marker_before_block(self):
        [panel] = pe.find_panels("Text\n\n<!-- panel style=diagram -->\n" + SCRIPT, "p.md")
        self.assertEqual((3, "berry", "script"), (panel.line, panel.lang, panel.example.kind))
        self.assertEqual(10, panel.close)
        self.assertTrue(panel.code.startswith("class Hi\n"))

    def test_marker_inside_code_is_text(self):
        self.assertEqual([], pe.find_panels("````\n<!-- panel -->\n```berry\n````\n", "p.md"))

    def test_marker_without_block(self):
        with self.assertRaisesRegex(pe.PanelError, "p.md:1: a panel marker must stand directly"):
            pe.find_panels("<!-- panel -->\n\n" + SCRIPT, "p.md")

    def test_indented_block_in_a_tab(self):
        text = '=== "Script"\n\n    <!-- panel -->\n' + "".join(
            "    " + line for line in SCRIPT.splitlines(keepends=True))
        [panel] = pe.find_panels(text, "p.md")
        self.assertEqual("    ", panel.indent)
        self.assertTrue(panel.code.startswith("class Hi\n"))


class Pictures(unittest.TestCase):
    def setUp(self):
        [self.panel] = pe.find_panels("<!-- panel -->\n" + SCRIPT, "p.md")

    def test_hash_ignores_alt_line_ends_and_trailing_spaces(self):
        [other] = pe.find_panels('<!-- panel alt="x" -->\r\n'
                                 + SCRIPT.replace("\n", "  \r\n"), "q.md")
        self.assertEqual(pe.picture_hash(self.panel, (32, 8)), pe.picture_hash(other, (32, 8)))

    def test_hash_follows_size_and_options(self):
        [diagram] = pe.find_panels("<!-- panel style=diagram -->\n" + SCRIPT, "p.md")
        first = pe.picture_hash(self.panel, (32, 8))
        self.assertNotEqual(first, pe.picture_hash(self.panel, (52, 16)))
        self.assertNotEqual(first, pe.picture_hash(diagram, (32, 8)))
        self.assertRegex(first, "^[0-9a-f]{12}$")

    def test_figure_goes_under_the_block_and_the_marker_goes(self):
        text = "Intro\n<!-- panel -->\n" + SCRIPT + "After\n"
        out = pe.insert_figures(text, "esp32", "guides/x.md", lambda path: True)
        path = pe.picture_path(self.panel, (32, 8))
        self.assertNotIn("<!-- panel", out)
        self.assertIn("```\n\n![What the script shows on the display](../%s)"
                      "{ .awx-panel loading=lazy }\n\nAfter\n" % path, out)

    def test_messages_name_the_source_line(self):
        origin = [1, 7] + list(range(9, 17))
        [panel] = pe.find_panels("Intro\n<!-- panel -->\n" + SCRIPT, "p.md", origin)
        self.assertEqual((2, 7), (panel.line, panel.source))
        with self.assertRaisesRegex(pe.PanelError, r"guides/x.md:7: picture missing"):
            pe.insert_figures("Intro\n<!-- panel -->\n" + SCRIPT, "esp32", "guides/x.md",
                              lambda path: False, origin)

    def test_missing_picture_stops_the_build(self):
        with self.assertRaisesRegex(pe.PanelError,
                                    r"guides/x.md:1: picture missing, run python3 tools/docs/panels.py"):
            pe.insert_figures("<!-- panel -->\n" + SCRIPT, "tc002", "guides/x.md", lambda path: False)


if __name__ == "__main__":
    unittest.main()
