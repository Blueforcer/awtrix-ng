#!/usr/bin/env python3
"""UDP broadcast discovery on Linux: FIND_AWTRIXNG on port 4210 is answered on 4211 with the
host name and port, as on the ESP32, but only when the web server is reachable from the network
(--lan)."""

from __future__ import annotations
import sys

import argparse
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import free_port, wait_ready, stop_process


def ask(timeout):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("127.0.0.1", 4211))
        sock.settimeout(0.2)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            sock.sendto(b"FIND_AWTRIXNG", ("127.0.0.1", 4210))
            try:
                reply, _ = sock.recvfrom(128)
                return reply.decode()
            except socket.timeout:
                continue
    return None


def run(binary, webui, lan):
    port = free_port()
    with tempfile.TemporaryDirectory(prefix="awtrix-discovery-") as data:
        command = [str(binary), "--data", data, "--port", str(port), "--webui", str(webui)]
        if lan:
            command += ["--lan", "--listen", "127.0.0.1"]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            def ready():
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/v1/version", timeout=1) as response:
                    return response.status == 200
            wait_ready(process, ready)
            return port, ask(3 if lan else 0.8)
        finally:
            stop_process(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    args = parser.parse_args()
    binary, webui = args.binary.resolve(), args.webui.resolve()
    _, reply = run(binary, webui, lan=False)
    assert reply is None, f"a loopback-only runtime answered discovery: {reply!r}"
    port, reply = run(binary, webui, lan=True)
    expected = f"{socket.gethostname()}:{port}"
    assert reply == expected, f"expected {expected!r}, got {reply!r}"
    print("PASS: discovery answers with host name and port in LAN mode and stays silent otherwise")


if __name__ == "__main__":
    main()
