#!/usr/bin/env python3
"""Real-socket regressions for bounded cpp-httplib header parsing.

Runs against the hardened Linux HTTPS application given as --binary; that needs a non-root Linux
user and OpenSSL.
The local cpp-httplib patch limits each header/trailer section to 128 fields and
64 KiB of wire bytes, each line to 8 KiB; limits include CRLF. Responses and
chunk metadata use the same bounded line reader. No device/network access beyond
loopback. Oversized unfinished streams must fail promptly, before authentication.
"""

from __future__ import annotations
import sys

import argparse
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import ssl
import subprocess
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import stop_process, wait_ready


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def header_line(size):
    require(size >= 5, "invalid fixture line length")
    return b"X: " + b"a" * (size - 5) + b"\r\n"


class Runtime:
    def __init__(self, executable, webui):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-header-contract-")
        self.root = Path(self.temporary.name)
        self.process = None
        self.log = None
        self.token = secrets.token_hex(32)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            self.port = listener.getsockname()[1]
        self.host = f"localhost:{self.port}".encode()
        data = self.root / "data"
        data.mkdir(mode=0o700)
        command = [str(executable), "--data", str(data), "--webui", str(webui), "--port", str(self.port)]
        try:
            require(os.name == "posix" and os.geteuid() != 0, "TLS regression requires a non-root Linux user")
            credential = self.root / "admin.token"
            credential.write_text(self.token + "\n")
            credential.chmod(0o600)
            cert, key = self.root / "tls.crt", self.root / "tls.key"
            generated = subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-days", "2", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
                "-keyout", str(key), "-out", str(cert)], capture_output=True, timeout=15)
            require(generated.returncode == 0, "could not generate local TLS fixture")
            key.chmod(0o600)
            self.context = ssl.create_default_context(cafile=str(cert))
            command += ["--hardened", "--credentials-file", str(credential), "--tls-cert", str(cert),
                        "--tls-key", str(key), "--origin", f"https://localhost:{self.port}"]
            self.log = open(self.root / "runtime.log", "wb")
            self.process = subprocess.Popen(command, stdout=self.log, stderr=self.log)
            wait_ready(self.process, lambda: self.health() == 200)
        except BaseException:
            self.close()
            raise

    def connect(self):
        stream = socket.create_connection(("127.0.0.1", self.port), timeout=3)
        stream = self.context.wrap_socket(stream, server_hostname="localhost")
        stream.settimeout(2)
        return stream

    def health(self):
        client = http.client.HTTPSConnection("localhost", self.port, context=self.context, timeout=3)
        try:
            client.request("GET", "/api/v1/version", headers={"Authorization": "Bearer " + self.token})
            response = client.getresponse()
            response.read()
            return response.status
        finally:
            client.close()

    def exchange(self, headers, *, expected, incomplete=False, request_line=None, barrier=None, suffix=b""):
        request_line = request_line or b"GET /api/v1/version HTTP/1.1\r\n"
        wire = request_line + headers + (b"" if incomplete else b"\r\n") + suffix
        response = bytearray()
        with self.connect() as stream:
            if barrier:
                barrier.wait(timeout=5)
            started = time.monotonic()
            try:
                stream.sendall(wire)
                while True:
                    part = stream.recv(4096)
                    if not part:
                        break
                    response.extend(part)
                    require(len(response) <= 65536, "unbounded error response")
            except (ConnectionResetError, BrokenPipeError, ssl.SSLEOFError):
                pass
            except socket.timeout as error:
                raise AssertionError("oversized/incomplete header stream was not closed promptly") from error
            require(time.monotonic() - started < 3, "header rejection exceeded its deadline")
        if response:
            first = bytes(response).split(b"\r\n", 1)[0].split()
            require(len(first) >= 2 and first[0].startswith(b"HTTP/"), "invalid HTTP response framing")
            status = int(first[1])
            require(status in expected, f"unexpected header response HTTP {status}")
        else:
            require(None in expected, "valid request closed without a response")

    def close(self):
        try:
            stop_process(self.process)
        finally:
            self.process = None
            if self.log:
                self.log.close()
                self.log = None
            self.temporary.cleanup()


def exercise(runtime):
    basic = b"Host: " + runtime.host + b"\r\nConnection: close\r\n"
    authenticated = basic + b"Authorization: Bearer " + runtime.token.encode() + b"\r\n"
    denied = {400, 431, None}
    # No terminating blank line, EOF or write shutdown: only the parser limits
    # can reject these streams promptly. Authorization is deliberately absent.
    cases = {
        "incomplete-many-headers": (basic + b"X: a\r\n" * 129, None),
        "incomplete-long-line": (basic + b"X: " + b"a" * 8192, None),
        "incomplete-total-bytes": (basic + header_line(8192) * 9, None),
        "unterminated-request-line": (b"", b"GET /" + b"a" * 10000),
    }
    checked = []
    for name, (headers, request_line) in cases.items():
        runtime.exchange(headers, expected=denied, incomplete=True, request_line=request_line)
        require(runtime.health() == 200, "runtime unavailable after " + name)
        checked.append(name)
    barrier = threading.Barrier(8)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as workers:
        attempts = [workers.submit(runtime.exchange, basic + b"X: a\r\n" * 129,
                    expected=denied, incomplete=True, barrier=barrier) for _ in range(8)]
        for attempt in attempts:
            attempt.result(timeout=8)
    require(runtime.health() == 200, "runtime unavailable after concurrent streams")
    checked.append("eight-concurrent-incomplete-streams")
    runtime.exchange(authenticated + header_line(8192), expected={200})
    runtime.exchange(authenticated + b"X: a\r\n" * 125, expected={200})
    total = authenticated
    while len(total) + 8192 < 65536 - 2:
        total += header_line(8192)
    total += header_line(65536 - 2 - len(total))
    runtime.exchange(total, expected={200})
    checked += ["exact-line-boundary", "exact-count-boundary", "exact-total-boundary"]
    runtime.exchange(basic, expected={401})
    checked.append("authentication-still-required")
    require(runtime.health() == 200, "runtime unavailable after boundary requests")
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    args = parser.parse_args()
    runtime = Runtime(args.binary.resolve(), args.webui.resolve())
    try:
        results = {"linux-https": exercise(runtime)}
    finally:
        runtime.close()
    print(json.dumps({"ok": True, "checks": results}, sort_keys=True))


if __name__ == "__main__":
    main()
