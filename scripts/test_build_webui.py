"""Linux-only regions of webui/index.html stay out of the ESP32 embed: python scripts/test_build_webui.py"""

from pathlib import Path
import sys
import json
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_webui as build
import webui_source

ROOT = Path(__file__).resolve().parent.parent


class SourceAssembly(unittest.TestCase):
    def test_shipped_html_matches_the_sources(self):
        self.assertEqual((ROOT / "webui/index.html").read_bytes(), webui_source.assemble(ROOT))

    def test_changed_or_missing_tab_is_rejected_without_overwriting_html(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / "webui/src"
            source.mkdir(parents=True)
            (source / "manifest.json").write_text(json.dumps(["shell.html", "audio.js"]), encoding="utf-8")
            (source / "shell.html").write_bytes(b"<script>\n")
            tab = source / "audio.js"
            tab.write_bytes(b"const title='Musik';\n</script>")
            output = root / "webui/index.html"
            output.write_bytes(webui_source.assemble(root))
            original = output.read_bytes()
            command = [sys.executable, str(ROOT / "scripts/webui_source.py"), "--root", str(root), "--check"]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            tab.write_bytes("const title='Töne';\n</script>".encode())
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            self.assertIn("Töne".encode(), webui_source.assemble(root))
            tab.unlink()
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), original)


class StripLinuxOnly(unittest.TestCase):
    def test_removes_each_region_with_its_markers(self):
        text = ("const A=1;\n"
                "//linux:begin\n"
                "const B=2;\n"
                "//linux:end\n"
                "const I18N={en:{\n"
                "  ok:'OK',\n"
                "  //linux:begin\n"
                "  gp:'Pad',\n"
                "  //linux:end\n"
                "  no:'No'}};\n")
        self.assertEqual(build.strip_linux_only(text),
                         "const A=1;\nconst I18N={en:{\n  ok:'OK',\n  no:'No'}};\n")

    def test_keeps_text_without_markers_byte_for_byte(self):
        text = "a\r\n  b\n\nc Ü"
        self.assertEqual(build.strip_linux_only(text), text)

    def test_keeps_crlf_outside_regions(self):
        self.assertEqual(build.strip_linux_only("a\r\n//linux:begin\r\nb\r\n//linux:end\r\nc\r\n"), "a\r\nc\r\n")

    def test_region_at_end_of_file_without_newline(self):
        self.assertEqual(build.strip_linux_only("a\n//linux:begin\nb\n//linux:end"), "a\n")

    def test_rejects_end_without_begin(self):
        with self.assertRaisesRegex(build.MarkerError, "line 2: //linux:end without //linux:begin"):
            build.strip_linux_only("a\n//linux:end\n")

    def test_rejects_unclosed_region(self):
        with self.assertRaisesRegex(build.MarkerError, "line 2: //linux:begin is never closed"):
            build.strip_linux_only("a\n//linux:begin\nb\n")

    def test_rejects_nested_regions(self):
        with self.assertRaisesRegex(build.MarkerError, "line 3: //linux:begin inside the region opened on line 1"):
            build.strip_linux_only("//linux:begin\na\n//linux:begin\nb\n//linux:end\n//linux:end\n")

    def test_rejects_a_marker_sharing_its_line(self):
        for line in ("x=1;//linux:begin\n", "//linux:end x\n", "// //linux:begin\n"):
            with self.subTest(line=line), self.assertRaisesRegex(build.MarkerError, "line 1: .* alone on its line"):
                build.strip_linux_only(line)

    def test_keep_regions_drops_only_the_marker_lines(self):
        text = "a\n  //linux:begin\n  b\n  //linux:end\nc\n"
        self.assertEqual(build.strip_linux_only(text, keep_regions=True), "a\n  b\nc\n")
        with self.assertRaises(build.MarkerError):
            build.strip_linux_only("//linux:begin\n", keep_regions=True)

    def test_header_guards_the_full_stream(self):
        header = build.render_header(b"raw", b"mini", b"\x1f\x8b\x01", b"full mini", b"\x1f\x8b\x02\x03")
        esp32, guarded = header.split("#if defined(AWTRIX_WEBUI_FULL)\n")
        guarded, tail = guarded.split("#endif\n")
        self.assertIn("inline constexpr unsigned WEBUI_GZ_LEN = 3;", esp32)
        self.assertIn("inline const uint8_t WEBUI_GZ[] PROGMEM = {\n    0x1f,0x8b,0x01,\n};", esp32)
        self.assertNotIn("WEBUI_FULL", esp32.split("#include")[1])
        self.assertIn("inline constexpr unsigned WEBUI_FULL_GZ_LEN = 4;", guarded)
        self.assertIn("inline const uint8_t WEBUI_FULL_GZ[] PROGMEM = {\n    0x1f,0x8b,0x02,0x03,\n};", guarded)
        self.assertIn('WEBUI_FULL_ETAG[] = "\\"', guarded)
        self.assertEqual(tail, "\n}  // namespace awtrix\n")
        self.assertIn("SRC-MD5 %s " % build.hashlib.md5(b"raw").hexdigest(), header)

    def test_real_web_ui_drops_the_gamepad_card(self):
        raw = (ROOT / "webui" / "index.html").read_text(encoding="utf-8")
        self.assertIn("function gamepadSection(page)", raw)
        embedded = build.strip_linux_only(raw)
        for needle in ("gamepadSection", "S.gamepad", "/api/v1/gamepad", "gpPair", "gpNone", "gpForget", "GPSTATE", "//linux:"):
            self.assertNotIn(needle, embedded)
        self.assertIn("capGamepad", embedded)
        self.assertIn("function hubTokenSection(page)", embedded)

    def test_tc002_settings_and_voice_styles_stay_out_of_esp32(self):
        raw = (ROOT / "webui" / "index.html").read_text(encoding="utf-8")
        embedded = build.strip_linux_only(raw)
        full = build.strip_linux_only(raw, keep_regions=True)
        for feature in ("musicSource", "clockFace", "#sec-voice"):
            with self.subTest(feature=feature):
                self.assertNotIn(feature, embedded)
                self.assertIn(feature, full)
        self.assertIn("#sec-hub .badge", embedded)

    def test_layout_editor_api_is_linux_only(self):
        raw = (ROOT / "webui" / "index.html").read_text(encoding="utf-8")
        self.assertIn('layout.prepare(spec)', raw)
        embedded = build.strip_linux_only(raw)
        self.assertNotIn('layout.prepare(spec)', embedded)
        self.assertNotIn("CAP_NAMES.push('layout')", embedded)


if __name__ == "__main__":
    unittest.main()
