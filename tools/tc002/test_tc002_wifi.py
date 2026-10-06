#!/usr/bin/env python3
import contextlib
import getpass
import io
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import unittest
import warnings
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tc002_wifi  # noqa: E402

SECRET = "synthetic-secret-42"

FAKE_ADB = r'''
import json, os, sys
log = os.environ["FAKE_ADB_LOG"]
with open(log, "a", encoding="utf-8") as out:
    out.write(json.dumps(sys.argv[1:]) + "\n")
args = sys.argv[1:]
if args == ["devices"]:
    print("List of devices attached")
    print(os.environ["FAKE_ADB_SERIAL"] + "\tdevice")
    sys.exit(0)
if args[:2] != ["-s", os.environ["FAKE_ADB_SERIAL"]]:
    sys.exit(1)
if args[2:4] == ["forward", "tcp:0"]:
    print(os.environ["FAKE_ADB_PORT"])
elif args[2:4] == ["forward", "--remove"]:
    pass
else:
    sys.exit(1)
'''


class FakeDaemon:
    def __init__(self, replies):
        self.replies = replies
        self.requests = []
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(8)
        self.port = self.server.getsockname()[1]
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        while True:
            try:
                conn, _ = self.server.accept()
            except OSError:
                return
            with conn:
                header = self.receive(conn, 4)
                if len(header) < 4:
                    continue
                (length,) = struct.unpack(">I", header)
                body = self.receive(conn, length)
                command, _, payload = body.partition(b"\n")
                self.requests.append((command.decode(), payload))
                reply = self.replies(command.decode(), payload)
                if reply is None:
                    continue
                data = json.dumps(reply).encode()
                conn.sendall(struct.pack(">I", len(data)) + data)

    @staticmethod
    def receive(conn, size):
        data = b""
        while len(data) < size:
            chunk = conn.recv(size - len(data))
            if not chunk:
                break
            data += chunk
        return data

    def close(self):
        self.server.close()


class ToolTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.log = Path(self.dir.name) / "adb.log"
        script = Path(self.dir.name) / "fake_adb.py"
        script.write_text(FAKE_ADB, encoding="utf-8")
        self.adb = [sys.executable, str(script)]
        self.daemon = None

    def tearDown(self):
        if self.daemon:
            self.daemon.close()
        self.dir.cleanup()

    def start_daemon(self, replies):
        self.daemon = FakeDaemon(replies)
        os.environ.update(FAKE_ADB_LOG=str(self.log), FAKE_ADB_SERIAL="0123456789ABCDEF",
                          FAKE_ADB_PORT=str(self.daemon.port))

    def adb_calls(self):
        """Every adb call but `adb devices`, with which the tool looks for the serial first."""
        if not self.log.exists():
            return []
        calls = [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()]
        return [call for call in calls if call != ["devices"]]

    def run_tool(self, argv, stdin_text=""):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch("sys.stdin", io.StringIO(stdin_text)), contextlib.redirect_stdout(out), \
                contextlib.redirect_stderr(err):
            code = tc002_wifi.main(argv, adb_program=self.adb)
        return code, out.getvalue(), err.getvalue()

    def test_network_serial_refused_before_any_adb_call(self):
        code, _, err = self.run_tool(["--serial", "192.0.2.110:5555", "status"])
        self.assertEqual(code, tc002_wifi.EXIT_USAGE)
        self.assertIn("USB-only", err)
        self.assertEqual(self.adb_calls(), [])

    def test_frame_encoding(self):
        frame = tc002_wifi.encode_frame("wifi-set", b'{"a":1}')
        self.assertEqual(frame, struct.pack(">I", 16) + b'wifi-set\n{"a":1}')
        with self.assertRaises(tc002_wifi.ToolError):
            tc002_wifi.encode_frame("wifi-set", b"x" * 4096)

    def test_invalid_credentials_are_refused_before_adb(self):
        for ssid, password in (("", SECRET), ("é" * 17, SECRET), ("Home", "short"), ("Home", "tab\there!!")):
            code, out, err = self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", ssid,
                                            "--password-stdin"], password + "\n")
            self.assertEqual(code, tc002_wifi.EXIT_USAGE, err)
            self.assertEqual(self.adb_calls(), [])
            self.assertNotIn(SECRET, out + err)

    def test_set_success_follows_link_and_removes_forward(self):
        states = iter(["connecting", "connected"])

        def replies(command, payload):
            if command == "wifi-set":
                return {"ok": True}
            return {"link": next(states), "ssid": "Caf\u00e9 \"5G\"", "store": "ok"}

        self.start_daemon(replies)
        with mock.patch("time.sleep"):
            code, out, err = self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", "Caf\u00e9 \"5G\"",
                                            "--password-stdin"], SECRET + "\n")
        self.assertEqual(code, tc002_wifi.EXIT_OK, err)
        command, payload = self.daemon.requests[0]
        self.assertEqual(command, "wifi-set")
        self.assertEqual(json.loads(payload), {"ssid": "Caf\u00e9 \"5G\"", "password": SECRET})
        self.assertIn("link: connected", out)
        calls = self.adb_calls()
        self.assertEqual(calls[0], ["-s", "0123456789ABCDEF", "forward", "tcp:0",
                                    "localfilesystem:/tmp/awtrix-tc002d/control.sock"])
        self.assertEqual(calls[-1], ["-s", "0123456789ABCDEF", "forward", "--remove", f"tcp:{self.daemon.port}"])
        self.assertNotIn(SECRET, json.dumps(calls))
        self.assertNotIn(SECRET, out + err)

    def test_wrong_password_reported(self):
        self.start_daemon(lambda c, p: {"ok": True} if c == "wifi-set" else
                          {"link": "failed", "ssid": "Home", "store": "ok"})
        code, out, err = self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", "Home", "--password-stdin"],
                                       SECRET + "\n")
        self.assertEqual(code, tc002_wifi.EXIT_DEVICE)
        self.assertIn("rejected the password", err)
        self.assertNotIn(SECRET, out + err)

    def follow(self, ssid, statuses, *extra):
        def replies(command, payload):
            if command == "wifi-set":
                return {"ok": True}
            return statuses.pop(0) if len(statuses) > 1 else statuses[0]

        self.start_daemon(replies)
        with mock.patch("time.sleep"):
            return self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", ssid, "--password-stdin", *extra],
                                 SECRET + "\n")

    def test_link_from_before_the_new_credentials_is_ignored(self):
        code, out, err = self.follow("Home", [{"link": "failed", "ssid": "Home", "store": "saving"},
                                              {"link": "connecting", "ssid": "Home", "store": "ok"},
                                              {"link": "connected", "ssid": "Home", "store": "ok"}])
        self.assertEqual(code, tc002_wifi.EXIT_OK, err)
        self.assertNotIn("link: failed", out)
        self.assertIn("link: connected", out)

    def test_failed_store_is_reported_even_for_the_stored_network(self):
        code, out, err = self.follow("Home", [{"link": "connecting", "ssid": "Home", "store": "saving"},
                                              {"link": "connected", "ssid": "Home", "store": "ok",
                                               "error": "credential store write failed: No space left on device"}])
        self.assertEqual(code, tc002_wifi.EXIT_DEVICE)
        self.assertIn("could not store the new network", err)
        self.assertNotIn("link: connected", out)

    def test_link_of_another_network_is_ignored(self):
        code, out, err = self.follow("Home", [{"link": "connected", "ssid": "Office", "store": "saving"},
                                              {"link": "connected", "ssid": "Office"},
                                              {"link": "connecting", "ssid": "Home"},
                                              {"link": "failed", "ssid": "Home"}])
        self.assertEqual(code, tc002_wifi.EXIT_DEVICE)
        self.assertIn("rejected the password", err)
        self.assertNotIn("link: connected", out)

    def test_link_matches_the_daemons_escaped_ssid(self):
        code, out, err = self.follow("Tab\tNet", [{"link": "connected", "ssid": "Tab\\x09Net", "store": "ok"}])
        self.assertEqual(code, tc002_wifi.EXIT_OK, err)
        self.assertIn("link: connected", out)

    def test_new_network_never_reported(self):
        code, out, err = self.follow("Home", [{"link": "connected", "ssid": "Office", "store": "ok"}], "--wait", "0.2")
        self.assertEqual(code, tc002_wifi.EXIT_OK, err)
        self.assertNotIn("link: connected", out)
        self.assertIn("has not applied", out)

    def test_device_refusal_and_forward_removed(self):
        self.start_daemon(lambda c, p: {"ok": False, "error": "invalid-password"})
        code, out, err = self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", "Home", "--password-stdin"],
                                       SECRET + "\n")
        self.assertEqual(code, tc002_wifi.EXIT_DEVICE)
        self.assertIn("invalid-password", err)
        self.assertEqual(self.adb_calls()[-1][2:4], ["forward", "--remove"])

    def test_transport_failure_still_removes_forward(self):
        self.start_daemon(lambda c, p: None)
        code, _, err = self.run_tool(["--serial", "0123456789ABCDEF", "status"])
        self.assertEqual(code, tc002_wifi.EXIT_TRANSPORT)
        self.assertIn("complete reply", err)
        self.assertEqual(self.adb_calls()[-1][2:4], ["forward", "--remove"])

    def test_status_command(self):
        self.start_daemon(lambda c, p: {"command": c, "payload": p.decode()})
        code, out, _ = self.run_tool(["--serial", "0123456789ABCDEF", "status"])
        self.assertEqual(code, tc002_wifi.EXIT_OK)
        self.assertEqual(json.loads(out), {"command": "status", "payload": ""})

    def test_scan_waits_for_results(self):
        polls = iter([{"scanning": True, "ageMs": -1, "networks": []},
                      {"scanning": False, "ageMs": 5, "networks": [
                          {"ssid": "Home", "rssi": -48, "secure": True},
                          {"ssid": "Caf\u00e9", "rssi": -60, "secure": False}]}])

        def replies(command, payload):
            if command == "wifi-scan":
                return {"ok": True, "scanning": True}
            return next(polls)

        self.start_daemon(replies)
        with mock.patch("time.sleep"):
            code, out, err = self.run_tool(["--serial", "0123456789ABCDEF", "scan"])
        self.assertEqual(code, tc002_wifi.EXIT_OK, err)
        self.assertEqual([c for c, _ in self.daemon.requests], ["wifi-scan", "wifi-scan-results", "wifi-scan-results"])
        lines = out.splitlines()
        self.assertEqual(lines[1].split(), ["-48", "secured", "Home"])
        self.assertEqual(lines[2].split(), ["-60", "open", "Caf\u00e9"])
        self.assertEqual(self.adb_calls()[-1][2:4], ["forward", "--remove"])

    def test_scan_refused(self):
        self.start_daemon(lambda c, p: {"ok": False, "error": "stopping"})
        code, _, err = self.run_tool(["--serial", "0123456789ABCDEF", "scan"])
        self.assertEqual(code, tc002_wifi.EXIT_DEVICE)
        self.assertIn("stopping", err)

    def test_open_network_needs_confirmation(self):
        self.start_daemon(lambda c, p: {"ok": True})
        code, _, err = self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", "Cafe", "--password-stdin"], "\n")
        self.assertEqual(code, tc002_wifi.EXIT_USAGE)
        self.assertIn("--open", err)
        self.assertEqual(self.adb_calls(), [])
        code, out, _ = self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", "Cafe", "--password-stdin", "--open",
                                      "--wait", "0"], "\n")
        self.assertEqual(code, tc002_wifi.EXIT_OK)
        self.assertIn("stored; connecting", out)
        self.assertEqual(json.loads(self.daemon.requests[0][1]), {"ssid": "Cafe", "password": ""})

    def test_echoing_getpass_is_fatal(self):
        def echoing(prompt=""):
            warnings.warn("Can not control echo on the terminal.", getpass.GetPassWarning)
            return SECRET

        with mock.patch("getpass.getpass", echoing):
            code, out, err = self.run_tool(["--serial", "0123456789ABCDEF", "--ssid", "Home"])
        self.assertEqual(code, tc002_wifi.EXIT_USAGE)
        self.assertIn("refusing", err)
        self.assertEqual(self.adb_calls(), [])
        self.assertNotIn(SECRET, out + err)


if __name__ == "__main__":
    unittest.main()
