"""Real-process HTTPS/authentication contracts for awtrix-linux, including the --lan device login.

Requires Python's standard library, openssl, mosquitto, Linux and a non-root user. Without
openssl or mosquitto the run exits 77, which CTest reports as skipped; --require-tools turns that
into a failure. All credentials, certificates and application data are generated in private
temporary directories; no Internet connection or TC002 hardware is used.
"""
from __future__ import annotations
import sys

import argparse
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import unittest

from test_contract import MqttClient, eventually, free_port

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import stop_process, wait_ready


OPTIONS = None


class SecuredRuntime:
    def __init__(self, root, certificate, key):
        self.root = Path(root)
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.data = self.root / "data"
        self.data.mkdir(mode=0o700)
        self.token = secrets.token_hex(32)
        self.credentials = self.root / "admin.token"
        self.credentials.write_text(self.token + "\n")
        self.credentials.chmod(0o600)
        self.certificate = self.root / "tls.crt"
        self.key = self.root / "tls.key"
        shutil.copyfile(certificate, self.certificate)
        shutil.copyfile(key, self.key)
        self.certificate.chmod(0o644)
        self.key.chmod(0o600)
        self.port = free_port()
        self.process = None
        self.output = None
        self.extra_options = {}
        self.context = ssl.create_default_context(cafile=str(self.certificate))

    @property
    def origin(self):
        return f"https://localhost:{self.port}"

    def command(self, overrides=None, hardened=True):
        values = {"--data": str(self.data), "--webui": OPTIONS.webui, "--port": str(self.port),
                  "--credentials-file": str(self.credentials), "--tls-cert": str(self.certificate),
                  "--tls-key": str(self.key), "--origin": self.origin}
        values.update(self.extra_options)
        values.update(overrides or {})
        command = [OPTIONS.binary] + (["--hardened"] if hardened else [])
        for key, value in values.items():
            if value is not None:
                command.extend([key, str(value)])
        return command

    def logs(self):
        path = self.root / "runtime.log"
        return path.read_text(errors="replace") if path.exists() else ""

    def start(self):
        self.output = open(self.root / "runtime.log", "ab")
        self.process = subprocess.Popen(self.command(), stdout=self.output, stderr=self.output)

        wait_ready(self.process,
                   lambda: self.request("GET", "/api/v1/version", auth=None)[0] == 401, self.logs)
        return self.request("GET", "/api/v1/version", auth=None)[0] == 401

        eventually(ready, timeout=12)
        return self

    def stop(self):
        try:
            stop_process(self.process, self.logs)
        finally:
            self.process = None
            if self.output:
                self.output.close()
                self.output = None

    def request(self, method, path, body=None, auth="bearer", headers=None, duplicates=(), context=None):
        request_headers = {"Host": f"localhost:{self.port}", "Connection": "close"}
        if auth == "bearer":
            request_headers["Authorization"] = "Bearer " + self.token
        elif auth == "basic":
            request_headers["Authorization"] = "Basic " + base64.b64encode(("admin:" + self.token).encode()).decode()
        elif auth is not None:
            request_headers["Authorization"] = auth
        if body is not None:
            body = json.dumps(body).encode() if isinstance(body, dict) else body.encode() if isinstance(body, str) else body
            request_headers["Content-Type"] = "application/json"
            request_headers["Content-Length"] = str(len(body))
        request_headers.update(headers or {})
        connection = http.client.HTTPSConnection("localhost", self.port, context=context or self.context, timeout=4)
        try:
            connection.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
            for key, value in [*request_headers.items(), *duplicates]:
                connection.putheader(key, value)
            connection.endheaders(body)
            response = connection.getresponse()
            content = response.read()
            return response.status, content, dict((k.lower(), v) for k, v in response.getheaders())
        finally:
            connection.close()

    def json(self, method, path, body=None):
        status, content, _ = self.request(method, path, body)
        if status != 200:
            raise AssertionError(f"{method} {path}: {status}: {content!r}")
        return json.loads(content)


class TlsMqttClient(MqttClient):
    def __init__(self, port, ca, username, password):
        context = ssl.create_default_context(cafile=str(ca))
        self.sock = context.wrap_socket(socket.create_connection(("localhost", port), timeout=3), server_hostname="localhost")
        flags = 2 | (0xC0 if username is not None else 0)
        packet = self.string("MQTT") + bytes([4, flags]) + struct.pack("!H", 30) + self.string("security-" + secrets.token_hex(4))
        if username is not None:
            packet += self.string(username) + self.string(password)
        self.send(0x10, packet)
        self.connack = self.receive()


class SecurityContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != "posix" or os.geteuid() == 0:
            raise AssertionError("security contracts require a non-root Linux user")
        for executable in ("openssl", "mosquitto", "mosquitto_passwd"):
            if not shutil.which(executable):
                raise AssertionError(f"{executable} is required; security tests must not be skipped")
        cls.provisioning = tempfile.TemporaryDirectory(prefix="awtrix-security-ca-")
        cls.addClassCleanup(cls.provisioning.cleanup)
        cls.cert = Path(cls.provisioning.name) / "tls.crt"
        cls.key = Path(cls.provisioning.name) / "tls.key"
        result = subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
            "-keyout", str(cls.key), "-out", str(cls.cert)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        if result.returncode:
            raise AssertionError("cannot generate local TLS fixture")
        cls.mqtt_cert = Path(cls.provisioning.name) / "mqtt.crt"
        cls.mqtt_key = Path(cls.provisioning.name) / "mqtt.key"
        # DNS-only certificate: a successful localhost connection proves that
        # certificate verification retained the name through IPv4 resolution.
        result = subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
            "-keyout", str(cls.mqtt_key), "-out", str(cls.mqtt_cert)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        if result.returncode:
            raise AssertionError("cannot generate local MQTT TLS fixture")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-security-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app = SecuredRuntime(self.root, self.cert, self.key)
        self.addCleanup(self.app.stop)

    def test_all_routes_assets_methods_and_upgrade_requests_require_auth(self):
        self.app.start()
        for method, path, headers in [
            ("GET", "/", {}), ("HEAD", "/index.html", {}), ("GET", "/fullscreen", {}),
            ("GET", "/api/v1/version", {}), ("GET", "/api/v1/system?secrets=1", {}),
            ("GET", "/api/v1/logs", {"Accept": "text/event-stream"}),
            ("GET", "/events", {"Accept": "text/event-stream"}),
            ("GET", "/ws", {"Upgrade": "websocket", "Connection": "Upgrade", "Sec-WebSocket-Version": "13"}),
            ("GET", "/PALETTES/private.txt", {}), ("GET", "/device.json", {}),
            ("OPTIONS", "/api/v1/settings", {}), ("GET", "/unknown", {})]:
            with self.subTest(method=method, path=path):
                status, _, response_headers = self.app.request(method, path, auth=None, headers=headers)
                self.assertEqual(401, status)
                self.assertIn("Basic ", response_headers["www-authenticate"])
                self.assertEqual("no-store", response_headers["cache-control"])

    def test_browser_basic_and_bearer_authenticate_the_real_webui_and_api(self):
        (self.app.data / "PALETTES").mkdir()
        (self.app.data / "PALETTES/private.txt").write_text("private palette fixture")
        self.app.start()
        status, html, headers = self.app.request("GET", "/", auth="basic")
        self.assertEqual(200, status)
        self.assertIn(b"<html", html.lower())
        self.assertEqual("DENY", headers["x-frame-options"])
        self.assertEqual("nosniff", headers["x-content-type-options"])
        self.assertEqual("no-referrer", headers["referrer-policy"])
        self.assertIn("max-age=", headers["strict-transport-security"])
        self.assertNotIn("access-control-allow-origin", headers)
        self.assertEqual(200, self.app.request("GET", "/api/v1/version")[0])
        self.assertEqual(200, self.app.request("GET", "/PALETTES/private.txt", auth="basic")[0])
        self.assertEqual(401, self.app.request("GET", "/PALETTES/private.txt", auth=None)[0])

    def test_wrong_duplicate_query_and_cookie_credentials_are_rejected(self):
        self.app.start()
        for auth in ("Bearer invalid", "Basic YWRtaW46YmFk", "bearer " + self.app.token, "Bearer " + self.app.token + "extra"):
            with self.subTest(kind=auth.split(" ", 1)[0]):
                self.assertEqual(401, self.app.request("GET", "/api/v1/version", auth=auth)[0])
        self.assertEqual(401, self.app.request("GET", "/api/v1/version",
            duplicates=[("Authorization", "Bearer " + self.app.token)])[0])
        self.assertEqual(401, self.app.request("GET", "/api/v1/version?token=" + self.app.token, auth=None)[0])
        self.assertEqual(401, self.app.request("GET", "/api/v1/version", auth=None,
            headers={"Cookie": "Authorization=Bearer " + self.app.token})[0])
        self.assertNotIn(self.app.token, self.app.logs())
        self.assertNotIn(base64.b64encode(("admin:" + self.app.token).encode()).decode(), self.app.logs())

    def test_origin_host_and_duplicate_header_guards_apply_after_auth(self):
        self.app.start()
        self.assertEqual(200, self.app.request("GET", "/api/v1/version", headers={"Origin": self.app.origin})[0])
        cases = [{"Host": "evil.invalid"}, {"Host": f"127.0.0.1:{self.app.port}"},
                 {"Origin": "https://evil.invalid"}, {"Origin": "null"},
                 {"Origin": self.app.origin.replace("https:", "http:")}, {"Sec-Fetch-Site": "cross-site"},
                 {"Host": "evil.invalid", "X-Forwarded-Host": f"localhost:{self.app.port}"}]
        for headers in cases:
            with self.subTest(headers=headers):
                self.assertEqual(403, self.app.request("POST", "/api/v1/device/reboot", {}, headers=headers)[0])
        for key, value in (("Host", f"localhost:{self.app.port}"), ("Origin", self.app.origin), ("Sec-Fetch-Site", "same-origin")):
            with self.subTest(duplicate=key):
                self.assertEqual(403, self.app.request("GET", "/api/v1/version", headers={key: value}, duplicates=[(key, value)])[0])

    def test_mutations_and_body_reads_are_behind_authentication(self):
        self.app.start()
        original = self.app.json("GET", "/api/v1/settings")["brightness"]
        self.assertEqual(401, self.app.request("PATCH", "/api/v1/settings", {"brightness": 33}, auth=None)[0])
        self.assertEqual(original, self.app.json("GET", "/api/v1/settings")["brightness"])
        self.app.json("PATCH", "/api/v1/settings", {"brightness": 33})
        self.assertEqual(33, self.app.json("GET", "/api/v1/settings")["brightness"])
        connection = http.client.HTTPSConnection("localhost", self.app.port, context=self.app.context, timeout=2)
        try:
            connection.putrequest("POST", "/api/v1/restore")
            connection.putheader("Content-Length", "4096")
            connection.endheaders()  # No body: authentication must reject before waiting for it.
            self.assertEqual(401, connection.getresponse().status)
        finally:
            connection.close()

    def test_tls_trust_protocol_and_plain_http_fail_closed(self):
        self.app.start()
        with self.assertRaises(ssl.SSLCertVerificationError):
            self.app.request("GET", "/api/v1/version", context=ssl.create_default_context())
        tls12 = ssl.create_default_context(cafile=str(self.app.certificate))
        tls12.minimum_version = tls12.maximum_version = ssl.TLSVersion.TLSv1_2
        self.assertEqual(200, self.app.request("GET", "/api/v1/version", context=tls12)[0])
        old = subprocess.run(["openssl", "s_client", "-connect", f"localhost:{self.app.port}", "-servername", "localhost",
            "-tls1_1", "-cipher", "DEFAULT:@SECLEVEL=0"], input=b"", stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        self.assertNotEqual(0, old.returncode)
        self.assertIn(b"alert protocol version", old.stderr.lower())
        plain = http.client.HTTPConnection("localhost", self.app.port, timeout=2)
        try:
            with self.assertRaises((OSError, http.client.HTTPException)):
                plain.request("GET", "/api/v1/version")
                plain.getresponse()
        finally:
            plain.close()

    def assert_refused(self, overrides=None, hardened=True):
        result = subprocess.run(self.app.command(overrides, hardened=hardened), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=5)
        self.assertNotEqual(0, result.returncode)
        self.assertIn(b"Administrative security:", result.stderr)
        self.assertNotIn(self.app.token.encode(), result.stdout + result.stderr)
        with self.assertRaises(OSError):
            socket.create_connection(("localhost", self.app.port), timeout=0.1)

    def test_missing_invalid_or_downgraded_provisioning_never_starts_listener(self):
        for overrides in ({"--credentials-file": None}, {"--tls-cert": None}, {"--tls-key": None}, {"--origin": None},
                          {"--credentials-file": self.root / "missing"}, {"--origin": f"http://localhost:{self.app.port}"},
                          {"--origin": "https://localhost:1"}, {"--origin": f"https://other.invalid:{self.app.port}"},
                          {"--listen": "localhost"}):
            with self.subTest(overrides=overrides):
                self.assert_refused(overrides)
        self.assert_refused(hardened=False)
        self.app.credentials.write_text("password\n")
        self.assert_refused()

    def test_secret_permissions_symlinks_and_data_directory_are_rejected(self):
        for path in (self.app.credentials, self.app.key):
            with self.subTest(file=path.name):
                path.chmod(0o644)
                self.assert_refused()
                path.chmod(0o600)
        symlink = self.root / "credentials-link"
        symlink.symlink_to(self.app.credentials)
        self.assert_refused({"--credentials-file": symlink})
        inside_data = self.app.data / "credentials"
        inside_data.write_text(self.app.token)
        inside_data.chmod(0o600)
        self.assert_refused({"--credentials-file": inside_data})
        self.root.chmod(0o777)
        try:
            self.assert_refused()
        finally:
            self.root.chmod(0o700)

    def test_invalid_and_mismatched_tls_keys_are_rejected_without_http_fallback(self):
        self.app.key.write_text("not a PEM private key")
        self.assert_refused()
        generated = subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
            "-out", str(self.app.key)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        self.assertEqual(0, generated.returncode)
        self.assert_refused()
        shutil.copyfile(self.key, self.app.key)
        self.app.certificate.write_text("not a PEM certificate")
        self.assert_refused()

    def test_reset_preserves_external_security_material_and_authentication(self):
        credentials = self.app.credentials.read_bytes()
        key = self.app.key.read_bytes()
        self.app.start()
        self.app.json("PATCH", "/api/v1/settings", {"brightness": 33})
        self.app.json("POST", "/api/v1/device/factory-reset", {})
        self.app.process.wait(timeout=5)
        self.app.stop()
        self.assertEqual(credentials, self.app.credentials.read_bytes())
        self.assertEqual(key, self.app.key.read_bytes())
        self.assertEqual({".lock"}, {p.name for p in self.app.data.iterdir()})
        self.app.start()
        self.assertEqual(401, self.app.request("GET", "/api/v1/settings", auth=None)[0])
        self.assertEqual(120, self.app.json("GET", "/api/v1/settings")["brightness"])

    def test_hardened_mode_never_connects_to_unprotected_mqtt(self):
        with socket.socket() as broker:
            broker.bind(("127.0.0.1", 0))
            broker.listen()
            port = broker.getsockname()[1]
            (self.app.data / "device.json").write_text(json.dumps({"mqttEnabled": True, "mqttHost": "127.0.0.1",
                "mqttPort": port, "mqttPrefix": "security-contract"}))
            self.app.start()
            self.app.json("PUT", "/api/v1/system", {"mqttEnabled": True, "mqttHost": "127.0.0.1", "mqttPort": port})
            broker.settimeout(1.5)
            with self.assertRaises(socket.timeout):
                broker.accept()

    def broker(self):
        port, self.broker_password = start_broker(self, self.root, self.mqtt_cert, self.mqtt_key)
        return port

    def configure_mqtt(self, port, host="localhost", password=None, ca=None):
        (self.app.data / "device.json").write_text(json.dumps({"mqttEnabled": True, "mqttHost": host,
            "mqttPort": port, "mqttPrefix": "contract", "mqttUser": "device",
            "mqttPass": password if password is not None else self.broker_password}))
        self.app.extra_options["--mqtt-ca-file"] = str(ca or self.mqtt_cert)

    def mqtt_state(self):
        return self.app.json("GET", "/api/v1/device")["mqtt"]

    def test_mqtt_tls_auth_uses_shared_notify_screen_and_broker_acl_contract(self):
        port = self.broker()
        operator = TlsMqttClient(port, self.mqtt_cert, "operator", self.broker_password)
        self.addCleanup(operator.close)
        self.assertEqual((0x20, b"\x00\x00"), operator.connack)
        operator.subscribe("contract/#")
        self.configure_mqtt(port)
        self.app.start()
        self.assertEqual("online", operator.wait_topic("contract/availability"))
        self.assertEqual("connected", self.mqtt_state()["state"])
        operator.publish("contract/cmd/notify", json.dumps({"backgroundColor": "#13579B", "hold": True}))
        self.assertEqual({"ok": True}, json.loads(operator.wait_topic("contract/cmd/notify/result")))
        operator.publish("contract/cmd/screen/get", "")
        screen = json.loads(operator.wait_topic("contract/state/screen"))
        self.assertEqual((52, 16, 832, 0x13579B), (screen["width"], screen["height"], len(screen["pixels"]), screen["pixels"][-1]))
        anonymous = TlsMqttClient(port, self.mqtt_cert, None, None)
        self.addCleanup(anonymous.close)
        self.assertEqual((0x20, b"\x00\x05"), anonymous.connack)
        outsider = TlsMqttClient(port, self.mqtt_cert, "outsider", self.broker_password)
        self.addCleanup(outsider.close)
        outsider.publish("contract/cmd/notify", json.dumps({"backgroundColor": "#FFFFFF", "hold": True}))
        time.sleep(0.15)
        self.assertEqual(0x13579B, self.app.json("GET", "/api/v1/display/screen")["pixels"][-1])
        self.assertNotIn(self.broker_password, self.app.logs())
        # Deleting the trust file after startup cannot disable the live policy.
        # Its contents were loaded once; there is no API switch to plaintext.
        self.assertEqual(401, self.app.request("GET", "/api/v1/system?secrets=1", auth=None)[0])

    def test_mqtt_rejects_untrusted_ca_wrong_hostname_and_wrong_credentials(self):
        port = self.broker()
        for name, kwargs in (("wrong_ca", {"ca": self.cert}), ("wrong_hostname", {"host": "127.0.0.1"}),
                             ("wrong_password", {"password": "invalid-fixture-password"})):
            with self.subTest(name=name):
                self.configure_mqtt(port, **kwargs)
                self.app.start()
                state = eventually(self.mqtt_state, lambda value: value["attempts"] >= 1)
                self.assertEqual("offline", state["state"])
                self.assertEqual(0, state["connects"])
                if name == "wrong_password":
                    self.assertEqual("badCredentials", state["error"])
                self.assertEqual(200, self.app.request("GET", "/api/v1/version")[0])
                self.app.stop()

    def test_mqtt_ca_and_credentials_are_required_for_explicit_tls_enablement(self):
        port = free_port()
        self.broker_password = ""
        self.configure_mqtt(port)
        result = subprocess.run(self.app.command(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        self.assertNotEqual(0, result.returncode)
        self.assertIn(b"MQTT security:", result.stderr)
        self.configure_mqtt(port, password="nonempty")
        malformed = self.root / "bad-ca.pem"
        malformed.write_text("not a PEM certificate")
        for ca in (malformed, self.app.data / "ca.pem"):
            if ca.parent == self.app.data:
                shutil.copyfile(self.mqtt_cert, ca)
            with self.subTest(ca=ca.name):
                result = subprocess.run(self.app.command({"--mqtt-ca-file": ca}), stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, timeout=5)
                self.assertNotEqual(0, result.returncode)
                self.assertIn(b"MQTT security:", result.stderr)

    def test_slow_tls_handshake_does_not_block_http_or_shutdown(self):
        with socket.socket() as peer:
            peer.bind(("127.0.0.1", 0))
            peer.listen()
            peer.settimeout(4)
            self.broker_password = "local-fixture"
            self.configure_mqtt(peer.getsockname()[1])
            self.app.start()
            connection, _ = peer.accept()
            self.addCleanup(connection.close)
            for _ in range(12):
                started = time.monotonic()
                self.assertEqual(200, self.app.request("GET", "/api/v1/display/screen")[0])
                self.assertLess(time.monotonic() - started, 0.75, "TLS handshake blocked the application loop")
                time.sleep(0.04)
            started = time.monotonic()
            self.app.stop()
            self.assertLess(time.monotonic() - started, 3)

    def test_partial_mqtt_packet_does_not_block_render_loop_and_has_deadline(self):
        # Fault injection over a real TLS socket; positive commands use Mosquitto
        # above. This peer intentionally sends an incomplete MQTT publish packet.
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(self.mqtt_cert, self.mqtt_key)
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(5)
        release, send_partial = threading.Event(), threading.Event()
        failures = []

        def serve():
            try:
                raw, _ = listener.accept()
                with context.wrap_socket(raw, server_side=True) as stream:
                    stream.settimeout(4)
                    first = stream.recv(8192)
                    if not first or first[0] != 0x10:
                        raise AssertionError("expected MQTT CONNECT")
                    stream.sendall(b"\x20\x02\x00\x00")
                    if not send_partial.wait(timeout=5):
                        raise AssertionError("runtime did not connect")
                    stream.sendall(b"\x30\x30\x00\x0acontract/x")
                    release.wait(timeout=6)
            except Exception as error:
                failures.append(error)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()

        def cleanup():
            release.set()
            listener.close()
            worker.join(timeout=6)
            self.assertFalse(worker.is_alive())
            self.assertEqual([], failures)
        self.addCleanup(cleanup)
        self.broker_password = "local-fixture"
        self.configure_mqtt(listener.getsockname()[1])
        self.app.start()
        eventually(self.mqtt_state, lambda value: value["state"] == "connected")
        send_partial.set()
        for _ in range(15):
            started = time.monotonic()
            self.app.json("GET", "/api/v1/display/screen")
            self.assertLess(time.monotonic() - started, 0.75, "partial MQTT packet blocked the application loop")
            time.sleep(0.05)
        eventually(self.mqtt_state, lambda value: value["state"] != "connected", timeout=4)


def start_broker(test, root, cert, key):
    """A TLS mosquitto on 127.0.0.1 with the users device, operator and outsider; (port, password)."""
    port = free_port()
    password = secrets.token_hex(24)
    password_file = root / "broker.passwords"
    password_file.write_text("device:" + password + "\noperator:" + password + "\noutsider:" + password + "\n")
    password_file.chmod(0o600)
    converted = subprocess.run(["mosquitto_passwd", "-U", str(password_file)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
    test.assertEqual(0, converted.returncode, "cannot hash local broker fixture credentials")
    acl = root / "broker.acl"
    acl.write_text("user device\ntopic readwrite contract/#\nuser operator\ntopic readwrite contract/#\n"
                   "user outsider\ntopic readwrite outsider/#\n")
    config = root / "broker.conf"
    config.write_text(f"listener {port} 127.0.0.1\nallow_anonymous false\npassword_file {password_file}\n"
                      f"acl_file {acl}\ncertfile {cert}\nkeyfile {key}\n"
                      "persistence false\ntls_version tlsv1.2\n")
    log = open(root / "broker.log", "wb")
    test.addCleanup(log.close)
    process = subprocess.Popen(["mosquitto", "-c", str(config)], stdout=log, stderr=log)

    def cleanup():
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)
    test.addCleanup(cleanup)

    def ready():
        if process.poll() is not None:
            test.fail((root / "broker.log").read_text())
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            return True
    eventually(ready)
    return port, password


def certificate_fingerprint(pem):
    return hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()


def listening_addresses(port):
    """IPv4 addresses with a listening TCP socket on port, from /proc/net/tcp."""
    addresses = set()
    for line in Path("/proc/net/tcp").read_text().splitlines()[1:]:
        fields = line.split()
        address, local_port = fields[1].split(":")
        if int(local_port, 16) == port and fields[3] == "0A":
            addresses.add(socket.inet_ntoa(struct.pack("<I", int(address, 16))))
    return addresses


def outward_address():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        try:
            probe.connect(("192.0.2.1", 9))
            address = probe.getsockname()[0]
        except OSError:
            return None
    return None if address.startswith("127.") or address == "0.0.0.0" else address


class LanRuntime:
    def __init__(self, root):
        self.root = Path(root)
        self.data = self.root / "data"
        self.port = free_port()
        self.process = None
        self.output = None

    def command(self, *extra):
        return [OPTIONS.binary, "--data", str(self.data), "--webui", OPTIONS.webui, "--port", str(self.port),
                "--lan", *extra]

    def logs(self):
        path = self.root / "runtime.log"
        return path.read_text(errors="replace") if path.exists() else ""

    def start(self, *extra):
        self.output = open(self.root / "runtime.log", "ab")
        self.process = subprocess.Popen(self.command(*extra), stdout=self.output, stderr=self.output)

        def ready():
            if self.process.poll() is not None:
                raise AssertionError(f"LAN runtime exited {self.process.returncode}: {self.logs()}")
            return self.request("GET", "/api/v1/version")[0] in (200, 401)

        eventually(ready, timeout=12)
        return self

    def stop(self):
        try:
            stop_process(self.process, self.logs)
        finally:
            self.process = None
            if self.output:
                self.output.close()
                self.output = None

    def request(self, method, path, body=None, auth=None, headers=None, host="127.0.0.1"):
        request_headers = {"Host": f"awtrix.local:{self.port}", "Connection": "close"}
        if auth is not None:
            request_headers["Authorization"] = "Basic " + base64.b64encode(":".join(auth).encode()).decode()
        if body is not None:
            body = json.dumps(body).encode()
            request_headers["Content-Type"] = "application/json"
        request_headers.update(headers or {})
        connection = http.client.HTTPConnection(host, self.port, timeout=4)
        try:
            connection.request(method, path, body=body, headers=request_headers)
            response = connection.getresponse()
            return response.status, response.read(), dict((k.lower(), v) for k, v in response.getheaders())
        finally:
            connection.close()


class LanServiceContracts(unittest.TestCase):
    """--lan: the TC002 product service, reachable like AWTRIX NG on ESP32."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-lan-")
        self.addCleanup(self.temporary.cleanup)
        self.app = LanRuntime(self.temporary.name)
        self.addCleanup(self.app.stop)

    def refused(self, *arguments, port=None):
        command = [OPTIONS.binary, "--data", str(self.app.data), "--webui", OPTIONS.webui,
                   "--port", str(self.app.port if port is None else port), *arguments]
        return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)

    def test_binds_every_interface_and_serves_the_ui_for_any_host_name(self):
        self.app.start()
        self.assertEqual({"0.0.0.0"}, listening_addresses(self.app.port))
        status, html, headers = self.app.request("GET", "/")
        self.assertEqual(200, status)
        self.assertIn(b"<html", html.lower())
        self.assertEqual("*", headers["access-control-allow-origin"])
        status, _, _ = self.app.request("POST", "/api/v1/notifications", {"text": "lan"},
                                        headers={"Origin": "http://elsewhere.example"})
        self.assertEqual(200, status)
        outward = outward_address()
        if outward:
            self.assertEqual(200, self.app.request("GET", "/api/v1/version", host=outward)[0])
        self.assertIn(f"http://0.0.0.0:{self.app.port} (52x16) [LAN]", self.app.logs())
        status, body, _ = self.app.request("GET", "/api/v1/system/wifi-scan")
        self.assertEqual((200, []), (status, json.loads(body)), "no supervisor: nothing to scan with")

    def test_secrets_and_device_administration_answer_only_the_own_page(self):
        self.app.start()
        own = f"http://awtrix.local:{self.app.port}"
        for headers in ({"Origin": "http://evil.example"}, {"Sec-Fetch-Site": "cross-site"},
                        {"Origin": "null"}, {"Origin": own, "Sec-Fetch-Site": "same-site"}):
            with self.subTest(headers=headers):
                status, body, response = self.app.request("GET", "/api/v1/system?secrets=1", headers=headers)
                self.assertEqual(403, status)
                self.assertEqual("forbiddenOrigin", json.loads(body)["error"]["code"])
                self.assertNotIn(b"wifiPass", body)
                self.assertNotIn("access-control-allow-origin", response)
        foreign = {"Origin": "http://evil.example"}
        for method, path, body in (("PUT", "/api/v1/system", {"hostname": "evil"}),
                                   ("POST", "/api/v1/device/factory-reset", None)):
            with self.subTest(path=path):
                status, answer, _ = self.app.request(method, path, body, headers=foreign)
                self.assertEqual(403, status)
                self.assertEqual("forbiddenOrigin", json.loads(answer)["error"]["code"])
        self.assertNotEqual("evil", json.loads(self.app.request("GET", "/api/v1/system")[1]).get("hostname"))

        def restore(headers):
            body = (b'--B\r\nContent-Disposition: form-data; name="file"; filename="backup.zip"\r\n'
                    b"Content-Type: application/zip\r\n\r\nPK\r\n--B--\r\n")
            connection = http.client.HTTPConnection("127.0.0.1", self.app.port, timeout=4)
            try:
                connection.request("POST", "/api/v1/restore", body=body, headers={
                    "Host": f"awtrix.local:{self.app.port}", "Content-Type": "multipart/form-data; boundary=B",
                    **headers})
                return connection.getresponse().status
            finally:
                connection.close()

        self.assertEqual(403, restore(foreign))
        self.assertEqual(400, restore({"Origin": own}))

        for headers in ({"Origin": own, "Sec-Fetch-Site": "same-origin"}, {"Sec-Fetch-Site": "none"}, {}):
            with self.subTest(own=headers):
                status, body, response = self.app.request("GET", "/api/v1/system?secrets=1", headers=headers)
                self.assertEqual(200, status)
                self.assertIn("wifiPass", json.loads(body))
                self.assertNotIn("access-control-allow-origin", response)
        status, body, response = self.app.request("GET", "/api/v1/system", headers=foreign)
        self.assertEqual(200, status)
        self.assertNotIn("wifiPass", json.loads(body))
        self.assertEqual("*", response["access-control-allow-origin"])

    def test_a_preflight_names_one_allowed_origin(self):
        self.app.start()
        connection = http.client.HTTPConnection("127.0.0.1", self.app.port, timeout=4)
        try:
            connection.request("OPTIONS", "/api/v1/notifications", headers={
                "Host": f"awtrix.local:{self.app.port}", "Origin": "http://evil.example",
                "Access-Control-Request-Method": "POST"})
            response = connection.getresponse()
            response.read()
            self.assertEqual(204, response.status)
            self.assertEqual(["*"], [value for name, value in response.getheaders()
                                     if name.lower() == "access-control-allow-origin"])
        finally:
            connection.close()

    def test_listen_selects_one_ipv4_address(self):
        self.app.start("--listen", "127.0.0.1")
        self.assertEqual({"127.0.0.1"}, listening_addresses(self.app.port))
        self.assertEqual(200, self.app.request("GET", "/api/v1/version")[0])

    def test_login_matches_the_device(self):
        self.app.start()
        status, body, _ = self.app.request("PUT", "/api/v1/system", {"authEnabled": True})
        self.assertEqual(422, status)
        self.assertEqual("authUser", json.loads(body)["error"]["field"])
        status, _, _ = self.app.request("PUT", "/api/v1/system",
                                        {"authEnabled": True, "authUser": "admin", "authPass": "s3cret"})
        self.assertEqual(200, status)
        for path in ("/", "/api/v1/version", "/api/v1/system?secrets=1", "/api/v1/device", "/unknown"):
            with self.subTest(path=path):
                status, body, headers = self.app.request("GET", path)
                self.assertEqual(401, status)
                self.assertEqual('Basic realm="AWTRIX NG"', headers["www-authenticate"])
                self.assertEqual("unauthorized", json.loads(body)["error"]["code"])
                if "secrets" in path:
                    self.assertNotIn("access-control-allow-origin", headers)
                else:
                    self.assertEqual("*", headers["access-control-allow-origin"])
        for credentials in (("admin", "wrong"), ("other", "s3cret"), ("admin", "s3cret ")):
            with self.subTest(credentials=credentials):
                self.assertEqual(401, self.app.request("GET", "/api/v1/version", auth=credentials)[0])
        self.assertEqual(401, self.app.request("GET", "/api/v1/version", headers={"Authorization": "Bearer s3cret"})[0])
        status, _, headers = self.app.request("OPTIONS", "/api/v1/settings")
        self.assertEqual(204, status)
        self.assertIn("Authorization", headers["access-control-allow-headers"])
        status, html, _ = self.app.request("GET", "/", auth=("admin", "s3cret"))
        self.assertEqual(200, status)
        self.assertIn(b"<html", html.lower())
        self.assertEqual(200, self.app.request("PATCH", "/api/v1/settings", {"brightness": 40},
                                               auth=("admin", "s3cret"))[0])
        connection = http.client.HTTPConnection("127.0.0.1", self.app.port, timeout=2)
        try:
            connection.putrequest("POST", "/api/v1/restore")
            connection.putheader("Content-Length", "4096")
            connection.endheaders()
            self.assertEqual(401, connection.getresponse().status)
        finally:
            connection.close()

        self.app.stop()
        self.app.start()
        self.assertEqual(401, self.app.request("GET", "/api/v1/version")[0])
        self.assertEqual(200, self.app.request("PUT", "/api/v1/system", {"authEnabled": False},
                                               auth=("admin", "s3cret"))[0])
        self.assertEqual(200, self.app.request("GET", "/api/v1/version")[0])

    def test_large_bodies_are_read_one_at_a_time(self):
        self.app.start()
        boundary = "awtrix-body"

        def upload(name, size):
            body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{name}"\r\n'
                    'Content-Type: audio/mpeg\r\n\r\n').encode() + b"ID3" + bytes(size - 3) + \
                f"\r\n--{boundary}--\r\n".encode()
            head = (f"POST /api/v1/audio/mp3 HTTP/1.1\r\nHost: awtrix.local:{self.app.port}\r\n"
                    f"Content-Type: multipart/form-data; boundary={boundary}\r\nContent-Length: {len(body)}\r\n"
                    "Connection: close\r\n\r\n").encode()
            return head + body

        def connect():
            connection = socket.create_connection(("127.0.0.1", self.app.port), timeout=10)
            self.addCleanup(connection.close)
            return connection

        def send_in_background(connection, data):
            def send():
                try:
                    connection.sendall(data)
                except OSError:
                    pass
            sender = threading.Thread(target=send, daemon=True)
            sender.start()
            return sender

        def status(connection):
            reply = b""
            while b"\r\n" not in reply:
                chunk = connection.recv(4096)
                if not chunk:
                    break
                reply += chunk
            return int(reply.split(b" ", 2)[1])

        first_request = upload("first.mp3", 1 << 20)
        first = connect()
        first.sendall(first_request[:len(first_request) // 2])
        time.sleep(0.3)
        second = connect()
        send_in_background(second, upload("second.mp3", 1 << 20))
        second.settimeout(1.5)
        with self.assertRaises(socket.timeout, msg="a second large body was read beside the first"):
            second.recv(1)
        started = time.monotonic()
        self.assertEqual(200, self.app.request("POST", "/api/v1/notifications", {"text": "small"})[0])
        self.assertLess(time.monotonic() - started, 1.0, "small bodies do not wait for a large one")
        first.sendall(first_request[len(first_request) // 2:])
        self.assertEqual(200, status(first))
        second.settimeout(10)
        self.assertEqual(200, status(second))
        status_code, listing, _ = self.app.request("GET", "/api/v1/audio/mp3")
        self.assertEqual(200, status_code)
        self.assertEqual({"first.mp3", "second.mp3"}, {entry["name"] for entry in json.loads(listing)["files"]})

        chunked = connect()
        piece = bytes(1 << 16)
        head = (f"POST /api/v1/restore HTTP/1.1\r\nHost: awtrix.local:{self.app.port}\r\n"
                "Content-Type: application/zip\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n").encode()
        chunks = b"".join(b"%x\r\n" % len(piece) + piece + b"\r\n" for _ in range((8 << 20) // len(piece) + 1))
        send_in_background(chunked, head + chunks + b"0\r\n\r\n")
        self.assertEqual(413, status(chunked), "a chunked body is capped like a declared one")
        with_body = connect()
        with_body.sendall(f"GET /api/v1/version HTTP/1.1\r\nHost: awtrix.local:{self.app.port}\r\n"
                          "Content-Length: 1048576\r\nConnection: close\r\n\r\n".encode())
        self.assertEqual(413, status(with_body), "only body methods take a large body")
        self.assertEqual(200, self.app.request("GET", "/api/v1/version")[0])

    def test_lan_options_are_validated_before_startup(self):
        for arguments in (["--lan", "--hardened"], ["--lan", "--listen", "localhost"], ["--lan", "--listen", "::1"],
                          ["--lan", "--origin", "https://localhost:8443"], ["--lan", "--credentials-file", "/dev/null"],
                          ["--lan", "--mqtt-ca-file", "/dev/null"], ["--listen", "0.0.0.0"]):
            with self.subTest(arguments=arguments):
                result = self.refused(*arguments)
                self.assertEqual(2, result.returncode, result.stdout)
                self.assertIn(b"Administrative security:", result.stdout)
        for arguments, port in (([], 80), ([], 1023), (["--lan"], 0), (["--lan"], 65536)):
            with self.subTest(arguments=arguments, port=port):
                self.assertEqual(2, self.refused(*arguments, port=port).returncode)
        self.assertFalse(self.app.data.exists())

    def test_privileged_ports_are_accepted_in_lan_mode(self):
        self.app.port = 80
        self.app.output = open(self.app.root / "runtime.log", "ab")
        self.app.process = subprocess.Popen(self.app.command(), stdout=self.app.output, stderr=self.app.output)
        try:
            code = self.app.process.wait(timeout=3)
            self.assertEqual(1, code, self.app.logs())
            self.assertIn("cannot bind port 80", self.app.logs())
            self.app.process = None
            self.app.output.close()
        except subprocess.TimeoutExpired:
            self.assertEqual(200, self.app.request("GET", "/api/v1/version")[0])

class LanMqttTlsContracts(unittest.TestCase):
    """--lan: MQTT over TLS without uploads."""

    @classmethod
    def setUpClass(cls):
        cls.fixtures = tempfile.TemporaryDirectory(prefix="awtrix-lan-tls-ca-")
        cls.addClassCleanup(cls.fixtures.cleanup)
        cls.mqtt_cert = Path(cls.fixtures.name) / "mqtt.crt"
        cls.mqtt_key = Path(cls.fixtures.name) / "mqtt.key"
        result = subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256",
            "-nodes", "-days", "2", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
            "-keyout", str(cls.mqtt_key), "-out", str(cls.mqtt_cert)], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=15)
        if result.returncode:
            raise AssertionError("cannot generate local MQTT TLS fixture")
        cls.fingerprint = certificate_fingerprint(cls.mqtt_cert.read_text())

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-lan-tls-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app = LanRuntime(self.root)
        self.addCleanup(self.app.stop)

    def json(self, method, path, body=None):
        status, payload, _ = self.app.request(method, path, body)
        self.assertEqual(200, status, payload)
        return json.loads(payload)

    def configure(self, **values):
        self.app.data.mkdir(mode=0o700, exist_ok=True)
        (self.app.data / "device.json").write_text(json.dumps(values))

    def operator(self, port):
        operator = TlsMqttClient(port, self.mqtt_cert, "operator", self.password)
        self.addCleanup(operator.close)
        self.assertEqual((0x20, b"\x00\x00"), operator.connack)
        operator.subscribe("contract/#")
        return operator

    def mqtt_broker(self, host="localhost"):
        port, self.password = start_broker(self, self.root, self.mqtt_cert, self.mqtt_key)
        self.configure(mqttEnabled=True, mqttTls=True, mqttHost=host, mqttPort=port, mqttPrefix="contract",
                       mqttUser="device", mqttPass=self.password)
        return port

    def test_an_unknown_broker_certificate_waits_for_trust_then_connects(self):
        port = self.mqtt_broker()
        operator = self.operator(port)
        self.app.start()
        pending = eventually(lambda: self.json("GET", "/api/v1/mqtt/tls")["pending"],
                             lambda value: value is not None)
        self.assertEqual(self.fingerprint, pending)
        self.assertEqual(0, self.json("GET", "/api/v1/device")["mqtt"]["connects"])
        self.assertIn("broker certificate not trusted, SHA-256 " + self.fingerprint, self.app.logs())
        self.json("PUT", "/api/v1/system", {"mqttTlsPin": pending})
        self.assertEqual("online", operator.wait_topic("contract/availability"))
        self.assertIsNone(self.json("GET", "/api/v1/mqtt/tls")["pending"])
        self.assertEqual(pending, self.json("GET", "/api/v1/system")["mqttTlsPin"])
        self.assertNotIn(self.password, self.app.logs())

    def test_a_changed_broker_certificate_is_refused_until_trusted_again(self):
        port = self.mqtt_broker()
        values = json.loads((self.app.data / "device.json").read_text())
        values["mqttTlsPin"] = "ab" * 32
        (self.app.data / "device.json").write_text(json.dumps(values))
        self.app.start()
        pending = eventually(lambda: self.json("GET", "/api/v1/mqtt/tls")["pending"],
                             lambda value: value is not None)
        self.assertEqual(self.fingerprint, pending)
        self.assertEqual("ab" * 32, self.json("GET", "/api/v1/system")["mqttTlsPin"])
        self.assertEqual(0, self.json("GET", "/api/v1/device")["mqtt"]["connects"])

    def test_an_uploaded_ca_verifies_the_broker_and_its_name(self):
        port = self.mqtt_broker()
        operator = self.operator(port)
        self.app.start()
        status = self.json("PUT", "/api/v1/mqtt/tls/ca", {"certificate": self.mqtt_cert.read_text()})
        self.assertEqual("uploaded", status["ca"])
        self.assertEqual("online", operator.wait_topic("contract/availability"))
        self.assertIsNone(self.json("GET", "/api/v1/mqtt/tls")["pending"])
        self.app.stop()
        self.mqtt_broker(host="127.0.0.1")
        self.app.start()
        state = eventually(lambda: self.json("GET", "/api/v1/device")["mqtt"], lambda value: value["attempts"] >= 1)
        self.assertEqual(0, state["connects"])
        self.assertIsNone(self.json("GET", "/api/v1/mqtt/tls")["pending"])
        self.assertEqual("public", self.json("DELETE", "/api/v1/mqtt/tls/ca")["ca"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    parser.add_argument("--require-tools", action="store_true", help="fail instead of skipping without openssl or mosquitto")
    OPTIONS, remaining = parser.parse_known_args()
    missing = [tool for tool in ("openssl", "mosquitto", "mosquitto_passwd") if not shutil.which(tool)]
    if missing and not OPTIONS.require_tools:
        print(f"skipped: {', '.join(missing)} not installed")
        raise SystemExit(77)
    OPTIONS.binary = str(Path(OPTIONS.binary).resolve())
    OPTIONS.webui = str(Path(OPTIONS.webui).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
