import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
import zipfile

import browser_package as browser
import bundle

COUNTER = 1790000000


def write_bundle(root, version="1.2.0-gabcdef123456", commit="abcdef123456", counter=COUNTER):
    """manifest.json and a stand-in release image, as build_bundle.sh leaves them."""
    (root / bundle.IMAGE).unlink(missing_ok=True)
    manifest = bundle.write_manifest(root, version, commit, False, counter=counter)
    image = bytearray(bytes(range(256)) * 40)
    image[:4] = b"hsqs"
    image[40:48] = len(image).to_bytes(8, "little")
    (root / bundle.IMAGE).write_bytes(image)
    manifest["image"] = {"size": len(image), "sha256": hashlib.sha256(image).hexdigest()}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return bundle.verify(root)


class BrowserPackageTests(unittest.TestCase):
    def test_bench_prepare_helper_is_not_a_public_bundle_file(self):
        name = "bin/awtrix-tc002-mcu-prepare"
        with self.assertRaisesRegex(bundle.BundleError, "public file allowlist"):
            bundle.public_files(set(browser.PUBLIC_FILES) | {name})
        (self.bundle / name).write_bytes(b"bench helper")
        with self.assertRaisesRegex(browser.PackageError, "not a public bundle file"):
            self.create()

    def test_previous_release_without_mcu_remains_supported(self):
        for name in bundle.MCU_PATCH:
            (self.bundle / name).unlink()
        self.manifest = write_bundle(self.bundle)
        self.assertEqual(self.create()["release"], self.manifest["release"])

    def test_release_without_voice_remains_supported(self):
        (self.bundle / bundle.SPEECH_VOICE).unlink()
        self.manifest = write_bundle(self.bundle)
        self.assertEqual(self.create()["release"], self.manifest["release"])

    def test_the_voice_is_the_only_file_in_its_directory(self):
        (self.bundle / "share/speech/other.atts").write_bytes(b"another voice")
        with self.assertRaisesRegex(browser.PackageError, "not a public bundle file"):
            self.create()

    def test_incomplete_mcu_patch_is_refused(self):
        (self.bundle / "share/mcu/extension.bin").unlink()
        self.manifest = write_bundle(self.bundle)
        with self.assertRaisesRegex(browser.PackageError, "incomplete MCU"):
            self.create()

    def test_combined_vendor_image_cannot_enter_bundle(self):
        (self.bundle / "share/mcu/firmware.pot").write_bytes(b"vendor image")
        with self.assertRaisesRegex(bundle.BundleError, "manufacturer MCU"):
            bundle.write_manifest(self.bundle, "1.2.0-gabcdef123456", "abcdef123456", False, counter=1790000000)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = self.root / "bundle"
        for name in browser.PUBLIC_FILES - {bundle.IMAGE}:
            path = self.bundle / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((name + "\n").encode() * 3)
        self.manifest = write_bundle(self.bundle)
        self.license = self.root / "LICENSE.md"
        self.license.write_text("Test licence\n", encoding="utf-8")
        self.notices = self.root / "THIRD-PARTY-NOTICES.md"
        self.notices.write_text("Test notices\n", encoding="utf-8")
        self.output = self.root / "browser.zip"

    def create(self, output=None, **options):
        return browser.create(self.bundle, output or self.output, license_path=self.license,
                              notices_path=self.notices, source_url="https://example.invalid/sources.tar.gz",
                              source_sha256="a" * 64, **options)

    def rewrite(self):
        (self.bundle / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_complete_deterministic_stored_archive(self):
        first = self.create()
        second = self.root / "second.zip"
        for path in self.bundle.rglob("*"):
            os.utime(path, (1000000000, 1000000000))
        self.create(second)
        self.assertEqual(self.output.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.namelist()[0], browser.MANIFEST)
            self.assertEqual(set(archive.namelist()), {browser.MANIFEST} | {e["path"] for e in first["files"]})
            for entry in first["files"]:
                info = archive.getinfo(entry["path"])
                data = archive.read(info)
                self.assertEqual((len(data), hashlib.sha256(data).hexdigest()), (entry["size"], entry["sha256"]))
                self.assertEqual(info.compress_type, zipfile.ZIP_STORED)
                self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                self.assertEqual(info.external_attr >> 16, stat.S_IFREG | int(entry["mode"], 8))
            self.assertEqual(archive.read("bundle/manifest.json"), (self.bundle / "manifest.json").read_bytes())
            self.assertEqual(sorted(archive.namelist()), sorted([
                browser.MANIFEST, "bundle/manifest.json", "bundle/release.img", "bundle/bin/awtrix-tc002-flash",
                "bundle/lib/libawtrix-loader.so", "bundle/lib/modules/loop.ko", "notices/LICENSE.md",
                "notices/THIRD-PARTY-NOTICES.md", "notices/SOURCE-NOTICE.txt"]))
            self.assertEqual(archive.read("bundle/release.img"), (self.bundle / bundle.IMAGE).read_bytes())
            self.assertEqual(first["image"], {"path": "bundle/release.img", **self.manifest["image"]})
            self.assertEqual((first["schemaVersion"], first["flashHelper"], first["loader"], first["loop"]),
                             (2, "bundle/bin/awtrix-tc002-flash", "bundle/lib/libawtrix-loader.so",
                              "bundle/lib/modules/loop.ko"))
            roles = {e["path"]: e["role"] for e in first["files"]}
            self.assertEqual((roles["bundle/release.img"], roles["bundle/bin/awtrix-tc002-flash"],
                              roles["bundle/lib/modules/loop.ko"]), ("image", "helper", "host"))
            self.assertIn("https://example.invalid/sources.tar.gz", archive.read("notices/SOURCE-NOTICE.txt").decode())

    def test_large_counter_is_a_decimal_string(self):
        self.manifest["counter"] = bundle.MAX_COUNTER
        self.rewrite()
        self.assertEqual(self.create()["counter"], str(bundle.MAX_COUNTER))

    def test_dirty_is_explicit_and_source_commit_required(self):
        self.manifest["dirty"] = True
        self.rewrite()
        with self.assertRaisesRegex(browser.PackageError, "dirty"):
            self.create()
        self.assertTrue(self.create(allow_dirty=True)["dirty"])
        self.manifest["commit"] = "unknown"
        self.rewrite()
        with self.assertRaisesRegex(browser.PackageError, "source commit"):
            self.create(self.root / "unknown.zip", allow_dirty=True)

    def test_missing_or_changed_files_are_refused(self):
        path = self.bundle / "bin/awtrix-tc002-flash"
        path.write_bytes(b"different")
        with self.assertRaises(bundle.BundleError):
            self.create()
        path.unlink()
        with self.assertRaises(bundle.BundleError):
            self.create()
        self.assertFalse(self.output.exists())

    def test_missing_required_helper_cannot_be_hidden_by_a_new_manifest(self):
        (self.bundle / "lib/libawtrix-loader.so").unlink()
        write_bundle(self.bundle, "1.2.0", counter=1)
        with self.assertRaisesRegex(browser.PackageError, "both host helpers"):
            self.create()

    def test_a_bundle_without_release_image_is_refused(self):
        (self.bundle / bundle.IMAGE).unlink()
        bundle.write_manifest(self.bundle, "1.2.0-gabcdef123456", "abcdef123456", False, counter=COUNTER)
        with self.assertRaisesRegex(browser.PackageError, "manifest members"):
            self.create()
        (self.bundle / bundle.IMAGE).write_bytes(b"hsqs")
        with self.assertRaises((browser.PackageError, bundle.BundleError)):
            self.create()
        self.assertFalse(self.output.exists())

    def test_unexpected_files_and_symlinks_are_refused_before_reading(self):
        private = self.bundle / "share/settings.ini"
        private.write_bytes(b"not a public bundle input")
        with self.assertRaisesRegex(browser.PackageError, "not a public bundle file"):
            self.create()
        private.unlink()
        if os.name != "posix":
            return
        path = self.bundle / "bin/awtrix-linux"
        path.unlink()
        path.symlink_to(self.license)
        with self.assertRaisesRegex(browser.PackageError, "not a public bundle file"):
            self.create()

    def test_invalid_metadata_and_duplicate_json_members_are_refused(self):
        self.manifest["secret"] = "unexpected metadata"
        self.rewrite()
        with self.assertRaisesRegex(browser.PackageError, "manifest members"):
            self.create()
        del self.manifest["secret"]
        self.rewrite()
        path = self.bundle / "manifest.json"
        path.write_bytes(path.read_bytes().replace(b'"counter": 1790000000', b'"counter": 1, "counter": 1790000000'))
        with self.assertRaisesRegex(browser.PackageError, "duplicate JSON"):
            self.create()

    def test_size_count_and_output_limits(self):
        with mock.patch.object(browser, "MAX_FILE_BYTES", 10):
            with self.assertRaisesRegex(browser.PackageError, "size outside"):
                self.create()
        with mock.patch.object(browser, "MAX_ARCHIVE_BYTES", 10):
            with self.assertRaisesRegex(browser.PackageError, "32 MiB"):
                self.create()
        with mock.patch.object(browser, "MAX_FILES", 2):
            with self.assertRaisesRegex(browser.PackageError, "too many files"):
                self.create()
        self.output.write_bytes(b"existing")
        with self.assertRaises(FileExistsError):
            self.create()
        self.assertEqual(self.output.read_bytes(), b"existing")

    def test_invalid_identity_types_and_unsorted_manifest_are_refused(self):
        self.manifest["version"] = 42
        self.rewrite()
        with self.assertRaisesRegex(browser.PackageError, "release identity"):
            self.create()
        self.manifest["version"] = "1.2.0-gabcdef123456"
        self.manifest["files"].reverse()
        self.manifest["release"] = bundle.release_name(self.manifest["version"], self.manifest["files"])
        self.rewrite()
        with self.assertRaisesRegex(browser.PackageError, "must be sorted"):
            self.create()

    def test_write_failure_removes_only_new_output(self):
        with mock.patch.object(browser.os, "fsync", side_effect=OSError("fixture failure")):
            with self.assertRaisesRegex(OSError, "fixture failure"):
                self.create()
        self.assertFalse(self.output.exists())

    def test_sources_reference_is_explicit_https_without_tokens(self):
        for url in ("http://example.invalid/src", "https://user:secret@example.invalid/src",
                    "https://example.invalid/src?token=secret", "https://example.invalid/src#part",
                    "https://example.invalid/\nsecret", "https://example.invalid\\src", "file:///src"):
            with self.subTest(url=url), self.assertRaises(browser.PackageError):
                browser.source_reference(url, "a" * 64)
        with self.assertRaises(browser.PackageError):
            browser.source_reference("https://example.invalid/src", "invalid")


if __name__ == "__main__":
    unittest.main()
