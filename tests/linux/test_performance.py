"""Exercise optional Linux aggregate reports with real process shutdown and files."""
import argparse
import json
from pathlib import Path
import resource
import signal
import socket
import stat
import subprocess
import tempfile
import time
import unittest
import urllib.request

OPTIONS = None


class PerformanceContract(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="awtrix-performance-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            self.port = listener.getsockname()[1]

    def command(self, *extra):
        return [OPTIONS.binary, "--data", str(self.root / "data"), "--webui", OPTIONS.webui,
                "--port", str(self.port), *extra]

    def rejected(self, *extra):
        result = subprocess.run(self.command(*extra), capture_output=True, text=True, timeout=5)
        self.assertEqual(2, result.returncode, result.stdout + result.stderr)
        self.assertFalse((self.root / "data").exists())

    def test_invalid_cli_and_report_paths(self):
        self.rejected("--performance-report")
        self.rejected("--performance-report", "")
        self.rejected("--performance-report", str(self.root / "one"), "--performance-report", str(self.root / "two"))
        self.rejected("--performance-report", str(self.root / "missing" / "report.json"))
        self.rejected("--performance-report", str(self.root))

    def test_existing_and_symlink_targets_are_never_overwritten(self):
        target = self.root / "existing"
        target.write_text("original evidence")
        link = self.root / "link"
        link.symlink_to(target)
        self.rejected("--performance-report", str(target))
        self.rejected("--performance-report", str(link))
        self.assertEqual("original evidence", target.read_text())
        dangling = self.root / "dangling"
        dangling.symlink_to(self.root / "absent")
        self.rejected("--performance-report", str(dangling))
        self.assertFalse((self.root / "absent").exists())

    def test_shutdown_flushes_report_or_exposes_write_failure(self):
        for stop_signal, write_failure in ((signal.SIGTERM, False), (signal.SIGINT, False), (signal.SIGTERM, True)):
            with self.subTest(signal=stop_signal, write_failure=write_failure):
                report = self.root / f"report-{stop_signal}-{write_failure}.json"
                process = subprocess.Popen(self.command("--performance-report", str(report)),
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                           preexec_fn=(lambda: signal.signal(signal.SIGXFSZ, signal.SIG_IGN)) if write_failure else None)
                try:
                    deadline = time.monotonic() + 8
                    while True:
                        self.assertIsNone(process.poll(), "runtime exited before HTTP readiness")
                        try:
                            with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/api/v1/version", timeout=.3) as response:
                                if response.status == 200:
                                    break
                        except OSError:
                            pass
                        self.assertLess(time.monotonic(), deadline, "runtime readiness timeout")
                        time.sleep(.02)
                    time.sleep(.2)
                    if write_failure:
                        # Force the final regular-file write to fail after successful startup.
                        resource.prlimit(process.pid, resource.RLIMIT_FSIZE, (0, 0))
                    process.send_signal(stop_signal)
                    output, _ = process.communicate(timeout=10)
                    self.assertEqual(1 if write_failure else 0, process.returncode, output)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate(timeout=5)
                if write_failure:
                    self.assertIn("Performance report failed", output)
                    self.assertEqual(0, report.stat().st_size)
                    continue
                self.assertEqual(0o600, stat.S_IMODE(report.stat().st_mode))
                self.assertLess(report.stat().st_size, 8192)
                result = json.loads(report.read_text())
                self.assertEqual((1, "linux", "clean", True),
                                 (result["schema"], result["platform"], result["status"], result["measurement_valid"]))
                self.assertEqual(24000, result["frame_budget_us"])
                self.assertNotIn("panel", result)
                self.assertGreater(result["frames"], 2)
                self.assertEqual(result["frames"], result["attempted_frames"])
                self.assertEqual(0, result["aborted_frames"])
                self.assertGreater(result["elapsed_us"], 0)
                self.assertGreater(result["process_peak_rss_kib"], 0)
                self.assertGreaterEqual(result["cpu_user_us"], 0)
                self.assertGreaterEqual(result["cpu_system_us"], 0)
                bounds = result["histogram_upper_bounds_us"]
                self.assertIsNone(bounds[-1])
                self.assertEqual(sorted(bounds[:-1]), bounds[:-1])
                for metric in ("render", "display", "work", "loop", "deadline_lateness"):
                    aggregate = result[metric]
                    self.assertEqual(result["frames"], aggregate["samples"])
                    self.assertEqual(result["frames"], sum(aggregate["histogram"]))
                    self.assertEqual(len(bounds), len(aggregate["histogram"]))
                    self.assertAlmostEqual(aggregate["total_us"] / result["frames"], aggregate["avg_us"], places=3)
                    self.assertGreaterEqual(aggregate["max_us"], aggregate["avg_us"])
                    rank = (result["frames"] * 95 + 99) // 100
                    cumulative = 0
                    for upper, count in zip(bounds, aggregate["histogram"]):
                        cumulative += count
                        if cumulative >= rank:
                            self.assertEqual(upper, aggregate["p95_upper_us"])
                            break
                self.assertLessEqual(result["render"]["total_us"] + result["display"]["total_us"], result["work"]["total_us"])
                self.assertLessEqual(result["work"]["total_us"], result["loop"]["total_us"])
                self.assertLessEqual(result["loop"]["total_us"], result["elapsed_us"])
                self.assertLessEqual(result["deadline_misses"], result["frames"])
                self.assertLessEqual(result["work_over_budget"], result["frames"])

    def test_startup_failure_writes_incomplete_report(self):
        # Missing guardian descriptors fail before physical display access.
        report = self.root / "failed-start.json"
        result = subprocess.run(self.command("--board", "tc002", "--tc002-input-fds", "100,101",
                                             "--performance-report", str(report)),
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(1, result.returncode, result.stderr)
        body = json.loads(report.read_text())
        self.assertEqual("incomplete", body["status"])
        self.assertEqual((0, 0), (body["frames"], body["elapsed_us"]))
        self.assertEqual("runtime_loop", body["panel"]["scope"])
        self.assertTrue(body["panel"]["measurement_valid"])
        self.assertEqual((0, 0, 0), (body["panel"]["shows"], body["panel"]["transfers"],
                                   body["panel"]["spi_calls"]))
        self.assertTrue(all(metric["samples"] == 0 for metric in body["panel"]["durations"].values()))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining], verbosity=2)
