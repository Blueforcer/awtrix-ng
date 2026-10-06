"""Shared lifecycle and HTTP helpers for tests of the real Linux runtime."""
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import urllib.error
import urllib.request


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def eventually(operation, predicate=bool, timeout=8):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = operation()
            if predicate(last):
                return last
        except (OSError, http.client.HTTPException) as error:
            last = error
        time.sleep(0.04)
    raise AssertionError(f"condition not met in {timeout}s; last result: {last!r}")


def wait_ready(process, probe, logs=lambda: "", timeout=12):
    """Poll readiness; the timeout is a failure bound, never an unconditional wait."""
    def ready():
        if process.poll() is not None:
            raise AssertionError(f"runtime exited {process.returncode}: {logs()}")
        return probe()
    return eventually(ready, timeout=timeout)


def stop_process(process, logs=lambda: "", timeout=12, require_success=True):
    """Reap a runtime even after a failed probe, reporting abnormal termination."""
    if process is None:
        return
    terminated = process.poll() is None
    timed_out = False
    if terminated:
        process.terminate()
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
            timed_out = True
    if timed_out:
        raise AssertionError(f"runtime did not stop after SIGTERM: {logs()}")
    if require_success and process.returncode != 0 and not (os.name == "nt" and terminated):
        raise AssertionError(f"runtime exited {process.returncode}: {logs()}")


class Runtime:
    def __init__(self, root, binary, webui):
        self.binary, self.webui = str(binary), str(webui)
        self.root = Path(root)
        self.data = self.root / "data"
        self.data.mkdir(parents=True, exist_ok=True)
        self.port = free_port()
        self.width = 52
        self.height = 16
        self.process = None
        self.output = None

    def command(self, port=None):
        return [self.binary, "--data", str(self.data), "--port", str(port or self.port),
                "--webui", self.webui, "--width", str(self.width), "--height", str(self.height)]

    def start(self, environment=None):
        self.output = open(self.root / "runtime.log", "ab")
        self.process = subprocess.Popen(self.command(), stdout=self.output, stderr=self.output,
                                        env={**os.environ, **(environment or {})})
        wait_ready(self.process, lambda: self.request("GET", "/api/v1/version")[0] == 200, self.logs)
        return self

    def logs(self):
        return (self.root / "runtime.log").read_text(errors="replace")[-8000:]

    def stop(self):
        try:
            stop_process(self.process, self.logs)
        finally:
            self.process = None
            if self.output:
                self.output.close()
                self.output = None

    def request(self, method, path, body=None, content_type="application/json", headers=None):
        if body is None:
            data = None
        elif isinstance(body, bytes):
            data = body
        elif isinstance(body, str):
            data = body.encode()
        else:
            data = json.dumps(body).encode()
        request = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=data,
            method=method, headers={"Content-Type": content_type, **(headers or {})})
        try:
            response = urllib.request.urlopen(request, timeout=4)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read()
            try:
                result = json.loads(raw)
            except (ValueError, UnicodeDecodeError):
                result = raw.decode(errors="replace")
            return response.status, result

    def json(self, method, path, body=None):
        status, value = self.request(method, path, body)
        if status != 200:
            raise AssertionError(f"{method} {path}: {status}: {value!r}")
        return value

    def install(self, name, source):
        status, value = self.request("PUT", "/api/v1/apps/script/" + name, source, "text/plain")
        if status != 200:
            raise AssertionError(f"script {name}: {status}: {value!r}")

    def screen(self):
        return self.json("GET", "/api/v1/display/screen")
