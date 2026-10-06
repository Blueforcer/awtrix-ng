"""The comparison must never promote partial or different builds to success."""
import copy
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

import build
import reproduce


class CompareTests(unittest.TestCase):
    def setUp(self):
        self.result = {
            "schema_version": 1,
            "status": "crossbuild_passed", "profile": "awtrix_armv7_defconfig",
            "application_commit": "a" * 40, "inputs_sha256": "b" * 64,
            "config_sha256": "c" * 64,
            "artifacts": {"output/images/rootfs.tar": {"bytes": 100, "sha256": "d" * 64},
                          "output/target/usr/bin/awtrix-linux": {"bytes": 50, "sha256": "e" * 64}},
        }

    def test_matching_clean_outputs(self):
        self.assertEqual(reproduce.compare(self.result, copy.deepcopy(self.result)), self.result["artifacts"])

    def test_refuses_failed_partial_or_missing_build(self):
        for status in ("failed", "building", None):
            with self.subTest(status=status), self.assertRaises(build.BuildError):
                other = dict(self.result, status=status)
                reproduce.compare(self.result, other)
        with self.assertRaises(build.BuildError):
            reproduce.compare(dict(self.result, artifacts={}), self.result)

    def test_refuses_different_or_unidentified_inputs(self):
        for key in ("profile", "application_commit", "inputs_sha256", "config_sha256"):
            for value in (None, "different"):
                with self.subTest(key=key, value=value), self.assertRaises(build.BuildError):
                    reproduce.compare(self.result, dict(self.result, **{key: value}))

    def test_refuses_different_hash_size_or_artifact_set(self):
        for key, value in (("sha256", "e" * 64), ("bytes", 101)):
            other = copy.deepcopy(self.result)
            other["artifacts"]["output/images/rootfs.tar"][key] = value
            with self.subTest(key=key), self.assertRaises(build.BuildError):
                reproduce.compare(self.result, other)
        other = copy.deepcopy(self.result)
        other["artifacts"]["unexpected"] = {"sha256": "d" * 64, "bytes": 100}
        with self.assertRaises(build.BuildError):
            reproduce.compare(self.result, other)

    def test_identically_wrong_or_malformed_artifact_sets_do_not_pass(self):
        for artifacts in ({"../private": {"bytes": 5, "sha256": "d" * 64}},
                          {"output/images/rootfs.tar": {"bytes": 100, "sha256": "d" * 64}},
                          {key: {"bytes": True, "sha256": "d" * 64} for key in reproduce.ARTIFACTS}):
            invalid = dict(self.result, artifacts=artifacts)
            with self.subTest(artifacts=artifacts), self.assertRaises(build.BuildError):
                reproduce.compare(invalid, copy.deepcopy(invalid))


class ExistingRunTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="awtrix-reproduction-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.root / "source"
        self.destination = self.root / "reproduction"
        for name, content in {"application/src/main.cpp": b"application", "external/Config.in": b"recipe",
                              "buildroot/Makefile": b"build tools", "downloads/buildroot-test.tar.xz": b"archive",
                              "application.tar": b"application archive",
                              build.MARKER: b'{"schema_version":1,"purpose":"awtrix-armv7-build"}'}.items():
            self.put(self.source / name, content)
        self.inputs = {
            "schema_version": 1, "profile": "awtrix_armv7_defconfig", "application_commit": "a" * 40,
            "source_date_epoch": 1234567890,
            "source_lock": {"buildroot": {"version": "test", "sha256": build.digest(self.source / "downloads/buildroot-test.tar.xz")}},
            "application_archive_sha256": build.digest(self.source / "application.tar"),
            "application_files": build.tree_hashes(self.source / "application"),
            "external_files": build.tree_hashes(self.source / "external"),
            "buildroot_files": build.tree_hashes(self.source / "buildroot"),
        }
        build.write_json(self.source / "inputs.json", self.inputs)
        self.inputs_sha = build.digest(self.source / "inputs.json")
        results = []
        for name in ("first", "second"):
            work = self.destination / name
            shutil.copytree(self.source, work)
            self.completed_build(work)
            results.append(json.loads((work / "build-result.json").read_text()))
            self.put(self.destination / (name + ".log"), b"trusted local build log")
        self.put(self.destination / "runner/build.py", b"trusted original runner")
        self.put(self.destination / "host-packages.txt", b"fixture-package\t1.0\n")
        self.image_id = "sha256:" + "c" * 64
        self.runner_sha = build.digest(self.destination / "runner/build.py")
        self.evidence = {
            "schema_version": 1, "status": "reproduced", "clean_builds": 2,
            "image_id": self.image_id, "runner_sha256": self.runner_sha,
            "host_packages_sha256": build.digest(self.destination / "host-packages.txt"),
            "internal_work_path": "/work", "network_during_build": False,
            "application_commit": self.inputs["application_commit"], "inputs_sha256": self.inputs_sha,
            "config_sha256": results[0]["config_sha256"], "artifacts": reproduce.compare(*results),
            "bit_reproducibility_tested": True,
        }
        build.write_json(self.destination / "reproduction.json", self.evidence)

    @staticmethod
    def put(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def completed_build(self, work):
        header = bytearray(20)
        header[:6] = b"\x7fELF\x01\x01"
        header[18:20] = b"\x28\x00"
        self.put(work / "output/target/usr/bin/awtrix-linux", bytes(header) + b"ARM application")
        self.put(work / "output/images/rootfs.tar", b"matching root filesystem")
        self.put(work / "output/.config", b"BR2_arm=y\n")
        build.record_result(work, self.inputs)

    def verify(self):
        with mock.patch("sys.stdout", new_callable=io.StringIO):
            return reproduce.verify_existing(self.source, self.destination, self.image_id, self.inputs_sha, self.runner_sha)

    def test_post_run_verification_preserves_original_and_records_trust_boundary(self):
        original = (self.destination / "reproduction.json").read_bytes()
        with mock.patch.object(reproduce.subprocess, "run", side_effect=AssertionError("must not recompile")):
            report = self.verify()
        self.assertEqual(original, (self.destination / "reproduction.json").read_bytes())
        self.assertEqual("post-run-integrity-check", report["verification_mode"])
        self.assertIn("no authenticated Docker", report["process_provenance"])
        self.assertEqual({"first", "second"}, set(report["verified_outputs"]))
        self.assertFalse(report["tc002_hardware_verified"])

    def test_rejects_changed_source_even_if_its_manifest_is_rewritten(self):
        self.put(self.source / "application/src/main.cpp", b"changed source")
        with self.assertRaises(build.BuildError):
            self.verify()
        changed = dict(self.inputs, application_files=build.tree_hashes(self.source / "application"))
        build.write_json(self.source / "inputs.json", changed)
        with self.assertRaisesRegex(build.BuildError, "checksum mismatch: inputs.json"):
            self.verify()

    def test_rejects_build_mutating_its_frozen_tree(self):
        self.put(self.destination / "second/external/Config.in", b"changed during build")
        with self.assertRaisesRegex(build.BuildError, "frozen external inputs changed"):
            self.verify()

    def test_rejects_result_rewriting_inputs_hash(self):
        work = self.destination / "first"
        changed = dict(self.inputs, source_date_epoch=1)
        build.write_json(work / "inputs.json", changed)
        build.record_result(work, changed)
        with self.assertRaisesRegex(build.BuildError, "checksum mismatch: inputs.json"):
            self.verify()

    def test_rejects_corrupt_artifact_stale_config_and_escaping_path(self):
        work = self.destination / "first"
        original = (work / "build-result.json").read_bytes()
        for filename in ("output/images/rootfs.tar", "output/.config"):
            path = work / filename
            content = path.read_bytes()
            path.write_bytes(b"corrupt")
            with self.subTest(path=filename), self.assertRaises(build.BuildError):
                self.verify()
            path.write_bytes(content)
        result = json.loads(original)
        result["artifacts"]["../private"] = result["artifacts"].pop("output/images/rootfs.tar")
        build.write_json(work / "build-result.json", result)
        with self.assertRaisesRegex(build.BuildError, "both expected"):
            self.verify()

    def test_rejects_mutated_runner_host_inventory_and_image_identity(self):
        for filename in ("runner/build.py", "host-packages.txt"):
            path = self.destination / filename
            content = path.read_bytes()
            path.write_bytes(b"changed")
            with self.subTest(path=filename), self.assertRaises(build.BuildError):
                self.verify()
            path.write_bytes(content)
        self.image_id = "sha256:" + "d" * 64
        with self.assertRaisesRegex(build.BuildError, "image ID"):
            self.verify()

    def test_rejects_incomplete_process_record(self):
        self.evidence["clean_builds"] = 1
        build.write_json(self.destination / "reproduction.json", self.evidence)
        with self.assertRaisesRegex(build.BuildError, "two completed"):
            self.verify()
        self.assertFalse((self.destination / "reproduction-verified.json").exists())

    def test_requires_trusted_original_pins(self):
        with self.assertRaisesRegex(build.BuildError, "trusted original inputs"):
            reproduce.verify_existing(self.source, self.destination, self.image_id, None, self.runner_sha)
        with self.assertRaisesRegex(build.BuildError, "trusted runner hash"):
            reproduce.verify_existing(self.source, self.destination, self.image_id, self.inputs_sha, None)

    def test_symlinked_build_directory_cannot_masquerade_as_second_clean_build(self):
        probe = self.root / "probe"
        try:
            probe.symlink_to(self.destination / "first", target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symlinks unavailable: {error}")
        with self.assertRaisesRegex(build.BuildError, "real directory"):
            reproduce.verify_output(probe, self.inputs, self.inputs_sha)

    def test_post_run_verification_reports_the_current_lock_without_refusing(self):
        lock = self.root / "fixture.lock"
        self.evidence["host_packages_lock_sha256"] = "e" * 64
        build.write_json(self.destination / "reproduction.json", self.evidence)
        for content, matches in ((b"fixture-package\t1.0\n", True), (b"fixture-package\t1.1\n", False),
                                 (b"fixture-package\t1.0\nother-package\t2\n", False)):
            with self.subTest(lock=content):
                self.put(lock, content)
                with mock.patch.object(reproduce, "LOCK", lock):
                    report = self.verify()
                self.assertEqual("reproduced", report["status"])
                self.assertEqual({"lock_sha256": build.digest(lock), "inventory_matches": matches},
                                 report["host_packages_lock_check"])
                self.assertEqual("e" * 64, report["host_packages_lock_sha256"])
                self.assertEqual(report, json.loads((self.destination / "reproduction-verified.json").read_text()))
        self.put(self.destination / "host-packages.txt", b"ii  fixture-package  1.0  amd64\r\n")
        self.evidence["host_packages_sha256"] = build.digest(self.destination / "host-packages.txt")
        build.write_json(self.destination / "reproduction.json", self.evidence)
        self.put(lock, b"fixture-package\t1.0\n")
        with mock.patch.object(reproduce, "LOCK", lock):
            report = self.verify()
        self.assertEqual({"lock_sha256": build.digest(lock), "inventory_matches": False}, report["host_packages_lock_check"])

    def orchestrate(self, destination, inventory):
        """Run the fresh orchestrator against a fixture lock; records the docker calls in self.calls."""
        def fake_docker(command, **_):
            mount = next(x for x in command if x.startswith("type=bind,source=") and x.endswith(",target=/work"))
            work = Path(mount.removeprefix("type=bind,source=").removesuffix(",target=/work"))
            self.completed_build(work)
            if work.name == "second":
                self.put(destination / "first/output/images/rootfs.tar", b"mutated while second was compiling")

        lock = self.root / "fixture.lock"
        self.put(lock, b"fixture-package\t1.0\nother-package\t2:3.4-1ubuntu1\n")
        with mock.patch.object(reproduce.sys, "platform", "linux"), \
                mock.patch.object(reproduce.os, "geteuid", return_value=1000, create=True), \
                mock.patch.object(reproduce.os, "getuid", return_value=1000, create=True), \
                mock.patch.object(reproduce.os, "getgid", return_value=1000, create=True), \
                mock.patch.object(reproduce, "LOCK", lock), \
                mock.patch.object(reproduce.subprocess, "check_output", side_effect=[self.image_id + "\n", inventory]) as output, \
                mock.patch.object(reproduce.subprocess, "run", side_effect=fake_docker) as run:
            try:
                reproduce.reproduce(self.source, destination, self.image_id, 2)
            finally:
                self.calls = (output.call_args_list, run.call_args_list, lock)

    def test_fresh_orchestrator_uses_explicit_runner_and_rechecks_first_output(self):
        destination = self.root / "new-run"
        with self.assertRaisesRegex(build.BuildError, "checksum mismatch"):
            self.orchestrate(destination, "other-package\t2:3.4-1ubuntu1\nfixture-package\t1.0\n")
        commands = self.calls[1]
        self.assertEqual(2, len(commands))
        for call in commands:
            command = call.args[0]
            self.assertEqual("/usr/bin/python3", command[command.index("--entrypoint") + 1])
            self.assertIn("/runner/tools/system/build.py", command)
            self.assertEqual("none", command[command.index("--network") + 1])
        record = json.loads((destination / "reproduction.json").read_text())
        self.assertEqual("failed", record["status"])
        self.assertEqual(build.digest(self.calls[2]), record["host_packages_lock_sha256"])
        self.assertEqual(build.digest(destination / "host-packages.txt"), record["host_packages_sha256"])
        self.assertEqual(b"other-package\t2:3.4-1ubuntu1\nfixture-package\t1.0\n",
                         (destination / "host-packages.txt").read_bytes())

    def test_image_inventory_differing_from_lock_is_refused_before_any_build(self):
        destination = self.root / "refused-run"
        with self.assertRaisesRegex(build.BuildError, r"differs from host-packages.lock: missing \[other-package\], "
                                                      r"unexpected \[intruder\], changed \[fixture-package 1.0->1.1\]"):
            self.orchestrate(destination, "fixture-package\t1.1\nintruder\t0.1\n")
        outputs, runs, _ = self.calls
        self.assertEqual(2, len(outputs))
        self.assertEqual("inspect", outputs[0].args[0][2])
        self.assertEqual("/usr/bin/dpkg-query", outputs[1].args[0][outputs[1].args[0].index("--entrypoint") + 1])
        self.assertEqual([], runs)
        self.assertFalse(destination.exists())


class HostLockTests(unittest.TestCase):
    def test_parse_packages_accepts_dpkg_query_lines(self):
        text = ("adduser\t3.137ubuntu1\ngcc-13-base:amd64\t13.3.0-6ubuntu2~24.04\n"
                "libssl3t64:amd64\t3.0.13-0ubuntu3.5\nzlib1g\t1:1.3.dfsg-3.1ubuntu2.1\n")
        self.assertEqual({"adduser": "3.137ubuntu1", "gcc-13-base:amd64": "13.3.0-6ubuntu2~24.04",
                          "libssl3t64:amd64": "3.0.13-0ubuntu3.5", "zlib1g": "1:1.3.dfsg-3.1ubuntu2.1"},
                         reproduce.parse_packages(text, "fixture"))
        self.assertEqual({"a": "1"}, reproduce.parse_packages("a\t1", "fixture"))

    def test_parse_packages_rejects_malformed_and_empty_inventories(self):
        for invalid, line in (("a 1\n", 1), ("a\t1\na\t2\n", 2), ("a\t1\r\n", 1), ("a\t1 \n", 1),
                              ("a\t1\n\tb\n", 2), ("a\t\n", 1), ("a\t1\nb\t2\tc\n", 2)):
            with self.subTest(text=invalid):
                with self.assertRaisesRegex(build.BuildError, f"invalid host package entry in fixture at line {line}"):
                    reproduce.parse_packages(invalid, "fixture")
        for empty in ("", "\n\n"):
            with self.subTest(text=empty):
                with self.assertRaisesRegex(build.BuildError, "empty host package inventory: fixture"):
                    reproduce.parse_packages(empty, "fixture")

    def test_check_host_packages_ignores_order_and_names_every_difference(self):
        lock = "a\t1\nb\t2\nc\t3\n"
        reproduce.check_host_packages("c\t3\na\t1\nb\t2\n", lock)
        with self.assertRaisesRegex(build.BuildError, r"missing \[c\], unexpected \[d\], changed \[b 2->9\]"):
            reproduce.check_host_packages("a\t1\nb\t9\nd\t4\n", lock)
        many = "".join(f"p{i:02d}\t1\n" for i in range(12))
        with self.assertRaisesRegex(build.BuildError, r"missing \[p00, p01, p02, p03, p04, p05, p06, p07, p08, p09, "
                                                      r"\.\.\.\], unexpected \[a\], changed \[\]"):
            reproduce.check_host_packages("a\t1\n", many)
        with self.assertRaisesRegex(build.BuildError, "invalid host package entry in host-packages.lock"):
            reproduce.check_host_packages("a\t1\n", "a 1\n")
        with self.assertRaisesRegex(build.BuildError, "invalid host package entry in image inventory"):
            reproduce.check_host_packages("a 1\n", lock)

    def test_check_host_lock_mode_needs_only_an_image(self):
        temporary = tempfile.TemporaryDirectory(prefix="awtrix-lock-")
        self.addCleanup(temporary.cleanup)
        lock = Path(temporary.name) / "fixture.lock"
        lock.write_bytes(b"a\t1\nb\t2\n")
        image_id = "sha256:" + "c" * 64
        argv = ["reproduce.py", "--check-host-lock", "--image", "awtrix-build-host:test"]
        with mock.patch.object(reproduce, "LOCK", lock), \
                mock.patch.object(reproduce.subprocess, "check_output", side_effect=[image_id + "\n", "b\t2\na\t1\n"]) as output, \
                mock.patch.object(reproduce.subprocess, "run", side_effect=AssertionError("must not build")), \
                mock.patch("sys.argv", argv), mock.patch("sys.stdout", new_callable=io.StringIO) as out:
            self.assertEqual(0, reproduce.main())
        self.assertEqual(f"host packages match host-packages.lock: {build.digest(lock)} (2 packages)\n", out.getvalue())
        self.assertEqual(["docker", "image", "inspect", "--format", "{{.Id}}", "awtrix-build-host:test"],
                         output.call_args_list[0].args[0])
        self.assertEqual(["docker", "run", "--rm", "--network", "none", "--read-only", "--entrypoint",
                          "/usr/bin/dpkg-query", image_id, "-W"], output.call_args_list[1].args[0])
        with mock.patch.object(reproduce, "LOCK", lock), \
                mock.patch.object(reproduce.subprocess, "check_output", side_effect=[image_id + "\n", "a\t1\n"]), \
                mock.patch("sys.argv", argv), mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, reproduce.main())
        self.assertIn("reproduction: host package inventory differs from host-packages.lock: missing [b]", err.getvalue())
        with mock.patch("sys.argv", ["reproduce.py", "--image", "awtrix-build-host:test"]), \
                mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(1, reproduce.main())
        self.assertIn("--work and --output are required", err.getvalue())

    def test_check_host_lock_refuses_every_other_option(self):
        base = ["reproduce.py", "--check-host-lock", "--image", "awtrix-build-host:test"]
        for extra in (["--work", "w"], ["--output", "o"], ["--jobs", "4"], ["--expected-inputs-sha256", "0" * 64],
                      ["--expected-runner-sha256", "0" * 64]):
            with self.subTest(extra=extra), mock.patch("sys.argv", base + extra), \
                    mock.patch.object(reproduce.subprocess, "check_output", side_effect=AssertionError("docker called")), \
                    mock.patch("sys.stderr", new_callable=io.StringIO) as err:
                self.assertEqual(1, reproduce.main())
            self.assertEqual("reproduction: --check-host-lock takes only --image\n", err.getvalue())
        with mock.patch("sys.argv", base + ["--verify-existing"]), mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            with self.assertRaises(SystemExit) as raised:
                reproduce.main()
        self.assertEqual(2, raised.exception.code)
        self.assertIn("not allowed with argument", err.getvalue())
        with mock.patch("sys.argv", ["reproduce.py", "--work", "w", "--output", "o", "--image", "x"]), \
                mock.patch.object(reproduce, "reproduce") as fresh:
            self.assertEqual(0, reproduce.main())
        self.assertEqual((Path("w"), Path("o"), "x", 4), fresh.call_args.args)


class HostRecipeTests(unittest.TestCase):
    """The committed build-host recipe must stay snapshot-pinned and match the lock."""
    directory = Path(reproduce.__file__).resolve().parent

    def test_recipe_pins_base_digest_snapshot_and_user(self):
        dockerfile = (self.directory / "Dockerfile").read_text(encoding="utf-8")
        froms = [line for line in dockerfile.splitlines() if line.startswith("FROM ")]
        self.assertEqual(2, len(froms))
        digests = {line.split("@")[1].split()[0] for line in froms}
        self.assertEqual(1, len(digests))
        self.assertRegex(digests.pop(), r"^sha256:[0-9a-f]{64}$")
        self.assertTrue(froms[0].endswith(" AS trust"))
        self.assertIn("COPY --from=trust /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt", dockerfile)
        self.assertIn("COPY ubuntu.sources /etc/apt/sources.list.d/ubuntu.sources", dockerfile)
        self.assertIn("USER 1000:1000", dockerfile)
        self.assertNotIn("Verify-Peer", dockerfile)
        self.assertNotIn("ARG ", dockerfile)
        final = dockerfile.split("COPY ubuntu.sources")[1]
        self.assertNotIn("--chmod", dockerfile, "the legacy builder rejects COPY --chmod")
        self.assertRegex(final, r"^ /etc/apt/sources.list.d/ubuntu.sources\n"
                                r"RUN chmod 0644 /etc/apt/sources.list.d/ubuntu.sources && apt-get update && ")
        for package in ("build-essential", "git", "python3", "rsync", "patch", "bc", "bison", "flex", "file", "unzip",
                        "cpio", "wget", "xz-utils", "ca-certificates", "cmake", "libssl-dev", "openssl",
                        "qemu-user", "mosquitto"):
            self.assertRegex(final, rf"\s{package}[\s\\]")
        sources = (self.directory / "ubuntu.sources").read_text(encoding="utf-8")
        stanzas = [s for s in sources.split("\n\n") if s.strip()]
        self.assertEqual(2, len(stanzas))
        snapshots = set()
        for stanza in stanzas:
            fields = dict(line.split(": ", 1) for line in stanza.strip().splitlines())
            self.assertEqual({"Types", "URIs", "Suites", "Components", "Signed-By", "Snapshot"}, set(fields))
            self.assertEqual("deb", fields["Types"])
            self.assertRegex(fields["Snapshot"], r"^\d{8}T\d{6}Z$")
            snapshots.add(fields["Snapshot"])
        self.assertEqual(1, len(snapshots))
        self.assertIn("http://archive.ubuntu.com/ubuntu/", sources)
        self.assertIn("http://security.ubuntu.com/ubuntu/", sources)
        self.assertNotIn("\r", sources)

    def test_committed_lock_parses_and_names_installed_tools(self):
        data = (self.directory / "host-packages.lock").read_bytes()
        self.assertNotIn(b"\r", data)
        self.assertTrue(data.endswith(b"\n"))
        packages = reproduce.parse_packages(data.decode("utf-8"), "host-packages.lock")
        self.assertEqual(sorted(packages, key=lambda name: name.split(":")[0]), list(packages), "raw dpkg-query order")
        for name in ("build-essential", "cmake", "python3", "qemu-user", "mosquitto", "openssl", "ca-certificates", "git"):
            self.assertIn(name, packages)
        self.assertEqual(reproduce.LOCK, self.directory / "host-packages.lock")


if __name__ == "__main__":
    unittest.main()
