import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import release_highlights  # noqa: E402

PAGE = """# AWTRIX NG 9.9.9

Release of 2099-01-01.

### New

* **Everywhere:** every clock gets it. See [Guide](../guides/x.md).
<!-- only esp32 esp32-s3 -->
* **Script host:** your scripts run on <!-- only esp32 -->a TC001<!-- /only --><!-- only esp32-s3 -->your
  clock<!-- /only --> with room to spare. It installs on Windows.
* **AHT20 sensors** are found on the sensor bus. See [Sensor bus](../reference/gpio.md).
<!-- /only -->
<!-- only tc002 -->
* **Wake word cutoff:** enter a value from 0.30 to 0.99.
<!-- /only -->
<!-- only esp32 -->
* **More free memory.** About 23 KB more stays free.
<!-- /only -->

### For script developers

* **`hint=`** puts gray text in an empty box.
* The log names scripts that scan &amp; stop.

### Fixed

* **MQTT:** a slow broker connects.

### Breaking changes

* **`voice` is a built-in name now.** A module of yours needs a new name.
"""


class Highlights(unittest.TestCase):
    def setUp(self):
        self.lines = release_highlights.highlights(PAGE).splitlines()

    def test_heading_and_order(self):
        self.assertEqual("**Highlights**", self.lines[0])
        self.assertEqual("• ⚠️ **`voice` is a built-in name now.**", self.lines[1])
        self.assertEqual(9, len(self.lines))

    def test_colon_title_keeps_the_first_sentence_without_links(self):
        self.assertIn("• **Everywhere**: every clock gets it.", self.lines)
        self.assertIn("• **Wake word cutoff** (TC002): enter a value from 0.30 to 0.99.", self.lines)

    def test_one_line_per_title_in_the_first_clock_s_words(self):
        self.assertIn("• **Script host** (ESP32 / TC001, ESP32-S3): your scripts run on a TC001 with room to spare.",
                      self.lines)
        self.assertEqual(1, sum("Script host" in line for line in self.lines))

    def test_title_that_starts_a_sentence(self):
        self.assertIn("• **AHT20 sensors** are found on the sensor bus. (ESP32 / TC001, ESP32-S3)", self.lines)

    def test_title_that_is_a_sentence(self):
        self.assertIn("• **More free memory.** (ESP32 / TC001)", self.lines)

    def test_bullet_without_title_unescapes(self):
        self.assertIn("• The log names scripts that scan & stop.", self.lines)

    def test_fixes_are_left_out(self):
        self.assertFalse(any("MQTT" in line for line in self.lines))

    def test_page_without_highlight_sections(self):
        self.assertEqual("", release_highlights.highlights("# X\n\n### Fixed\n\n* **A:** b.\n"))


if __name__ == "__main__":
    unittest.main()
