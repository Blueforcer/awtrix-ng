#!/usr/bin/env python3
"""A POST without Content-Length or chunked encoding has an empty body (RFC 9112 6.3).

`curl -X POST http://<awtrix-ip>/api/v1/device/reboot` sends exactly that, and the ESP32 answers
at once. The host server must not wait for the connection to close before dispatching.
"""

from __future__ import annotations
import sys

import argparse
from pathlib import Path
import socket
import subprocess
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import free_port, wait_ready, stop_process


def listening(port):
    with socket.create_connection(("127.0.0.1", port), timeout=0.2):
        return True


def post(port, path, extra_headers=b""):
    request = (b"POST " + path + b" HTTP/1.1\r\nHost: 127.0.0.1:" + str(port).encode() +
               b"\r\n" + extra_headers + b"Connection: close\r\n\r\n")
    started = time.monotonic()
    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
        connection.sendall(request)
        reply = b""
        while True:
            chunk = connection.recv(4096)
            if not chunk:
                break
            reply += chunk
    return reply, time.monotonic() - started


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="awtrix-bodyless-post-") as root:
        data = Path(root) / "data"
        data.mkdir(mode=0o700)
        port = free_port()
        process = subprocess.Popen([args.binary, "--data", str(data), "--webui", args.webui, "--port", str(port)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_ready(process, lambda: listening(port))
            for headers in (b"", b"Content-Length: 0\r\n"):
                reply, elapsed = post(port, b"/api/v1/apps/next", headers)
                status = reply.split(b"\r\n", 1)[0]
                if b" 200 " not in status or elapsed > 2.0:
                    raise AssertionError(f"POST with {headers!r}: {status!r} after {elapsed:.2f} s")
        finally:
            stop_process(process)
    print("bodyless POST: ok")


if __name__ == "__main__":
    main()
