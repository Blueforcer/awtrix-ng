#!/usr/bin/env python3
"""The TC002 web update in awtrix-linux: the streaming POST /update, its refusals, the hand-off to
the supervisor, the update fields of /api/v1/device and the gzip web index."""

from __future__ import annotations

import argparse
import base64
import gzip
import hashlib
import http.client
import json
import os
from pathlib import Path
import select
import socket
import stat
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "update"))
import package  # noqa: E402
import bundle  # noqa: E402  (tools/tc002/install, on the path package.py set)

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import free_port

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import stop_process, wait_ready

OPTIONS = None
BOUNDARY = "awtrix-update-boundary"
CAPACITY = 5_800_000
RUNNING_COUNTER = 1000


def multipart(data, name="firmware", filename="awtrix-ng-tc002.awup"):
    return (f'--{BOUNDARY}\r\nContent-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{BOUNDARY}--\r\n".encode()


def write_release(root, version, counter, files):
    """An installed release at root, written from a bundle next to it; returns its name."""
    source = root.with_name(root.name + "-bundle")
    for path, data in files.items():
        target = source / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    bundle.write_manifest(source, version, "abcdef0", False, counter, host_only=(), required=())
    return bundle.write_release(source, root)["release"]


class Fixture:
    """The running release the runtime serves its web index from, and update packages."""

    def __init__(self, root):
        self.root = root
        (root / "state").mkdir(mode=0o700)
        self.state = root / "state" / "update-state.json"
        self.index = gzip.compress(Path(OPTIONS.webui).read_bytes(), mtime=0)
        write_release(root / "running", "1.1.2", RUNNING_COUNTER, {"share/index.html.gz": self.index})
        self.webui = root / "running" / "share" / "index.html.gz"
        self.packages = root / "packages"
        self.packages.mkdir(mode=0o700)
        self.release = write_release(root / "new-release", "1.2.0", 2000,
                                     {"bin/awtrix-linux": b"\x7fELF" + os.urandom(3000) + bytes(30000),
                                      "share/index.html.gz": self.index})
        self.valid = self.packages / "valid.awup"
        package.create_release(root / "new-release", self.valid)
        container = self.valid.read_bytes()
        self.image_bytes = len(container) - int.from_bytes(container[12:16], "big") - 32
        write_release(root / "old-release", "1.0.0", 900, {"bin/awtrix-linux": b"\x7fELF" + bytes(100)})
        self.old = self.packages / "old.awup"
        package.create_release(root / "old-release", self.old)
        payload = self.packages / "payload.bin"
        payload.write_bytes(b"payload" * 100)
        self.experimental = self.packages / "experimental.awup"
        package.create(payload, self.experimental, "experimental:tc002", "1.2.0-x", 3000)
        data = bytearray(self.valid.read_bytes())
        data[-1] ^= 1
        self.tampered = self.packages / "tampered.awup"
        self.tampered.write_bytes(bytes(data))
        data = bytearray(self.valid.read_bytes())
        manifest_size = int.from_bytes(data[12:16], "big")
        data[manifest_size] ^= 1
        self.manifest_tampered = self.packages / "manifest-tampered.awup"
        self.manifest_tampered.write_bytes(bytes(data))


class Runtime:
    def __init__(self, fixture, root):
        self.fixture = fixture
        self.root = root
        self.data = root / "data"
        self.temporary = root / "tmp"
        self.temporary.mkdir(mode=0o700)
        self.work = self.temporary / "awtrix-update"
        self.log = open(root / "runtime.log", "wb")
        self.process = None
        self.channel = None
        self.port = free_port()

    def command(self, extra=()):
        return [OPTIONS.binary, "--data", str(self.data), "--port", str(self.port),
                "--webui", str(self.fixture.webui), *extra]

    def start(self, extra=(), update=True, supervised=True):
        descriptors = []
        if supervised:
            ours, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            self.channel = ours
            os.dup2(theirs.fileno(), 103)
            theirs.close()
            descriptors.append(103)
            extra = ["--supervisor-fd", "103", *extra]
        if update:
            extra = ["--update-state", str(self.fixture.state),
                     "--update-dir", str(self.work), *extra]
        try:
            self.process = subprocess.Popen(self.command(extra), stdout=self.log, stderr=self.log,
                                            pass_fds=descriptors)
        finally:
            for descriptor in descriptors:
                os.close(descriptor)
        if supervised and "--lan" in extra:
            self.channel.send(json.dumps({'v': 2, 'type': 'network', 'link': 'connected',
                'ssid': 'Fixture', 'rssi': -45, 'mac': '02:00:00:00:00:01',
                'ipv4': '192.168.1.20', 'gateway': '192.168.1.1', 'dns': '192.168.1.1',
                'hostname': 'fixture'}).encode())
        wait_ready(self.process, lambda: self.request("GET", "/api/v1/version")[0] == 200, self.output, timeout=20)
        return self

    def stop(self):
        try:
            stop_process(self.process, self.output, timeout=10)
        finally:
            self.process = None
            if self.channel:
                self.channel.close()
                self.channel = None
            self.log.close()

    def output(self):
        self.log.flush()
        return (self.root / "runtime.log").read_text(errors="replace")

    def request(self, method, path, body=None, headers=None, timeout=10):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=timeout)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()
        finally:
            connection.close()

    def upload(self, data, headers=None, **fields):
        status, _, body = self.request("POST", "/update", multipart(data, **fields),
                                       {"Content-Type": f"multipart/form-data; boundary={BOUNDARY}",
                                        **(headers or {})})
        return status, json.loads(body)

    def device(self, headers=None):
        status, _, body = self.request("GET", "/api/v1/device", headers=headers)
        assert status == 200, (status, body)
        return json.loads(body)

    def messages(self, seconds):
        found = []
        deadline = time.monotonic() + seconds
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.channel], [], [], remaining)[0]:
                return found
            datagram = self.channel.recv(4096)
            if not datagram:
                return found
            found.append(json.loads(datagram))

    def wait_for(self, kind, seconds=10):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for message in self.messages(min(0.2, deadline - time.monotonic())):
                if message["type"] == kind:
                    return message
        raise AssertionError(f"no {kind} message; runtime output:\n" + self.output())

    def send(self, message):
        self.channel.send(json.dumps({"v": 2, **message}).encode())

    def hello(self, state="confirmed", release="1.1.2-running", error="", capacity=CAPACITY):
        """The supervisor's hello: the state of the last update and the size of the release slot."""
        self.send({"type": "hello", "version": "1.1.2",
                   "update": {"state": state, "release": release, "error": error, "capacity": capacity}})


class WebUpdate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="awtrix-web-update-")
        cls.fixture = Fixture(Path(cls.workspace.name))

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=self.workspace.name)
        self.addCleanup(self.folder.cleanup)
        if self.fixture.state.exists():
            self.fixture.state.unlink()

    def runtime(self, **options):
        app = Runtime(self.fixture, Path(self.folder.name))
        self.addCleanup(app.stop)
        return app.start(**options)

    def assert_refused(self, app, status, code, data, **fields):
        result = app.upload(data, **fields)
        self.assertEqual((status, code), (result[0], result[1]["error"]["code"]), result)
        self.assertFalse((app.work / "package.awup").exists(), "a refused package does not stay behind")
        return result[1]["error"]["message"]

    def runtime_with_slot(self, **options):
        app = self.runtime(**options)
        app.wait_for("hello")
        app.hello()
        self.wait_for_update(app, "confirmed")
        return app

    def test_valid_package_is_handed_to_the_supervisor_after_the_reply(self):
        app = self.runtime()
        app.wait_for("hello")
        device = app.device()
        self.assertEqual("awtrix-ng-tc002.awup", device["updateImage"])
        self.assertEqual({"state": "idle", "release": "", "error": ""}, device["update"])
        self.assertNotIn("factory", device)
        app.hello()
        self.assertEqual({"state": "confirmed", "release": "1.1.2-running", "error": ""},
                         self.wait_for_update(app, "confirmed"))
        status, result = app.upload(self.fixture.valid.read_bytes(),
                                    headers={"Origin": f"http://127.0.0.1:{app.port}"})
        self.assertEqual((200, {"ok": True, "applying": True}), (status, result))
        ready = app.wait_for("updateReady")
        container = self.fixture.valid.read_bytes()
        manifest_size = int.from_bytes(container[12:16], "big")
        self.assertEqual({"v": 2, "type": "updateReady", "package": str(app.work / "package.awup"),
                          "release": self.fixture.release, "counter": 2000,
                          "payloadSha256": hashlib.sha256(container[manifest_size + 32:]).hexdigest()}, ready)
        staged = app.work / "package.awup"
        self.assertEqual(container, staged.read_bytes())
        self.assertEqual(0o600, stat.S_IMODE(staged.stat().st_mode))
        self.assertEqual(0o700, stat.S_IMODE(app.work.stat().st_mode))
        self.assertEqual({"state": "applying", "release": self.fixture.release, "error": ""},
                         app.device()["update"])
        self.assertEqual(409, app.upload(container)[0], "one update at a time")
        self.assertTrue(staged.exists(), "a second upload leaves the staged package alone")
        self.assertEqual([], [m for m in app.messages(0.5) if m["type"] == "updateReady"], "handed off once")
        app.process.terminate()
        self.assertEqual(0, app.process.wait(timeout=10), app.output())
        self.assertTrue(staged.exists(), "the supervisor still finds the package")
        self.assertIn("verified, handing it to the supervisor", app.output())

    def wait_for_update(self, app, state):
        deadline = time.monotonic() + 5
        while app.device()["update"]["state"] != state and time.monotonic() < deadline:
            time.sleep(0.05)
        return app.device()["update"]

    def test_a_hand_off_the_supervisor_refuses_ends_the_update(self):
        app = self.runtime_with_slot()
        self.assertEqual(200, app.upload(self.fixture.valid.read_bytes())[0])
        app.wait_for("updateReady")
        staged = app.work / "package.awup"
        self.assertTrue(staged.exists())
        app.hello(error="update 1.2.0-other refused: busy")
        time.sleep(0.5)
        self.assertEqual("applying", app.device()["update"]["state"],
                         "a refusal of another release does not end this one")
        self.assertTrue(staged.exists())
        error = f"update {self.fixture.release} refused: the flash helper is missing"
        app.hello(error=error)
        self.assertEqual({"state": "confirmed", "release": "1.1.2-running", "error": error},
                         self.wait_for_update(app, "confirmed"))
        self.assertFalse(staged.exists(), "the refused package is removed")
        self.assertEqual(200, app.upload(self.fixture.valid.read_bytes())[0], "the lock is free again")

    def test_uploads_wait_while_the_last_update_is_confirmed(self):
        app = self.runtime()
        app.wait_for("hello")
        app.hello(state="boot-pending", release="1.1.3-new")
        self.assertEqual("boot-pending", self.wait_for_update(app, "boot-pending")["state"])
        message = self.assert_refused(app, 409, "updateBusy", self.fixture.valid.read_bytes())
        self.assertIn("confirmed", message)
        app.hello(release="1.1.3-new")
        self.assertEqual("confirmed", self.wait_for_update(app, "confirmed")["state"])
        self.assertEqual(200, app.upload(self.fixture.valid.read_bytes())[0])

    def test_refusals_leave_nothing_behind(self):
        app = self.runtime_with_slot()
        message = self.assert_refused(app, 400, "invalidPackage", self.fixture.manifest_tampered.read_bytes())
        self.assertIn("damaged", message)
        self.assert_refused(app, 409, "notNewer", self.fixture.old.read_bytes())
        self.assert_refused(app, 400, "invalidPackage", self.fixture.tampered.read_bytes())
        self.assert_refused(app, 400, "wrongTarget", self.fixture.experimental.read_bytes())
        message = self.assert_refused(app, 400, "invalidPackage", b"\xe9" + bytes(4000))
        self.assertIn("not an AWTRIX NG TC002 update package", message)
        self.assert_refused(app, 400, "badRequest", self.fixture.valid.read_bytes(), name="file")
        self.assert_refused(app, 400, "badRequest", self.fixture.valid.read_bytes(), filename="")
        status, _, body = app.request("POST", "/update", self.fixture.valid.read_bytes(),
                                      {"Content-Type": "application/octet-stream"})
        self.assertEqual((400, "badRequest"), (status, json.loads(body)["error"]["code"]))
        status, _, body = app.request("POST", "/update", multipart(self.fixture.valid.read_bytes()),
                                      {"Content-Type": f"multipart/form-data; boundary={BOUNDARY}",
                                       "Origin": "http://evil.example"})
        self.assertEqual((403, "forbiddenOrigin"), (status, json.loads(body)["error"]["code"]))
        status, _, body = app.request("POST", "/update", multipart(bytes(9 * 1024 * 1024)),
                                      {"Content-Type": f"multipart/form-data; boundary={BOUNDARY}"}, timeout=30)
        self.assertEqual((413, "payloadTooLarge"), (status, json.loads(body)["error"]["code"]))
        status, _, body = app.request("POST", "/update", b"{}", {"Content-Type": "application/json",
                                                                 "X-HTTP-Method-Override": "DELETE"})
        self.assertEqual(405, status)
        self.assertEqual(405, app.request("GET", "/update")[0])
        self.assertEqual([], [m for m in app.messages(0.3) if m["type"] == "updateReady"])
        self.assertEqual({"state": "confirmed", "release": "1.1.2-running", "error": ""}, app.device()["update"])
        self.assertEqual(200, app.upload(self.fixture.valid.read_bytes())[0], "the refusals left no lock behind")

    def test_the_image_must_fit_the_release_slot(self):
        app = self.runtime()
        app.wait_for("hello")
        message = self.assert_refused(app, 500, "internalError", self.fixture.valid.read_bytes())
        self.assertIn("did not report the release slot", message)
        app.hello(capacity=self.fixture.image_bytes - 1)
        self.wait_for_update(app, "confirmed")
        message = self.assert_refused(app, 409, "insufficientStorage", self.fixture.valid.read_bytes())
        self.assertRegex(message, r"^the firmware image takes \d+\.\d MB, the clock holds \d+\.\d MB$")
        app.hello(capacity=self.fixture.image_bytes)
        time.sleep(0.3)
        self.assertEqual(200, app.upload(self.fixture.valid.read_bytes())[0], "an image that fills the slot fits")

    def test_the_accepted_counter_of_the_state_file_refuses_replays(self):
        state = {"schema": 2, "state": "idle", "acceptedCounter": 2000,
                 "current": None, "candidate": None, "lease": None, "failure": ""}
        self.fixture.state.write_text(json.dumps(state))
        app = self.runtime_with_slot()
        self.assert_refused(app, 409, "notNewer", self.fixture.valid.read_bytes())
        state["acceptedCounter"] = 1999
        self.fixture.state.write_text(json.dumps(state))
        self.assertEqual(200, app.upload(self.fixture.valid.read_bytes())[0])

    def test_incomplete_state_uses_the_running_release_counter(self):
        self.fixture.state.write_text(json.dumps({"acceptedCounter": 9999}))
        app = self.runtime_with_slot()
        self.assert_refused(app, 409, "notNewer", self.fixture.old.read_bytes())
        self.assertEqual(200, app.upload(self.fixture.valid.read_bytes())[0])

    def test_supervisor_status_reaches_the_device_state(self):
        app = self.runtime()
        app.wait_for("hello")
        error = "the image differs from its header after writing"
        app.hello(state="failed", release="1.2.0-new", error=error)
        self.assertEqual({"state": "failed", "release": "1.2.0-new", "error": error},
                         self.wait_for_update(app, "failed"))
        self.assertNotIn("factory", app.device())
        app.send({"type": "hello", "version": "1.1.2",
                  "update": {"state": "confirmed", "release": "1.1.2-running", "error": ""}})
        time.sleep(0.3)
        self.assertEqual("failed", app.device()["update"]["state"], "an update status without capacity is refused")

    def test_login_guards_the_upload(self):
        app = self.runtime_with_slot(extra=["--lan"])
        status, _, body = app.request("PUT", "/api/v1/system",
                                      json.dumps({"authEnabled": True, "authUser": "admin", "authPass": "s3cret"}),
                                      {"Content-Type": "application/json"})
        self.assertEqual(200, status, body)
        self.assertEqual(401, app.upload(self.fixture.valid.read_bytes())[0])
        self.assertFalse((app.work / "package.awup").exists())
        authorization = {"Authorization": "Basic " + base64.b64encode(b"admin:s3cret").decode()}
        status, result = app.upload(self.fixture.valid.read_bytes(), headers=authorization)
        self.assertEqual((200, True), (status, result.get("ok")), result)

    def test_web_index_gzip(self):
        app = self.runtime()
        status, headers, body = app.request("GET", "/")
        self.assertEqual(200, status)
        self.assertEqual("gzip", headers["content-encoding"])
        self.assertIn("text/html", headers["content-type"])
        self.assertEqual("no-cache", headers["cache-control"])
        self.assertEqual(self.fixture.index, body)
        self.assertIn(b"<html", gzip.decompress(body).lower())
        status, headers304, body = app.request("GET", "/index.html", headers={"If-None-Match": headers["etag"]})
        self.assertEqual((304, b""), (status, body))
        self.assertEqual(headers["etag"], headers304["etag"])
        self.assertEqual(200, app.request("GET", "/fullscreen", headers={"If-None-Match": '"other"'})[0])

    def test_without_update_capability_the_route_stays_unsupported(self):
        app = self.runtime(update=False)
        status, result = app.upload(self.fixture.valid.read_bytes())
        self.assertEqual((501, "notSupported"), (status, result["error"]["code"]))
        self.assertEqual("", app.device()["updateImage"])
        self.assertNotIn("update", app.device())
        self.assertNotIn("factory", app.device())

    def test_without_supervisor_the_route_stays_unsupported(self):
        app = self.runtime(update=False, supervised=False)
        status, result = app.upload(self.fixture.valid.read_bytes())
        self.assertEqual((501, "notSupported"), (status, result["error"]["code"]))

    def test_large_bodies_elsewhere_keep_their_ceiling(self):
        app = self.runtime()
        status, _, body = app.request("POST", "/api/v1/audio/mp3",
                                      multipart(b"ID3" + bytes(8 * 1024 * 1024 + 1), name="file", filename="a.mp3"),
                                      {"Content-Type": f"multipart/form-data; boundary={BOUNDARY}"}, timeout=30)
        self.assertEqual((413, "payloadTooLarge"), (status, json.loads(body)["error"]["code"]))
        self.assertEqual(200, app.request("GET", "/api/v1/version")[0])

    def test_update_options_are_validated(self):
        base = [OPTIONS.binary, "--data", str(Path(self.folder.name) / "data"), "--webui", OPTIONS.webui]
        for extra in (["--update-state", str(self.fixture.state),
                       "--update-dir", str(self.fixture.root / "work")],
                      ["--update-state", str(self.fixture.state),
                       "--supervisor-fd", "103"],
                      ["--update-state", str(self.fixture.state)],
                      ["--update-dir", str(self.fixture.root / "work")],
                      ["--release-root", str(self.fixture.root)],
                      ["--install-root", str(self.fixture.root)],
                      ["--ca-file", "a", "--ca-file", "b"]):
            with self.subTest(extra=extra):
                result = subprocess.run(base + extra, capture_output=True, timeout=10)
                self.assertEqual(2, result.returncode, result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.binary = str(Path(OPTIONS.binary).resolve())
    OPTIONS.webui = str(Path(OPTIONS.webui).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
