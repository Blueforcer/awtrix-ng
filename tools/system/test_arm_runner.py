"""Offline ARM-runner evidence contracts; no compiler, emulator or network runs."""
from __future__ import annotations

import ast
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

import run_arm_contracts as arm


class ArmRunnerContracts(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="awtrix-arm-runner-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.target = self.root / "output/target"
        header = bytearray(20)
        header[:6] = b"\x7fELF\x01\x01"
        header[18:20] = b"\x28\x00"
        self.files = {
            "usr/bin/awtrix-linux": bytes(header) + b"application",
            "usr/bin/awtrix-update-verify": bytes(header) + b"verifier",
            "lib/libc.so": b"target runtime",
        }
        for name, data in self.files.items():
            self.put("output/target/" + name, data)
        self.put("output/target/THIS_IS_NOT_YOUR_ROOT_FILESYSTEM", b"Buildroot warning")
        self.archive()
        self.compiler = self.put("output/host/bin/arm-buildroot-linux-musleabihf-g++", b"compiler")
        self.emulator = self.put("qemu-arm", b"emulator")
        self.scripts = {name: "a" * 64 for name in arm.TEST_INPUTS}
        self.result = {"application_commit": "b" * 40, "inputs_sha256": "c" * 64,
                       "artifacts": {"output/images/rootfs.tar": {"sha256": "d" * 64}}}
        self.commands = []

    def put(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def archive(self, extras=()):
        path = self.root / "output/images/rootfs.tar"
        path.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(path, "w") as archive:
            directory = tarfile.TarInfo(".")
            directory.type = tarfile.DIRTYPE
            archive.addfile(directory)
            for name, data in [*self.files.items(), *extras]:
                info = tarfile.TarInfo("./" + name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))

    def fake_run(self, command, **options):
        self.assertTrue(options.get("check"))
        self.commands.append(command)
        if command[0] == str(self.compiler):
            Path(command[command.index("-o") + 1]).write_bytes(b"compiled ARM fault fixture")
        if "--binary" in command:
            wrapper = Path(command[command.index("--binary") + 1])
            tree = ast.parse(wrapper.read_text(encoding="utf-8"))
            call = tree.body[1].value
            self.assertEqual(ast.literal_eval(call.args[0]), str(self.emulator))
            self.assertEqual(ast.literal_eval(call.args[1].left),
                             [str(self.emulator), "-L", str(self.target),
                              str(self.target / "usr/bin/awtrix-linux")])
        return subprocess.CompletedProcess(command, 0)

    def invoke(self, run=None, scripts=None, verify=None):
        with mock.patch.object(arm.sys, "platform", "linux"), \
             mock.patch.object(arm.os, "geteuid", return_value=1000, create=True), \
             mock.patch.object(arm.shutil, "which", return_value=str(self.emulator)), \
             mock.patch.object(arm.qualify, "verify_build", side_effect=verify or [(dict(), self.result)] * 2), \
             mock.patch.object(arm, "test_inputs", side_effect=scripts or [self.scripts] * 2), \
             mock.patch.object(arm.subprocess, "run", side_effect=run or self.fake_run), \
             mock.patch.object(arm.subprocess, "check_output", return_value="qemu-arm test version\n"), \
             mock.patch("sys.stdout", new_callable=io.StringIO), \
             mock.patch("sys.stderr", new_callable=io.StringIO):
            return arm.main(["--work", str(self.root)])

    def test_sysroot_matches_archive_with_only_buildroot_warning_excluded(self):
        files = arm.runtime_files(self.root)
        self.assertEqual(set(files), set(self.files))

    def test_rejects_replaced_runtime_verifier_and_extra_library(self):
        for name in ["lib/libc.so", "usr/bin/awtrix-update-verify", "lib/injected.so"]:
            with self.subTest(name=name):
                path = self.put("output/target/" + name, b"replacement")
                with self.assertRaises(arm.build.BuildError):
                    arm.runtime_files(self.root)
                if name in self.files:
                    path.write_bytes(self.files[name])
                else:
                    path.unlink()

    def test_rejects_foreign_architecture_even_when_archive_matches(self):
        name = "usr/bin/awtrix-update-verify"
        self.files[name] = b"not an ARM executable"
        self.put("output/target/" + name, self.files[name])
        self.archive()
        with self.assertRaisesRegex(arm.build.BuildError, "not ARM"):
            arm.runtime_files(self.root)

    def test_rejects_duplicate_and_traversing_rootfs_entries(self):
        for name in ["lib/libc.so", "../outside", "/absolute"]:
            with self.subTest(name=name):
                self.archive([(name, b"bad")])
                with self.assertRaises(arm.build.BuildError):
                    arm.runtime_files(self.root)

    def test_update_harness_gets_actual_arm_binary_qemu_and_sysroot(self):
        self.assertEqual(self.invoke(), 0)
        update = next(command for command in self.commands if "--verifier" in command)
        self.assertEqual(update[update.index("--verifier") + 1],
                         str(self.target / "usr/bin/awtrix-update-verify"))
        self.assertEqual(update[update.index("--qemu") + 1], str(self.emulator))
        self.assertEqual(update[update.index("--sysroot") + 1], str(self.target))
        report = json.loads((self.root / "arm-contracts.json").read_text())
        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["test_inputs_sha256"], self.scripts)
        self.assertEqual(report["artifacts"], self.result["artifacts"])
        self.assertFalse(report["tc002_hardware_verified"])
        contract = next(command for command in self.commands if "--require-tls" in command)
        self.assertIn("--require-mqtt", contract)

    def test_failed_suite_invalidates_previous_success_and_stops(self):
        self.put("arm-contracts.json", b'{"status":"passed"}')
        def fail(command, **options):
            if "--binary" in command:
                raise subprocess.CalledProcessError(1, command)
            return self.fake_run(command, **options)
        self.assertEqual(self.invoke(run=fail), 1)
        report = json.loads((self.root / "arm-contracts.json").read_text())
        self.assertEqual(report["status"], "failed")
        self.assertFalse(any("--verifier" in command for command in self.commands))

    def test_script_mutation_cannot_publish_success(self):
        changed = dict(self.scripts, **{"tools/update/package.py": "f" * 64})
        self.assertEqual(self.invoke(scripts=[self.scripts, changed]), 1)
        self.assertEqual(json.loads((self.root / "arm-contracts.json").read_text())["status"], "failed")

    def test_gif_fixture_bytes_are_bound_to_contract_evidence(self):
        fixture = "scripts/test_dynamic_gif_api.py"
        for name in arm.TEST_INPUTS:
            self.put("test-sources/" + name, b"original test source")
        with mock.patch.object(arm, "REPO", self.root / "test-sources"):
            before = arm.test_inputs()
            self.assertIn(fixture, before)
            self.put("test-sources/" + fixture, b"changed GIF pixels")
            after = arm.test_inputs()
        self.assertNotEqual(before[fixture], after[fixture])
        self.assertEqual(self.invoke(scripts=[before, after]), 1)
        self.assertEqual(json.loads((self.root / "arm-contracts.json").read_text())["status"], "failed")

    def test_runtime_mutation_during_suite_cannot_publish_success(self):
        def change(command, **options):
            result = self.fake_run(command, **options)
            if "--verifier" in command:
                self.put("output/target/lib/libc.so", b"changed during emulation")
            return result
        self.assertEqual(self.invoke(run=change), 1)

    def test_input_manifest_mutation_cannot_publish_success(self):
        self.assertEqual(self.invoke(verify=[({}, self.result), ({"changed": True}, self.result)]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
