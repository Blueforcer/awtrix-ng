"""The clock rules shared by the docs build and the docs tools."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import only  # noqa: E402


class Names(unittest.TestCase):
    def test_known_names(self):
        self.assertEqual({"esp32", "tc002"}, only.names("esp32 tc002", "x"))

    def test_unknown_name_is_an_error(self):
        with self.assertRaisesRegex(only.OnlyError, "x: unknown clock tc003"):
            only.names("tc003", "x")

    def test_no_name_is_an_error(self):
        with self.assertRaisesRegex(only.OnlyError, "x: marker names no clock"):
            only.names("  ", "x")


class PageOnly(unittest.TestCase):
    def test_no_front_matter_means_every_clock(self):
        self.assertEqual(set(only.VARIANTS), only.page_only("# Title\n", "p.md"))

    def test_front_matter_without_only_means_every_clock(self):
        self.assertEqual(set(only.VARIANTS), only.page_only("---\ntitle: X\n---\n# T\n", "p.md"))

    def test_flow_list(self):
        text = "---\nonly: [esp32, esp32-s3]\n---\n# T\n"
        self.assertEqual({"esp32", "esp32-s3"}, only.page_only(text, "p.md"))

    def test_single_name_with_crlf_and_bom(self):
        text = "﻿---\r\nonly: [tc002]\r\n---\r\n# T\r\n"
        self.assertEqual({"tc002"}, only.page_only(text, "p.md"))

    def test_block_list_is_refused(self):
        with self.assertRaisesRegex(only.OnlyError, "p.md: marker names no clock"):
            only.page_only("---\nonly:\n  - tc002\n---\n", "p.md")


class FilterMarkdown(unittest.TestCase):
    def test_passage_kept_and_dropped(self):
        text = "a\n<!-- only tc002 -->\nb\n<!-- /only -->\nc\n"
        self.assertEqual("a\nb\nc\n", only.filter_markdown(text, "tc002", "p"))
        self.assertEqual("a\nc\n", only.filter_markdown(text, "esp32", "p"))

    def test_origin_maps_each_kept_line_to_its_source_line(self):
        text = "a\n<!-- only tc002 -->\nb\n<!-- /only -->\nc\n"
        origin = []
        only.filter_markdown(text, "esp32", "p", origin)
        self.assertEqual([1, 5], origin)

    def test_inline_markers(self):
        text = "x <!-- only esp32 -->32<!-- /only --><!-- only tc002 -->52<!-- /only --> y\n"
        self.assertEqual("x 52 y\n", only.filter_markdown(text, "tc002", "p"))

    def test_markers_inside_code_are_text(self):
        text = "```\n<!-- only tc002 -->\n```\n"
        self.assertEqual(text, only.filter_markdown(text, "esp32", "p"))

    def test_unclosed_marker(self):
        with self.assertRaisesRegex(only.OnlyError, "p:1: marker is never closed"):
            only.filter_markdown("<!-- only tc002 -->\nx\n", "tc002", "p")

    def test_closing_without_opening(self):
        with self.assertRaisesRegex(only.OnlyError, "without an opening marker"):
            only.filter_markdown("<!-- /only -->\n", "tc002", "p")


if __name__ == "__main__":
    unittest.main()
