#!/usr/bin/env python3
import contextlib
import hashlib
import io
import json
import os
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import bundle
import source_package as package


class SourcePackageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.binary = self.root / "bundle"
        self.binary.mkdir()
        self.commit = "1" * 40
        for name in (*bundle.REQUIRED, *bundle.HOST_ONLY, "share/boot.mp3"):
            path = self.binary / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        self.manifest = bundle.write_manifest(self.binary, "1.0", self.commit[:12], False, counter=123)
        self.path = self.binary / "manifest.json"

    def git(self, source, *arguments):
        if arguments[0] == "cat-file":
            return "commit"
        if arguments[:3] == ("show", "-s", "--format=%ct"):
            return "123"
        if arguments[0] == "rev-parse":
            return package.AIC_TREE
        if arguments[0] == "ls-tree":
            return "100644 blob " + "0" * 40 + "\tsource.c"
        return "\n".join((package.KERNEL_COMMIT, package.AIC_COMMIT, package.AIC_TREE, *package.PINS.values()))

    def verify(self):
        with mock.patch.object(package, "git", side_effect=self.git):
            return package.verify_binary(self.root, self.commit, self.path)

    def test_binary_binding_and_payload(self):
        self.assertEqual(self.verify(), self.path.read_bytes())
        for field, value in (("dirty", True), ("commit", "2" * 12), ("counter", 124), ("counter", True)):
            with self.subTest(field=field, value=value):
                changed = {**self.manifest, field: value}
                self.path.write_text(json.dumps(changed), encoding="utf-8")
                with self.assertRaises(package.SourceError):
                    self.verify()
        self.path.write_text(json.dumps(self.manifest), encoding="utf-8")
        (self.binary / "bin/udhcpc").write_bytes(b"modified")
        with self.assertRaises(bundle.BundleError):
            self.verify()

    def test_commit_and_duplicate_json_validation(self):
        for value in ("HEAD", "1" * 12, "../commit", None):
            with self.subTest(value=value), self.assertRaises(package.SourceError):
                package.verify_binary(self.root, value, self.path)
        self.path.write_text('{"dirty":true,"dirty":false}', encoding="utf-8")
        with self.assertRaises(package.SourceError):
            self.verify()

    def inputs(self):
        downloads = self.root / "downloads"
        downloads.mkdir()
        recipe = self.root / "musl"
        recipe.mkdir()
        (downloads / "source.tar.gz").write_bytes(b"pinned source")
        (recipe / "0003-fix.patch").write_bytes(b"pinned patch")
        return {"repo": self.root, "commit": self.commit, "bundle_manifest": self.path,
                "downloads": downloads, "kernel_source": self.root, "aic_source": self.root,
                "musl_buildroot_recipe": recipe}

    def test_preflight_enforces_cache_and_recipe_pins(self):
        inputs = self.inputs()
        pins = {"source.tar.gz": package.digest(inputs["downloads"] / "source.tar.gz")}
        recipe_pins = {"0003-fix.patch": package.digest(inputs["musl_buildroot_recipe"] / "0003-fix.patch")}
        with mock.patch.object(package, "PINS", pins), mock.patch.object(package, "MUSL_RECIPE_PINS", recipe_pins), \
                mock.patch.object(package, "git", side_effect=self.git):
            self.assertEqual(package.preflight(**inputs), self.path.read_bytes())
            (inputs["musl_buildroot_recipe"] / "private.txt").write_text("not a source input")
            with self.assertRaisesRegex(package.SourceError, "file set"):
                package.preflight(**inputs)
            (inputs["musl_buildroot_recipe"] / "private.txt").unlink()
            (inputs["musl_buildroot_recipe"] / "0003-fix.patch").write_bytes(b"wrong patch")
            with self.assertRaisesRegex(package.SourceError, "recipe hash"):
                package.preflight(**inputs)
            (inputs["downloads"] / "source.tar.gz").write_bytes(b"wrong source")
            with self.assertRaisesRegex(package.SourceError, "source hash"):
                package.preflight(**inputs)

    def test_gitlink_is_not_silently_omitted(self):
        inputs = self.inputs()
        def gitlink(source, *arguments):
            return "160000 commit " + "0" * 40 + "\tdependency" if arguments[0] == "ls-tree" else self.git(source, *arguments)
        with mock.patch.object(package, "git", side_effect=gitlink), self.assertRaisesRegex(package.SourceError, "submodule"):
            package.preflight(**inputs)

    def test_archive_is_deterministic_and_normalized(self):
        source = self.root / "source"
        source.mkdir()
        (source / "nested").mkdir()
        path = source / "nested/file.txt"
        path.write_bytes(b"source")
        first, second = self.root / "first.tar.gz", self.root / "second.tar.gz"
        package.deterministic_archive(source, first)
        os.utime(path, (100, 100))
        path.chmod(0o777)
        package.deterministic_archive(source, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with tarfile.open(first) as archive:
            entry = archive.getmember("tc002-sources/nested/file.txt")
            self.assertEqual((entry.uid, entry.gid, entry.mtime, entry.mode), (0, 0, 0, 0o644))
        with self.assertRaises(FileExistsError):
            package.deterministic_archive(source, first)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_git_tree_archive_timestamps_are_normalized(self):
        outputs = []
        for timestamp in (100, 200):
            raw = io.BytesIO()
            with tarfile.open(fileobj=raw, mode="w") as archive:
                entry = tarfile.TarInfo("source/build.sh")
                entry.mode = 0o755
                entry.mtime = timestamp
                entry.size = 4
                archive.addfile(entry, io.BytesIO(b"code"))
            process = mock.Mock(stdout=io.BytesIO(raw.getvalue()))
            process.wait.return_value = process.poll.return_value = 0
            output = self.root / (str(timestamp) + ".tar.gz")
            with mock.patch.object(package.subprocess, "Popen", return_value=process):
                package.git_archive(self.root, self.commit, output, "source/")
            outputs.append(output.read_bytes())
            with tarfile.open(output) as archive:
                entry = archive.getmember("source/build.sh")
                self.assertEqual((entry.mode, entry.mtime), (0o755, 0))
        self.assertEqual(outputs[0], outputs[1])

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_staging_symlink_is_rejected_and_partial_output_removed(self):
        source = self.root / "source"
        source.mkdir()
        (source / "linked").symlink_to(self.path)
        output = self.root / "bad.tar.gz"
        with self.assertRaises(package.SourceError):
            package.deterministic_archive(source, output)
        self.assertFalse(output.exists())

    def test_create_binds_manifest_and_driver_selection(self):
        inputs = self.inputs()
        for archive_name, member in (("libnl-3.11.0.tar.gz", "libnl-3.11.0/COPYING"),
                                     ("wpa_supplicant-2.12.tar.gz", "wpa_supplicant-2.12/COPYING")):
            with tarfile.open(inputs["downloads"] / archive_name, "w:gz") as archive:
                entry = tarfile.TarInfo(member)
                entry.size = 7
                archive.addfile(entry, io.BytesIO(b"license"))
        pins = {path.name: package.digest(path) for path in inputs["downloads"].iterdir()}
        recipe_pins = {"0003-fix.patch": package.digest(inputs["musl_buildroot_recipe"] / "0003-fix.patch")}
        exported = []
        def archive(source, revision, target, prefix, paths=()):
            exported.append((revision, prefix, paths))
            Path(target).write_bytes((revision + prefix).encode())
        output = self.root / "tc002-sources.tar.gz"
        with mock.patch.object(package, "PINS", pins), mock.patch.object(package, "MUSL_RECIPE_PINS", recipe_pins), \
                mock.patch.object(package, "git", side_effect=self.git), mock.patch.object(package, "git_archive", side_effect=archive):
            result = package.create(output, **inputs)
        self.assertEqual(result["sha256"], package.digest(output))
        self.assertIn((package.AIC_COMMIT + ":" + package.AIC_PATH, "aic8800/", package.AIC_PARTS), exported)
        self.assertIn((package.KERNEL_COMMIT, "linux-chenxing/", ()), exported)
        with tarfile.open(output) as archive:
            manifest = json.load(archive.extractfile("tc002-sources/manifest.json"))
            original = archive.extractfile("tc002-sources/binary-manifest.json").read()
            self.assertEqual(manifest["binary_manifest_sha256"], hashlib.sha256(original).hexdigest())
            self.assertEqual(manifest["application_commit"], self.commit)
            self.assertFalse(manifest["tc002_stock_firmware_included"])
            self.assertTrue(manifest["upstream_kernel_firmware_tree_retained"])
            self.assertNotIn("radio_firmware_included", manifest)
            self.assertNotIn(str(self.root), json.dumps(manifest))
        self.assertFalse(list(self.root.glob(".tc002-sources-*")))

    def test_cli_requires_explicit_inputs(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
            package.main([])
        self.assertEqual(raised.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
