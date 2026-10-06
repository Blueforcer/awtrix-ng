"""Contracts for tools/linux/provision.py against the real awtrix-linux.

Requires Linux, a non-root user, openssl and a built awtrix-linux binary. Every
file lives in a private temporary directory on a Linux filesystem; the suite
fails instead of skipping when a prerequisite is missing.
"""
from __future__ import annotations

import argparse
import base64
import contextlib
import errno
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import ssl
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "linux"))
import provision  # noqa: E402
from test_contract import eventually, free_port  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import stop_process, wait_ready


OPTIONS = None


def require_environment(platform, euid, openssl):
    if platform != "linux":
        raise AssertionError("provisioning contracts require Linux")
    if euid == 0:
        raise AssertionError("provisioning contracts require a non-root user")
    if not openssl:
        raise AssertionError("openssl is required; provisioning tests must not be skipped")


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = provision.main(argv)
        except SystemExit as exit:
            code = exit.code
    return code, out.getvalue(), err.getvalue()


class ProvisionCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        require_environment(sys.platform, os.geteuid(), shutil.which("openssl"))

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-provision-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.root.chmod(0o700)
        self.dir = self.root / "security"
        self.dir.mkdir(mode=0o700)
        self.port = free_port()
        self.origin = f"https://localhost:{self.port}"

    def listing(self, directory=None):
        return sorted(os.listdir(directory or self.dir))

    def generate_argv(self, directory=None, origin=None, extra=()):
        return ["generate", "--dir", str(directory or self.dir), "--origin", origin or self.origin, *extra]

    def check_argv(self, directory=None, origin=None, extra=()):
        return ["check", "--dir", str(directory or self.dir), "--origin", origin or self.origin, *extra]

    def assert_refused(self, argv, message, directory=None):
        directory = Path(directory or self.dir)
        before = self.listing(directory) if directory.is_dir() else None
        code, out, err = run_main(argv)
        self.assertEqual(1, code, err)
        self.assertEqual("", out)
        self.assertIn(message, err)
        if before is not None:
            self.assertEqual(before, self.listing(directory))
        return err

    def assert_generated(self, argv=None):
        code, out, err = run_main(argv or self.generate_argv())
        self.assertEqual(0, code, err)
        self.assertEqual("", err)
        self.assertEqual(["admin.token", "tls.crt", "tls.key"], self.listing())
        return json.loads(out)

    def openssl(self, *arguments, cwd=None, expect=0):
        result = subprocess.run(["openssl", *arguments], stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd, timeout=30)
        self.assertEqual(expect, result.returncode, result.stderr.decode(errors="replace"))
        return result.stdout


class Directories(ProvisionCase):
    def test_missing_file_and_symlinked_directories_are_refused(self):
        missing = self.root / "missing"
        self.assert_refused(self.generate_argv(missing), "missing")
        self.assert_refused(self.check_argv(missing), "missing")
        self.assertFalse(missing.exists())
        regular = self.root / "regular"
        regular.write_text("")
        self.assert_refused(self.generate_argv(regular), "not a directory")
        self.assert_refused(self.check_argv(regular), "not a directory")
        link = self.root / "link"
        link.symlink_to(self.dir)
        self.assert_refused(self.generate_argv(link), "must not be a symlink")
        for suffix in ("/", "/.", "/./", "/./.", "/.//."):
            with self.subTest(suffix=suffix):
                self.assert_refused(self.generate_argv(str(link) + suffix), "must not be a symlink")
                self.assert_refused(self.check_argv(str(link) + suffix), "must not be a symlink")
        self.assert_refused(self.check_argv(link), "must not be a symlink")
        self.assertEqual([], self.listing())

    def test_trailing_dot_components_keep_their_meaning_for_real_directories(self):
        self.assertEqual("/", provision.directory_name("/."))
        self.assertEqual(".", provision.directory_name("./."))
        self.assertEqual("a/..", provision.directory_name("a/../"))
        self.assert_generated(self.generate_argv(str(self.dir) + "/./"))
        self.assertEqual(0, run_main(self.check_argv(str(self.dir) + "/."))[0])
        result = json.loads(run_main(self.check_argv(str(self.dir / ".." / "security")))[1])
        self.assertEqual(os.path.realpath(self.dir), result["directory"])

    def test_group_or_other_writable_directories_are_refused_until_fixed(self):
        for mode in (0o770, 0o707, 0o777, 0o722):
            with self.subTest(mode=oct(mode)):
                self.dir.chmod(mode)
                self.assert_refused(self.generate_argv(), "must not be writable by group or other")
                self.assert_refused(self.check_argv(), "must not be writable by group or other")
        self.dir.chmod(0o700)
        self.assert_generated()
        self.dir.chmod(0o750)
        self.assertEqual(0, run_main(self.check_argv())[0])

    def test_foreign_owned_directory_is_refused(self):
        self.assertEqual(0, os.stat("/usr").st_uid)
        self.assert_refused(self.generate_argv("/usr"), "directory owner must be the invoking account")
        # check accepts root ownership, as the application does, and then misses the files.
        self.assert_refused(self.check_argv("/usr"), "admin.token: missing")

    def test_a_root_invocation_accepts_only_root_owned_directories_and_files(self):
        # The suite cannot run as root; substituting the effective user ID applies the same rule.
        self.assert_generated()
        empty = self.root / "empty"
        empty.mkdir(mode=0o700)
        fd = os.open(self.dir, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        self.addCleanup(os.close, fd)
        with mock.patch.object(provision.os, "geteuid", return_value=0):
            self.assert_refused(self.check_argv(), "directory owner must be the invoking account or root")
            self.assert_refused(self.generate_argv(empty), "directory owner must be the invoking account", empty)
            for name in provision.FILES:
                with self.subTest(name=name):
                    message = f"{name}: owner must be the invoking account or root"
                    with self.assertRaisesRegex(provision.ProvisionError, message):
                        provision.open_provisioned(fd, name)
        self.assertEqual(0, run_main(self.check_argv())[0])
        self.assertEqual([], self.listing(empty))

    def test_directory_inside_or_equal_to_data_is_refused_before_any_write(self):
        for data in (self.dir, str(self.dir) + "/", self.root, str(self.root) + "/"):
            with self.subTest(data=str(data)):
                self.assert_refused(self.generate_argv(extra=["--data", str(data)]), "must not be inside the application data directory")
                self.assert_refused(self.check_argv(extra=["--data", str(data)]), "must not be inside the application data directory")
        self.assert_refused(self.generate_argv(extra=["--data", str(self.root / "missing-data" / "..")]),
                            "must not be inside the application data directory")
        self.assert_generated(self.generate_argv(extra=["--data", str(self.dir / "state")]))
        self.assertEqual(0, run_main(self.check_argv(extra=["--data", str(self.dir / "state")]))[0])
        self.assertEqual(0, run_main(self.check_argv(extra=["--data", str(self.root / "sibling")]))[0])

    def test_existing_destination_entries_are_never_overwritten(self):
        for name in provision.FILES:
            for kind in ("file", "dangling symlink", "directory"):
                with self.subTest(name=name, kind=kind):
                    entry = self.dir / name
                    if kind == "file":
                        entry.write_text("keep")
                    elif kind == "dangling symlink":
                        entry.symlink_to(self.root / "nowhere")
                    else:
                        entry.mkdir()
                    self.assert_refused(self.generate_argv(), f"{name}: already exists")
                    self.assertEqual([name], self.listing())
                    if kind == "directory":
                        entry.rmdir()
                    else:
                        entry.unlink()
        self.assert_generated()


class Generate(ProvisionCase):
    def test_generates_an_accepted_set_for_a_dns_origin(self):
        code, out, err = run_main(self.generate_argv())
        self.assertEqual(0, code, err)
        self.assertEqual("", err)
        self.assertEqual(["admin.token", "tls.crt", "tls.key"], self.listing())
        for name, mode in provision.MODES.items():
            info = os.lstat(self.dir / name)
            self.assertTrue(stat.S_ISREG(info.st_mode), name)
            self.assertEqual(mode, stat.S_IMODE(info.st_mode), name)
            self.assertEqual(os.geteuid(), info.st_uid, name)
        token = (self.dir / "admin.token").read_bytes()
        self.assertEqual(65, len(token))
        self.assertRegex(token, rb"\A[0-9a-f]{64}\n\Z")
        key = (self.dir / "tls.key").read_bytes()
        self.assertTrue(key.startswith(b"-----BEGIN PRIVATE KEY-----\n"), key[:40])
        certificate = (self.dir / "tls.crt").read_bytes()
        self.assertTrue(certificate.startswith(b"-----BEGIN CERTIFICATE-----\n"))
        self.assertEqual(1, certificate.count(b"-----BEGIN CERTIFICATE-----"))
        result = json.loads(out)
        self.assertEqual(json.dumps(result, sort_keys=True) + "\n", out)
        self.assertEqual(["chain_length", "chain_verified", "days_remaining", "directory", "fingerprint_sha256", "host",
                          "not_after", "not_before", "openssl", "origin", "port", "san"], sorted(result))
        self.assertEqual("localhost", result["host"])
        self.assertEqual(self.port, result["port"])
        self.assertEqual(self.origin, result["origin"])
        self.assertEqual(os.path.realpath(self.dir), result["directory"])
        self.assertEqual(["DNS:localhost"], result["san"])
        self.assertRegex(result["fingerprint_sha256"], r"\A([0-9A-F]{2}:){31}[0-9A-F]{2}\Z")
        self.assertRegex(result["not_before"], r"\A\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\Z")
        self.assertRegex(result["not_after"], r"\A\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\Z")
        self.assertIn(result["days_remaining"], (364, 365))
        self.assertEqual(1, result["chain_length"])
        self.assertIs(False, result["chain_verified"])
        self.assertEqual(ssl.OPENSSL_VERSION, result["openssl"])
        self.assertNotIn(token[:64].decode(), out + err)
        for line in key.decode().splitlines():
            if not line.startswith("-----"):
                self.assertNotIn(line, out + err)
        text = self.openssl("x509", "-noout", "-subject", "-ext", "subjectAltName", "-in", str(self.dir / "tls.crt")).decode()
        self.assertIn("CN = localhost", text)
        self.assertIn("DNS:localhost", text)
        self.assertNotIn("IP Address", text)
        names = self.openssl("x509", "-noout", "-subject", "-issuer", "-nameopt", "RFC2253", "-in", str(self.dir / "tls.crt"))
        self.assertEqual(["subject=CN=localhost", "issuer=CN=localhost"], names.decode().splitlines())
        details = self.openssl("x509", "-noout", "-text", "-in", str(self.dir / "tls.crt")).decode()
        self.assertIn("Signature Algorithm: ecdsa-with-SHA256", details)
        self.assertIn("NIST CURVE: P-256", details)
        fingerprint = self.openssl("x509", "-noout", "-fingerprint", "-sha256", "-in", str(self.dir / "tls.crt")).decode()
        self.assertEqual("sha256 Fingerprint=" + result["fingerprint_sha256"], fingerprint.strip())

    def test_generates_an_ip_san_for_an_ipv4_origin(self):
        origin = f"https://127.0.0.1:{self.port}"
        result = self.assert_generated(self.generate_argv(origin=origin))
        self.assertEqual(["IP:127.0.0.1"], result["san"])
        self.assertEqual("127.0.0.1", result["host"])
        text = self.openssl("x509", "-noout", "-subject", "-ext", "subjectAltName", "-in", str(self.dir / "tls.crt")).decode()
        self.assertIn("IP Address:127.0.0.1", text)
        self.assertIn("CN = 127.0.0.1", text)
        self.assertEqual(0, run_main(self.check_argv(origin=origin))[0])
        self.assert_refused(self.check_argv(origin=self.origin), "tls.crt: ")

    def test_long_hosts_keep_the_san_and_fall_back_to_a_generic_subject(self):
        host = "a" * 63 + "." + "b" * 6
        result = self.assert_generated(self.generate_argv(origin=f"https://{host}:{self.port}"))
        self.assertEqual([f"DNS:{host}"], result["san"])
        text = self.openssl("x509", "-noout", "-subject", "-in", str(self.dir / "tls.crt")).decode()
        self.assertIn("CN = AWTRIX NG", text)
        self.assertEqual(0, run_main(self.check_argv(origin=f"https://{host}:{self.port}"))[0])

    def test_days_bounds_the_validity(self):
        result = self.assert_generated(self.generate_argv(extra=["--days", "30"]))
        self.assertIn(result["days_remaining"], (29, 30))

    def test_failing_openssl_leaves_nothing_and_echoes_nothing(self):
        fake = self.root / "fake-openssl"
        fake.write_text("#!/bin/sh\necho '-----BEGIN PRIVATE KEY-----' >&2\necho FAKESECRETLINE >&2\necho FAKESTDOUT\nexit 1\n")
        fake.chmod(0o700)
        err = self.assert_refused(self.generate_argv(extra=["--openssl", str(fake)]), "OpenSSL genpkey failed")
        self.assertNotIn("FAKESECRETLINE", err)
        self.assertNotIn("FAKESTDOUT", err)
        self.assertNotIn("PRIVATE KEY", err)
        failing_req = self.root / "failing-req"
        failing_req.write_text('#!/bin/sh\nif [ "$1" = genpkey ]; then exec openssl "$@"; fi\necho FAKESECRETLINE >&2\nexit 1\n')
        failing_req.chmod(0o700)
        err = self.assert_refused(self.generate_argv(extra=["--openssl", str(failing_req)]), "OpenSSL req failed")
        self.assertNotIn("FAKESECRETLINE", err)
        silent = self.root / "silent"
        silent.write_text("#!/bin/sh\nexit 0\n")
        silent.chmod(0o700)
        self.assert_refused(self.generate_argv(extra=["--openssl", str(silent)]), "OpenSSL genpkey failed")

    def test_openssl_command_output_never_reaches_the_process_output(self):
        chatty = self.root / "chatty-openssl"
        chatty.write_text('#!/bin/sh\necho OPENSSL-DIAGNOSTIC >&2\nexec openssl "$@"\n')
        chatty.chmod(0o700)
        failing = self.root / "failing-openssl"
        failing.write_text("#!/bin/sh\necho OPENSSL-DIAGNOSTIC >&2\necho OPENSSL-OUTPUT\nexit 1\n")
        failing.chmod(0o700)
        script = str(Path(provision.__file__).resolve())

        def run(directory, openssl):
            argv = [sys.executable, script, *self.generate_argv(directory, extra=["--openssl", str(openssl)])]
            return subprocess.run(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)

        generated = run(self.dir, chatty)
        self.assertEqual(0, generated.returncode, generated.stderr.decode(errors="replace"))
        self.assertEqual(b"", generated.stderr)
        self.assertNotIn(b"OPENSSL-", generated.stdout)
        self.assertEqual(["admin.token", "tls.crt", "tls.key"], self.listing())
        refused_dir = self.root / "refused"
        refused_dir.mkdir(mode=0o700)
        refused = run(refused_dir, failing)
        self.assertEqual(1, refused.returncode)
        self.assertEqual(b"", refused.stdout)
        self.assertEqual(b"generate failed: OpenSSL genpkey failed\n", refused.stderr)
        self.assertEqual([], self.listing(refused_dir))


class Atomicity(ProvisionCase):
    def faulting(self, target):
        real = provision._io

        def faulty(step, call, *args, **kwargs):
            if step == target:
                raise OSError(errno.EIO, "injected fault")
            return real(step, call, *args, **kwargs)
        return mock.patch.object(provision, "_io", faulty)

    def test_every_faulted_step_leaves_the_directory_unchanged(self):
        self.assertEqual(16, len(provision.STEPS))
        files = {"key": "tls.key", "cert": "tls.crt", "token": "admin.token", "dir": str(self.dir)}
        for step in provision.STEPS:
            with self.subTest(step=step):
                with self.faulting(step):
                    code, out, err = run_main(self.generate_argv())
                self.assertEqual(1, code, err)
                self.assertEqual("", out)
                subject = [kind for kind in files if kind in step.split("_")]
                self.assertEqual(1, len(subject), step)
                self.assertIn(files[subject[0]], err)
                self.assertIn("injected fault", err)
                self.assertEqual([], self.listing())
        self.assert_generated()

    def test_publication_is_all_or_nothing(self):
        published = []
        real = provision._io

        def observing(step, call, *args, **kwargs):
            if step == "link_cert":
                published.append(self.listing())
                raise OSError(errno.EIO, "injected fault")
            return real(step, call, *args, **kwargs)
        with mock.patch.object(provision, "_io", observing):
            code, _, err = run_main(self.generate_argv())
        self.assertEqual(1, code, err)
        self.assertEqual(1, len(published))
        self.assertIn("admin.token", published[0])
        self.assertIn("tls.key", published[0])
        self.assertNotIn("tls.crt", published[0])
        self.assertEqual([], self.listing())

    def test_failing_self_check_rolls_the_published_set_back(self):
        def failing(cert_fd, key_fd, host, ca_file):
            raise provision.ProvisionError("tls.crt: injected self-check failure")
        with mock.patch.object(provision, "verify_tls", failing):
            code, out, err = run_main(self.generate_argv())
        self.assertEqual(1, code, err)
        self.assertEqual("", out)
        self.assertIn("tls.crt: injected self-check failure", err)
        self.assertEqual([], self.listing())
        self.assert_generated()


class Arguments(ProvisionCase):
    def test_environment_guard_raises_instead_of_skipping(self):
        for platform, euid, openssl in (("win32", 1000, "/usr/bin/openssl"), ("darwin", 1000, "/usr/bin/openssl"),
                                        ("linux", 0, "/usr/bin/openssl"), ("linux", 1000, None)):
            with self.subTest(platform=platform, euid=euid, openssl=openssl):
                with self.assertRaises(AssertionError):
                    require_environment(platform, euid, openssl)
        require_environment("linux", 1000, "/usr/bin/openssl")

    def test_parse_origin_accepts_dns_names_and_ipv4_addresses(self):
        self.assertEqual(("localhost", False, 8443), provision.parse_origin("https://localhost:8443"))
        self.assertEqual(("127.0.0.1", True, 8443), provision.parse_origin("https://127.0.0.1:8443"))
        self.assertEqual(("device.example", False, 1024), provision.parse_origin("https://device.example:1024"))
        self.assertEqual(("a-b.c", False, 65535), provision.parse_origin("https://a-b.c:65535"))
        long_host = "a" * 63 + "." + "b" * 6
        self.assertEqual((long_host, False, 8443), provision.parse_origin(f"https://{long_host}:8443"))
        # Hyphens at a label edge are accepted by the application and by the IDNA codec.
        self.assertEqual(("a-.-b", False, 8443), provision.parse_origin("https://a-.-b:8443"))

    def test_parse_origin_rejects_everything_the_application_would_refuse(self):
        for origin in ("http://localhost:8443", "https://localhost", "https://localhost:80", "https://localhost:1023",
                       "https://localhost:65536", "https://localhost:08443", "https://localhost:8443/",
                       "https://localhost:8443/api", "https://localhost:8443?x=1", "https://Localhost:8443",
                       "https://[::1]:8443", "https://.localhost:8443", "https://localhost.:8443", "https://127.0.0.01:8443",
                       "https://1.2.3:8443", "https://:8443", "https://admin@localhost:8443", "https://localhost:8443:1",
                       "https://" + "a" * 254 + ":8443", "https://local host:8443", "", "localhost:8443",
                       "HTTPS://localhost:8443", "https://localhost:8443\n", "https://a..b:8443",
                       "https://" + "a" * 64 + ".b:8443", "https://b." + "a" * 64 + ":8443"):
            with self.subTest(origin=origin):
                with self.assertRaises(provision.UsageError):
                    provision.parse_origin(origin)

    def test_hosts_the_idna_codec_refuses_are_usage_errors_without_a_traceback(self):
        for host in ("a..b", "a" * 64 + ".b", "b." + "a" * 64):
            origin = f"https://{host}:{self.port}"
            for argv in (self.generate_argv(origin=origin), self.check_argv(origin=origin)):
                with self.subTest(command=argv[0], host=host[:8]):
                    code, out, err = run_main(argv)
                    self.assertEqual(2, code, err)
                    self.assertEqual("", out)
                    self.assertIn("--origin must be https://HOST:PORT", err)
                    self.assertNotIn("Traceback", err)
                    self.assertEqual([], self.listing())

    def test_validate_path_rejects_whitespace_control_characters_and_empty_values(self):
        for value in ("", "/tmp/a b", "/tmp/a\tb", "/tmp/a\nb", "/tmp/a\x7fb", "/tmp/a\x01b", " /tmp/a", "/tmp/a\r"):
            with self.subTest(value=value):
                with self.assertRaises(provision.UsageError):
                    provision.validate_path(value, "--dir")
        self.assertEqual("/tmp/ä-ö", provision.validate_path("/tmp/ä-ö", "--dir"))
        self.assertEqual("relative/dir", provision.validate_path("relative/dir", "--dir"))

    def test_main_reports_usage_errors_with_exit_2_and_empty_stdout(self):
        directory = str(self.dir)
        cases = [[], ["bogus"], ["generate"], ["check"], ["generate", "--origin", self.origin],
                 ["generate", "--dir", directory], ["check", "--dir", directory], ["check", "--origin", self.origin],
                 ["generate", "--dir", directory, "--origin", self.origin, "--days", "0"],
                 ["generate", "--dir", directory, "--origin", self.origin, "--days", "3651"],
                 ["generate", "--dir", directory, "--origin", self.origin, "--days", "x"],
                 ["generate", "--dir", directory, "--origin", self.origin, "--openssl", str(self.root / "missing-openssl")],
                 ["generate", "--dir", directory, "--origin", "http://localhost:8443"],
                 ["check", "--dir", directory, "--origin", "https://localhost"],
                 ["generate", "--dir", directory, "--origin", self.origin, "--san", "localhost"],
                 ["check", "--dir", directory, "--origin", self.origin, "--days", "30"]]
        for option in ("--dir", "--data", "--openssl"):
            for bad in ("/tmp/a b", "/tmp/a\tb", "/tmp/a\nb", "/tmp/a\x7fb"):
                cases.append(["generate", "--dir", directory, "--origin", self.origin, option, bad])
        for option in ("--dir", "--data", "--ca-file"):
            for bad in ("/tmp/a b", "/tmp/a\tb", "/tmp/a\nb", "/tmp/a\x7fb"):
                cases.append(["check", "--dir", directory, "--origin", self.origin, option, bad])
        for argv in cases:
            with self.subTest(argv=argv):
                code, out, err = run_main(argv)
                self.assertEqual(2, code, err)
                self.assertEqual("", out)
                self.assertEqual([], self.listing())


class CheckCase(ProvisionCase):
    """Certificate fixtures issued by a private test CA, generated once per class."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.fixtures = tempfile.TemporaryDirectory(prefix="awtrix-provision-ca-")
        cls.addClassCleanup(cls.fixtures.cleanup)
        root = Path(cls.fixtures.name)
        cls.fixture = root

        def run(*arguments, cwd=None):
            result = subprocess.run(["openssl", *arguments], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    cwd=cwd or root, timeout=60)
            if result.returncode:
                raise AssertionError(f"fixture generation failed: openssl {arguments[0]}")
            return result.stdout

        def ec_key(name):
            run("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-pkeyopt", "ec_param_enc:named_curve",
                "-out", name)

        def self_signed(key, name, subject, *extensions, days="2"):
            (root / name).write_bytes(run("req", "-x509", "-new", "-batch", "-sha256", "-key", key, "-days", days,
                                          "-subj", subject, *extensions))

        for name, subject in (("ca", "/CN=Provision Test CA"), ("other-ca", "/CN=Unrelated Test CA")):
            ec_key(f"{name}.key")
            self_signed(f"{name}.key", f"{name}.crt", subject, "-addext", "basicConstraints=critical,CA:TRUE")
        run("req", "-x509", "-newkey", "rsa:1024", "-nodes", "-days", "2", "-subj", "/CN=Weak Test CA",
            "-addext", "basicConstraints=critical,CA:TRUE", "-keyout", "weak-ca.key", "-out", "weak-ca.crt")
        ec_key("leaf.key")
        ec_key("other.key")
        self_signed("leaf.key", "self.crt", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost")
        self_signed("leaf.key", "other-san.crt", "/CN=localhost", "-addext", "subjectAltName=DNS:other.invalid")
        self_signed("leaf.key", "cn-only.crt", "/CN=localhost")
        self_signed("leaf.key", "empty-label.crt", "/CN=a..b", "-addext", "subjectAltName=DNS:a..b")
        legacy = (root / "self.crt").read_bytes().replace(b" CERTIFICATE-----", b" X509 CERTIFICATE-----")
        (root / "old.crt").write_bytes(legacy)
        (root / "trusted.crt").write_bytes(run("x509", "-in", "self.crt", "-trustout"))
        run("req", "-x509", "-newkey", "rsa:1024", "-nodes", "-days", "2", "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost", "-keyout", "weak-self.key", "-out", "weak-self.crt")
        (root / "encrypted.key").write_bytes(run("pkcs8", "-topk8", "-in", "leaf.key", "-passout", "pass:fixture-passphrase"))
        (root / "extensions.cnf").write_text("subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n")
        run("req", "-new", "-key", "leaf.key", "-subj", "/CN=localhost", "-out", "leaf.csr")

        def issue(csr, ca, name, *extra):
            (root / name).write_bytes(run("x509", "-req", "-in", csr, "-CA", f"{ca}.crt", "-CAkey", f"{ca}.key",
                                          "-CAcreateserial", "-days", "2", "-extfile", "extensions.cnf", *extra))

        issue("leaf.csr", "ca", "issued.crt")
        issue("leaf.csr", "ca", "sha1.crt", "-sha1")
        issue("leaf.csr", "weak-ca", "weak-issued.crt")
        (root / "chain.crt").write_bytes((root / "issued.crt").read_bytes() + (root / "ca.crt").read_bytes())
        trusted_leaf = run("x509", "-in", "issued.crt", "-trustout")
        trusted_ca = run("x509", "-in", "ca.crt", "-trustout")
        issued, ca = (root / "issued.crt").read_bytes(), (root / "ca.crt").read_bytes()
        (root / "trusted-leaf-then-ca.crt").write_bytes(trusted_leaf + ca)
        (root / "ca-then-trusted-leaf.crt").write_bytes(ca + trusted_leaf)
        (root / "trusted-between-leaf-and-ca.crt").write_bytes(issued + trusted_ca + ca)
        (root / "trusted-before-leaf.crt").write_bytes(trusted_ca + issued + ca)
        tab_end = trusted_ca.replace(b"-----END TRUSTED CERTIFICATE-----", b"-----END TRUSTED\tCERTIFICATE-----")
        tab_begin = trusted_ca.replace(b"-----BEGIN TRUSTED CERTIFICATE-----", b"-----BEGIN TRUSTED\tCERTIFICATE-----")
        (root / "tab-end-trusted-before-leaf.crt").write_bytes(tab_end + issued + ca)
        (root / "tab-begin-trusted-before-leaf.crt").write_bytes(tab_begin + issued + ca)
        (root / "tab-end-trusted-between-leaf-and-ca.crt").write_bytes(issued + tab_end + ca)
        (root / "untrusted.crt").write_bytes(b"TRUSTED CERTIFICATE\n" +
                                             trusted_leaf.replace(b"TRUSTED CERTIFICATE", b"UNTRUSTED CERTIFICATE"))

        def nul_begin(pem):
            return pem.replace(b"-----\n", b"-----\x00x\n", 1)

        def nul_end(pem):
            return pem[:-1] + b"\x00x\n"

        (root / "nul-end-leaf-then-ca.crt").write_bytes(nul_end(issued) + ca)
        (root / "nul-begin-leaf-then-ca.crt").write_bytes(nul_begin(issued) + ca)
        (root / "nul-begin-ca-then-chain.crt").write_bytes(nul_begin(ca) + issued + ca)
        (root / "nul-begin.key").write_bytes(nul_begin((root / "leaf.key").read_bytes()))
        (root / "nul-end.key").write_bytes(nul_end((root / "leaf.key").read_bytes()))
        (root / "nul-after.key").write_bytes((root / "leaf.key").read_bytes() + b"#\x00\n")
        self_der = run("x509", "-in", "self.crt", "-outform", "DER")
        (root / "trailing-data.crt").write_bytes(ssl.DER_cert_to_PEM_cert(self_der + b"\x05\x00").encode())
        (root / "marker-inside-a-line.crt").write_bytes(b"see " + (root / "other-ca.crt").read_bytes() +
                                                        (root / "self.crt").read_bytes())
        (root / "weak-chain.crt").write_bytes((root / "weak-issued.crt").read_bytes() + (root / "weak-ca.crt").read_bytes())
        run("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", "rsa.key")
        run("req", "-new", "-key", "rsa.key", "-subj", "/CN=localhost", "-out", "rsa.csr")
        issue("rsa.csr", "ca", "rsa.crt")
        # Issue certificates with explicit validity dates.
        (root / "ca.cnf").write_text("[ca]\ndefault_ca = local\n[local]\ndir = .\ndatabase = ./index.txt\n"
                                     "new_certs_dir = ./newcerts\nserial = ./serial\ncertificate = ./ca.crt\n"
                                     "private_key = ./ca.key\ndefault_md = sha256\npolicy = any\nunique_subject = no\n"
                                     "[any]\ncommonName = optional\n[leaf]\nsubjectAltName = DNS:localhost\n"
                                     "basicConstraints = CA:FALSE\n")
        (root / "index.txt").write_text("")
        (root / "serial").write_text("01\n")
        (root / "newcerts").mkdir()
        for name, start, end in (("expired.crt", "20200101000000Z", "20200102000000Z"),
                                 ("future.crt", "20400101000000Z", "20400102000000Z")):
            run("ca", "-batch", "-config", "ca.cnf", "-in", "leaf.csr", "-notext", "-extensions", "leaf",
                "-startdate", start, "-enddate", end, "-out", name)
        (root / "garbage.crt").write_text("not a PEM certificate\n")
        (root / "garbage.key").write_text("not a PEM private key\n")

    def place(self, key="leaf.key", cert="self.crt", token=None):
        token = secrets.token_hex(32) + "\n" if token is None else token
        for name, source in (("tls.key", key), ("tls.crt", cert)):
            shutil.copyfile(self.fixture / source, self.dir / name)
            (self.dir / name).chmod(provision.MODES[name])
        target = self.dir / "admin.token"
        target.write_bytes(token.encode() if isinstance(token, str) else token)
        target.chmod(0o600)
        return token.strip() if isinstance(token, str) else token

    NEGATIVE_SETS = (("weak-self.key", "weak-self.crt", "EE_KEY_TOO_SMALL"),
                     ("leaf.key", "other-san.crt", "tls.crt: Hostname mismatch"),
                     ("leaf.key", "cn-only.crt", "tls.crt: Hostname mismatch"),
                     ("leaf.key", "expired.crt", "tls.crt: certificate has expired"),
                     ("leaf.key", "future.crt", "tls.crt: certificate is not yet valid"),
                     ("leaf.key", "sha1.crt", "CA_MD_TOO_WEAK"),
                     ("other.key", "self.crt", "KEY_VALUES_MISMATCH"),
                     ("encrypted.key", "self.crt", "tls.key: encrypted private keys are not accepted"),
                     ("leaf.key", "garbage.crt", "tls.crt: no PEM CERTIFICATE block"),
                     ("leaf.key", "trusted.crt", "tls.crt: no PEM CERTIFICATE block; TRUSTED CERTIFICATE blocks are skipped"),
                     ("leaf.key", "untrusted.crt", "tls.crt: no PEM CERTIFICATE block"),
                     ("garbage.key", "self.crt", "tls.crt/tls.key: rejected by OpenSSL"))


class Check(CheckCase):
    def test_accepts_a_generated_set_and_agrees_with_generate(self):
        generated = self.assert_generated()
        code, out, err = run_main(self.check_argv())
        self.assertEqual(0, code, err)
        self.assertEqual("", err)
        checked = json.loads(out)
        checked.pop("days_remaining")
        generated.pop("days_remaining")
        self.assertEqual(generated, checked)
        fingerprint = self.openssl("x509", "-noout", "-fingerprint", "-sha256", "-in", str(self.dir / "tls.crt")).decode()
        self.assertEqual("sha256 Fingerprint=" + checked["fingerprint_sha256"], fingerprint.strip())

    def test_token_rule_matches_the_application(self):
        token = secrets.token_hex(32)
        accepted_variants = (token + "\n", token + "\r\n", token, token.upper() + "\n",
                             token[:32].upper() + token[32:] + "\n")
        for variant, accepted in enumerate(accepted_variants):
            with self.subTest(accepted_variant=variant):
                self.place(token=accepted)
                code, _, err = run_main(self.check_argv())
                self.assertEqual(0, code, err)
        for variant, (refused, message) in enumerate(((token[:63] + "\n", "64 hexadecimal"), (token + "0\n", "64 hexadecimal"),
                                 ("", "1 to 65536 bytes"), (token + "\n\n", "64 hexadecimal"), ("g" + token[1:] + "\n", "64 hexadecimal"),
                                 (b"\xef\xbb\xbf" + token.encode() + b"\n", "64 hexadecimal"),
                                 (token + "\n" + "x" * (65537 - 65), "1 to 65536 bytes"), (token + " \n", "64 hexadecimal"),
                                 ("\n" + token + "\n", "64 hexadecimal"), (token + "\n\r", "64 hexadecimal"))):
            with self.subTest(refused_variant=variant):
                self.place(token=refused)
                err = self.assert_refused(self.check_argv(), "admin.token: ")
                self.assertIn(message, err)
                self.assertNotIn("tls.", err)
                self.assertNotIn(token, err)

    def test_file_modes_follow_the_application(self):
        self.place()
        for name in ("admin.token", "tls.key"):
            for mode in (0o640, 0o604, 0o644, 0o660, 0o606, 0o610, 0o601):
                with self.subTest(name=name, mode=oct(mode)):
                    (self.dir / name).chmod(mode)
                    self.assert_refused(self.check_argv(), f"{name}: mode must not grant group or other access")
            (self.dir / name).chmod(0o400)
            self.assertEqual(0, run_main(self.check_argv())[0], name)
            (self.dir / name).chmod(0o600)
        for mode in (0o664, 0o646, 0o666, 0o622):
            with self.subTest(name="tls.crt", mode=oct(mode)):
                (self.dir / "tls.crt").chmod(mode)
                self.assert_refused(self.check_argv(), "tls.crt: must not be writable by group or other")
        for mode in (0o644, 0o640, 0o600, 0o400, 0o444):
            with self.subTest(name="tls.crt", mode=oct(mode)):
                (self.dir / "tls.crt").chmod(mode)
                self.assertEqual(0, run_main(self.check_argv())[0])

    def test_symlinks_fifos_directories_and_missing_files_are_refused(self):
        self.place()
        real = self.root / "real-token"
        (self.dir / "admin.token").rename(real)
        (self.dir / "admin.token").symlink_to(real)
        self.assert_refused(self.check_argv(), "admin.token: must not be a symlink")
        (self.dir / "admin.token").unlink()
        real.rename(self.dir / "admin.token")
        (self.dir / "tls.key").unlink()
        os.mkfifo(self.dir / "tls.key", 0o600)
        self.assert_refused(self.check_argv(), "tls.key: must be a regular file")
        (self.dir / "tls.key").unlink()
        shutil.copyfile(self.fixture / "leaf.key", self.dir / "tls.key")
        (self.dir / "tls.key").chmod(0o600)
        (self.dir / "tls.crt").unlink()
        self.assert_refused(self.check_argv(), "tls.crt: missing")
        (self.dir / "tls.crt").mkdir()
        self.assert_refused(self.check_argv(), "tls.crt: must be a regular file")
        (self.dir / "tls.crt").rmdir()
        (self.dir / "tls.crt").write_bytes(b"")
        self.assert_refused(self.check_argv(), "tls.crt: must be 1 to 65536 bytes")

    def test_certificate_and_key_material_is_judged_by_openssl(self):
        for key, cert, message in self.NEGATIVE_SETS + (("leaf.key", "weak-chain.crt", "CA_KEY_TOO_SMALL"),):
            with self.subTest(key=key, cert=cert):
                self.place(key=key, cert=cert)
                err = self.assert_refused(self.check_argv(), message)
                self.assertIn("tls.", err)
                self.assertNotIn("BEGIN", err)
                self.assertNotIn("fixture-passphrase", err)

    def test_skipped_trusted_certificate_hint_needs_a_trusted_certificate_begin_line(self):
        self.place(cert="untrusted.crt")
        err = self.assert_refused(self.check_argv(), "tls.crt: no PEM CERTIFICATE block")
        self.assertNotIn("TRUSTED CERTIFICATE blocks are skipped", err)

    def test_legacy_x509_certificate_header_is_accepted_like_the_application(self):
        self.place(cert="old.crt")
        self.assertTrue((self.dir / "tls.crt").read_bytes().startswith(b"-----BEGIN X509 CERTIFICATE-----\n"))
        code, out, err = run_main(self.check_argv())
        self.assertEqual(0, code, err)
        self.assertEqual(1, json.loads(out)["chain_length"])

    def test_guards_behind_a_loaded_certificate_report_their_refusal(self):
        self.place()
        with mock.patch.object(ssl.SSLContext, "load_verify_locations", side_effect=ssl.SSLError("injected")):
            self.assert_refused(self.check_argv(), "tls.crt: first certificate cannot serve as a trust anchor")
        with mock.patch.object(ssl.SSLObject, "do_handshake", side_effect=ssl.SSLWantReadError()):
            self.assert_refused(self.check_argv(), "tls.crt: handshake did not complete")
        self.assertEqual(0, run_main(self.check_argv())[0])

    def test_chains_are_exercised_without_and_verified_with_a_ca_file(self):
        self.place(cert="chain.crt")
        code, out, err = run_main(self.check_argv())
        self.assertEqual(0, code, err)
        result = json.loads(out)
        self.assertEqual(2, result["chain_length"])
        self.assertIs(False, result["chain_verified"])
        code, out, err = run_main(self.check_argv(extra=["--ca-file", str(self.fixture / "ca.crt")]))
        self.assertEqual(0, code, err)
        self.assertIs(True, json.loads(out)["chain_verified"])
        self.assertEqual(2, json.loads(out)["chain_length"])
        self.assert_refused(self.check_argv(extra=["--ca-file", str(self.fixture / "other-ca.crt")]), "tls.crt: ")
        self.assert_refused(self.check_argv(extra=["--ca-file", str(self.fixture / "missing-ca.crt")]), "--ca-file: cannot load")
        self.assert_refused(self.check_argv(extra=["--ca-file", str(self.fixture / "garbage.crt")]), "--ca-file: cannot load")
        self.place(cert="issued.crt")
        code, out, err = run_main(self.check_argv(extra=["--ca-file", str(self.fixture / "ca.crt")]))
        self.assertEqual(0, code, err)
        self.assertEqual((1, True), (json.loads(out)["chain_length"], json.loads(out)["chain_verified"]))
        self.assertEqual(0, run_main(self.check_argv())[0])
        # A leaf issued by a weak CA passes alone: the leaf is the anchor. Naming the CA exposes it.
        self.place(cert="weak-issued.crt")
        self.assertEqual(0, run_main(self.check_argv())[0])
        self.assert_refused(self.check_argv(extra=["--ca-file", str(self.fixture / "weak-ca.crt")]), "tls.crt: CA certificate key too weak")
        self.place(key="rsa.key", cert="rsa.crt")
        self.assertEqual(0, run_main(self.check_argv(extra=["--ca-file", str(self.fixture / "ca.crt")]))[0])


class Runtime:
    """One awtrix-linux --hardened process started on a provisioned directory."""

    def __init__(self, directory, port, data, log, cafile=None, host="localhost"):
        self.directory = Path(directory)
        self.port = port
        self.host = host
        self.data = Path(data)
        self.data.mkdir(mode=0o700, exist_ok=True)
        self.log = Path(log)
        self.process = None
        self.output = None
        self.cafile = str(cafile or self.directory / "tls.crt")

    @property
    def origin(self):
        return f"https://{self.host}:{self.port}"

    def command(self):
        return [OPTIONS.binary, "--hardened", "--data", str(self.data), "--webui", OPTIONS.webui, "--port", str(self.port),
                "--credentials-file", str(self.directory / "admin.token"), "--tls-cert", str(self.directory / "tls.crt"),
                "--tls-key", str(self.directory / "tls.key"), "--origin", self.origin]

    def logs(self):
        return self.log.read_text(errors="replace") if self.log.exists() else ""

    def launch(self):
        self.output = open(self.log, "ab")
        self.process = subprocess.Popen(self.command(), stdout=self.output, stderr=self.output)

    def running(self):
        if self.process.poll() is not None:
            raise AssertionError(f"runtime exited {self.process.returncode}: {self.logs()}")
        return True

    def start(self):
        self.launch()
        eventually(lambda: self.running() and self.request("GET", "/api/v1/version")[0] == 401, timeout=12)
        return self

    def listen(self):
        """Start without an HTTPS probe, for sets a verifying client cannot reach."""
        self.launch()

        def listening():
            with socket.create_connection(("127.0.0.1", self.port), timeout=0.2):
                return self.running()

        eventually(listening, timeout=12)
        return self

    def stop(self):
        try:
            stop_process(self.process, self.logs)
        finally:
            self.process = None
            if self.output:
                self.output.close()
                self.output = None

    def request(self, method, path, authorization=None):
        context = ssl.create_default_context(cafile=self.cafile)
        connection = http.client.HTTPSConnection(self.host, self.port, context=context, timeout=4)
        headers = {"Host": f"{self.host}:{self.port}", "Connection": "close"}
        if authorization is not None:
            headers["Authorization"] = authorization
        try:
            connection.request(method, path, headers=headers)
            response = connection.getresponse()
            return response.status, response.read(), {k.lower(): v for k, v in response.getheaders()}
        finally:
            connection.close()

    def fingerprint(self):
        context = ssl.create_default_context(cafile=self.cafile)
        with socket.create_connection(("127.0.0.1", self.port), timeout=4) as connection:
            with context.wrap_socket(connection, server_hostname=self.host) as tls:
                digest = hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest().upper()
        return ":".join(digest[i:i + 2] for i in range(0, len(digest), 2))

    def refused(self):
        result = subprocess.run(self.command(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        if result.returncode == 0 or b"Administrative security:" not in result.stderr:
            raise AssertionError(f"runtime accepted the set: {result.returncode} {result.stderr!r}")
        with socket.socket() as probe:
            probe.settimeout(0.2)
            if probe.connect_ex(("127.0.0.1", self.port)) == 0:
                raise AssertionError("a listener exists although provisioning was refused")
        return result.stderr


class Agreement(CheckCase):
    """check and the real awtrix-linux --hardened judge the same directory alike."""

    def runtime(self, cafile=None, host="localhost"):
        runtime = Runtime(self.dir, self.port, self.root / "data", self.root / "runtime.log", cafile, host)
        self.addCleanup(runtime.stop)
        return runtime

    def test_sets_refused_by_check_are_refused_by_the_application(self):
        for key, cert, message in self.NEGATIVE_SETS:
            with self.subTest(key=key, cert=cert):
                token = self.place(key=key, cert=cert)
                self.assert_refused(self.check_argv(), message)
                stderr = self.runtime().refused()
                self.assertNotIn(token.encode(), stderr)
        for variant, bad_token in enumerate(("password\n", secrets.token_hex(31) + "\n")):
            with self.subTest(bad_token_variant=variant):
                self.place(token=bad_token)
                self.assert_refused(self.check_argv(), "admin.token")
                self.runtime().refused()
        self.place()
        (self.dir / "tls.key").chmod(0o644)
        self.assert_refused(self.check_argv(), "tls.key")
        self.runtime().refused()

    def test_weak_issuer_in_the_chain_is_refused_by_check_although_the_application_starts(self):
        self.place(cert="weak-chain.crt")
        self.assert_refused(self.check_argv(), "CA_KEY_TOO_SMALL")
        runtime = self.runtime().listen()
        # Every client that enforces the same key strength fails the handshake.
        with self.assertRaises(ssl.SSLError):
            runtime.request("GET", "/api/v1/version")

    def test_unparsable_chain_block_is_refused_by_check_although_the_application_starts(self):
        self.place()
        with open(self.dir / "tls.crt", "ab") as certificate:
            certificate.write(b"-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n")
        self.assert_refused(self.check_argv(), "tls.crt/tls.key: rejected by OpenSSL")
        # The application stops reading the chain at the first block it cannot parse.
        runtime = self.runtime(self.fixture / "self.crt").start()
        self.assertEqual(401, runtime.request("GET", "/api/v1/version")[0])

    def test_empty_dns_label_is_a_usage_error_for_check_although_the_application_starts(self):
        self.place(cert="empty-label.crt")
        code, out, err = run_main(self.check_argv(origin=f"https://a..b:{self.port}"))
        self.assertEqual((2, ""), (code, out), err)
        self.assertIn("--origin must be https://HOST:PORT", err)
        self.runtime(host="a..b").listen().stop()

    def test_sets_accepted_by_check_start_the_application(self):
        for key, cert, cafile in (("leaf.key", "chain.crt", "ca.crt"), ("leaf.key", "chain.crt", None),
                                  ("rsa.key", "rsa.crt", "ca.crt"), ("leaf.key", "issued.crt", "ca.crt"),
                                  ("leaf.key", "old.crt", None)):
            with self.subTest(key=key, cert=cert, cafile=cafile):
                self.place(key=key, cert=cert)
                extra = ["--ca-file", str(self.fixture / cafile)] if cafile else []
                self.assertEqual(0, run_main(self.check_argv(extra=extra))[0])
                runtime = self.runtime(self.fixture / cafile if cafile else None).start()
                status, _, headers = runtime.request("GET", "/api/v1/version")
                self.assertEqual(401, status)
                self.assertIn("Basic ", headers["www-authenticate"])
                runtime.stop()

    def test_trusted_certificate_blocks_are_skipped_alike_by_check_and_the_application(self):
        for cert, cafile, accepted in (("trusted-leaf-then-ca.crt", "ca.crt", False),
                                       ("ca-then-trusted-leaf.crt", "ca.crt", False),
                                       ("trusted-between-leaf-and-ca.crt", "ca.crt", True),
                                       ("trusted-before-leaf.crt", "ca.crt", True),
                                       ("old.crt", None, True)):
            self.place(cert=cert)
            runtime = self.runtime(self.fixture / cafile if cafile else None)
            if accepted:
                served = runtime.start().fingerprint()
                runtime.stop()
            else:
                runtime.refused()
            for extra in ([], ["--ca-file", str(self.fixture / cafile)]) if cafile else ([],):
                with self.subTest(cert=cert, extra=extra):
                    code, out, err = run_main(self.check_argv(extra=extra))
                    if accepted:
                        self.assertEqual(0, code, err)
                        self.assertEqual(served, json.loads(out)["fingerprint_sha256"])
                    else:
                        self.assertEqual((1, ""), (code, out))
                        self.assertIn("TRUSTED CERTIFICATE blocks are skipped", err)

    def test_tab_spelled_trusted_certificate_name_is_refused_by_check(self):
        message = "tls.crt: a TRUSTED CERTIFICATE block name written with a tab is not accepted"
        for cert in ("tab-end-trusted-before-leaf.crt", "tab-begin-trusted-before-leaf.crt"):
            self.place(cert=cert)
            with self.subTest(cert=cert):
                self.runtime(self.fixture / "ca.crt").refused()
            for extra in ([], ["--ca-file", str(self.fixture / "ca.crt")]):
                with self.subTest(cert=cert, extra=extra):
                    self.assert_refused(self.check_argv(extra=extra), message)
        self.place(cert="tab-end-trusted-between-leaf-and-ca.crt")
        for extra in ([], ["--ca-file", str(self.fixture / "ca.crt")]):
            with self.subTest(cert="tab-end-trusted-between-leaf-and-ca.crt", extra=extra):
                self.assert_refused(self.check_argv(extra=extra), message)
        # The application stops reading the chain at the malformed block and serves the leaf alone.
        runtime = self.runtime(self.fixture / "ca.crt").start()
        self.assertEqual(401, runtime.request("GET", "/api/v1/version")[0])
        runtime.stop()

    def test_nul_bytes_in_the_leaf_or_key_framing_are_refused_by_check_and_the_application(self):
        for key, cert, name in (("leaf.key", "nul-end-leaf-then-ca.crt", "tls.crt"),
                                ("leaf.key", "nul-begin-leaf-then-ca.crt", "tls.crt"),
                                ("nul-begin.key", "chain.crt", "tls.key"),
                                ("nul-end.key", "chain.crt", "tls.key")):
            self.place(key=key, cert=cert)
            with self.subTest(key=key, cert=cert):
                self.runtime().refused()
            for extra in ([], ["--ca-file", str(self.fixture / "ca.crt")]):
                with self.subTest(key=key, cert=cert, extra=extra):
                    self.assert_refused(self.check_argv(extra=extra), f"{name}: must not contain NUL bytes")

    def test_nul_byte_is_refused_by_check_although_the_application_can_start(self):
        for key, cert in (("leaf.key", "nul-begin-ca-then-chain.crt"), ("nul-after.key", "chain.crt")):
            with self.subTest(key=key, cert=cert):
                self.place(key=key, cert=cert)
                self.assert_refused(self.check_argv(), "must not contain NUL bytes")
                runtime = self.runtime(self.fixture / "ca.crt").start()
                self.assertEqual(401, runtime.request("GET", "/api/v1/version")[0])
                runtime.stop()

    def test_leaf_block_with_data_after_the_certificate_is_refused_by_check_although_the_application_starts(self):
        self.place(cert="trailing-data.crt")
        self.assert_refused(self.check_argv(), "tls.crt/tls.key: rejected by OpenSSL")
        runtime = self.runtime(self.fixture / "self.crt").start()
        self.assertEqual(401, runtime.request("GET", "/api/v1/version")[0])

    def test_leaf_is_the_trust_anchor_when_a_certificate_marker_stands_inside_a_line(self):
        self.place(cert="marker-inside-a-line.crt")
        served = self.runtime(self.fixture / "self.crt").start().fingerprint()
        code, out, err = run_main(self.check_argv())
        self.assertEqual(0, code, err)
        self.assertEqual(served, json.loads(out)["fingerprint_sha256"])

    def test_generated_set_serves_authenticated_https_end_to_end(self):
        self.assert_generated()
        token = (self.dir / "admin.token").read_text().strip()
        runtime = self.runtime().start()
        status, body, headers = runtime.request("GET", "/api/v1/version")
        self.assertEqual(401, status)
        self.assertIn("Basic ", headers["www-authenticate"])
        self.assertEqual("no-store", headers["cache-control"])
        status, body, headers = runtime.request("GET", "/api/v1/version", "Bearer " + token)
        self.assertEqual(200, status, body)
        self.assertIn("version", json.loads(body))
        self.assertIn("max-age=", headers["strict-transport-security"])
        basic = base64.b64encode(("admin:" + token).encode()).decode()
        self.assertEqual(200, runtime.request("GET", "/", "Basic " + basic)[0])
        wrong = token[:-1] + ("1" if token.endswith("0") else "0")
        self.assertEqual(401, runtime.request("GET", "/api/v1/version", "Bearer " + wrong)[0])
        altered = basic[:10] + ("A" if basic[10] != "A" else "B") + basic[11:]
        self.assertEqual(401, runtime.request("GET", "/api/v1/version", "Basic " + altered)[0])
        with self.assertRaises(ssl.SSLCertVerificationError):
            Runtime(self.dir, self.port, self.root / "data", self.root / "runtime.log", self.fixture / "ca.crt").request("GET", "/")
        runtime.stop()
        logs = runtime.logs()
        self.assertIn("authenticated HTTPS", logs)
        self.assertNotIn(token, logs)
        self.assertNotIn(basic, logs)
        self.assertEqual(0, run_main(self.check_argv(extra=["--data", str(self.root / "data")]))[0])

    def test_generated_ipv4_set_serves_authenticated_https_end_to_end(self):
        self.assert_generated(self.generate_argv(origin=f"https://127.0.0.1:{self.port}"))
        token = (self.dir / "admin.token").read_text().strip()
        self.assertIn(b"missing origin SAN", self.runtime().refused())
        runtime = self.runtime(host="127.0.0.1").start()
        status, _, headers = runtime.request("GET", "/api/v1/version")
        self.assertEqual(401, status)
        self.assertIn("Basic ", headers["www-authenticate"])
        status, body, _ = runtime.request("GET", "/api/v1/version", "Bearer " + token)
        self.assertEqual(200, status, body)
        self.assertIn("version", json.loads(body))
        runtime.stop()
        self.assertNotIn(token, runtime.logs())
        (self.dir / "admin.token").unlink()
        (self.dir / "tls.crt").unlink()
        (self.dir / "tls.key").unlink()
        self.assert_generated()
        self.assertIn(b"missing origin SAN", self.runtime(host="127.0.0.1").refused())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.binary = str(Path(OPTIONS.binary).resolve())
    OPTIONS.webui = str(Path(OPTIONS.webui).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
