"""Offline contracts for the pinned ARM build helper (Python 3.12+).

Run with ``python3 tools/system/test_build.py``. No downloads, toolchain builds,
device access, or changes to the application's Git repository are performed.
"""
from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location("awtrix_system_build", Path(__file__).with_name("build.py"))
assert SPEC and SPEC.loader
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class BuildContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-build-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        # Any accidentally unmocked network access is a failure, including in
        # negative tests that should reject a cached artifact before downloading.
        self.network = mock.patch.object(build.urllib.request, "urlopen", side_effect=AssertionError("unexpected network access"))
        self.network.start()
        self.addCleanup(self.network.stop)

    def put(self, name, data=b"fixture"):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def license_fixture(self):
        files = {"LICENSE.md": b"license\n", "THIRD-PARTY-NOTICES.md": "Copyright – Crème ©\nUTF-8 only.\n".encode("utf-8"),
                 "LICENSES/MIT-fixture.txt": b"MIT\n"}
        for name, data in files.items():
            self.put("application/" + name, data)
        self.put("external/package/awtrix-ng/awtrix-ng.mk",
                 b"AWTRIX_NG_LICENSE_FILES = LICENSE.md THIRD-PARTY-NOTICES.md \\\n\tLICENSES/MIT-fixture.txt\n")
        self.put("external/package/awtrix-ng/awtrix-ng.hash",
                 ("# Exact exported bytes\n" + "".join("sha256  " + hashlib.sha256(data).hexdigest() + "  " + name + "\n"
                                                        for name, data in files.items())).encode("utf-8"))
        return files

    @staticmethod
    def source(data=b"archive"):
        return {"version": "test", "url": "https://example.invalid/source.tar.xz",
                "sha256": hashlib.sha256(data).hexdigest()}

    def archive(self, entries):
        """Entries: (name, bytes), (name, kind, target), or (name, kind)."""
        path = self.root / "source.tar"
        with tarfile.open(path, "w") as stream:
            for entry in entries:
                member = tarfile.TarInfo(entry[0])
                member.mode = 0o755 if entry[1] == tarfile.DIRTYPE else 0o644
                if len(entry) == 2 and entry[1] not in (tarfile.DIRTYPE, tarfile.CHRTYPE):
                    data = entry[1]
                    member.size = len(data)
                    stream.addfile(member, io.BytesIO(data))
                else:
                    member.type = entry[1]
                    if len(entry) == 3:
                        member.linkname = entry[2]
                    stream.addfile(member)
        return path

    def assert_archive_rejected(self, entries, *, allow_absolute_links=False):
        archive = self.archive(entries)
        destination = self.root / "extracted"
        with self.assertRaises((build.BuildError, tarfile.TarError)):
            build.extract_archive(archive, destination, "application", allow_absolute_links=allow_absolute_links)
        self.assertFalse(destination.exists(), "failed extraction must not publish partial sources")
        self.assertEqual([], list(self.root.glob(".extract-*")), "temporary extraction must be cleaned up")

    def require_symlinks(self):
        probe = self.root / "symlink-probe"
        try:
            probe.symlink_to("not-present")
        except OSError as error:
            self.skipTest(f"platform does not permit symlinks: {error}")
        probe.unlink()

    def frozen_inputs(self):
        self.license_fixture()
        self.put("application/src/main.cpp", b"committed application")
        self.put("external/configs/awtrix_armv7_defconfig", b"pinned recipe")
        self.put("buildroot/Makefile", b"pinned Buildroot makefile")
        archive = self.put("application.tar", b"committed archive")
        source = self.source()
        self.put("downloads/buildroot-test.tar.xz", b"archive")
        inputs = {"schema_version": 1, "profile": "awtrix_armv7_defconfig",
                  "application_commit": "a" * 40, "source_date_epoch": 1700000000,
                  "application_archive_sha256": build.digest(archive),
                  "source_lock": {"schema_version": 1, "buildroot": source, "linux_headers": self.source(b"headers")},
                  "application_files": build.tree_hashes(self.root / "application"),
                  "external_files": build.tree_hashes(self.root / "external"),
                  "buildroot_files": build.tree_hashes(self.root / "buildroot", allow_links=True),
                  "tc002_hardware_verified": False, "bootable_tc002_image": False}
        build.write_json(self.root / "inputs.json", inputs)
        return inputs

    @unittest.skipUnless(os.name == "posix", "post-build hook requires a POSIX shell")
    def test_rootfs_post_build_removes_only_libstdcxx_debug_helpers(self):
        hook = build.EXTERNAL / "scripts/post-build.sh"
        config = (build.EXTERNAL / "configs/awtrix_armv7_defconfig").read_text(encoding="utf-8")
        self.assertIn('BR2_ROOTFS_POST_BUILD_SCRIPT="$(BR2_EXTERNAL_AWTRIX_PATH)/scripts/post-build.sh"', config)
        target = self.root / "target with spaces"
        helper = self.put("target with spaces/usr/lib/libstdc++.so.6.0.33-gdb.py")
        foreign = self.put("foreign.py", b"preserved")
        linked = target / "usr/lib/libstdc++.so.7-gdb.py"
        linked.symlink_to(foreign)
        retained = [self.put("target with spaces/usr/lib/" + name)
                    for name in ("libstdc++.so.6", "libother.so.1-gdb.py", "other.py")]
        for _ in range(2):
            subprocess.run(["sh", str(hook), str(target)], check=True)
            self.assertFalse(helper.exists())
            self.assertFalse(linked.is_symlink())
            self.assertEqual(b"preserved", foreign.read_bytes())
            self.assertTrue(all(path.is_file() for path in retained))
        self.assertNotEqual(0, subprocess.run(["sh", str(hook)], capture_output=True).returncode)

    def test_recipe_license_files_parses_literal_list_and_rejects_variants(self):
        self.license_fixture()
        package = self.root / "external/package/awtrix-ng"
        self.assertEqual(["LICENSE.md", "THIRD-PARTY-NOTICES.md", "LICENSES/MIT-fixture.txt"],
                         build.recipe_license_files(package))
        recipe = package / "awtrix-ng.mk"
        original = recipe.read_bytes()
        for invalid, message in ((original + b"AWTRIX_NG_LICENSE_FILES += unlisted.txt\n", "one literal"),
                                 (original + b"AWTRIX_NG_LICENSE_FILES = other.txt\n", "one literal"),
                                 (b"AWTRIX_NG_LICENSE_FILES := LICENSE.md\n", "one literal"),
                                 (b"AWTRIX_NG_LICENSE_FILES = LICENSE.md LICENSE.md\n", "empty or duplicate"),
                                 (b"AWTRIX_NG_LICENSE_FILES =\n", "empty or duplicate")):
            with self.subTest(recipe=invalid):
                recipe.write_bytes(invalid)
                with self.assertRaisesRegex(build.BuildError, message):
                    build.recipe_license_files(package)
        recipe.unlink()
        with self.assertRaises(OSError):
            build.recipe_license_files(package)

    def test_write_json_never_writes_through_an_existing_partial(self):
        target = self.root / "result.json"
        foreign = self.put("foreign.txt", b"foreign content")
        partial = self.root / "result.json.partial"
        links = [("hard link", lambda: os.link(foreign, partial))]
        if hasattr(os, "symlink") and os.name == "posix":
            links.append(("symlink", lambda: os.symlink(foreign, partial)))
        for kind, make in links:
            with self.subTest(kind=kind):
                make()
                with self.assertRaises(FileExistsError):
                    build.write_json(target, {"status": "passed"})
                self.assertEqual(b"foreign content", foreign.read_bytes())
                self.assertFalse(target.exists())
                partial.unlink()
        build.write_json(target, {"status": "passed"})
        build.write_json(target, {"status": "failed"})
        self.assertEqual({"status": "failed"}, json.loads(target.read_text(encoding="utf-8")))
        self.assertFalse(partial.exists())
        with mock.patch.object(build.json, "dump", side_effect=ValueError("not serializable")):
            with self.assertRaises(ValueError):
                build.write_json(target, {"status": "passed"})
        self.assertFalse(partial.exists())
        self.assertEqual({"status": "failed"}, json.loads(target.read_text(encoding="utf-8")))

    def test_json_file_rejects_duplicate_keys_and_non_objects(self):
        self.assertEqual({"schema_version": 1, "nested": {"key": [1, 2]}},
                         build.json_file(self.put("valid.json", b'{"schema_version": 1, "nested": {"key": [1, 2]}}')))
        with self.assertRaisesRegex(build.BuildError, "duplicate JSON key: schema_version"):
            build.json_file(self.put("duplicate.json", b'{"schema_version":1,"schema_version":2}'))
        with self.assertRaisesRegex(build.BuildError, "duplicate JSON key: inner"):
            build.json_file(self.put("nested.json", b'{"outer":{"inner":1,"inner":2}}'))
        for data in (b"[1, 2]", b'"text"', b"null"):
            with self.subTest(data=data):
                with self.assertRaisesRegex(build.BuildError, "expected JSON object: list.json"):
                    build.json_file(self.put("list.json", data))
        self.assertEqual({"a": 1}, build.json_bytes(b'{"a": 1}', "record.json"))
        with self.assertRaisesRegex(build.BuildError, "duplicate JSON key: a"):
            build.json_bytes(b'{"a": 1, "a": 1}', "record.json")
        with self.assertRaisesRegex(build.BuildError, "expected JSON object: record.json"):
            build.json_bytes(b"[]", "record.json")

    def test_license_preflight_uses_exact_utf8_lf_bytes(self):
        files = self.license_fixture()
        build.verify_application_licenses(self.root / "application", self.root / "external")
        name = "THIRD-PARTY-NOTICES.md"
        self.put("application/" + name, files[name].replace(b"\n", b"\r\n"))
        with self.assertRaisesRegex(build.BuildError, "checksum mismatch: THIRD-PARTY-NOTICES.md"):
            build.verify_application_licenses(self.root / "application", self.root / "external")

    def test_license_preflight_rejects_hash_of_wrongly_decoded_unicode(self):
        files = self.license_fixture()
        name = "THIRD-PARTY-NOTICES.md"
        original = hashlib.sha256(files[name]).hexdigest()
        wrong = hashlib.sha256(files[name].decode("cp1252").encode("utf-8")).hexdigest()
        hashes = self.root / "external/package/awtrix-ng/awtrix-ng.hash"
        hashes.write_bytes(hashes.read_bytes().replace(original.encode(), wrong.encode()))
        with self.assertRaisesRegex(build.BuildError, "checksum mismatch: THIRD-PARTY-NOTICES.md"):
            build.verify_application_licenses(self.root / "application", self.root / "external")

    def test_license_preflight_requires_complete_unique_hash_list(self):
        self.license_fixture()
        hashes = self.root / "external/package/awtrix-ng/awtrix-ng.hash"
        original = hashes.read_bytes()
        for invalid in (b"", original.splitlines(keepends=True)[1] * 2,
                        original.replace(b"sha256", b"sha512"),
                        original.replace(b"LICENSE.md", b"unlisted.txt")):
            with self.subTest(hashes=invalid):
                hashes.write_bytes(invalid)
                with self.assertRaises(build.BuildError):
                    build.verify_application_licenses(self.root / "application", self.root / "external")
        hashes.write_bytes(original)
        recipe = self.root / "external/package/awtrix-ng/awtrix-ng.mk"
        recipe.write_bytes(recipe.read_bytes() + b"AWTRIX_NG_LICENSE_FILES += unlisted.txt\n")
        with self.assertRaisesRegex(build.BuildError, "one literal"):
            build.verify_application_licenses(self.root / "application", self.root / "external")

    def test_license_preflight_rejects_missing_and_escaping_files(self):
        self.license_fixture()
        path = self.root / "application/LICENSE.md"
        path.unlink()
        with self.assertRaisesRegex(build.BuildError, "missing application license file"):
            build.verify_application_licenses(self.root / "application", self.root / "external")
        for filename in ("awtrix-ng.mk", "awtrix-ng.hash"):
            file = self.root / "external/package/awtrix-ng" / filename
            file.write_bytes(file.read_bytes().replace(b"LICENSE.md", b"../LICENSE.md"))
        with self.assertRaisesRegex(build.BuildError, "invalid application license path"):
            build.verify_application_licenses(self.root / "application", self.root / "external")

    def test_prepare_bad_license_stops_before_download_and_manifest(self):
        files = self.license_fixture()
        self.put("external/package/awtrix-ng/awtrix-ng.hash", b"sha256  " + b"0" * 64 + b"  LICENSE.md\n")
        work = self.root / "new-build"
        def snapshot(_repo, _revision, destination):
            with tarfile.open(destination, "w") as archive:
                for name, data in files.items():
                    member = tarfile.TarInfo("application/" + name)
                    member.size = len(data)
                    archive.addfile(member, io.BytesIO(data))
            return "a" * 40, 1700000000
        with mock.patch.object(build, "EXTERNAL", self.root / "external"), \
             mock.patch.object(build, "load_lock", return_value={"buildroot": self.source()}), \
             mock.patch.object(build, "snapshot_git", side_effect=snapshot), \
             mock.patch.object(build, "download_verified") as download:
            with self.assertRaisesRegex(build.BuildError, "license hashes do not match"):
                build.prepare(work, "HEAD")
            download.assert_not_called()
        self.assertFalse((work / "inputs.json").exists())
        self.assertFalse((work / "buildroot").exists())
        self.assertEqual((work / "application/THIRD-PARTY-NOTICES.md").read_bytes(), files["THIRD-PARTY-NOTICES.md"])

    def test_toolchain_prepare_skips_the_application_license_check(self):
        files = self.license_fixture()
        self.put("external/package/awtrix-ng/awtrix-ng.hash", b"sha256  " + b"0" * 64 + b"  LICENSE.md\n")
        buildroot = self.archive([("buildroot-test/Makefile", b"pinned Buildroot makefile")]).read_bytes()
        work = self.root / "new-build"
        def snapshot(_repo, _revision, destination):
            with tarfile.open(destination, "w") as archive:
                for name, data in files.items():
                    member = tarfile.TarInfo("application/" + name)
                    member.size = len(data)
                    archive.addfile(member, io.BytesIO(data))
            return "a" * 40, 1700000000
        with mock.patch.object(build, "EXTERNAL", self.root / "external"), \
             mock.patch.object(build, "load_lock", return_value={"buildroot": self.source(buildroot)}), \
             mock.patch.object(build, "snapshot_git", side_effect=snapshot), \
             mock.patch.object(build, "download_verified", side_effect=lambda _source, path: path.write_bytes(buildroot)):
            with self.assertRaisesRegex(build.BuildError, "license hashes do not match"):
                build.prepare(self.root / "checked-build", "HEAD")
            inputs = build.prepare(work, "HEAD", check_licenses=False)
        self.assertEqual(inputs["application_commit"], "a" * 40)
        self.assertEqual((work / "buildroot/Makefile").read_bytes(), b"pinned Buildroot makefile")

    def test_toolchain_command_builds_only_the_toolchain_target(self):
        inputs = {"profile": "awtrix_armv7_defconfig", "application_commit": "a" * 40}
        work = self.root / "toolchain-build"
        argv = ["build.py", "toolchain", "--work", str(work), "--jobs", "3"]
        with mock.patch.object(build.sys, "argv", argv), \
             mock.patch.object(build, "prepare", return_value=inputs) as prepare, \
             mock.patch.object(build, "doctor"), \
             mock.patch.object(build, "make") as make, \
             mock.patch.object(build, "record_result") as record:
            self.assertEqual(build.main(), 0)
        prepare.assert_called_once_with(work.resolve(), "HEAD", check_licenses=False)
        self.assertEqual(make.call_args_list, [mock.call(work.resolve(), inputs, "awtrix_armv7_defconfig"),
                                               mock.call(work.resolve(), inputs, "-j3", "BR2_JLEVEL=3", "toolchain")])
        record.assert_not_called()
        self.assertFalse((work / "build-result.json").exists())

    def test_prepare_rechecks_existing_license_inputs_without_changing_work(self):
        inputs = self.frozen_inputs()
        build.write_json(self.root / build.MARKER, {"schema_version": 1, "purpose": "awtrix-armv7-build"})
        before = build.tree_hashes(self.root)
        with mock.patch.object(build, "EXTERNAL", self.root / "external"), \
             mock.patch.object(build, "load_lock", return_value=inputs["source_lock"]), \
             mock.patch.object(build, "git_output", return_value=inputs["application_commit"]), \
             mock.patch.object(build, "snapshot_git") as snapshot:
            self.assertEqual(build.prepare(self.root, "HEAD"), inputs)
            snapshot.assert_not_called()
        self.assertEqual(build.tree_hashes(self.root), before)

    def test_digest_includes_all_chunks(self):
        data = b"first" + b"x" * (2 * 1024 * 1024) + b"last"
        self.assertEqual(hashlib.sha256(data).hexdigest(), build.digest(self.put("large.bin", data)))

    def test_source_lock_requires_schema_https_and_full_sha256(self):
        valid = {"schema_version": 1, "buildroot": self.source(), "linux_headers": self.source(b"headers")}
        path = self.root / "sources.lock.json"
        build.write_json(path, valid)
        self.assertEqual(valid, build.load_lock(path))
        for key, field, value in ((None, "schema_version", 2), ("buildroot", "sha256", "a" * 63),
                                  ("linux_headers", "sha256", "G" * 64), ("buildroot", "url", "http://example.invalid/source")):
            with self.subTest(key=key, field=field, value=value):
                invalid = json.loads(json.dumps(valid))
                (invalid if key is None else invalid[key])[field] = value
                build.write_json(path, invalid)
                with self.assertRaises(build.BuildError):
                    build.load_lock(path)

    def test_valid_cached_download_never_uses_network(self):
        destination = self.put("cached.tar.xz", b"archive")
        build.download_verified(self.source(), destination)
        self.assertEqual(b"archive", destination.read_bytes())

    def test_invalid_cached_download_is_preserved_and_rejected(self):
        destination = self.put("cached.tar.xz", b"corrupted")
        with self.assertRaisesRegex(build.BuildError, "cached source checksum mismatch"):
            build.download_verified(self.source(), destination)
        self.assertEqual(b"corrupted", destination.read_bytes())

    def test_verified_download_is_published_only_after_hash_check(self):
        destination = self.root / "download.tar.xz"
        with mock.patch.object(build.urllib.request, "urlopen", return_value=io.BytesIO(b"archive")) as request:
            build.download_verified(self.source(), destination)
        request.assert_called_once_with(self.source()["url"], timeout=45)
        self.assertEqual(b"archive", destination.read_bytes())
        self.assertFalse(destination.with_suffix(".xz.partial").exists())

    def test_download_hash_mismatch_removes_only_partial_output(self):
        destination = self.root / "download.tar.xz"
        with mock.patch.object(build.urllib.request, "urlopen", return_value=io.BytesIO(b"tampered")):
            with self.assertRaisesRegex(build.BuildError, "download checksum mismatch"):
                build.download_verified(self.source(), destination)
        self.assertFalse(destination.exists())
        self.assertFalse(destination.with_suffix(".xz.partial").exists())

    def test_interrupted_download_does_not_leave_publishable_output(self):
        destination = self.root / "download.tar.xz"
        response = mock.MagicMock()
        response.__enter__.return_value = response
        response.read.side_effect = [b"part", OSError("connection interrupted")]
        with mock.patch.object(build.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(OSError, "connection interrupted"):
                build.download_verified(self.source(), destination)
        self.assertFalse(destination.exists())
        self.assertFalse(destination.with_suffix(".xz.partial").exists())

    def test_preexisting_partial_download_is_never_deleted(self):
        destination = self.root / "download.tar.xz"
        partial = self.put("download.tar.xz.partial", b"another interrupted build")
        with self.assertRaises((build.BuildError, FileExistsError)):
            build.download_verified(self.source(), destination)
        self.assertEqual(b"another interrupted build", partial.read_bytes())
        self.assertFalse(destination.exists())

    def test_valid_archive_uses_expected_root_and_preserves_content(self):
        archive = self.archive([("application/", tarfile.DIRTYPE), ("application/", tarfile.DIRTYPE),
                                ("application/src/main.cpp", b"source"),
                                ("application/LICENSE.md", b"license")])
        destination = self.root / "extracted"
        build.extract_archive(archive, destination, "application")
        self.assertEqual({"LICENSE.md": hashlib.sha256(b"license").hexdigest(),
                          "src/main.cpp": hashlib.sha256(b"source").hexdigest()}, build.tree_hashes(destination))
        with self.assertRaisesRegex(build.BuildError, "already exists"):
            build.extract_archive(archive, destination, "application")
        self.assertEqual(b"source", (destination / "src/main.cpp").read_bytes())

    def test_archive_rejects_traversal_absolute_wrong_root_and_duplicates(self):
        for name in ("../outside", "/absolute", "application/../../outside", "other/file", "application/..\\outside"):
            with self.subTest(name=name):
                self.assert_archive_rejected([(name, b"bad")])
        self.assert_archive_rejected([("application/file", b"one"), ("application/./file", b"two")])
        self.assertFalse((self.root / "outside").exists())

    def test_archive_requires_source_directory(self):
        self.assert_archive_rejected([])
        self.assert_archive_rejected([("application", b"not a directory")])

    def test_archive_rejects_writes_below_links_regardless_of_order(self):
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE):
            for link_first in (True, False):
                with self.subTest(kind=kind, link_first=link_first):
                    entries = [("application/link", kind, "/tmp"), ("application/link/payload", b"bad")]
                    self.assert_archive_rejected(entries if link_first else list(reversed(entries)), allow_absolute_links=True)

    def test_archive_rejects_escaping_or_absolute_symlinks_by_default(self):
        for target in ("/tmp", "../../outside", "..\\..\\outside"):
            with self.subTest(target=target):
                self.assert_archive_rejected([("application/link", tarfile.SYMTYPE, target)])

    def test_archive_rejects_special_files(self):
        self.assert_archive_rejected([("application/device", tarfile.CHRTYPE)])

    def test_archive_accepts_relative_symlink_before_its_target(self):
        self.require_symlinks()
        archive = self.archive([("application/sub/link", tarfile.SYMTYPE, "../original"), ("application/original", b"safe")])
        destination = self.root / "extracted"
        build.extract_archive(archive, destination, "application")
        self.assertTrue((destination / "sub/link").is_symlink())
        self.assertEqual(b"safe", (destination / "sub/link").read_bytes())

    def test_archive_accepts_hardlink_before_target_without_directory_entries(self):
        archive = self.archive([("application/sub/link", tarfile.LNKTYPE, "application/original"),
                                ("application/original", b"safe")])
        destination = self.root / "extracted"
        build.extract_archive(archive, destination, "application")
        self.assertEqual(b"safe", (destination / "sub/link").read_bytes())
        self.assertTrue((destination / "sub/link").samefile(destination / "original"))

    def test_archive_rejects_hardlinks_to_missing_or_link_targets(self):
        self.assert_archive_rejected([("application/link", tarfile.LNKTYPE, "application/missing")])
        self.assert_archive_rejected([("application/link", tarfile.LNKTYPE, "application/symlink"),
                                      ("application/symlink", tarfile.SYMTYPE, "original"),
                                      ("application/original", b"safe")])

    def test_buildroot_absolute_symlinks_require_explicit_opt_in(self):
        self.require_symlinks()
        archive = self.archive([("application/skeleton/run", tarfile.SYMTYPE, "/run")])
        destination = self.root / "extracted"
        build.extract_archive(archive, destination, "application", allow_absolute_links=True)
        self.assertEqual("/run", os.readlink(destination / "skeleton/run"))

    def test_tree_hashes_rejects_even_dangling_symlinks(self):
        self.require_symlinks()
        self.put("frozen/source", b"safe")
        (self.root / "frozen/link").symlink_to("missing")
        with self.assertRaisesRegex(build.BuildError, "unexpected symlink"):
            build.tree_hashes(self.root / "frozen")
        inputs = self.frozen_inputs()
        outside = self.put("outside-buildroot", b"outside data")
        link = self.root / "buildroot/runtime-link"
        link.symlink_to(outside)
        inputs["buildroot_files"] = build.tree_hashes(self.root / "buildroot", allow_links=True)
        self.assertEqual("symlink:" + str(outside), inputs["buildroot_files"]["runtime-link"])
        build.write_json(self.root / "inputs.json", inputs)
        self.assertEqual(inputs, build.verify(self.root))
        # A Buildroot skeleton link describes a future target path. Its host
        # contents must never be followed, while the link target is frozen.
        outside.write_bytes(b"host data changes independently")
        self.assertEqual(inputs, build.verify(self.root))
        link.unlink()
        link.symlink_to("different-and-missing-target")
        with self.assertRaisesRegex(build.BuildError, "frozen Buildroot inputs changed"):
            build.verify(self.root)

    def git_fixture(self):
        """A local repository committing one fixture file per application input."""
        repo = self.root / "repo"
        repo.mkdir()
        environment = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                           GIT_AUTHOR_NAME="Contract Test", GIT_AUTHOR_EMAIL="test@example.invalid",
                           GIT_COMMITTER_NAME="Contract Test", GIT_COMMITTER_EMAIL="test@example.invalid",
                           GIT_AUTHOR_DATE="1700000000 +0000", GIT_COMMITTER_DATE="1700000000 +0000")

        def git(*args):
            return subprocess.check_output(["git", "-C", str(repo), *args], env=environment, stderr=subprocess.PIPE, text=True).strip()

        git("init", "--quiet")
        # Deliberately hostile host/repository EOL settings must not change
        # committed LF bytes in snapshot_git's independently invoked archive.
        git("config", "core.autocrlf", "true")
        git("config", "core.eol", "crlf")
        directories = {"src", "lib", "cmake", "LICENSES", "packaging/linux"}
        expected = {}
        for name in build.APP_PATHS:
            relative = name + "/fixture.txt" if name in directories else name
            data = ("committed Crème © " + relative + "\n").encode("utf-8")
            path = repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            expected[relative] = data
        (repo / "unrelated.txt").write_text("tracked but outside application snapshot")
        git("-c", "core.autocrlf=false", "add", "--all")
        git("commit", "--quiet", "-m", "local fixture")
        return repo, git, expected

    @unittest.skipUnless(shutil.which("git"), "local Git executable is required")
    def test_snapshot_requires_component_manifest(self):
        self.assertIn("tools/system/components.json", build.APP_PATHS)
        repo, git, expected = self.git_fixture()
        self.assertIn("tools/system/components.json", expected)
        archive = self.root / "complete.tar"
        build.snapshot_git(repo, "HEAD", archive)
        destination = self.root / "snapshot"
        build.extract_archive(archive, destination, "application")
        self.assertEqual(expected["tools/system/components.json"],
                         (destination / "tools/system/components.json").read_bytes())
        git("rm", "--quiet", "tools/system/components.json")
        git("commit", "--quiet", "-m", "fixture without component manifest")
        with self.assertRaisesRegex(build.BuildError, "missing required build inputs"):
            build.snapshot_git(repo, "HEAD", self.root / "incomplete.tar")
        self.assertFalse((self.root / "incomplete.tar").exists())

    @unittest.skipUnless(shutil.which("git"), "local Git executable is required")
    def test_snapshot_uses_real_git_commit_not_dirty_or_untracked_files(self):
        repo, git, expected = self.git_fixture()
        self.assertIn("cmake/fixture.txt", expected)
        commit = git("rev-parse", "HEAD")
        (repo / "src/fixture.txt").write_text("dirty worktree must not enter snapshot")
        (repo / "src/untracked.txt").write_text("untracked must not enter snapshot")
        archive = self.root / "application.tar"
        actual_commit, epoch = build.snapshot_git(repo, "HEAD", archive)
        self.assertEqual(commit, actual_commit)
        self.assertEqual(1700000000, epoch)
        destination = self.root / "snapshot"
        build.extract_archive(archive, destination, "application")
        self.assertEqual(set(expected), set(build.tree_hashes(destination)))
        for name, data in expected.items():
            self.assertEqual(data, (destination / name).read_bytes())
        self.assertIn("dirty worktree", (repo / "src/fixture.txt").read_text())

        # Service packaging is optional; missing required inputs refuse the snapshot.
        git("rm", "-r", "packaging/linux")
        git("commit", "--quiet", "-m", "fixture without service packaging")
        build.snapshot_git(repo, "HEAD", self.root / "without-service.tar")
        git("rm", "version")
        git("commit", "--quiet", "-m", "fixture missing required version")
        with self.assertRaisesRegex(build.BuildError, "missing required"):
            build.snapshot_git(repo, "HEAD", self.root / "incomplete.tar")
        self.assertFalse((self.root / "incomplete.tar").exists())

    def test_work_directory_requires_ownership_marker_before_reuse(self):
        work = self.root / "work"
        build.own_work_directory(work)
        self.assertEqual(1, json.loads((work / build.MARKER).read_text())["schema_version"])
        self.put("work/preserved", b"existing")
        build.own_work_directory(work)
        self.assertEqual(b"existing", (work / "preserved").read_bytes())
        self.put("foreign/preserved", b"foreign")
        with self.assertRaisesRegex(build.BuildError, "no AWTRIX build marker"):
            build.own_work_directory(self.root / "foreign")
        self.assertEqual(b"foreign", (self.root / "foreign/preserved").read_bytes())
        build.write_json(work / build.MARKER, {"schema_version": 2})
        with self.assertRaisesRegex(build.BuildError, "unsupported work directory marker"):
            build.own_work_directory(work)
        for invalid in (build.REPO, build.REPO.parent, Path(self.root.anchor), self.root / "with spaces"):
            with self.subTest(path=str(invalid)), self.assertRaises(build.BuildError):
                build.own_work_directory(invalid)

    def test_verify_accepts_frozen_inputs_and_detects_content_tampering(self):
        inputs = self.frozen_inputs()
        self.assertEqual(inputs, build.verify(self.root))
        for name, message in (("application.tar", "application archive changed"),
                              ("application/src/main.cpp", "frozen application inputs changed"),
                              ("external/configs/awtrix_armv7_defconfig", "frozen external inputs changed"),
                              ("buildroot/Makefile", "frozen Buildroot inputs changed"),
                              ("downloads/buildroot-test.tar.xz", "Buildroot archive changed")):
            with self.subTest(name=name):
                path = self.root / name
                original = path.read_bytes()
                path.write_bytes(original + b"tampering")
                with self.assertRaisesRegex(build.BuildError, message):
                    build.verify(self.root)
                path.write_bytes(original)
        added = self.put("application/untracked.cpp", b"unexpected")
        with self.assertRaisesRegex(build.BuildError, "frozen application inputs changed"):
            build.verify(self.root)
        added.unlink()
        (self.root / "application/src/main.cpp").unlink()
        with self.assertRaisesRegex(build.BuildError, "frozen application inputs changed"):
            build.verify(self.root)

    def test_verify_rejects_changed_schema_or_profile(self):
        inputs = self.frozen_inputs()
        for key, value in (("schema_version", 2), ("profile", "unreviewed_board")):
            with self.subTest(key=key):
                changed = dict(inputs, **{key: value})
                build.write_json(self.root / "inputs.json", changed)
                with self.assertRaisesRegex(build.BuildError, "unsupported prepared inputs"):
                    build.verify(self.root)

    def test_legal_evidence_contains_the_exact_frozen_sources(self):
        inputs = self.frozen_inputs()
        build.collect_legal_evidence(self.root, inputs)
        destination = self.root / "output/legal-info/build-inputs"
        manifest = json.loads((destination / "manifest.json").read_text())
        self.assertEqual(inputs["application_commit"], manifest["application_commit"])
        for name in ("buildroot-test.tar.xz", "application.tar", "inputs.json", "external.tar"):
            self.assertEqual(build.digest(destination / name), manifest["files"][name])
        with tarfile.open(destination / "external.tar") as archive:
            self.assertEqual(b"pinned recipe", archive.extractfile("external/configs/awtrix_armv7_defconfig").read())

    @staticmethod
    def arm_elf_header():
        header = bytearray(20)
        header[:6] = b"\x7fELF\x01\x01"
        header[18:20] = b"\x28\x00"
        return bytes(header)

    def test_result_records_arm_artifact_hashes_without_claiming_hardware_support(self):
        binary = self.put("output/target/usr/bin/awtrix-linux", self.arm_elf_header() + b"fixture")
        rootfs = self.put("output/images/rootfs.tar", b"root filesystem fixture")
        inputs = {"profile": "awtrix_armv7_defconfig", "application_commit": "b" * 40}
        build.write_json(self.root / "inputs.json", inputs)
        config = self.put("output/.config", b"resolved config")
        build.record_result(self.root, inputs)
        result = json.loads((self.root / "build-result.json").read_text())
        self.assertEqual("crossbuild_passed", result["status"])
        self.assertEqual(inputs["application_commit"], result["application_commit"])
        self.assertEqual(build.digest(self.root / "inputs.json"), result["inputs_sha256"])
        self.assertEqual(build.digest(config), result["config_sha256"])
        for path in (binary, rootfs):
            self.assertEqual({"sha256": build.digest(path), "bytes": path.stat().st_size},
                             result["artifacts"][path.relative_to(self.root).as_posix()])
        for flag in ("tc002_hardware_verified", "bootable_tc002_image", "bit_reproducibility_tested"):
            self.assertIs(False, result[flag])

    def test_result_rejects_non_arm_or_truncated_elf_before_writing_success(self):
        valid = self.arm_elf_header()
        invalid = {"truncated": valid[:18], "not_elf": b"not an executable",
                   "elf64": valid[:4] + b"\x02" + valid[5:],
                   "big_endian": valid[:5] + b"\x02" + valid[6:],
                   "x86": valid[:18] + b"\x03\x00"}
        self.put("output/images/rootfs.tar", b"rootfs")
        for name, header in invalid.items():
            with self.subTest(name=name):
                self.put("output/target/usr/bin/awtrix-linux", header)
                with self.assertRaisesRegex(build.BuildError, "ARM 32-bit little-endian"):
                    build.record_result(self.root, {"profile": "awtrix_armv7_defconfig", "application_commit": "c" * 40})
                self.assertFalse((self.root / "build-result.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
