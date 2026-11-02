#!/usr/bin/env python3
"""--ca-file: every script HTTPS request verifies chain and host name against one store read at
startup, and an unreadable bundle fails closed without stopping the runtime."""

from __future__ import annotations
import sys

import argparse
import http.client
import http.server
import json
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import free_port, wait_ready, stop_process, eventually

OPTIONS = None


def openssl(*arguments):
    subprocess.run(["openssl", *arguments], check=True, capture_output=True, timeout=30)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.received.append((self.command, self.path, b""))
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/final")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        body = b"trusted-ok"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        self.server.received.append((self.command, self.path, body))
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, *_):
        pass


class SharedTrust(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="awtrix-ca-file-")
        cls.root = Path(cls.workspace.name)
        certificates = {}
        for name, names in (("trusted", "DNS:localhost,IP:127.0.0.1"), ("elsewhere", "DNS:example.invalid"),
                            ("unrelated", "DNS:localhost,IP:127.0.0.1")):
            key, cert = cls.root / f"{name}.key", cls.root / f"{name}.pem"
            openssl("req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-days", "1",
                    "-subj", f"/CN={name}", "-addext", f"subjectAltName={names}", "-keyout", str(key),
                    "-out", str(cert))
            certificates[name] = (key, cert)
        cls.bundle = cls.root / "bundle.pem"
        cls.bundle.write_bytes(certificates["trusted"][1].read_bytes() + certificates["elsewhere"][1].read_bytes())
        cls.unrelated = certificates["unrelated"][1]
        cls.servers = []
        for name in ("trusted", "elsewhere"):
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            server.received = []
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(certificates[name][1], certificates[name][0])
            server.socket = context.wrap_socket(server.socket, server_side=True)
            threading.Thread(target=server.serve_forever, daemon=True).start()
            cls.servers.append(server)

    @classmethod
    def tearDownClass(cls):
        for server in cls.servers:
            server.shutdown()
            server.server_close()
        cls.workspace.cleanup()

    def request(self, port, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, response.read()
        finally:
            connection.close()

    def fetch(self, ca_file):
        folder = Path(tempfile.mkdtemp(dir=self.workspace.name))
        port = free_port()
        log = open(folder / "runtime.log", "wb")
        self.addCleanup(log.close)
        process = subprocess.Popen([OPTIONS.binary, "--data", str(folder / "data"), "--port", str(port),
                                    "--webui", OPTIONS.webui, "--ca-file", str(ca_file)], stdout=log, stderr=log)

        logs = lambda: (folder / "runtime.log").read_text(errors="replace")
        self.addCleanup(lambda: stop_process(process, logs))
        wait_ready(process, lambda: self.request(port, "GET", "/api/v1/version")[0] == 200, logs)
        trusted, elsewhere = (server.server_port for server in self.servers)
        source = f'''# @headless true
class Fetch
 var sent
 def init() self.sent = false end
 def loop()
  if self.sent return end
  self.sent = true
  http.get("https://127.0.0.1:{trusted}/", /body,status -> shared.set("ip", status))
  http.get("https://localhost:{trusted}/", /body,status -> shared.set("name", status))
  http.get("https://127.0.0.1:{elsewhere}/", /body,status -> shared.set("mismatch", status))
 end
end
return Fetch()
'''
        status, body = self.request(port, "PUT", "/api/v1/apps/script/cafetch", source.encode(),
                                    {"Content-Type": "text/plain"})
        self.assertEqual(200, status, body)
        def shared_values():
            status, body = self.request(port, "GET", "/api/v1/scripts/shared")
            self.assertEqual(200, status, body)
            return {row["key"]: row["value"] for row in json.loads(body) if row["owner"] == "cafetch"}
        values = eventually(shared_values, lambda values: len(values) == 3, timeout=20)
        return values

    def test_bundle_verifies_chain_and_host(self):
        self.assertEqual({"ip": 200, "name": 200, "mismatch": 0}, self.fetch(self.bundle))

    def test_other_roots_fail_closed(self):
        self.assertEqual({"ip": 0, "name": 0, "mismatch": 0}, self.fetch(self.unrelated))

    def test_unreadable_bundle_fails_closed_but_the_runtime_runs(self):
        self.assertEqual({"ip": 0, "name": 0, "mismatch": 0}, self.fetch(self.root / "missing.pem"))

    def test_shared_host_trust_for_scripts(self):
        folder = Path(tempfile.mkdtemp(dir=self.workspace.name))
        port = free_port()
        log = open(folder / "runtime.log", "wb")
        self.addCleanup(log.close)
        process = None
        ca_file = self.unrelated
        def start():
            nonlocal process
            process = subprocess.Popen([OPTIONS.binary, "--data", str(folder / "data"), "--port", str(port),
                "--webui", OPTIONS.webui, "--lan", "--ca-file", str(ca_file)], stdout=log, stderr=log)
            wait_ready(process, lambda: self.request(port, "GET", "/api/v1/version")[0] == 200,
                       lambda: (folder / "runtime.log").read_text(errors="replace"))
        def stop():
            nonlocal process
            if process:
                stop_process(process, lambda: (folder / "runtime.log").read_text(errors="replace"))
                process = None
        self.addCleanup(stop)
        start()
        def api(method, path="", value=None, expected=200, origin=None):
            headers = {"Origin": origin or f"http://127.0.0.1:{port}", "X-Awtrix-Trust": "1",
                       "Content-Type": "application/json"}
            status, body = self.request(port, method, "/api/v1/tls/trust" + path,
                                        json.dumps(value).encode() if value is not None else None, headers)
            self.assertEqual(expected, status, body)
            return json.loads(body)
        endpoint = self.servers[0]
        endpoint.received.clear()
        origin = f"https://localhost:{endpoint.server_port}"
        count = 0
        def fetch(url, method="GET", options="{}"):
            nonlocal count
            count += 1
            owner = f"trust{count}"
            call = f'http.get({json.dumps(url)}, /body,status -> shared.set("status", status), {options})'
            if method == "POST":
                call = f'http.post({json.dumps(url)}, "secret-payload", /body,status -> shared.set("status", status), {options})'
            source = f"# @headless true\nclass Fetch\n var sent\n def init() self.sent=false end\n def loop()\n if self.sent return end\n self.sent=true\n {call}\n end\nend\nreturn Fetch()\n"
            status, body = self.request(port, "PUT", "/api/v1/apps/script/" + owner, source.encode(),
                                        {"Content-Type": "text/plain"})
            self.assertEqual(200, status, body)
            def value():
                _, shared = self.request(port, "GET", "/api/v1/scripts/shared")
                return next((r["value"] for r in json.loads(shared) if r["owner"] == owner and r["key"] == "status"), None)
            return eventually(value, lambda v: v is not None, timeout=20)
        options = json.dumps({"pin": "a" * 64, "ca": self.bundle.read_text()})
        self.assertEqual(0, fetch(origin + "/untrusted", "POST", options))
        self.assertEqual([], endpoint.received, "no payload is sent to an untrusted certificate")
        pending = api("GET")["hosts"][0]["pending"]
        api("POST", "/localhost/pin", {"fingerprint": "a" * 64}, 409)
        api("POST", "/localhost/pin", {"fingerprint": pending}, 403, "http://other.invalid")
        api("POST", "/localhost/pin", {"fingerprint": pending})
        self.assertEqual(200, fetch(origin + "/approved", "POST"))
        self.assertEqual(b"secret-payload", endpoint.received[-1][2])
        self.assertEqual(302, fetch(origin + "/redirect"))
        self.assertFalse(any(path == "/final" for _, path, _ in endpoint.received))
        self.assertEqual(0, fetch(f"https://127.0.0.1:{endpoint.server_port}/other-host", "POST"))
        self.assertNotIn("BEGIN CERTIFICATE", json.dumps(api("GET")))
        api("PUT", "/localhost/ca", {"certificate": self.bundle.read_text()})
        api("DELETE", "/localhost/pin")
        self.assertEqual(200, fetch(origin + "/ca"))
        stored = folder / "tls-private" / "trust.json"
        self.assertEqual(0o600, stored.stat().st_mode & 0o777)
        stop(); start()
        self.assertEqual(200, fetch(origin + "/after-restart"))
        api("DELETE")
        self.assertEqual(0, fetch(origin + "/revoked", "POST"))
        stop()
        stored.write_text("corrupt")
        ca_file = self.bundle
        start()
        self.assertFalse(api("GET")["usable"])
        self.assertEqual(0, fetch(origin + "/corrupt", "POST"))
        api("DELETE")
        self.assertEqual(200, fetch(origin + "/redirect"))
        self.assertTrue(any(path == "/final" for _, path, _ in endpoint.received))
        status, _ = self.request(port, "GET", "/api/v1/mqtt/tls")
        self.assertEqual(404, status, "retired MQTT certificate API")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.binary = str(Path(OPTIONS.binary).resolve())
    OPTIONS.webui = str(Path(OPTIONS.webui).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
