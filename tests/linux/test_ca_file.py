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
        body = b"trusted-ok"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.binary = str(Path(OPTIONS.binary).resolve())
    OPTIONS.webui = str(Path(OPTIONS.webui).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
