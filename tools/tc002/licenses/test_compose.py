"""Tests for the TC002 licence file (compose.py)."""
import gzip
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compose  # noqa: E402


class ComposeTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="awtrix-licenses-test-"))
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        (self.root / "preamble").write_text("Licences\n\nContents:\n")
        (self.root / "gpl").write_bytes(b"GNU GENERAL PUBLIC LICENSE\r\nVersion 2\r\n")
        (self.root / "copying").write_text("See README.\n")
        (self.root / "readme").write_text("\nBSD licence\n\n")

    def run_compose(self, *extra):
        output = self.root / "licenses.txt.gz"
        code = compose.main([str(output), "--preamble", str(self.root / "preamble"), *extra])
        return code, output

    def test_sections_in_order_and_reproducible(self):
        arguments = ["--text", "BusyBox - GPL-2.0 - bin/udhcpc", str(self.root / "gpl"),
                     "--text", "wpa_supplicant - BSD-3-Clause", str(self.root / "copying"),
                     str(self.root / "readme")]
        code, output = self.run_compose(*arguments)
        self.assertEqual(code, 0)
        first = output.read_bytes()
        self.assertEqual(first[3], 0, "no file name or other optional gzip fields")
        self.assertEqual(first[4:8], bytes(4), "no time stamp")
        text = gzip.decompress(first).decode()
        self.assertTrue(text.startswith("Licences\n\nContents:\n\n 1. BusyBox - GPL-2.0 - bin/udhcpc\n"
                                        " 2. wpa_supplicant - BSD-3-Clause\n"))
        self.assertIn("1. BusyBox - GPL-2.0 - bin/udhcpc\n" + compose.RULE +
                      "\n\nGNU GENERAL PUBLIC LICENSE\nVersion 2\n", text)
        self.assertLess(text.index("See README."), text.index("BSD licence"))
        self.assertNotIn("\r", text)
        self.assertTrue(text.endswith("BSD licence\n"))
        self.assertEqual(self.run_compose(*arguments)[1].read_bytes(), first)

    def test_refuses_missing_or_empty_texts(self):
        (self.root / "empty").write_text("\n\n")
        code, output = self.run_compose("--text", "Empty", str(self.root / "empty"))
        self.assertEqual(code, 1)
        self.assertFalse(output.exists())
        code, _ = self.run_compose("--text", "Missing", str(self.root / "missing"))
        self.assertEqual(code, 1)
        code, _ = self.run_compose("--text", "No file")
        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
