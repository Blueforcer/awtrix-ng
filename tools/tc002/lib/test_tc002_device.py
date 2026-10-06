"""Tests of the ADB layer the TC002 host tools share (tc002_device.py)."""
import contextlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tc002_device  # noqa: E402



class DeviceTest(unittest.TestCase):
    def test_usb_keeper_script(self):
        script = tc002_device.USB_KEEPER
        self.assertEqual(set(re.findall(r"soc:usbotg/\w+", script)), {"soc:usbotg/otg_role"})
        self.assertIn(f"end=$((s + {tc002_device.KEEPER_SECONDS}))", script)
        self.assertIn(f"next=$((s + {tc002_device.KEEPER_HOLD}))", script)
        self.assertIn("case $r in usb_host|usb_null)", script)
        self.assertLess(script.index("trap '' HUP"), script.index(" & "))
        self.assertNotIn('"', script)
        self.assertNotIn("\n", script)
        self.assertLess(len(script), tc002_device.SHELL_LIMIT)
        self.assertRegex("awtrix-usb-keeper running 812", tc002_device.KEEPER_REPLY)
        self.assertNotRegex("awtrix-usb-keeper: not found", tc002_device.KEEPER_REPLY)

    def test_serials(self):
        for serial in ("0123456789ABCDEF", "adb-0123._adb-tls-connect._tcp", "192.168.1.9:5555",
                       "emulator-5554"):
            self.assertEqual(tc002_device.usb_serial(serial), serial == "0123456789ABCDEF", serial)
        for serial in (None, "", "-d", "a b", "a;b", "x" * 65):
            with self.assertRaises(tc002_device.DeviceError):
                tc002_device.Device("adb", serial)

    def test_quote_rejects_shell_syntax(self):
        self.assertEqual(tc002_device.quote("/data/awtrix-ng/releases/1.1.2-gabc"),
                         "/data/awtrix-ng/releases/1.1.2-gabc")
        for bad in ("a b", "a;b", "$(x)", "'", ""):
            with self.assertRaises(tc002_device.DeviceError):
                tc002_device.quote(bad)

    def test_find_adb_takes_the_option_then_the_environment(self):
        with tempfile.TemporaryDirectory() as work:
            first, second = Path(work, "first"), Path(work, "second")
            first.write_text("x")
            second.write_text("x")
            with mock.patch.dict(os.environ, {"ADB": str(second)}):
                self.assertEqual(tc002_device.find_adb(str(first)), str(first))
                self.assertEqual(tc002_device.find_adb(None), str(second))
            with mock.patch.dict(os.environ, {"ADB": str(Path(work, "missing")), "PATH": work}):
                with self.assertRaisesRegex(tc002_device.DeviceError, "adb not found"):
                    tc002_device.find_adb(None)


@unittest.skipUnless(os.name == "posix" and Path("/proc/uptime").exists() and shutil.which("sh"),
                     "needs a POSIX shell and /proc/uptime")
class UsbKeeperScriptTest(unittest.TestCase):
    """The keeper script in a real shell against a stand-in otg_role file."""

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="awtrix-keeper-test-"))
        self.addCleanup(shutil.rmtree, self.work, ignore_errors=True)
        self.role = self.work / "otg_role"
        self.role.write_text("unkown\n")
        self.pid_file = self.work / "keeper.pid"
        self.loader_log = self.work / "awtrix-loader.log"
        self.vendor = self.work / "awtrix-loader.vendor"
        self.pids = []

    def tearDown(self):
        for pid in self.pids:
            with contextlib.suppress(OSError):
                os.kill(pid, 9)

    def start(self, lifetime, hold, path=None):
        script = tc002_device.usb_keeper_script(
            role=str(self.role), pid_file=str(self.pid_file), loader_log=str(self.loader_log),
            vendor=str(self.vendor), lifetime=lifetime, hold=hold)
        env = dict(os.environ, PATH=path or os.environ.get("PATH", "/usr/bin:/bin"))
        reply = subprocess.run(["sh", "-c", script], capture_output=True, text=True, timeout=10,
                               env=env).stdout.strip()
        match = re.fullmatch(r"awtrix-usb-keeper (started|running) (\d+)", reply)
        self.assertTrue(match, reply)
        self.pids.append(int(match[2]))
        return match[1], int(match[2])

    @staticmethod
    def alive(pid):
        try:
            return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0] != "Z"
        except (OSError, IndexError):
            return False

    @staticmethod
    def wait_until(condition, limit):
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            if condition():
                return True
            time.sleep(0.05)
        return condition()

    def test_keeper_writes_only_on_host_or_null_and_ends_by_uptime(self):
        started = time.monotonic()
        how, pid = self.start(lifetime=10, hold=3)
        self.assertEqual(how, "started")
        self.assertEqual(self.pid_file.read_text().strip(), str(pid))
        time.sleep(1.3)
        self.assertEqual(self.role.read_text(), "unkown\n")
        self.role.write_text("usb_host\n")
        self.assertTrue(self.wait_until(lambda: self.role.read_text() == "usb_device", 2.5))
        self.role.write_text("usb_null\n")
        time.sleep(0.8)
        self.assertEqual(self.role.read_text(), "usb_null\n")
        self.assertTrue(self.wait_until(lambda: self.role.read_text() == "usb_device", 4.0))
        self.assertEqual(self.start(lifetime=10, hold=3), ("running", pid))
        os.kill(pid, 1)
        time.sleep(0.3)
        self.assertTrue(self.alive(pid))
        self.assertTrue(self.wait_until(lambda: not self.alive(pid),
                                        13 - (time.monotonic() - started)))
        self.assertGreater(time.monotonic() - started, 8.5)
        self.assertFalse(self.pid_file.exists())

    def no_sleep_path(self):
        fake_bin = self.work / "bin"
        fake_bin.mkdir(exist_ok=True)
        (fake_bin / "sleep").write_text("#!/bin/sh\nexit 0\n")
        (fake_bin / "sleep").chmod(0o755)
        return f"{fake_bin}:{os.environ.get('PATH', '/bin')}"

    def test_keeper_without_a_working_sleep_still_ends_by_uptime(self):
        started = time.monotonic()
        _, pid = self.start(lifetime=3, hold=1, path=self.no_sleep_path())
        self.role.write_text("usb_host\n")
        self.assertTrue(self.wait_until(lambda: self.role.read_text() == "usb_device", 2.0))
        self.assertTrue(self.wait_until(lambda: not self.alive(pid), 6.0))
        self.assertGreater(time.monotonic() - started, 1.5)

    def test_keeper_without_a_working_sleep_looks_once_a_second(self):
        self.role.write_text("usb_host\n")
        writes, stop = [], threading.Event()

        def undo():
            while not stop.is_set():
                if self.role.read_text() == "usb_device":
                    writes.append(time.monotonic())
                    self.role.write_text("usb_host\n")
                time.sleep(0.01)

        watcher = threading.Thread(target=undo, daemon=True)
        watcher.start()
        _, pid = self.start(lifetime=3, hold=0, path=self.no_sleep_path())
        self.assertTrue(self.wait_until(lambda: not self.alive(pid), 6.0))
        stop.set()
        watcher.join(2.0)
        self.assertGreaterEqual(len(writes), 2)
        self.assertLessEqual(len(writes), 4)

    def test_keeper_leaves_the_port_to_the_awtrix_loader(self):
        self.loader_log.write_text("loader ran\n")
        self.vendor.write_text("knob held at power-on: vendor app\n")
        _, pid = self.start(lifetime=30, hold=1)
        time.sleep(1.3)
        self.assertTrue(self.alive(pid))
        self.vendor.unlink()
        time.sleep(0.2)
        self.role.write_text("usb_host\n")
        self.assertTrue(self.wait_until(lambda: not self.alive(pid), 4.0))
        self.assertEqual(self.role.read_text(), "usb_host\n")
        time.sleep(1.2)
        self.assertEqual(self.role.read_text(), "usb_host\n")

    def test_keeper_stays_when_the_vendor_marker_follows_the_loader_log(self):
        _, pid = self.start(lifetime=30, hold=1, path=self.no_sleep_path())
        time.sleep(0.5)
        self.loader_log.write_text("loader ran\n")
        time.sleep(0.3)
        self.vendor.write_text("knob held at power-on: vendor app\n")
        time.sleep(3.2)
        self.assertTrue(self.alive(pid))
        self.role.write_text("usb_host\n")
        self.assertTrue(self.wait_until(lambda: self.role.read_text() == "usb_device", 2.5))


if __name__ == "__main__":
    unittest.main()
