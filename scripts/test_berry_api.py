"""The editor's API list: the signatures read from the device bindings."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import berry_api  # noqa: E402

SOURCE = """
  be_regfunc(b, "text", b_text);                    // text(x, y, txt, color?)
  be_regfunc(b, "scroll_text", b_scroll_text);      // scroll_text(txt, color?, opts?)
                                                    // scroll_text(x, y, w, txt, color, opts?)
  // a remark that is not a signature
  be_regfunc(b, "_hidden", b_hidden);               // _hidden()
                                                    // _hidden(x)
  be_regfunc(b, "clear", b_clear);
                                                    // text(x)
"""


class DeviceBuiltins(unittest.TestCase):
    def test_a_binding_lists_its_other_call_shapes_below_it(self):
        self.assertEqual(
            ["text(x, y, txt, color?)", "scroll_text(txt, color?, opts?)",
             "scroll_text(x, y, w, txt, color, opts?)", "clear()"],
            berry_api._device_builtins(SOURCE))


if __name__ == "__main__":
    unittest.main()
