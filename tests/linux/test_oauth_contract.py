#!/usr/bin/env python3
"""Script sign-in against a running awtrix-linux: the client secret never leaves its private store.

The secret must not come back through the sign-in routes, the script's config or data, raw file
downloads, file listings, the backup secrets, or any file in the data directory. Changes are
accepted only from the device's own page, and a changed @oauth line signs out.
"""

from __future__ import annotations
import sys

import argparse
import http.client
import json
import os
from pathlib import Path
import socket
import stat
import subprocess
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import free_port

SECRET = "TOPSECRET-123"
HEADER = ("# @oauth authorize=https://accounts.spotify.com/authorize "
          "token=https://accounts.spotify.com/api/token api=api.spotify.com "
          "scope=\"user-read-currently-playing\" pkce\n")
# setup() asks before the clock has saved the source; the listing must still follow the saved line.
BODY = "import oauth\nclass App\ndef setup() oauth.ready() end\ndef draw() end\nend\nreturn App()\n"


def wait_listening(port, process, deadline):
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"runtime exited with {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("runtime did not start listening")


class Device:
    def __init__(self, port):
        self.port = port
        self.host = f"127.0.0.1:{port}"

    def request(self, method, path, body=None, headers=None, content_type="application/json"):
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        data = body.encode() if isinstance(body, str) else body
        all_headers = {"Host": self.host}
        if data is not None:
            all_headers["Content-Type"] = content_type
        all_headers.update(headers or {})
        connection.request(method, path, body=data, headers=all_headers)
        response = connection.getresponse()
        payload = response.read().decode("utf-8", "replace")
        result = (response.status, payload, {k.lower(): v for k, v in response.getheaders()})
        connection.close()
        return result

    def own(self):
        return {"Origin": "http://" + self.host, "X-Awtrix-OAuth": "1"}

    def install(self, name, source):
        status, body, _ = self.request("PUT", "/api/v1/apps/script/" + name, source, content_type="text/plain")
        assert status == 200, (status, body)


def check(condition, what):
    if not condition:
        raise AssertionError(what)


def run(device, root, data):
    status, caps, _ = device.request("GET", "/api/v1/capabilities")
    check(status == 200 and json.loads(caps).get("oauth") is True, "capabilities report oauth")

    device.install("Spot", HEADER + BODY)
    status, listing, _ = device.request("GET", "/api/v1/oauth")
    apps = json.loads(listing)["apps"]
    check(status == 200 and [a["name"] for a in apps] == ["Spot"] and apps[0]["state"] == "signedOut",
          f"listing: {listing}")

    creds = json.dumps({"clientId": "cid", "clientSecret": SECRET})
    check(device.request("POST", "/api/v1/oauth/Spot", creds)[0] == 403, "no marker header: refused")
    check(device.request("POST", "/api/v1/oauth/Spot", creds,
                         {"Origin": "https://evil.example", "X-Awtrix-OAuth": "1"})[0] == 403,
          "foreign origin: refused")
    status, body, _ = device.request("POST", "/api/v1/oauth/Spot", creds, device.own())
    check(status == 200, f"own page saves: {status} {body}")

    _, _, preflight = device.request("OPTIONS", "/api/v1/oauth/Spot", None, {
        "Origin": "https://evil.example", "Access-Control-Request-Method": "POST",
        "Access-Control-Request-Headers": "x-awtrix-oauth"})
    check("x-awtrix-oauth" not in preflight.get("access-control-allow-headers", "").lower(),
          "the preflight does not allow the marker header")

    status, single, _ = device.request("GET", "/api/v1/oauth/Spot")
    check(status == 200 and json.loads(single)["clientSecretSet"] is True, "secret reported as set")

    for path in ("/api/v1/oauth", "/api/v1/oauth/Spot", "/api/v1/apps/Spot/config", "/api/v1/apps/Spot/data",
                 "/SCRIPTS/Spot.store.json", "/api/v1/files?dir=/", "/api/v1/files?dir=/SCRIPTS",
                 "/api/v1/system?secrets=1", "/api/v1/apps/script/Spot"):
        _, body, _ = device.request("GET", path)
        check(SECRET not in body, f"secret leaked through {path}")

    for file in data.rglob("*"):
        if file.is_file():
            check(SECRET.encode() not in file.read_bytes(), f"secret in data file {file}")
    private = root / "script-private"
    holders = [f for f in private.iterdir() if SECRET.encode() in f.read_bytes()]
    check(len(holders) == 1, "exactly one private record holds the secret")
    check(stat.S_IMODE(holders[0].stat().st_mode) == 0o600, "record is 0600")
    check(stat.S_IMODE(private.stat().st_mode) == 0o700, "directory is 0700")

    status, started, _ = device.request("POST", "/api/v1/oauth/Spot/start", "{}", device.own())
    url = json.loads(started).get("url", "")
    check(status == 200 and url.startswith("https://accounts.spotify.com/authorize?") and
          "redirect_uri=https%3A%2F%2Fawtrix.de%2Foauth%2Fcallback" in url and
          "code_challenge_method=S256" in url, f"authorize url: {started}")

    device.install("Spot", HEADER + "# changed code below the line\n" + BODY)
    _, single, _ = device.request("GET", "/api/v1/oauth/Spot")
    check(json.loads(single)["clientSecretSet"] is True, "a code change keeps the sign-in")

    device.install("Spot", HEADER.replace("api=api.spotify.com", "api=api.spotify.com,evil.example") + BODY)
    _, single, _ = device.request("GET", "/api/v1/oauth/Spot")
    check(json.loads(single)["clientSecretSet"] is False, "a changed @oauth line signs out")
    check(not any(SECRET.encode() in f.read_bytes() for f in private.iterdir()), "record erased")

    status, body, _ = device.request("POST", "/api/v1/oauth/Spot", creds, device.own())
    check(status == 200, "saved again")
    status, body, _ = device.request("DELETE", "/api/v1/apps/Spot")
    check(status == 200, f"delete: {status} {body}")
    check(device.request("GET", "/api/v1/oauth/Spot")[0] == 404, "a deleted script has no sign-in")
    check(not any(SECRET.encode() in f.read_bytes() for f in private.iterdir()), "deleted script's record erased")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="awtrix-oauth-") as temp:
        root = Path(temp)
        data = root / "data"
        data.mkdir(mode=0o700)
        port = free_port()
        process = subprocess.Popen([args.binary, "--data", str(data), "--webui", args.webui, "--port", str(port)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_listening(port, process, time.monotonic() + 30)
            run(Device(port), root, data)
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
    print("ok")


if __name__ == "__main__":
    main()
