#!/usr/bin/env python3
"""Unsigned AWUPD003, adversarial container, and Linux filesystem contracts."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "update"))
import package  # noqa: E402
import bundle  # noqa: E402  (tools/tc002/install, on the path package.py set)

VERIFIER = None
FAULTS = None
QEMU = None
SYSROOT = None
TARGET = "experimental:test-fixture"


class UpdateContracts(unittest.TestCase):
    def test_shared_target_vectors(self):
        vectors = json.loads(Path(__file__).with_name("target_vectors.json").read_text())
        for row in vectors:
            with self.subTest(target=row["target"]):
                self.assertEqual(row["allowed"], package.allowed_target(row["target"]))

    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="awtrix-update-tests-")
        cls.root = Path(cls.workspace.name)
        cls.payload = cls.root / "payload.bin"
        cls.payload_bytes = bytes(range(256)) * 1024 + b"\x00\xff\r\nnon-archive payload"
        cls.payload.write_bytes(cls.payload_bytes)
        cls.container = cls.root / "valid.awup"
        cls.metadata = package.create(cls.payload, cls.container, TARGET, "1.2.3-test", 7)
        cls.bytes = cls.container.read_bytes()
        cls.manifest_size = struct.unpack_from(">I", cls.bytes, 12)[0]

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=self.root)
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def invoke(self, container=None, target=TARGET, counter=6, stage=None,
               extra=(), preexec=None, env=None):
        args = [str(VERIFIER), "--package", str(container or self.container),
                "--target", target, "--current-counter", str(counter), *extra]
        if stage is not None:
            args += ["--stage-dir", str(stage)]
        if QEMU:
            # Preload the ARM fixture inside the emulated loader, never into
            # the host QEMU executable. All arguments bypass a shell.
            env = dict(os.environ if env is None else env)
            prefix = [str(QEMU), "-L", str(SYSROOT)]
            preload = env.pop("LD_PRELOAD", None)
            if preload:
                prefix += ["-E", "LD_PRELOAD=" + preload]
            args = prefix + args
        result = subprocess.run(args, capture_output=True, text=True, timeout=15, preexec_fn=preexec, env=env)
        return result, json.loads(result.stdout) if result.stdout else None

    def reject(self, container=None, **kwargs):
        result, response = self.invoke(container, **kwargs)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertFalse(response["ok"])
        return response

    def mutate(self, offset, value):
        data = bytearray(self.bytes)
        data[offset:offset + len(value)] = value
        path = self.directory / "mutated.awup"
        path.write_bytes(data)
        return path

    def rehash(self, data):
        data[self.manifest_size:self.manifest_size + 32] = hashlib.sha256(data[:self.manifest_size]).digest()
        path = self.directory / "rehashed.awup"
        path.write_bytes(data)
        return path

    def test_valid_unsigned_format_and_metadata(self):
        result, data = self.invoke()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(data["ok"])
        for key in ("target", "release", "counter", "payload_sha256", "payload_bytes"):
            self.assertEqual(data[key], self.metadata[key])
        self.assertEqual(data["staged_file"], "")
        self.assertNotIn("key_id", data)
        self.assertNotIn("key_id", self.metadata)
        self.assertEqual(self.metadata["format"], 3)
        self.assertEqual(package.HEADER.size, 68)
        self.assertEqual(self.bytes[:12], b"AWUPD003\x00\x03\x00\x00")
        self.assertEqual(self.bytes[self.manifest_size:self.manifest_size + 32],
                         hashlib.sha256(self.bytes[:self.manifest_size]).digest())
        self.assertEqual(self.bytes[self.manifest_size + 32:], self.payload_bytes)

    def test_stage_complete_bytes_and_no_overwrite(self):
        result, data = self.invoke(stage=self.directory)
        self.assertEqual(result.returncode, 0, result.stdout)
        path = Path(data["staged_file"])
        self.assertEqual(path.read_bytes(), self.bytes)
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(list(self.directory.iterdir()), [path])
        self.reject(stage=self.directory)
        self.assertEqual(path.read_bytes(), self.bytes)
        self.assertEqual(list(self.directory.iterdir()), [path])

    def test_utf8_staging_path_roundtrips_json(self):
        stage = self.directory / "pr\u00fcfung"
        stage.mkdir(mode=0o700)
        result, data = self.invoke(stage=stage)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(Path(data["staged_file"]).parent, stage)
        self.assertEqual(Path(data["staged_file"]).read_bytes(), self.bytes)

    def test_manifest_corruption_does_not_publish(self):
        changes = [(24, struct.pack(">Q", 8)), (68 + len(TARGET), b"9"),
                   (36, bytes([self.bytes[36] ^ 1])),
                   (self.manifest_size, bytes([self.bytes[self.manifest_size] ^ 1]))]
        stage = self.directory / "stage"
        stage.mkdir(mode=0o700)
        for offset, value in changes:
            with self.subTest(offset=offset):
                response = self.reject(self.mutate(offset, value), stage=stage)
                self.assertEqual(response["code"], "digest")
                self.assertIn("manifest digest mismatch", response["error"])
                self.assertEqual(list(stage.iterdir()), [])

    def test_recomputed_hashes_are_integrity_checks_without_authentication(self):
        data = bytearray(self.bytes)
        data[-1] ^= 1
        data[36:68] = hashlib.sha256(data[self.manifest_size + 32:]).digest()
        result, response = self.invoke(self.rehash(data))
        self.assertEqual(result.returncode, 0, response)
        self.assertNotEqual(response["payload_sha256"], self.metadata["payload_sha256"])

    def test_old_signed_format_is_explicitly_rejected(self):
        header = struct.Struct(">8sHHIQQHH32s32s")
        target, release = TARGET.encode(), b"1.2.3-test"
        size = header.size + len(target) + len(release)
        manifest = header.pack(b"AWUPD001", 1, 0, size, len(self.payload_bytes), 7,
                               len(target), len(release), hashlib.sha256(self.payload_bytes).digest(),
                               bytes(32)) + target + release
        path = self.directory / "old.awup"
        path.write_bytes(manifest + bytes(64) + self.payload_bytes)
        response = self.reject(path)
        self.assertEqual(response["code"], "format")
        self.assertIn("unsupported package format", response["error"])

    def test_payload_corruption_does_not_publish(self):
        path = self.mutate(len(self.bytes) - 1, bytes([self.bytes[-1] ^ 1]))
        stage = self.directory / "stage"
        stage.mkdir(mode=0o700)
        response = self.reject(path, stage=stage)
        self.assertIn("digest mismatch", response["error"])
        self.assertEqual(list(stage.iterdir()), [])

    def test_wrong_target_and_invalid_target_policy(self):
        self.reject(target="experimental:another-fixture")
        self.reject(target="tc002")
        self.reject(target="experimental:")

    def test_counter_replay_rollback_and_maximum(self):
        for current in [7, 8, (1 << 64) - 1]:
            with self.subTest(current=current):
                self.reject(counter=current)
        data = bytearray(self.bytes)
        struct.pack_into(">Q", data, 24, (1 << 64) - 1)
        result, response = self.invoke(self.rehash(data), counter=(1 << 64) - 2)
        self.assertEqual(result.returncode, 0, response)
        struct.pack_into(">Q", data, 24, 0)
        self.reject(self.rehash(data), counter=0)

    def test_truncations_and_trailing_data(self):
        for length in [0, 7, 67, 68, self.manifest_size - 1,
                       self.manifest_size + 31, len(self.bytes) - 1]:
            with self.subTest(length=length):
                path = self.directory / "truncated.awup"
                path.write_bytes(self.bytes[:length])
                self.reject(path)
        path = self.directory / "trailing.awup"
        path.write_bytes(self.bytes + b"\x00")
        self.reject(path)

    def test_bounds_overflow_format_and_reserved_flags(self):
        for offset, value in [(0, b"X"), (7, b"2"), (8, b"\x00\x02"), (8, b"\x00\x04"), (10, b"\x00\x01"),
                              (12, b"\xff" * 4), (16, b"\xff" * 8), (16, b"\x00" * 8),
                              (32, b"\xff\xff"), (34, b"\x00\x00")]:
            with self.subTest(offset=offset, value=value):
                self.reject(self.mutate(offset, value))
        self.reject(extra=["--max-payload-bytes", "1024"])
        self.reject(extra=["--max-payload-bytes", "0"])
        self.reject(extra=["--max-payload-bytes", str(package.MAX_PAYLOAD + 1)])

    def test_rehashed_path_traversal_or_nul_identifiers_rejected(self):
        for value in [b"/", b"\x00", b"\xff"]:
            data = bytearray(self.bytes)
            data[68 + len(TARGET)] = value[0]
            self.reject(self.rehash(data))

    def test_symlink_fifo_directory_and_unsafe_stage_rejected(self):
        link = self.directory / "link.awup"
        link.symlink_to(self.container)
        self.reject(link)
        self.reject(self.directory)
        fifo = self.directory / "pipe"
        os.mkfifo(fifo)
        self.reject(fifo)
        unsafe = self.directory / "unsafe"
        unsafe.mkdir(mode=0o755)
        self.reject(stage=unsafe)
        safe = self.directory / "safe"
        safe.mkdir(mode=0o700)
        stage_link = self.directory / "stage-link"
        stage_link.symlink_to(safe, target_is_directory=True)
        self.reject(stage=stage_link)
        self.assertEqual(list(safe.iterdir()), [])

    def test_existing_destination_symlink_never_followed(self):
        victim = self.directory / "victim"
        victim.write_bytes(b"unchanged")
        stage = self.directory / "stage"
        stage.mkdir(mode=0o700)
        destination = stage / f"7-{self.metadata['payload_sha256']}.awup"
        destination.symlink_to(victim)
        self.reject(stage=stage)
        self.assertEqual(victim.read_bytes(), b"unchanged")
        self.assertEqual(list(stage.iterdir()), [destination])

    def test_write_failure_cleans_incomplete_candidate(self):
        def limit():
            resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        self.reject(stage=self.directory, preexec=limit)
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_native_fsync_failure_never_acknowledges_commit(self):
        if FAULTS is None:
            self.skipTest("native fsync fault fixture not supplied")
        for call in (1, 2, 3):
            with self.subTest(fsync_call=call):
                stage = self.directory / str(call)
                stage.mkdir(mode=0o700)
                env = dict(os.environ, LD_PRELOAD=str(FAULTS), AWTRIX_TEST_FAIL_FSYNC_AT=str(call))
                response = self.reject(stage=stage, env=env)
                final = list(stage.glob("*.awup"))
                self.assertEqual(len(final), 0 if call == 1 else 1)
                self.assertFalse(list(stage.glob(".pending-*")))
                if final:
                    self.assertIn("uncertain", response["error"])
                    self.assertEqual(final[0].read_bytes(), self.bytes)
                    result, _ = self.invoke(final[0])
                    self.assertEqual(result.returncode, 0)

    def test_packager_fsync_failures(self):
        for failed_call in (1, 2, 3):
            with self.subTest(fsync_call=failed_call):
                output = self.directory / f"failure-{failed_call}.awup"
                calls = 0
                real = os.fsync
                def sync(fd):
                    nonlocal calls
                    calls += 1
                    if calls == failed_call:
                        raise OSError("simulated fsync failure")
                    return real(fd)
                with mock.patch.object(package.os, "fsync", side_effect=sync):
                    with self.assertRaises((OSError, ValueError)):
                        package.create(self.payload, output, TARGET, "1", 1)
                self.assertEqual(output.exists(), failed_call != 1)
                if output.exists():
                    result, _ = self.invoke(output, counter=0)
                    self.assertEqual(result.returncode, 0)
        self.assertFalse(list(self.directory.glob(".awup-build-*")))

    def test_process_crash_never_publishes_partial_file(self):
        def limit():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096))
            signal.signal(signal.SIGXFSZ, signal.SIG_DFL)
        result, _ = self.invoke(stage=self.directory, preexec=limit)
        self.assertEqual(result.returncode, -signal.SIGXFSZ)
        self.assertFalse(list(self.directory.glob("*.awup")))
        self.assertTrue(all(p.name.startswith(".pending-") for p in self.directory.iterdir()))
        result, data = self.invoke(stage=self.directory)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(Path(data["staged_file"]).read_bytes(), self.bytes)

    def test_packager_rejects_unsafe_input_output_and_directory(self):
        link = self.directory / "payload-link"
        link.symlink_to(self.payload)
        fifo = self.directory / "payload-pipe"
        os.mkfifo(fifo)
        empty = self.directory / "empty"
        empty.write_bytes(b"")
        for payload in [link, fifo, empty, self.directory]:
            with self.subTest(payload=payload):
                with self.assertRaises((OSError, ValueError)):
                    package.create(payload, self.directory / "bad.awup", TARGET, "1", 1)
        unsafe = self.directory / "unsafe"
        unsafe.mkdir(mode=0o755)
        with self.assertRaises(ValueError):
            package.create(self.payload, unsafe / "bad.awup", TARGET, "1", 1)
        output = self.directory / "existing-link.awup"
        output.symlink_to(self.payload)
        with self.assertRaises(FileExistsError):
            package.create(self.payload, output, TARGET, "1", 1)
        self.assertEqual(self.payload.read_bytes(), self.payload_bytes)
        self.assertFalse(list(self.directory.glob(".awup-build-*")))

    def test_packager_cli_has_no_key_or_openssl_dependency(self):
        output = self.directory / "cli.awup"
        args = [sys.executable, str(Path(package.__file__)), "--payload", str(self.payload),
                "--output", str(output), "--target", TARGET, "--release", "1.2.3-test", "--counter", "7"]
        result = subprocess.run(args, env=dict(os.environ, PATH=""), capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output.read_bytes(), self.bytes)
        self.assertNotIn("key_id", json.loads(result.stdout))
        for option in ["--key", "--openssl"]:
            result = subprocess.run(args + [option, "unused"], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 2, result.stderr)

    def test_packager_bounds_and_existing_output(self):
        for target, release, counter in [("tc002", "1", 1), (TARGET, "../bad", 1),
                                          (TARGET, "1", 0), (TARGET, "1", 1 << 64),
                                          (TARGET, "x" * 65, 1)]:
            with self.subTest(target=target, counter=counter):
                with self.assertRaises(ValueError):
                    package.create(self.payload, self.directory / "bad.awup", target, release, counter)
        output = self.directory / "existing.awup"
        output.write_bytes(b"do not replace")
        with self.assertRaises(FileExistsError):
            package.create(self.payload, output, TARGET, "1", 1)
        self.assertEqual(output.read_bytes(), b"do not replace")

    def test_packager_deterministic_and_verifier_cli_strict(self):
        output = self.directory / "same.awup"
        package.create(self.payload, output, TARGET, "1.2.3-test", 7)
        self.assertEqual(output.read_bytes(), self.bytes)
        for arguments in [["--install"], ["--current-counter", "-1"], ["--target", TARGET],
                          ["--current-counter", str(1 << 64)], ["--key", "unused"]]:
            result, _ = self.invoke(extra=arguments)
            self.assertEqual(result.returncode, 2)

    def test_failure_codes(self):
        self.assertEqual(self.reject(counter=7)["code"], "counter")
        self.assertEqual(self.reject(target="experimental:another-fixture")["code"], "target")
        self.assertEqual(self.reject(self.mutate(0, b"X"))["code"], "format")
        path = self.mutate(len(self.bytes) - 1, bytes([self.bytes[-1] ^ 1]))
        self.assertEqual(self.reject(path)["code"], "digest")
        self.assertEqual(self.reject(self.mutate(self.manifest_size, bytes([self.bytes[self.manifest_size] ^ 1])))["code"],
                         "digest")


def fake_image(size=8192, used=8000):
    """Bytes that pass for a release image: a squashfs superblock claiming used bytes, padded."""
    data = bytearray(size)
    data[:4] = b"hsqs"
    data[40:48] = used.to_bytes(8, "little")
    data[96:] = bytes((i * 7 + 3) & 0xff for i in range(size - 96))
    return bytes(data)


class ProductionContracts(unittest.TestCase):
    """The awtrix-ng:tc002 target: its packages carry a release image."""

    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="awtrix-update-production-")
        cls.root = Path(cls.workspace.name)
        cls.image = cls.root / "release.img"
        cls.image.write_bytes(fake_image())
        cls.other = cls.root / "payload.bin"
        cls.other.write_bytes(b"release payload " * 512)

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=self.root)
        self.directory = Path(self.temp.name)
        self.directory.chmod(0o700)

    def tearDown(self):
        self.temp.cleanup()

    def verify(self, container, target=package.PRODUCTION_TARGET, counter=0):
        args = [str(VERIFIER), "--package", str(container), "--target", target,
                "--current-counter", str(counter)]
        if QEMU:
            args = [str(QEMU), "-L", str(SYSROOT)] + args
        result = subprocess.run(args, capture_output=True, text=True, timeout=15)
        return result.returncode, json.loads(result.stdout)

    def raw(self, target, payload, magic=package.MAGIC, major=3, minor=0, record=b""):
        """A container assembled by hand, so the verifier sees what the packager refuses to write."""
        target_bytes, release_bytes = target.encode(), b"1.2.0-g1"
        size = package.HEADER.size + len(target_bytes) + len(release_bytes) + len(record)
        manifest = package.HEADER.pack(magic, major, minor, size, len(payload), 5, len(target_bytes),
                                       len(release_bytes), hashlib.sha256(payload).digest())
        manifest += target_bytes + release_bytes + record
        path = self.directory / f"raw-{major}-{minor}-{len(record)}-{len(payload)}.awup"
        path.write_bytes(manifest + hashlib.sha256(manifest).digest() + payload)
        return path

    def test_production_package_carries_a_release_image(self):
        output = self.directory / "production.awup"
        metadata = package.create(self.image, output, package.PRODUCTION_TARGET, "1.2.0-g1", 1758800000)
        self.assertEqual((metadata["format"], metadata["payload_bytes"]), (3, 8192))
        data = output.read_bytes()
        self.assertEqual(struct.unpack_from(">HHI", data, 8), (3, 0, 68 + 15 + 8))
        self.assertEqual(data[68 + 15 + 8 + 32:], self.image.read_bytes())
        code, response = self.verify(output)
        self.assertEqual(code, 0, response)
        self.assertEqual((response["target"], response["release"], response["payload_bytes"]),
                         ("awtrix-ng:tc002", "1.2.0-g1", 8192))
        self.assertNotIn("install_bytes", response)
        code, response = self.verify(output, counter=1758800000)
        self.assertEqual((code, response["code"]), (1, "counter"))
        code, response = self.verify(output, target="experimental:test-fixture")
        self.assertEqual((code, response["code"]), (1, "target"))
        for target in ["awtrix-ng:tc001", "awtrix-ng", "tc002"]:
            with self.subTest(target=target), self.assertRaises(ValueError):
                package.create(self.image, self.directory / "target.awup", target, "1", 1)

    def test_production_payload_must_be_a_release_image(self):
        with self.assertRaisesRegex(ValueError, "release image"):
            package.create(self.other, self.directory / "other.awup", package.PRODUCTION_TARGET, "1", 1)
        self.assertFalse((self.directory / "other.awup").exists())
        for payload in [self.other.read_bytes(), fake_image(12288, 8000), fake_image(4096, 8000),
                        fake_image(64, 60), fake_image(8192, 95)]:
            with self.subTest(size=len(payload)):
                code, response = self.verify(self.raw(package.PRODUCTION_TARGET, payload))
                self.assertEqual((code, response["code"]), (1, "format"))
                self.assertIn("not a release image", response["error"])
        code, response = self.verify(self.raw(package.PRODUCTION_TARGET, fake_image(8000, 8000)))
        self.assertEqual(code, 0, response)
        code, response = self.verify(self.raw("experimental:x", self.other.read_bytes()), target="experimental:x")
        self.assertEqual(code, 0, response)

    def test_verifier_refuses_the_earlier_formats(self):
        payload = self.image.read_bytes()
        install = struct.pack(">QII", 4096, 3, 0)
        for magic, major, minor, record in [(b"AWUPD002", 2, 1, install), (b"AWUPD002", 2, 0, b""),
                                            (package.MAGIC, 2, 0, b""), (package.MAGIC, 3, 1, install),
                                            (package.MAGIC, 3, 0, install)]:
            with self.subTest(magic=magic, major=major, minor=minor):
                code, response = self.verify(self.raw(package.PRODUCTION_TARGET, payload, magic, major, minor,
                                                      record))
                self.assertEqual((code, response["code"]), (1, "format"))

    def release_dir(self, counter=1758800000):
        """An installed release, written from a bundle the way build_bundle.sh makes one."""
        source = self.directory / "bundle"
        files = {"bin/awtrix-linux": os.urandom(6000) + bytes(9000),
                 "share/index.html.gz": b"<html>" * 700,
                 "lib/modules/x.ko": bytes(100)}
        for path, data in files.items():
            target = source / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        bundle.write_manifest(source, "1.2.0", "abcdef0", False, counter, host_only=(), required=())
        root = self.directory / "release"
        return root, files, bundle.write_release(source, root)

    def test_release_package_carries_the_squashfs_of_the_release(self):
        root, files, manifest = self.release_dir()
        output = self.directory / "release.awup"
        metadata = package.create_release(root, output)
        self.assertEqual((metadata["release"], metadata["counter"], metadata["target"]),
                         (manifest["release"], manifest["counter"], "awtrix-ng:tc002"))
        code, response = self.verify(output)
        self.assertEqual(code, 0, response)
        data = output.read_bytes()
        size = struct.unpack_from(">I", data, 12)[0]
        image = data[size + 32:]
        self.assertTrue(bundle.image_valid(image[:96], len(image)))
        self.assertEqual(len(image) % 4096, 0)
        self.assertEqual(int.from_bytes(image[8:12], "little"), manifest["counter"], "mkfs time")
        unsquashfs = shutil.which("unsquashfs")
        if unsquashfs:
            (self.directory / "image").write_bytes(image)
            tree = self.directory / "tree"
            subprocess.run([unsquashfs, "-q", "-n", "-d", str(tree), str(self.directory / "image")],
                           check=True, capture_output=True)
            listed = sorted(p.relative_to(tree).as_posix() for p in tree.rglob("*") if p.is_file())
            self.assertEqual(listed, sorted([*files, "manifest.json"]))
            for path, content in files.items():
                self.assertEqual((tree / path).read_bytes(), content)
            self.assertEqual((tree / "manifest.json").read_bytes(), (root / "manifest.json").read_bytes())
            self.assertEqual((tree / "bin/awtrix-linux").stat().st_mode & 0o777, 0o755)
            self.assertEqual((tree / "lib/modules/x.ko").stat().st_mode & 0o777, 0o644)
        again = self.directory / "again.awup"
        package.create_release(root, again)
        self.assertEqual(again.read_bytes(), data, "packaging is deterministic")
        self.assertFalse(list(self.directory.glob(".awup-*")), "no construction leftovers")

    def test_release_directory_must_match_its_manifest(self):
        refused = (ValueError, package.BundleError)
        root, files, manifest = self.release_dir()
        (root / "share/extra.txt").write_text("not listed")
        with self.assertRaises(refused):
            package.create_release(root, self.directory / "bad.awup")
        (root / "share/extra.txt").unlink()
        with self.assertRaises(refused):
            package.create_release(root, self.directory / "bad.awup", counter=manifest["counter"] + 1)
        with self.assertRaises(refused):
            package.create_release(root, self.directory / "bad.awup", release="other")
        (root / "lib/modules/x.ko").write_bytes(bytes(101))
        with self.assertRaises(refused):
            package.create_release(root, self.directory / "bad.awup")
        (root / "lib/modules/x.ko").write_bytes(bytes(100))
        (root / "lib/modules/link.ko").symlink_to("x.ko")
        with self.assertRaises(refused):
            package.create_release(root, self.directory / "bad.awup")
        (root / "lib/modules/link.ko").unlink()
        package.create_release(root, self.directory / "good.awup")
        self.assertFalse(list(self.directory.glob(".awup-*")), "no construction leftovers")

    def test_release_image_needs_a_32_bit_counter(self):
        root, _, _ = self.release_dir(counter=1 << 32)
        with self.assertRaisesRegex(package.BundleError, "32-bit"):
            package.create_release(root, self.directory / "late.awup")

    def test_a_bundle_is_no_release_directory(self):
        root, _, _ = self.release_dir()
        with self.assertRaisesRegex(package.BundleError, "is a bundle"):
            package.create_release(self.directory / "bundle", self.directory / "bundle.awup")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--verifier", required=True, type=Path)
    parser.add_argument("--faults", type=Path)
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--sysroot", type=Path)
    args = parser.parse_args()
    if bool(args.qemu) != bool(args.sysroot):
        parser.error("--qemu and --sysroot must be supplied together")
    VERIFIER = args.verifier.resolve()
    FAULTS = args.faults.resolve() if args.faults else None
    QEMU = args.qemu.resolve() if args.qemu else None
    SYSROOT = args.sysroot.resolve() if args.sysroot else None
    unittest.main(argv=[__file__], verbosity=2)
