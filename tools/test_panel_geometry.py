#!/usr/bin/env python3
"""Smoke-test native_sim panel sizing and reboot-only geometry changes.

Run after building native_sim, in the build container:
    python tools/test_panel_geometry.py
Uses only an isolated temporary data directory and a loopback HTTP port.
"""
import contextlib
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


@contextlib.contextmanager
def simulator(data):
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    with tempfile.TemporaryFile() as log:
        proc = subprocess.Popen(
            [".pio/build/native_sim/program", "--no-matrix", "--port", str(port),
             "--data", str(data)], stdout=log, stderr=log)
        base = f"http://127.0.0.1:{port}"
        try:
            deadline = time.monotonic() + 15
            while True:
                try:
                    request(base, "/api/v1/system")
                    break
                except (OSError, urllib.error.URLError):
                    if proc.poll() is not None or time.monotonic() >= deadline:
                        log.seek(0)
                        raise RuntimeError(log.read().decode(errors="replace"))
                    time.sleep(0.05)
            yield base
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


def request(base, path, body=None):
    req = urllib.request.Request(
        base + path, method="GET" if body is None else "PUT",
        data=None if body is None else json.dumps(body).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=3) as response:
        return json.load(response)


def screen(base, width, height):
    frame = request(base, "/api/v1/display/screen")
    assert (frame["width"], frame["height"]) == (width, height), frame.keys()
    assert len(frame["pixels"]) == width * height
    print(f"PASS screen {width}x{height}: {len(frame['pixels'])} pixels")


def main():
    with tempfile.TemporaryDirectory(prefix="awtrix-geometry-") as directory:
        data = Path(directory)
        with simulator(data) as base:
            assert request(base, "/api/v1/system")["panelHeight"] == 8
            screen(base, 32, 8)
            for height in (7, 17, 11.5):
                try:
                    request(base, "/api/v1/system", {"panelHeight": height})
                except urllib.error.HTTPError as error:
                    assert error.code == 422
                else:
                    raise AssertionError(f"accepted invalid height {height}")
            request(base, "/api/v1/system", {"panelWidth": 53, "panelHeight": 11})
            assert request(base, "/api/v1/system")["panelHeight"] == 11
            screen(base, 32, 8)
            print("PASS validation and deferred geometry")
        with simulator(data) as base:
            screen(base, 53, 11)
            request(base, "/api/v1/system", {"panelHeight": 16})
            screen(base, 53, 11)
        with simulator(data) as base:
            screen(base, 53, 16)
        print("PASS persisted height and restart geometry (8, 11, 16)")


if __name__ == "__main__":
    main()
