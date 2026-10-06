"""Offline contracts for the evidence bundle tool; no compiler, network or device."""
from __future__ import annotations

import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import tarfile
import tempfile
import unittest
from unittest import mock

import build
import bundle_evidence as bundle
import qualify


class BundleFixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="awtrix-bundle-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.work = self.root / "work"
        self.commit = "a" * 40
        for name, data in {
            "application/src/main.cpp": b"application source",
            "application/tools/system/components.json": b"{}",
            "external/configs/awtrix_armv7_defconfig": b"frozen configuration",
            "buildroot/Makefile": b"frozen build tool",
            "downloads/buildroot-test.tar.xz": b"buildroot archive",
            "downloads/packages/source.tar.gz": b"cached package source",
            "application.tar": b"application archive",
            "output/.config": b"BR2_arm=y\n",
            "output/images/rootfs.tar": b"rootfs fixture",
            "output/legal-info/README": b"legal readme",
            "output/legal-info/buildroot.config": b"BR2_arm=y\n",
            "output/legal-info/manifest.csv": b"PACKAGE,VERSION\nawtrix-ng,aaaa\n",
            "output/legal-info/host-manifest.csv": b"PACKAGE,VERSION\nbuildroot,test\n",
            "output/legal-info/sources/awtrix-ng/application.tar.gz": b"saved sources",
            "output/build/unrelated.o": b"not evidence",
        }.items():
            self.put(self.work / name, data)
        header = bytearray(20)
        header[:6] = b"\x7fELF\x01\x01"
        header[18:20] = b"\x28\x00"
        self.put(self.work / "output/target/usr/bin/awtrix-linux", bytes(header) + b"ARM fixture")
        self.inputs = {
            "schema_version": 1, "profile": "awtrix_armv7_defconfig", "application_commit": self.commit,
            "source_date_epoch": 1234567890,
            "source_lock": {"buildroot": {"version": "test", "sha256": build.digest(self.work / "downloads/buildroot-test.tar.xz")}},
            "application_archive_sha256": build.digest(self.work / "application.tar"),
            "application_files": build.tree_hashes(self.work / "application"),
            "external_files": build.tree_hashes(self.work / "external"),
            "buildroot_files": build.tree_hashes(self.work / "buildroot"),
        }
        build.write_json(self.work / "inputs.json", self.inputs)
        build.record_result(self.work, self.inputs)
        self.result = qualify.json_file(self.work / "build-result.json")
        self.write_legal_sums()
        build.collect_legal_evidence(self.work, self.inputs)
        (self.work / "output/qualification").mkdir(parents=True)
        build.write_json(self.work / "output/qualification/evidence-template.json", {"schema_version": 1})
        self.write_qualification()
        self.bundles = self.root / "bundles"
        self.bundles.mkdir()
        emulated = self.root / "emulated-tmp"
        emulated.mkdir()
        for patch in (mock.patch.object(bundle, "TEMPORARY_ROOTS", (str(emulated), str(self.root / "emulated-shm"))),
                      mock.patch.object(bundle.tempfile, "gettempdir", return_value=str(self.root / "emulated-gettempdir"))):
            patch.start()
            self.addCleanup(patch.stop)

    @staticmethod
    def put(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def write_legal_sums(self):
        """Write legal-info.sha256 like Buildroot: every file before build-inputs exists, sorted by path."""
        legal = self.work / "output/legal-info"
        names = sorted(path.relative_to(legal).as_posix() for path in legal.rglob("*")
                       if path.is_file() and path.name != "legal-info.sha256" and
                       path.relative_to(legal).parts[0] != "build-inputs")
        self.legal = {name: build.digest(legal / name) for name in names}
        self.put(legal / "legal-info.sha256", "".join(f"{self.legal[name]}  {name}\n" for name in names).encode())

    def write_qualification(self, **changes):
        """Bind the record like qualify.py: inputs, build result, artifacts, generated files and legal-info."""
        record = {"schema_version": 1, "inputs_sha256": self.result["inputs_sha256"],
                  "application_commit": self.commit, "public_release": {"eligible": False},
                  "build_result_sha256": build.digest(self.work / "build-result.json"),
                  "artifacts": qualify.json_file(self.work / "build-result.json")["artifacts"],
                  "generated_files_sha256": {"evidence-template.json":
                                             build.digest(self.work / "output/qualification/evidence-template.json")},
                  "legal_evidence_sha256": self.legal}
        record.update(changes)
        (self.work / "output/qualification").mkdir(parents=True, exist_ok=True)
        build.write_json(self.work / "output/qualification/qualification.json", record)

    def reproduction(self, **changes):
        directory = self.root / "reproduction"
        for name in bundle.REPRODUCTION_FILES:
            if name != "reproduction.json":
                self.put(directory / name, ("reproduction " + name).encode())
        record = {"schema_version": 1, "status": "reproduced", "clean_builds": 2,
                  "application_commit": self.commit, "source_inputs_sha256": self.result["inputs_sha256"]}
        record.update(changes)
        build.write_json(directory / "reproduction.json", record)
        return directory

    def logs(self):
        directory = self.root / "logs"
        self.put(directory / "build.log", b"build log")
        self.put(directory / "arm-contracts.log", b"contracts log")
        return directory

    def create(self, name="evidence.tar", **options):
        output = self.bundles / name
        return output, bundle.create(self.work, output, options.get("reproduction"), options.get("logs"),
                                     options.get("downloads", True))

    def require_symlinks(self):
        probe = self.root / "symlink-probe"
        try:
            probe.symlink_to("not-present")
        except OSError as error:
            self.skipTest(f"platform does not permit symlinks: {error}")
        probe.unlink()

    def assert_nothing_written(self):
        self.assertEqual([], list(self.bundles.iterdir()), "refusals must write nothing")

    def assert_refused(self, message):
        with self.assertRaisesRegex(build.BuildError, message):
            self.create("refused.tar")
        self.assert_nothing_written()

    @staticmethod
    def external_archive(files):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w:") as archive:
            for name, data in files.items():
                info = tarfile.TarInfo("external/" + name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        return stream.getvalue()

    def rewrite_supplement(self, **files):
        path = self.work / "output/legal-info/build-inputs/manifest.json"
        supplement = qualify.json_file(path)
        supplement["files"].update(files)
        build.write_json(path, supplement)

    @staticmethod
    def bundled(output):
        with tarfile.open(output) as archive:
            return {member.name: archive.extractfile(member).read() for member in archive.getmembers()}

    @staticmethod
    def rewrite_bundle(output, manifest, members, **changes):
        """Rewrite the tar and rehash its manifest, so only the checks against the bundled records can refuse it."""
        with tarfile.open(output, "w:", format=tarfile.PAX_FORMAT) as archive:
            for name, data in sorted(members.items()):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        edited = json.loads(json.dumps(manifest))
        edited.update(changes)
        edited["members"] = {name: {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
                             for name, data in members.items()}
        for key, name in (("inputs_sha256", "work/inputs.json"), ("build_result_sha256", "work/build-result.json")):
            if name in members:
                edited[key] = edited["members"][name]["sha256"]
        edited["bundle"].update(sha256=build.digest(output), bytes=output.stat().st_size)
        build.write_json(output.with_name(output.name + ".manifest.json"), edited)

    @staticmethod
    def rebind(members, inputs=None, result=None):
        """Rewrite inputs, build result, build-inputs manifest and qualification record to match the members."""
        def encode(value):
            return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()

        def digest(data):
            return hashlib.sha256(data).hexdigest()

        supplied = "work/output/legal-info/build-inputs/"
        prepared = json.loads(members["work/inputs.json"])
        prepared.update(inputs or {})
        members["work/inputs.json"] = members[supplied + "inputs.json"] = encode(prepared)
        supplement = json.loads(members[supplied + "manifest.json"])
        supplement["files"]["inputs.json"] = digest(members["work/inputs.json"])
        members[supplied + "manifest.json"] = encode(supplement)
        recorded = json.loads(members["work/build-result.json"])
        recorded.update(result or {})
        recorded.update(inputs_sha256=digest(members["work/inputs.json"]),
                        artifacts={name: {"sha256": digest(members["work/" + name]), "bytes": len(members["work/" + name])}
                                   for name in qualify.ARTIFACTS})
        members["work/build-result.json"] = encode(recorded)
        record = json.loads(members["work/output/qualification/qualification.json"])
        record.update(inputs_sha256=recorded["inputs_sha256"], artifacts=recorded["artifacts"],
                      build_result_sha256=digest(members["work/build-result.json"]))
        members["work/output/qualification/qualification.json"] = encode(record)
        return recorded["artifacts"]


class CreateContracts(BundleFixture):
    def test_bundle_lists_every_member_with_hashes_and_is_deterministic(self):
        output, manifest = self.create()
        self.assertTrue(output.is_file())
        self.assertFalse(output.with_name("evidence.tar.partial").exists())
        written = qualify.json_file(output.with_name("evidence.tar.manifest.json"))
        self.assertEqual(manifest, written)
        expected = {"work/" + name for name in bundle.WORK_FILES}
        expected |= {"work/output/legal-info/" + name for name in (
            "README", "buildroot.config", "manifest.csv", "host-manifest.csv", "legal-info.sha256",
            "sources/awtrix-ng/application.tar.gz", "build-inputs/application.tar", "build-inputs/buildroot-test.tar.xz",
            "build-inputs/external.tar", "build-inputs/inputs.json", "build-inputs/manifest.json")}
        expected |= {"work/output/qualification/qualification.json", "work/output/qualification/evidence-template.json",
                     "work/downloads/buildroot-test.tar.xz", "work/downloads/packages/source.tar.gz"}
        self.assertEqual(expected, set(manifest["members"]))
        self.assertNotIn("work/output/build/unrelated.o", manifest["members"])
        with tarfile.open(output) as archive:
            names = archive.getnames()
            self.assertEqual(sorted(names), names)
            self.assertEqual(expected, set(names))
            for member in archive.getmembers():
                self.assertTrue(member.isreg())
                self.assertEqual((0, 0, "", "", 0o644, 1234567890),
                                 (member.uid, member.gid, member.uname, member.gname, member.mode, member.mtime))
                data = archive.extractfile(member).read()
                self.assertEqual({"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)},
                                 manifest["members"][member.name])
        self.assertEqual({"path": str(output), "sha256": build.digest(output), "bytes": output.stat().st_size},
                         manifest["bundle"])
        self.assertEqual(self.commit, manifest["application_commit"])
        self.assertEqual(self.result["inputs_sha256"], manifest["inputs_sha256"])
        self.assertEqual(build.digest(self.work / "build-result.json"), manifest["build_result_sha256"])
        self.assertEqual(self.result["artifacts"], manifest["artifacts"])
        self.assertTrue(manifest["downloads_included"])
        self.assertEqual({"work": str(self.work), "reproduction": None, "logs": None}, manifest["sources"])
        self.assertEqual(build.digest(Path(bundle.__file__)), manifest["tool_sha256"])
        self.assertFalse(manifest["tc002_hardware_verified"])
        self.assertFalse(manifest["bootable_tc002_image"])
        self.assertIn("not a hardware", manifest["purpose"])
        self.assertEqual(manifest, bundle.verify(output))
        self.assertEqual(["evidence.tar", "evidence.tar.manifest.json"], sorted(path.name for path in self.bundles.iterdir()))
        second, _ = self.create("again.tar")
        self.assertEqual(output.read_bytes(), second.read_bytes(), "bundle bytes must be deterministic")

    def test_no_downloads_and_missing_optional_files_are_recorded(self):
        _, manifest = self.create(downloads=False)
        self.assertFalse(manifest["downloads_included"])
        self.assertFalse(any(name.startswith("work/downloads/") for name in manifest["members"]))
        self.assertEqual(["work/arm-contracts.json"], manifest["missing_optional"])
        self.put(self.work / "arm-contracts.json", b"{}")
        _, manifest = self.create("with-contracts.tar", reproduction=self.reproduction())
        self.assertIn("work/arm-contracts.json", manifest["members"])
        self.assertEqual(["reproduction/reproduction-verified.json"], manifest["missing_optional"])

    def test_output_location_refusals_write_nothing(self):
        emulated_shm = self.root / "emulated-shm"
        emulated_shm.mkdir()
        (self.root / "emulated-gettempdir").mkdir()
        logs = self.logs()
        reproduction = self.reproduction()
        cases = {
            "temporary storage": self.root / "emulated-tmp/evidence.tar",
            "temporary storage (shm)": emulated_shm / "evidence.tar",
            "temporary storage (gettempdir)": self.root / "emulated-gettempdir/evidence.tar",
            "inside its sources (work)": self.work / "evidence.tar",
            "inside its sources (reproduction)": reproduction / "evidence.tar",
            "inside its sources (logs)": logs / "evidence.tar",
            "must end with .tar": self.bundles / "evidence.tgz",
            "parent directory does not exist": self.bundles / "missing/evidence.tar",
        }
        for message, output in cases.items():
            with self.subTest(message=message):
                with self.assertRaisesRegex(build.BuildError, message.split(" (")[0]):
                    bundle.create(self.work, output, reproduction, logs)
                self.assertFalse(output.exists())
        self.assert_nothing_written()
        for existing in ("evidence.tar", "evidence.tar.partial", "evidence.tar.manifest.json",
                         "evidence.tar.manifest.json.partial"):
            with self.subTest(existing=existing):
                path = self.put(self.bundles / existing, b"previous")
                with self.assertRaisesRegex(build.BuildError, "already exists"):
                    self.create()
                self.assertEqual(b"previous", path.read_bytes())
                path.unlink()
        self.assert_nothing_written()
        self.require_symlinks()
        (self.bundles / "evidence.tar").symlink_to(self.root / "elsewhere.tar")
        with self.assertRaisesRegex(build.BuildError, "already exists"):
            self.create()
        self.assertFalse((self.root / "elsewhere.tar").exists())

    def test_unverified_or_unbound_builds_are_refused(self):
        original = (self.work / "output/images/rootfs.tar").read_bytes()
        self.put(self.work / "output/images/rootfs.tar", b"X" * len(original))
        with self.assertRaisesRegex(build.BuildError, "checksum mismatch: output/images/rootfs.tar"):
            self.create()
        self.put(self.work / "output/images/rootfs.tar", original)
        build.write_json(self.work / "build-result.json", dict(self.result, status="failed"))
        with self.assertRaisesRegex(build.BuildError, "successful build"):
            self.create()
        build.write_json(self.work / "build-result.json", self.result)
        self.write_qualification(inputs_sha256="0" * 64)
        with self.assertRaisesRegex(build.BuildError, "qualification record is not bound to this build"):
            self.create()
        self.write_qualification(application_commit="b" * 40)
        with self.assertRaisesRegex(build.BuildError, "qualification record is not bound to this build"):
            self.create()
        (self.work / "output/qualification/qualification.json").unlink()
        with self.assertRaisesRegex(build.BuildError, "missing evidence file: output/qualification/qualification.json"):
            self.create()
        self.assert_nothing_written()

    def test_qualification_of_an_earlier_build_of_the_same_inputs_is_refused(self):
        self.put(self.work / "output/images/rootfs.tar", b"rootfs fixture rebuilt with other bytes")
        build.record_result(self.work, self.inputs)
        rebuilt = qualify.json_file(self.work / "build-result.json")
        self.assertEqual(self.result["inputs_sha256"], rebuilt["inputs_sha256"])
        with self.assertRaisesRegex(build.BuildError, "qualification record is not bound to this build"):
            self.create()
        stale_artifacts = self.result["artifacts"]
        self.write_qualification(artifacts=stale_artifacts)
        with self.assertRaisesRegex(build.BuildError, "qualification record is not bound to this build"):
            self.create()
        for changes in ({"build_result_sha256": "0" * 64}, {"build_result_sha256": None}, {"artifacts": None}):
            with self.subTest(changes=changes):
                self.write_qualification(**changes)
                with self.assertRaisesRegex(build.BuildError, "qualification record is not bound to this build"):
                    self.create()
        self.write_qualification()
        self.put(self.work / "output/qualification/evidence-template.json", b'{"schema_version": 2}\n')
        with self.assertRaisesRegex(build.BuildError, "qualification output differs from its record: evidence-template.json"):
            self.create()
        for generated in (None, {}, {"../inputs.json": "0" * 64}, {"absent.json": "0" * 64}):
            with self.subTest(generated=generated):
                self.write_qualification(generated_files_sha256=generated)
                with self.assertRaisesRegex(build.BuildError, "qualification record|invalid evidence path|missing evidence file"):
                    self.create()
        self.assert_nothing_written()
        self.write_qualification()
        _, manifest = self.create()
        self.assertEqual(rebuilt["artifacts"], manifest["artifacts"])

    def test_evidence_changing_between_verification_and_copy_is_refused(self):
        original = bundle.tree_members
        targets = {"output/images/rootfs.tar": b"rootfs replaced during bundling",
                   "output/target/usr/bin/awtrix-linux": b"application replaced during bundling",
                   "inputs.json": b'{"schema_version": 1}\n', "build-result.json": b'{"schema_version": 1}\n',
                   "application.tar": b"other archive", "output/.config": b"BR2_arm=n\n",
                   "downloads/buildroot-test.tar.xz": b"other buildroot archive",
                   "output/qualification/qualification.json": b'{"schema_version": 1}\n',
                   "output/qualification/evidence-template.json": b'{"schema_version": 3}\n',
                   "output/legal-info/README": b"legal readme replaced during bundling",
                   "output/legal-info/legal-info.sha256": b"",
                   "output/legal-info/build-inputs/manifest.json": b'{"schema_version": 1}\n',
                   "output/legal-info/build-inputs/external.tar": b"other external archive"}
        for number, (name, data) in enumerate(targets.items()):
            with self.subTest(member=name):
                saved = (self.work / name).read_bytes()

                def mutate(*args, **kwargs):
                    (self.work / name).write_bytes(data)
                    return original(*args, **kwargs)

                output = self.bundles / f"raced-{number}.tar"
                with mock.patch.object(bundle, "tree_members", side_effect=mutate):
                    with self.assertRaisesRegex(build.BuildError, "evidence changed while bundling: work/" + name):
                        bundle.create(self.work, output, None, None, True)
                self.assertFalse(output.exists())
                self.assertFalse(output.with_name(output.name + ".manifest.json").exists())
                (self.work / name).write_bytes(saved)
        reproduction = self.reproduction()

        def mutate_reproduction(directory, *args):
            members = original_reproduction(directory, *args)
            (directory / "reproduction.json").write_bytes(b'{"status": "failed"}\n')
            return members

        original_reproduction = bundle.collect_reproduction
        with mock.patch.object(bundle, "collect_reproduction", side_effect=mutate_reproduction):
            with self.assertRaisesRegex(build.BuildError, "evidence changed while bundling: reproduction/reproduction.json"):
                self.create("raced-reproduction.tar", reproduction=reproduction, downloads=False)

    def test_source_directories_must_be_real_and_complete(self):
        legal = self.work / "output/legal-info"
        legal.rename(self.root / "legal-info-moved")
        with self.assertRaisesRegex(build.BuildError, "missing evidence file: output/legal-info/legal-info.sha256"):
            self.create()
        legal.mkdir()
        with self.assertRaisesRegex(build.BuildError, "missing evidence file: output/legal-info/legal-info.sha256"):
            self.create()
        legal.rmdir()
        (self.root / "legal-info-moved").rename(legal)
        logs = self.logs()
        (logs / "nested").mkdir()
        with self.assertRaisesRegex(build.BuildError, "log directory must contain only files: nested"):
            self.create(logs=logs)
        (logs / "nested").rmdir()
        for missing in (self.root / "absent-logs", self.work / "inputs.json"):
            with self.subTest(source=missing):
                with self.assertRaisesRegex(build.BuildError, "evidence source must be a real directory"):
                    self.create(logs=missing)
        self.assert_nothing_written()
        self.require_symlinks()
        link = self.work / "output/legal-info/link.txt"
        link.symlink_to(self.work / "inputs.json")
        with self.assertRaisesRegex(build.BuildError, "symlink in evidence tree: output/legal-info/link.txt"):
            self.create()
        link.unlink()
        link = self.work / "downloads/link.tar.xz"
        link.symlink_to(self.work / "downloads/buildroot-test.tar.xz")
        with self.assertRaisesRegex(build.BuildError, "symlink in evidence tree: downloads/link.tar.xz"):
            self.create()
        self.create("without-downloads.tar", downloads=False)
        link.unlink()
        (logs / "link.log").symlink_to(logs / "build.log")
        with self.assertRaisesRegex(build.BuildError, "symlink in evidence tree: logs/link.log"):
            self.create(logs=logs)
        (logs / "link.log").unlink()
        (self.root / "work-link").symlink_to(self.work, target_is_directory=True)
        with self.assertRaisesRegex(build.BuildError, "evidence source must be a real directory"):
            bundle.create(self.root / "work-link", self.bundles / "linked.tar", None, None)

    def test_reproduction_record_must_be_bound_and_complete(self):
        for changes in ({"status": "failed"}, {"clean_builds": 1}, {"source_inputs_sha256": "0" * 64},
                        {"application_commit": "b" * 40}):
            with self.subTest(changes=changes):
                with self.assertRaisesRegex(build.BuildError, "reproduction record is not bound to this build"):
                    self.create(reproduction=self.reproduction(**changes))
        reproduction = self.reproduction()
        (reproduction / "first.log").unlink()
        with self.assertRaisesRegex(build.BuildError, "missing evidence file: first.log"):
            self.create(reproduction=reproduction)
        self.assert_nothing_written()
        reproduction = self.reproduction()
        self.put(reproduction / "reproduction-verified.json", b"{}")
        _, manifest = self.create(reproduction=reproduction, logs=self.logs())
        expected = {"reproduction/" + name for name in bundle.REPRODUCTION_FILES + bundle.REPRODUCTION_OPTIONAL_FILES}
        self.assertTrue(expected <= set(manifest["members"]))
        self.assertEqual({"logs/arm-contracts.log", "logs/build.log"},
                         {name for name in manifest["members"] if name.startswith("logs/")})
        self.assertEqual(["work/arm-contracts.json"], manifest["missing_optional"])
        self.assertEqual(str(reproduction), manifest["sources"]["reproduction"])


class LegalEvidenceContracts(BundleFixture):
    """Every legal-info file is bound to the qualification record, the frozen inputs or the build result."""

    def test_legal_info_changed_or_deleted_after_qualification_is_refused(self):
        legal = self.work / "output/legal-info"
        saved = (legal / "README").read_bytes()
        self.put(legal / "README", b"legal README")
        self.assert_refused("legal evidence differs from its record: output/legal-info/README")
        self.put(legal / "README", saved)
        source = legal / "sources/awtrix-ng/application.tar.gz"
        saved = source.read_bytes()
        source.unlink()
        self.assert_refused("missing evidence file: output/legal-info/sources/awtrix-ng/application.tar.gz")
        self.put(source, saved)
        self.put(legal / "README", b"legal readme, edited and rehashed")
        self.write_legal_sums()
        self.assert_refused("legal-info checksum list differs from the qualification record")
        self.write_qualification()
        _, manifest = self.create()
        self.assertEqual(build.digest(legal / "README"), manifest["members"]["work/output/legal-info/README"]["sha256"])

    def test_modified_legal_info_checksum_list_is_refused(self):
        sums = self.work / "output/legal-info/legal-info.sha256"
        saved = sums.read_bytes()
        lines = saved.decode().splitlines(keepends=True)
        changes = {"digest": saved.replace(self.legal["README"].encode(), b"0" * 64),
                   "order": "".join(reversed(lines)).encode(),
                   "entry removed": "".join(lines[1:]).encode(),
                   "entry added": saved + f"{'0' * 64}  extra.txt\n".encode(),
                   "line ending": saved.replace(b"\n", b"\r\n")}
        for change, data in changes.items():
            with self.subTest(change=change):
                sums.write_bytes(data)
                self.assert_refused("legal-info checksum list differs from the qualification record")
        sums.unlink()
        self.assert_refused("missing evidence file: output/legal-info/legal-info.sha256")
        sums.write_bytes(saved)
        self.create()

    def test_modified_build_inputs_are_refused(self):
        inputs = self.work / "output/legal-info/build-inputs"
        supplement = (inputs / "manifest.json").read_bytes()
        for name in ("application.tar", "buildroot-test.tar.xz", "inputs.json", "external.tar"):
            with self.subTest(member=name):
                saved = (inputs / name).read_bytes()
                (inputs / name).write_bytes(saved + b" edited")
                self.assert_refused("legal evidence differs from its record: output/legal-info/build-inputs/" + name)
                if name != "external.tar":
                    self.rewrite_supplement(**{name: build.digest(inputs / name)})
                    self.assert_refused("build-inputs manifest is not bound to this build")
                (inputs / name).write_bytes(saved)
                (inputs / "manifest.json").write_bytes(supplement)
                (inputs / name).unlink()
                self.assert_refused("missing evidence file: output/legal-info/build-inputs/" + name)
                (inputs / name).write_bytes(saved)
        frozen = {name: (self.work / "external" / name).read_bytes() for name in self.inputs["external_files"]}
        replacements = {"content": dict(frozen, **{"configs/awtrix_armv7_defconfig": b"other configuration"}),
                        "file added": dict(frozen, **{"package/extra.mk": b"extra recipe"}),
                        "file removed": {}}
        for change, files in replacements.items():
            with self.subTest(external=change):
                (inputs / "external.tar").write_bytes(self.external_archive(files))
                self.rewrite_supplement(**{"external.tar": build.digest(inputs / "external.tar")})
                self.assert_refused("build-inputs/external.tar differs from the frozen external recipes")
        (inputs / "external.tar").write_bytes(self.external_archive(frozen))
        self.rewrite_supplement(**{"external.tar": build.digest(inputs / "external.tar")})
        _, manifest = self.create("equivalent-external.tar")
        self.assertIn("work/output/legal-info/build-inputs/external.tar", manifest["members"])
        (self.bundles / "equivalent-external.tar").unlink()
        (self.bundles / "equivalent-external.tar.manifest.json").unlink()
        for changes in ({"application_commit": "b" * 40}, {"schema_version": 2}, {"files": None}):
            with self.subTest(supplement=changes):
                build.write_json(inputs / "manifest.json", dict(json.loads(supplement), **changes))
                self.assert_refused("build-inputs manifest is not bound to this build")
        (inputs / "manifest.json").unlink()
        self.assert_refused("missing evidence file: output/legal-info/build-inputs/manifest.json")

    def test_external_archive_digest_and_content_come_from_one_read(self):
        target = self.work / "output/legal-info/build-inputs/external.tar"
        frozen = target.read_bytes()
        recipes = {name: (self.work / "external" / name).read_bytes() for name in self.inputs["external_files"]}
        altered = self.external_archive(dict(recipes, **{"package/extra.mk": b"extra recipe"}))
        self.assertEqual(len(frozen), len(altered), "both archives must keep one size across the swaps")
        target.write_bytes(altered)
        self.rewrite_supplement(**{"external.tar": build.digest(target)})
        opened = []
        original_open = Path.open

        def swapping_open(path, *args, **kwargs):
            if path == target:
                opened.append(args)
                with original_open(path, "wb") as stream:
                    stream.write(frozen if len(opened) == 2 else altered)
            return original_open(path, *args, **kwargs)

        with mock.patch.object(Path, "open", swapping_open):
            self.assert_refused("build-inputs/external.tar differs from the frozen external recipes")

    def test_create_writes_only_canonical_member_names(self):
        source = self.work / "inputs.json"
        for name in ("work/./inputs.json", "work//inputs.json", "work/output/legal-info/../extra.txt", "/work/inputs.json",
                     "work/other.json", "logs/nested/build.log"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(build.BuildError, "invalid evidence member name: " + re.escape(name) + "$"):
                    bundle.write_bundle(self.bundles / "names.tar", [(name, source)], 0, {})
                self.assert_nothing_written()
        if os.name == "posix":
            self.put(self.work / "downloads/packages/odd\\name.tar.gz", b"cached package source")
            self.assert_refused("invalid evidence member name: " + re.escape("work/downloads/packages/odd\\name.tar.gz"))

    def test_files_no_record_covers_are_refused(self):
        for name in ("extra.txt", "sources/awtrix-ng/patch.diff", "build-inputs/extra.tar"):
            with self.subTest(name=name):
                path = self.put(self.work / "output/legal-info" / name, b"unrecorded evidence")
                self.assert_refused("unrecorded legal evidence: work/output/legal-info/" + name)
                path.unlink()
        self.create()

    def test_qualification_record_must_cover_the_legal_evidence(self):
        cases = {"absent": None, "empty": {}, "other configuration": dict(self.legal, **{"buildroot.config": "0" * 64}),
                 "invalid digest": dict(self.legal, README="not a digest"),
                 "claims build-inputs": dict(self.legal, **{"build-inputs/inputs.json": self.result["inputs_sha256"]})}
        for case, legal in cases.items():
            with self.subTest(case=case):
                self.write_qualification(legal_evidence_sha256=legal)
                self.assert_refused("qualification record does not cover this build's legal evidence")
        self.write_qualification(legal_evidence_sha256=dict(self.legal, **{"licenses/absent/COPYING": "0" * 64}))
        self.assert_refused("legal-info checksum list differs from the qualification record")


class ManifestPartialContracts(BundleFixture):
    def assert_foreign_unchanged(self, foreign):
        self.assertEqual(b"foreign file", foreign.read_bytes(), "a link at the manifest partial path was written through")

    def symlinks_permitted(self):
        probe = self.root / "symlink-probe"
        try:
            probe.symlink_to("not-present")
        except OSError:
            return False
        probe.unlink()
        return True

    def test_existing_links_at_the_manifest_partial_path_are_refused(self):
        foreign = self.put(self.root / "foreign.json", b"foreign file")
        partial = self.bundles / "evidence.tar.manifest.json.partial"
        os.link(foreign, partial)
        with self.assertRaisesRegex(build.BuildError, "evidence bundle output already exists: .*manifest.json.partial"):
            self.create()
        self.assert_foreign_unchanged(foreign)
        self.assertEqual([partial], list(self.bundles.iterdir()))
        partial.unlink()
        self.require_symlinks()
        for target in (foreign, self.root / "absent.json"):
            with self.subTest(symlink=target.name):
                partial.symlink_to(target)
                with self.assertRaisesRegex(build.BuildError, "evidence bundle output already exists"):
                    self.create()
                self.assertEqual([partial], list(self.bundles.iterdir()))
                partial.unlink()
        self.assert_foreign_unchanged(foreign)
        self.assertFalse((self.root / "absent.json").exists())

    def test_manifest_partial_is_created_exclusively(self):
        foreign = self.put(self.root / "foreign.json", b"foreign file")
        links = {"hard link": lambda path: os.link(foreign, path)}
        if self.symlinks_permitted():
            links["symlink"] = lambda path: path.symlink_to(foreign)
            links["dangling symlink"] = lambda path: path.symlink_to(self.root / "absent.json")
        original = bundle.write_bundle
        for number, (kind, make_link) in enumerate(links.items()):
            with self.subTest(link=kind):
                output = self.bundles / f"raced-{number}.tar"
                partial = output.with_name(output.name + ".manifest.json.partial")

                def link_after_preflight(*args, **kwargs):
                    written = original(*args, **kwargs)
                    make_link(partial)
                    return written

                with mock.patch.object(bundle, "write_bundle", side_effect=link_after_preflight):
                    with self.assertRaisesRegex(build.BuildError, "evidence bundle output already exists: .*" + partial.name):
                        bundle.create(self.work, output, None, None, False)
                self.assert_foreign_unchanged(foreign)
                self.assertFalse(output.with_name(output.name + ".manifest.json").exists())
                self.assertFalse((self.root / "absent.json").exists())
        if os.name == "posix":
            self.assertEqual(3, len(links), "symlink cases must run on Linux")

    def test_manifest_is_published_with_its_final_mode(self):
        previous = os.umask(0o022)
        self.addCleanup(os.umask, previous)
        output, manifest = self.create()
        path = output.with_name("evidence.tar.manifest.json")
        self.assertEqual(manifest, qualify.json_file(path))
        self.assertFalse(path.with_name(path.name + ".partial").exists())
        if os.name == "posix":
            self.assertEqual(0o644, stat.S_IMODE(path.stat().st_mode))


class VerifyContracts(BundleFixture):
    def test_verify_accepts_intact_bundle_and_names_changed_members(self):
        output, manifest = self.create(reproduction=self.reproduction(), logs=self.logs())
        self.assertEqual(manifest, bundle.verify(output))
        data = bytearray(output.read_bytes())
        position = data.find(b"legal readme")
        self.assertGreater(position, 0)
        intact = bytes(data)
        data[position:position + 5] = b"LEGAL"
        output.write_bytes(bytes(data))
        with self.assertRaisesRegex(build.BuildError, "member differs from manifest: work/output/legal-info/README"):
            bundle.verify(output)
        output.write_bytes(intact[:len(intact) // 2])
        with self.assertRaisesRegex(build.BuildError, "truncated or corrupt|missing member"):
            bundle.verify(output)
        output.write_bytes(intact)
        self.assertEqual(manifest, bundle.verify(output))

    def test_verify_rejects_edited_manifest_and_member_set_changes(self):
        output, manifest = self.create()
        manifest_path = output.with_name("evidence.tar.manifest.json")
        edited = json.loads(json.dumps(manifest))
        edited["members"]["work/inputs.json"]["sha256"] = "0" * 64
        build.write_json(manifest_path, edited)
        with self.assertRaisesRegex(build.BuildError, "member differs from manifest: work/inputs.json"):
            bundle.verify(output)
        edited = json.loads(json.dumps(manifest))
        edited["members"]["work/extra.json"] = {"sha256": "0" * 64, "bytes": 1}
        build.write_json(manifest_path, edited)
        with self.assertRaisesRegex(build.BuildError, "missing member: work/extra.json"):
            bundle.verify(output)
        edited = json.loads(json.dumps(manifest))
        del edited["members"]["work/inputs.json"]
        build.write_json(manifest_path, edited)
        with self.assertRaisesRegex(build.BuildError, "unexpected member: work/inputs.json"):
            bundle.verify(output)
        edited = json.loads(json.dumps(manifest))
        edited["bundle"]["sha256"] = "0" * 64
        build.write_json(manifest_path, edited)
        with self.assertRaisesRegex(build.BuildError, "evidence bundle differs from its manifest"):
            bundle.verify(output)
        build.write_json(manifest_path, dict(manifest, schema_version=2))
        with self.assertRaisesRegex(build.BuildError, "unsupported evidence manifest"):
            bundle.verify(output)
        identity = {"work/inputs.json": lambda m: m.update(inputs_sha256="0" * 64),
                    "work/build-result.json": lambda m: m.update(build_result_sha256="0" * 64),
                    "work/output/images/rootfs.tar": lambda m: m["artifacts"]["output/images/rootfs.tar"].update(bytes=1)}
        for name, edit in identity.items():
            with self.subTest(identity=name):
                edited = json.loads(json.dumps(manifest))
                edit(edited)
                build.write_json(manifest_path, edited)
                with self.assertRaisesRegex(build.BuildError, "member differs from build identity: " + name):
                    bundle.verify(output)
        for artifacts in (None, {}, {"output/images/rootfs.tar": manifest["artifacts"]["output/images/rootfs.tar"]}):
            with self.subTest(artifacts=artifacts):
                build.write_json(manifest_path, dict(manifest, artifacts=artifacts))
                with self.assertRaisesRegex(build.BuildError, "evidence manifest does not identify both artifacts"):
                    bundle.verify(output)
        manifest_path.unlink()
        with self.assertRaises(OSError):
            bundle.verify(output)

    def test_verify_rechecks_legal_members_against_the_bundled_records(self):
        output, manifest = self.create(downloads=False)
        intact = output.read_bytes()
        manifest_path = output.with_name("evidence.tar.manifest.json")
        original = self.bundled(output)
        legal = "work/output/legal-info/"
        frozen = {name: (self.work / "external" / name).read_bytes() for name in self.inputs["external_files"]}
        external = self.external_archive(dict(frozen, extra=b"unfrozen recipe"))
        supplement = json.loads(original[legal + "build-inputs/manifest.json"])
        supplement["files"]["external.tar"] = hashlib.sha256(external).hexdigest()
        record = json.loads(original["work/output/qualification/qualification.json"])
        cases = {
            "legal file": ({legal + "README": b"legal README"},
                           "member differs from qualification record: " + legal + "README"),
            "deleted legal file": ({legal + "sources/awtrix-ng/application.tar.gz": None},
                                   "member differs from qualification record: " + legal + "sources/awtrix-ng/application.tar.gz"),
            "build input": ({legal + "build-inputs/application.tar": b"other archive"},
                            "member differs from qualification record: " + legal + "build-inputs/application.tar"),
            "checksum list": ({legal + "legal-info.sha256": original[legal + "legal-info.sha256"] + b"\n"},
                              "legal-info checksum list differs from the qualification record"),
            "unrecorded file": ({legal + "extra.txt": b"unrecorded"}, "unrecorded legal evidence: " + legal + "extra.txt"),
            "external recipes": ({legal + "build-inputs/external.tar": external,
                                  legal + "build-inputs/manifest.json": json.dumps(supplement).encode()},
                                 "build-inputs/external.tar differs from the frozen external recipes"),
            "unbound record": ({"work/output/qualification/qualification.json":
                                json.dumps(dict(record, inputs_sha256="0" * 64)).encode()},
                               "qualification record is not bound to this build"),
            "missing record": ({legal + "build-inputs/manifest.json": None},
                               "missing member: " + legal + "build-inputs/manifest.json"),
            "unbound build result": ({"work/build-result.json": json.dumps(dict(self.result, inputs_sha256="0" * 64)).encode()},
                                     "member differs from build identity: work/build-result.json"),
        }
        for case, (changes, message) in cases.items():
            with self.subTest(case=case):
                members = dict(original)
                for name, data in changes.items():
                    if data is None:
                        del members[name]
                    else:
                        members[name] = data
                self.rewrite_bundle(output, manifest, members)
                with self.assertRaisesRegex(build.BuildError, message):
                    bundle.verify(output)
        output.write_bytes(intact)
        build.write_json(manifest_path, manifest)
        self.assertEqual(manifest, bundle.verify(output))

    def test_verify_refuses_member_names_that_extract_elsewhere(self):
        output, manifest = self.create(downloads=False)
        original = self.bundled(output)
        for name in ("work/./output/legal-info/extra.txt", "work/output//legal-info/README",
                     "work/output/legal-info/sources/../extra.txt", "/work/output/legal-info/extra.txt",
                     "work\\output\\legal-info\\extra.txt", "work/output/Legal-Info/extra.txt",
                     "work/output/legal-info/README/", "work/extra.txt", "logs/nested/extra.log"):
            with self.subTest(name=name):
                self.rewrite_bundle(output, manifest, {**original, name: b"legal README"})
                self.assertIn(name, self.bundled(output))
                with self.assertRaisesRegex(build.BuildError, "invalid evidence member name: " + re.escape(name) + "$"):
                    bundle.verify(output)

    def test_verify_refuses_names_that_alias_a_member_on_windows(self):
        output, manifest = self.create()
        original = self.bundled(output)
        for name in ("work/output/qualification/qualification.json.", "work/output/qualification/qualification.json ",
                     "work/output/qualification/qualification.json::$DATA", "work/output/qualification/QUALIF~1.JSO",
                     "work/downloads/BUILDR~1.XZ", "logs/BUILD~1.LOG", "logs/build.log:stream"):
            with self.subTest(name=name):
                self.rewrite_bundle(output, manifest, {**original, name: b"aliased evidence"})
                with self.assertRaisesRegex(build.BuildError, "invalid evidence member name: " + re.escape(name) + "$"):
                    bundle.verify(output)
        for name in ("work/output/qualification/QUALIFICATION.json", "work/downloads/Buildroot-test.tar.xz"):
            with self.subTest(name=name):
                self.rewrite_bundle(output, manifest, {**original, name: b"aliased evidence"})
                with self.assertRaisesRegex(build.BuildError,
                                            "evidence member names collide when case is ignored: "):
                    bundle.verify(output)

    def test_create_refuses_names_that_alias_on_windows(self):
        source = self.work / "inputs.json"
        for name in ("logs/build.log.", "logs/build.log ", "logs/build.log:stream", "logs/BUILD~1.LOG"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(build.BuildError, "invalid evidence member name: " + re.escape(name) + "$"):
                    bundle.write_bundle(self.bundles / "names.tar", [(name, source)], 0, {})
                self.assert_nothing_written()
        with self.assertRaisesRegex(build.BuildError,
                                    "evidence member names collide when case is ignored: logs/Build.log$"):
            bundle.write_bundle(self.bundles / "names.tar", [("logs/build.log", source), ("logs/Build.log", source)], 0, {})
        self.assert_nothing_written()

    def test_verify_repeats_the_build_and_reproduction_checks_of_create(self):
        output, manifest = self.create(reproduction=self.reproduction())
        intact = output.read_bytes()
        manifest_path = output.with_name("evidence.tar.manifest.json")
        original = self.bundled(output)
        reproduction = json.loads(original["reproduction/reproduction.json"])
        elf = "work/output/target/usr/bin/awtrix-linux"
        cases = {
            "application archive": ({"work/application.tar": b"other archive"}, {},
                                    "member differs from build identity: work/application.tar"),
            "configuration": ({"work/output/.config": b"BR2_arm=n\n"}, {},
                              "member differs from build identity: work/output/.config"),
            "Buildroot archive": ({"work/downloads/buildroot-test.tar.xz": b"other buildroot archive"}, {},
                                  "member differs from build identity: work/downloads/buildroot-test.tar.xz"),
            "Buildroot archive removed": ({"work/downloads/buildroot-test.tar.xz": None}, {},
                                          "member differs from build identity: work/downloads/buildroot-test.tar.xz"),
            "manifest commit": ({}, {"manifest": {"application_commit": "b" * 40}},
                                "member differs from build identity: work/build-result.json"),
            "downloads flag": ({}, {"manifest": {"downloads_included": False}},
                               "evidence manifest does not match the bundled downloads"),
            "downloads removed": ({name: None for name in original if name.startswith("work/downloads/")}, {},
                                  "evidence manifest does not match the bundled downloads"),
            "reproduction status": ({"reproduction/reproduction.json": json.dumps(dict(reproduction, status="failed")).encode()},
                                    {}, "reproduction record is not bound to this build"),
            "reproduction inputs": ({"reproduction/reproduction.json":
                                     json.dumps(dict(reproduction, source_inputs_sha256="0" * 64)).encode()},
                                    {}, "reproduction record is not bound to this build"),
            "reproduction file removed": ({"reproduction/first.log": None}, {}, "missing member: reproduction/first.log"),
            "reproduction removed": ({name: None for name in original if name.startswith("reproduction/")}, {},
                                     "missing member: reproduction/reproduction.json"),
            "result status": ({}, {"result": {"status": "failed"}},
                              "build result does not identify a successful build of these inputs"),
            "result profile": ({}, {"result": {"profile": "other_defconfig"}},
                               "build result does not identify a successful build of these inputs"),
            "inputs profile": ({}, {"inputs": {"profile": "other_defconfig"}, "result": {"profile": "other_defconfig"}},
                               "unsupported prepared inputs"),
            "application artifact": ({elf: b"not an ARM executable"}, {"result": {}},
                                     "application artifact is not ARM 32-bit little-endian ELF"),
        }
        for case, (changes, records, message) in cases.items():
            with self.subTest(case=case):
                members = dict(original)
                for name, data in changes.items():
                    if data is None:
                        del members[name]
                    else:
                        members[name] = data
                edits = dict(records.get("manifest", {}))
                if "inputs" in records or "result" in records:
                    edits["artifacts"] = self.rebind(members, records.get("inputs"), records.get("result"))
                self.rewrite_bundle(output, manifest, members, **edits)
                with self.assertRaisesRegex(build.BuildError, message):
                    bundle.verify(output)
        output.write_bytes(intact)
        build.write_json(manifest_path, manifest)
        self.assertEqual(manifest, bundle.verify(output))

    def test_verify_rejects_non_regular_members(self):
        output, manifest = self.create()
        with tarfile.open(output, "a:") as archive:
            info = tarfile.TarInfo("work/link")
            info.type = tarfile.SYMTYPE
            info.linkname = "/etc/passwd"
            archive.addfile(info)
        with self.assertRaisesRegex(build.BuildError, "unexpected member type: work/link"):
            bundle.verify(output)


class CommandLine(BundleFixture):
    def test_cli_creates_and_verifies(self):
        output = self.bundles / "cli.tar"
        with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            self.assertEqual(0, bundle.main(["create", "--work", str(self.work), "--output", str(output),
                                             "--reproduction", str(self.reproduction()), "--logs", str(self.logs()),
                                             "--no-downloads"]))
        self.assertEqual(f"Evidence bundle written: {output.with_name('cli.tar.manifest.json')}\n", out.getvalue())
        self.assertFalse(qualify.json_file(output.with_name("cli.tar.manifest.json"))["downloads_included"])
        with mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            self.assertEqual(0, bundle.main(["verify", "--bundle", str(output)]))
        self.assertEqual(f"Evidence bundle verified: {output}\n", out.getvalue())
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, bundle.main(["create", "--work", str(self.work), "--output", str(output)]))
        self.assertTrue(err.getvalue().startswith("evidence bundle: "))
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, bundle.main(["verify", "--bundle", str(self.bundles / "absent.tar")]))
        self.assertTrue(err.getvalue().startswith("evidence bundle: "))

    def test_cli_reports_duplicate_json_keys_in_every_record(self):
        duplicate = b'{"schema_version":1,"schema_version":1}'
        output = self.bundles / "cli.tar"
        with mock.patch("sys.stdout", new_callable=io.StringIO):
            self.assertEqual(0, bundle.main(["create", "--work", str(self.work), "--output", str(output), "--no-downloads"]))
        output.with_name("cli.tar.manifest.json").write_bytes(duplicate)
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, bundle.main(["verify", "--bundle", str(output)]))
        self.assertEqual("evidence bundle: duplicate JSON key: schema_version\n", err.getvalue())
        reproduction = self.reproduction()
        for path in (self.work / "output/qualification/qualification.json", reproduction / "reproduction.json"):
            with self.subTest(record=path.name):
                saved = path.read_bytes()
                path.write_bytes(duplicate)
                with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
                    self.assertEqual(1, bundle.main(["create", "--work", str(self.work), "--output",
                                                     str(self.bundles / "duplicate.tar"), "--reproduction", str(reproduction)]))
                self.assertEqual("evidence bundle: duplicate JSON key: schema_version\n", err.getvalue())
                path.write_bytes(saved)

    def test_empty_log_directory_is_refused(self):
        logs = self.root / "empty-logs"
        logs.mkdir()
        with mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, bundle.main(["create", "--work", str(self.work), "--output",
                                             str(self.bundles / "logs.tar"), "--logs", str(logs)]))
        self.assertEqual("evidence bundle: empty evidence directory: logs\n", err.getvalue())
        self.assert_nothing_written()


if __name__ == "__main__":
    unittest.main(verbosity=2)
