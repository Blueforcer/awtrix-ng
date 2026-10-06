"""Tests for the TC002 bundle manifest (bundle.py)."""
import gzip
import hashlib
import io
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import contextlib
import unittest
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bundle  # noqa: E402

INSTALLED = ("bin/awtrix-linux", "bin/awtrix-tc002d", "bin/udhcpc", "bin/dhcp-callback",
             "bin/wpa_supplicant", "bin/awtrix-tc002-audio-pcm", "bin/awtrix-tc002-flash",
             "share/index.html.gz", "share/boot.mp3", "share/ca-certificates.crt",
             "share/licenses.txt.gz", "lib/modules/aic8800_bsp.ko", "lib/modules/aic8800_fdrv.ko",
             "lib/modules/awtrix_pcm.ko")
VERSION = "1.1.2-gabc1234"
COUNTER = 1790345998


def have_mksquashfs():
    try:
        bundle.mksquashfs_version()
        return bool(shutil.which("unsquashfs"))
    except (bundle.BundleError, OSError):
        return False


HAVE_MKSQUASHFS = have_mksquashfs()


class BundleTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="awtrix-bundle-test-"))
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.bundle = self.root / "bundle"
        noise = random.Random(7)
        for path in INSTALLED + bundle.HOST_ONLY:
            (self.bundle / path).parent.mkdir(parents=True, exist_ok=True)
            data = path.encode() * 10
            if path == "bin/awtrix-linux":
                data = noise.randbytes(9000) + bytes(9000)
            (self.bundle / path).write_bytes(data)

    def write(self, **options):
        return bundle.write_manifest(self.bundle, VERSION, "abc1234", False, **options)

    def test_manifest_round_trip(self):
        manifest = bundle.write_manifest(self.bundle, VERSION, "abc1234", True, counter=COUNTER)
        self.assertEqual(bundle.verify(self.bundle), manifest)
        self.assertEqual(json.loads((self.bundle / "manifest.json").read_text()), manifest)
        self.assertEqual(manifest["schema_version"], 3)
        self.assertEqual(manifest["counter"], COUNTER)
        self.assertTrue(manifest["dirty"])
        self.assertNotIn("image", manifest)
        self.assertEqual([e["path"] for e in manifest["files"]], sorted(INSTALLED))
        self.assertEqual([e["path"] for e in manifest["hostOnly"]], sorted(bundle.HOST_ONLY))
        for entry in manifest["files"] + manifest["hostOnly"]:
            data = (self.bundle / entry["path"]).read_bytes()
            self.assertEqual(set(entry), {"path", "size", "sha256", "mode"})
            self.assertEqual((entry["size"], entry["sha256"]), (len(data), hashlib.sha256(data).hexdigest()))
        modes = {e["path"]: e["mode"] for e in manifest["files"] + manifest["hostOnly"]}
        self.assertEqual(modes["share/index.html.gz"], "0644")
        self.assertEqual(modes["lib/modules/awtrix_pcm.ko"], "0644")
        self.assertEqual(modes["lib/modules/loop.ko"], "0644")
        self.assertEqual(modes["bin/udhcpc"], "0755")
        self.assertEqual(modes["bin/awtrix-tc002-flash"], "0755")
        self.assertEqual(modes["lib/libawtrix-loader.so"], "0755")
        self.assertRegex(manifest["release"], r"^1\.1\.2-gabc1234-[0-9a-f]{12}$")
        self.assertNotIn("counter", self.write())

    def test_web_ui_comes_from_the_embedded_asset(self):
        repo = Path(__file__).resolve().parents[3]
        data = bundle.embedded_webui(repo)
        html = gzip.decompress(data)
        self.assertEqual(html[:15].lower(), b"<!doctype html>")
        header = (repo / bundle.WEBUI_ASSET).read_text(encoding="utf-8")
        self.assertIn(f"WEBUI_FULL_GZ_LEN = {len(data)};", header)
        esp32 = header[header.index("WEBUI_GZ[] PROGMEM = {"):]
        esp32 = gzip.decompress(bytes(int(v, 16) for v in re.findall(r"0x([0-9a-f]{2}),", esp32[:esp32.index("};")])))
        source = (repo / bundle.WEBUI_SOURCE).read_bytes()
        self.assertIn(b"//linux:begin", source)
        for linux_only in (b"/api/v1/gamepad", b"gpPairBtn"):
            self.assertIn(linux_only, html)
            self.assertNotIn(linux_only, esp32)
        self.assertNotIn(b"linux:begin", html)
        self.assertGreater(len(html), len(esp32))
        self.assertLess(header.index("WEBUI_GZ[] PROGMEM"), header.index("#if defined(AWTRIX_WEBUI_FULL)"))
        self.assertLess(header.index("#if defined(AWTRIX_WEBUI_FULL)"), header.index("WEBUI_FULL_GZ[] PROGMEM"))
        fake = Path(tempfile.mkdtemp(prefix="awtrix-webui-"))
        self.addCleanup(shutil.rmtree, fake, ignore_errors=True)
        (fake / "webui").mkdir()
        shutil.copytree(repo / "webui/src", fake / "webui/src")
        (fake / "src/transport/http").mkdir(parents=True)
        (fake / bundle.WEBUI_ASSET).write_text(header, encoding="utf-8")
        (fake / bundle.WEBUI_SOURCE).write_bytes((repo / bundle.WEBUI_SOURCE).read_bytes() + b"\n")
        with self.assertRaisesRegex(bundle.BundleError, "not generated from this"):
            bundle.embedded_webui(fake)
        (fake / bundle.WEBUI_SOURCE).write_bytes((repo / bundle.WEBUI_SOURCE).read_bytes())
        fragment = fake / "webui/src/audio.js"
        original = fragment.read_bytes()
        fragment.write_bytes(original + b"\n// changed tab\n")
        with self.assertRaisesRegex(bundle.BundleError, "index.html is stale"):
            bundle.embedded_webui(fake)
        fragment.write_bytes(original)
        guard = header.index("#if defined(AWTRIX_WEBUI_FULL)")
        damaged = header[:guard] + header[guard:].replace("0x1f,0x8b,", "0x1f,0x8c,", 1)
        (fake / bundle.WEBUI_ASSET).write_text(damaged, encoding="utf-8")
        with self.assertRaisesRegex(bundle.BundleError, "damaged"):
            bundle.embedded_webui(fake)
        (fake / bundle.WEBUI_ASSET).write_text(header[:guard] + "}  // namespace awtrix\n", encoding="utf-8")
        with self.assertRaisesRegex(bundle.BundleError, "no AWTRIX_WEBUI_FULL stream"):
            bundle.embedded_webui(fake)

    def test_release_names_follow_the_device_rule(self):
        for good in ("1", "1.1.2-ge36eeadbd11b-e4d2371b18d2", "a+b_c.d-e", "x" * 64):
            self.assertTrue(bundle.valid_release_name(good), good)
        for bad in ("", ".hidden", "-x", "a:b", "a/b", "x" * 65, "1.2.0.partial", None):
            self.assertFalse(bundle.valid_release_name(bad), bad)

    def test_release_name_covers_installed_files_only(self):
        first = self.write()["release"]
        (self.bundle / "lib/modules/loop.ko").write_bytes(b"other module")
        with self.assertRaisesRegex(bundle.BundleError, "lib/modules/loop.ko differs"):
            bundle.verify(self.bundle)
        self.assertEqual(self.write()["release"], first)
        (self.bundle / "bin/awtrix-tc002-flash").write_bytes(b"other version string")
        self.assertNotEqual(self.write()["release"], first)

    def test_release_tree_holds_the_installed_files_only(self):
        manifest = self.write(counter=COUNTER)
        tree = self.root / "release"
        release = bundle.write_release(self.bundle, tree)
        self.assertNotIn("hostOnly", release)
        self.assertEqual(release["files"], manifest["files"])
        self.assertEqual(release["release"], manifest["release"])
        self.assertTrue((tree / "bin/awtrix-tc002-flash").exists())
        for path in bundle.HOST_ONLY:
            self.assertFalse((tree / path).exists(), path)
        if os.name == "posix":
            self.assertEqual((tree / "bin/awtrix-linux").stat().st_mode & 0o777, 0o755)
            self.assertEqual((tree / "share/boot.mp3").stat().st_mode & 0o777, 0o644)
        with self.assertRaises(FileExistsError):
            bundle.write_release(self.bundle, tree)

    def test_image_validity_follows_the_slot_rule(self):
        def superblock(used):
            return b"hsqs" + bytes(36) + used.to_bytes(8, "little") + bytes(48)
        self.assertTrue(bundle.image_valid(superblock(8000), 8000))
        self.assertTrue(bundle.image_valid(superblock(8000), 8192))
        self.assertFalse(bundle.image_valid(superblock(8000), 8193))
        self.assertFalse(bundle.image_valid(superblock(8000), 7999))
        self.assertFalse(bundle.image_valid(superblock(95), 4096))
        self.assertFalse(bundle.image_valid(b"hsqr" + superblock(8000)[4:], 8000))
        self.assertFalse(bundle.image_valid(superblock(8000)[:95], 8000))

    @unittest.skipUnless(HAVE_MKSQUASHFS, "needs mksquashfs 4.6 and unsquashfs")
    def test_the_bundle_carries_the_image_of_its_release(self):
        manifest = self.write(counter=COUNTER)
        with redirect_stdout(io.StringIO()) as output:
            self.assertEqual(bundle.main(["image", str(self.bundle)]), 0)
        with_image = bundle.verify(self.bundle)
        image = (self.bundle / bundle.IMAGE).read_bytes()
        self.assertEqual(with_image["image"], {"size": len(image), "sha256": hashlib.sha256(image).hexdigest()})
        self.assertEqual({k: v for k, v in with_image.items() if k != "image"}, manifest)
        self.assertIn(f"{bundle.IMAGE}, {len(image)} bytes", output.getvalue())
        self.assertTrue(bundle.image_valid(image[:96], len(image)))
        self.assertEqual(int.from_bytes(image[8:12], "little"), COUNTER)
        unpacked = self.root / "unpacked"
        subprocess.run(["unsquashfs", "-q", "-n", "-d", str(unpacked), str(self.bundle / bundle.IMAGE)],
                       check=True, capture_output=True)
        listed = sorted(p.relative_to(unpacked).as_posix() for p in unpacked.rglob("*") if p.is_file())
        self.assertEqual(listed, sorted([*INSTALLED, "manifest.json"]))
        inner = json.loads((unpacked / "manifest.json").read_text())
        self.assertEqual(inner, {k: v for k, v in manifest.items() if k != "hostOnly"})
        again = self.root / "again"
        shutil.copytree(self.bundle, again)
        bundle.add_image(again)
        self.assertEqual((again / bundle.IMAGE).read_bytes(), image, "the image is reproducible")
        tree = self.root / "tree"
        bundle.write_release(self.bundle, tree)
        self.assertNotIn("image", json.loads((tree / "manifest.json").read_text()))
        with self.assertRaisesRegex(bundle.BundleError, "is a bundle"):
            bundle.release_image(self.bundle, self.root / "bundle.img")

    @unittest.skipUnless(HAVE_MKSQUASHFS, "needs mksquashfs 4.6 and unsquashfs")
    def test_the_image_must_match_the_manifest(self):
        self.write(counter=COUNTER)
        manifest = bundle.add_image(self.bundle)
        image = self.bundle / bundle.IMAGE
        data = image.read_bytes()
        image.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        with self.assertRaisesRegex(bundle.BundleError, f"{bundle.IMAGE} differs"):
            bundle.verify(self.bundle)
        image.unlink()
        with self.assertRaisesRegex(bundle.BundleError, f"lacks {bundle.IMAGE}"):
            bundle.verify(self.bundle)
        image.write_bytes(data)
        bundle.verify(self.bundle)
        for bad in ({"size": 0, "sha256": manifest["image"]["sha256"]}, {"size": len(data)},
                    dict(manifest["image"], extra=1), "release.img"):
            (self.bundle / "manifest.json").write_text(json.dumps(dict(manifest, image=bad)))
            with self.assertRaisesRegex(bundle.BundleError, "invalid image entry"):
                bundle.verify(self.bundle)
        self.write(counter=COUNTER)
        with self.assertRaisesRegex(bundle.BundleError, f"does not list {bundle.IMAGE}"):
            bundle.verify(self.bundle)
        image.unlink()
        self.write()
        with self.assertRaisesRegex(bundle.BundleError, "32-bit"):
            bundle.add_image(self.bundle)
        self.assertFalse(image.exists())

    def test_tampering_is_detected(self):
        self.write()
        (self.bundle / "bin/awtrix-linux").write_bytes(b"other")
        with self.assertRaises(bundle.BundleError):
            bundle.verify(self.bundle)
        self.write()
        (self.bundle / "bin/extra").write_bytes(b"x")
        with self.assertRaisesRegex(bundle.BundleError, "bin/extra"):
            bundle.verify(self.bundle)
        (self.bundle / "bin/extra").unlink()
        (self.bundle / "lib/libawtrix-loader.so").unlink()
        with self.assertRaisesRegex(bundle.BundleError, "libawtrix-loader"):
            bundle.verify(self.bundle)
        self.write()
        original = json.loads((self.bundle / "manifest.json").read_text())
        for change in ({"release": "1.1.2-gabc1234-000000000000"}, {"counter": 0},
                       {"counter": True}, {"schema_version": 2}):
            (self.bundle / "manifest.json").write_text(json.dumps({**original, **change}))
            with self.assertRaises(bundle.BundleError, msg=str(change)):
                bundle.verify(self.bundle)
        broken = json.loads(json.dumps(original))
        broken["files"][0]["size"] += 1
        (self.bundle / "manifest.json").write_text(json.dumps(broken))
        with self.assertRaisesRegex(bundle.BundleError, "differs from manifest.json"):
            bundle.verify(self.bundle)
        broken = json.loads(json.dumps(original))
        broken["files"][0]["jffs2"] = 100
        (self.bundle / "manifest.json").write_text(json.dumps(broken))
        with self.assertRaisesRegex(bundle.BundleError, "invalid manifest entry"):
            bundle.verify(self.bundle)
        broken = json.loads(json.dumps(original))
        broken["hostOnly"].append(broken["files"][0])
        (self.bundle / "manifest.json").write_text(json.dumps(broken))
        with self.assertRaisesRegex(bundle.BundleError, "twice"):
            bundle.verify(self.bundle)

    def test_rejects_incomplete_or_odd_bundles(self):
        for path in bundle.REQUIRED:
            if "*" in path:
                continue
            data = (self.bundle / path).read_bytes()
            (self.bundle / path).unlink()
            with self.assertRaisesRegex(bundle.BundleError, f"lacks {re.escape(path)}$"):
                self.write()
            (self.bundle / path).write_bytes(data)
        self.write()
        (self.bundle / "share/boot.mp3").unlink()
        self.assertNotIn("share/boot.mp3", [e["path"] for e in self.write()["files"]])
        (self.bundle / "etc").mkdir()
        (self.bundle / "etc/x").write_bytes(b"x")
        with self.assertRaisesRegex(bundle.BundleError, "unexpected entry"):
            self.write()
        shutil.rmtree(self.bundle / "etc")
        (self.bundle / "bin/bad name").write_bytes(b"x")
        with self.assertRaisesRegex(bundle.BundleError, "file name"):
            self.write()
        (self.bundle / "bin/bad name").unlink()
        with self.assertRaisesRegex(bundle.BundleError, "version"):
            bundle.write_manifest(self.bundle, "1.1.2; rm", "abc1234", False)
        for counter in (0, -1, 1 << 63, "1790345998", True):
            with self.assertRaisesRegex(bundle.BundleError, "counter"):
                self.write(counter=counter)

    @unittest.skipIf(os.name == "nt", "the stand-in compilers are shell scripts")
    def test_records_the_compilers(self):
        def compiler(name, called_as, configure):
            path = self.root / name
            path.write_text("#!/bin/sh\ncat >&2 <<'EOF'\nUsing built-in specs.\n"
                            f"COLLECT_GCC={called_as}\nTarget: arm-test-linux-musleabihf\n"
                            f"Configured with: {configure}\nThread model: posix\n"
                            "gcc version 14.4.0 (Test) \nEOF\n")
            path.chmod(0o755)
            return path
        one = bundle.compiler_identity(compiler("one", "/a/bin/gcc", "./configure --prefix=/a"))
        self.assertEqual(one["version"], "14.4.0 (Test)")
        self.assertEqual(bundle.compiler_identity(compiler("two", "/b/gcc", "./configure --prefix=/a")), one)
        self.assertNotEqual(bundle.compiler_identity(compiler("three", "/a/bin/gcc", "./configure"))["sha256"],
                            one["sha256"])
        silent = self.root / "silent"
        silent.write_text("#!/bin/sh\nexit 0\n")
        silent.chmod(0o755)
        with self.assertRaisesRegex(bundle.BundleError, "does not describe a GCC"):
            bundle.compiler_identity(silent)

        with redirect_stdout(io.StringIO()):
            self.assertEqual(bundle.main(["manifest", str(self.bundle), "--version", VERSION, "--commit",
                                          "abc1234", "--compiler", f"musl={self.root / 'one'}"]), 0)
        manifest = bundle.verify(self.bundle)
        self.assertEqual(manifest["compilers"], {"musl": one})
        self.assertNotIn("compilers", self.write())
        for bad in ({"musl": {"version": "14"}}, {"musl": {"version": "", "sha256": "0" * 64}},
                    {"bad name": one}, {"musl": dict(one, extra=1)}, ["musl"]):
            with self.assertRaisesRegex(bundle.BundleError, "compilers"):
                self.write(compilers=bad)
        with redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(bundle.main(["manifest", str(self.bundle), "--version", VERSION, "--commit",
                                          "abc1234", "--compiler", "musl"]), 1)

    def test_only_the_current_schema_verifies(self):
        manifest = self.write()
        for schema in (1, 2, None):
            (self.bundle / "manifest.json").write_text(json.dumps(dict(manifest, schema_version=schema)))
            with self.assertRaisesRegex(bundle.BundleError, "unsupported bundle manifest schema"):
                bundle.verify(self.bundle)
        (self.bundle / "manifest.json").write_text(json.dumps(manifest))
        self.assertEqual(bundle.verify(self.bundle), manifest)
        first = dict(manifest["files"][0])
        del first["mode"]
        (self.bundle / "manifest.json").write_text(
            json.dumps(dict(manifest, files=[first] + manifest["files"][1:])))
        with self.assertRaisesRegex(bundle.BundleError, "invalid manifest entry"):
            bundle.verify(self.bundle)

    def test_release_pulled_from_a_device_keeps_its_name(self):
        tree = self.root / "pulled"
        for path in ("bin/awtrix-linux", "bin/awtrix-tc002-flash", "share/index.html"):
            (tree / path).parent.mkdir(parents=True, exist_ok=True)
            (tree / path).write_bytes(path.encode())
        files = [{key: e[key] for key in ("path", "size", "sha256", "mode")}
                 for e in bundle.collect(tree)]
        manifest = bundle.write_manifest(tree, VERSION, "unknown", False, host_only=(),
                                         required=("bin/awtrix-linux",))
        self.assertEqual(manifest["release"], bundle.release_name(VERSION, files))
        self.assertEqual(manifest["hostOnly"], [])
        self.assertEqual(bundle.verify(tree), manifest)

    def test_command_line(self):
        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(bundle.main(["manifest", str(self.bundle), "--version", VERSION,
                                          "--commit", "abc1234", "--counter", "1790345998"]), 0)
            self.assertEqual(bundle.main(["verify", str(self.bundle)]), 0)
            self.assertEqual(bundle.main(["release", str(self.bundle), str(self.root / "tree")]), 0)
        self.assertIn("host-only lib/libawtrix-loader.so, lib/modules/loop.ko", output.getvalue())
        self.assertTrue((self.root / "tree/manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
