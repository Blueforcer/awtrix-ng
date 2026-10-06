"""Exercise a built Linux runtime using real HTTP, files, processes and MQTT sockets.

Python packages: standard library only. MQTT additionally uses the mosquitto executable;
TLS certificate tests use openssl. --require-mqtt/--require-tls turn missing tools into failures.
Every process and file belongs to a temporary directory created by this runner.
"""

from __future__ import annotations

import argparse
import base64
import contextlib
import hashlib
import http.client
import http.server
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import socket
import socketserver
import ssl
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
import zipfile


OPTIONS = None


import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import Runtime as SharedRuntime, eventually, free_port, stop_process, wait_ready


class Runtime(SharedRuntime):
    def __init__(self, root):
        super().__init__(root, OPTIONS.binary, OPTIONS.webui)


class MqttClient:
    """Small MQTT 3.1.1 client; the broker is an actual mosquitto subprocess."""
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.send(0x10, self.string("MQTT") + bytes([4, 2]) + struct.pack("!H", 30) +
                  self.string("awtrix-contract-client"))
        kind, payload = self.receive()
        if kind != 0x20 or payload != b"\x00\x00":
            raise AssertionError((kind, payload))

    @staticmethod
    def string(value):
        raw = value.encode()
        return struct.pack("!H", len(raw)) + raw

    def send(self, kind, payload):
        remaining = len(payload)
        header = bytearray([kind])
        while True:
            byte = remaining % 128
            remaining //= 128
            header.append(byte | (128 if remaining else 0))
            if not remaining:
                break
        self.sock.sendall(header + payload)

    def read(self, length):
        result = bytearray()
        while len(result) < length:
            chunk = self.sock.recv(length - len(result))
            if not chunk:
                raise AssertionError("MQTT connection closed")
            result.extend(chunk)
        return bytes(result)

    def receive(self):
        kind = self.read(1)[0]
        length, multiplier = 0, 1
        for _ in range(4):
            byte = self.read(1)[0]
            length += (byte & 127) * multiplier
            if not byte & 128:
                return kind, self.read(length)
            multiplier *= 128
        raise AssertionError("invalid MQTT remaining length")

    def subscribe(self, topic):
        self.send(0x82, b"\x00\x01" + self.string(topic) + b"\x00")
        while True:
            kind, payload = self.receive()
            if kind == 0x90:
                if payload != b"\x00\x01\x00":
                    raise AssertionError(payload)
                return

    def publish(self, topic, value):
        self.send(0x30, self.string(topic) + value.encode())

    def wait_topic(self, topic, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.sock.settimeout(max(0.05, deadline - time.monotonic()))
            kind, payload = self.receive()
            if kind >> 4 != 3:
                continue
            length = struct.unpack("!H", payload[:2])[0]
            received_topic = payload[2:2 + length].decode()
            if received_topic == topic:
                return payload[2 + length:].decode()
        raise AssertionError(f"no MQTT publish on {topic}")

    def close(self):
        with contextlib.suppress(OSError):
            self.send(0xE0, b"")
        self.sock.close()


class BoardSelectionContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-board-contract-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_board(self, *arguments):
        return subprocess.run([OPTIONS.binary, "--data", str(self.root / "data"),
            "--webui", OPTIONS.webui, "--port", str(free_port()), *arguments],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=10)

    def test_unknown_board_is_rejected_before_startup(self):
        result = self.run_board("--board", "unknown")
        self.assertEqual(2, result.returncode, result.stdout)
        self.assertTrue(result.stdout.strip(), "refusal includes a diagnostic")
        self.assertFalse((self.root / "data").exists())

    def test_tc002_rejects_nonphysical_geometry_before_startup(self):
        for geometry in ((32, 16), (52, 8), (64, 32)):
            with self.subTest(geometry=geometry):
                result = self.run_board("--board", "tc002", "--width", str(geometry[0]),
                                        "--height", str(geometry[1]))
                self.assertEqual(2, result.returncode, result.stdout)
                self.assertTrue(result.stdout.strip(), "refusal includes a diagnostic")
                self.assertFalse((self.root / "data").exists())

    def test_tc002_without_spi_refuses_physical_startup(self):
        if Path("/dev/spidev0.0").exists():
            self.skipTest("physical peripheral exists; host refusal test must not access it")
        result = self.run_board("--board", "tc002")
        self.assertEqual(1, result.returncode, result.stdout)
        self.assertIn("Display startup failed:", result.stdout)
        self.assertNotIn(" @ http://", result.stdout)

    def test_tc002_input_rejects_unrecognized_descriptors(self):
        for descriptors in ("99,101", "100,102", "100", "/dev/input/event0"):
            with self.subTest(descriptors=descriptors):
                result = self.run_board("--board", "tc002", "--tc002-input-fds", descriptors)
                self.assertEqual(2, result.returncode, result.stdout)
                self.assertFalse((self.root / "data").exists())

    def test_tc002_input_rejects_headless_board(self):
        result = self.run_board("--board", "headless", "--tc002-input-fds", "100,101")
        self.assertEqual(2, result.returncode, result.stdout)
        self.assertTrue(result.stdout.strip(), "refusal includes a diagnostic")
        self.assertFalse((self.root / "data").exists())

    def test_tc002_input_requires_guardian_before_display_startup(self):
        # The subprocess closes inherited descriptors.
        result = self.run_board("--board", "tc002", "--tc002-input-fds", "100,101")
        self.assertEqual(1, result.returncode, result.stdout)
        self.assertIn("Input startup failed:", result.stdout)
        self.assertIn("validated descriptors 100 and 101", result.stdout)
        self.assertNotIn("Display startup failed:", result.stdout)
        self.assertNotIn(" @ http://", result.stdout)


    def run_with_speaker(self, descriptor, *arguments):
        os.dup2(descriptor, 104)
        try:
            return subprocess.run([OPTIONS.binary, "--data", str(self.root / "data"), "--webui", OPTIONS.webui,
                "--port", str(free_port()), *arguments], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, timeout=10, pass_fds=(104,))
        finally:
            os.close(104)

    def test_tc002_audio_requires_the_tc002_board_and_descriptor_104(self):
        for arguments, message in ((("--board", "headless", "--tc002-audio-fd", "104"), "TC002 audio requires --board tc002"),
                                   (("--board", "tc002", "--tc002-audio-fd", "105"), "--tc002-audio-fd 104")):
            with self.subTest(arguments=arguments):
                result = self.run_board(*arguments)
                self.assertEqual(2, result.returncode, result.stdout)
                self.assertIn(message, result.stdout)
                self.assertFalse((self.root / "data").exists())

    def test_speech_voice_comes_with_the_speaker(self):
        for arguments, message in ((("--board", "tc002", "--speech-voice", "/tmp/voice.atts"),
                                    "--speech-voice requires --tc002-audio-fd"),
                                   (("--speech-voice", "a.atts", "--speech-voice", "b.atts"),
                                    "awtrix-linux --data DIRECTORY")):
            with self.subTest(arguments=arguments):
                result = self.run_board(*arguments)
                self.assertEqual(2, result.returncode, result.stdout)
                self.assertIn(message, result.stdout)
                self.assertFalse((self.root / "data").exists())

    def test_tc002_audio_descriptor_is_validated_before_startup(self):
        if Path("/dev/spidev0.0").exists():
            self.skipTest("physical peripheral exists; host refusal test must not access it")
        read, write = os.pipe()
        try:
            result = self.run_with_speaker(read, "--board", "tc002", "--tc002-audio-fd", "104")
        finally:
            os.close(read)
            os.close(write)
        self.assertEqual(2, result.returncode, result.stdout)
        self.assertIn("Speaker channel refused:", result.stdout)
        self.assertFalse((self.root / "data").exists())
        runtime, helper = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        with runtime, helper:
            result = self.run_with_speaker(runtime.fileno(), "--board", "tc002", "--tc002-audio-fd", "104")
        self.assertEqual(1, result.returncode, result.stdout)
        self.assertNotIn("Speaker channel refused", result.stdout)
        self.assertIn("Display startup failed:", result.stdout)

class Contract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="awtrix-linux-contract-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app = Runtime(self.root)
        self.addCleanup(self.app.stop)
        self.app.start()

    def tool(self, name, required):
        executable = shutil.which(name)
        if not executable:
            if required:
                self.fail(f"required executable missing: {name}")
            self.skipTest(f"optional executable missing: {name}")
        return executable

    def test_capabilities_and_no_simulated_hardware(self):
        self.assertTrue(self.app.json("GET", "/api/v1/version")["version"])
        caps = self.app.json("GET", "/api/v1/capabilities")
        self.assertEqual({"id": "linux"}, caps["platform"])
        self.assertEqual({"width": 52, "height": 16, "configurable": False},
                         {key: caps["display"][key] for key in ("width", "height", "configurable")})
        self.assertTrue(caps["display"]["ready"])
        self.assertFalse(caps["display"]["restartRequired"])
        self.assertEqual(12, len(caps["fonts"]))
        self.assertEqual(1, caps["layouts"]["version"])
        self.assertTrue(caps["layout"])
        self.assertIsNone(caps["gpio"])
        self.assertFalse(any(caps["audio"].values()))
        self.assertIs(False, caps["microphone"])
        self.assertNotIn("ble", caps)
        self.assertIs(True, caps["gamepad"], "a phone is a gamepad without Bluetooth")
        self.assertIs(True, caps["gamepadRemote"])
        pads = self.app.json("GET", "/api/v1/gamepad")
        self.assertEqual([], pads["remotes"])
        self.assertTrue(all(d["state"] == "unpaired" for d in pads["devices"]))
        device = self.app.json("GET", "/api/v1/device")
        for field in ("batteryPercent", "batteryVoltage", "temperature", "humidity"):
            self.assertNotIn(field, device)
        screen = self.app.screen()
        self.assertEqual((52, 16, 832), (screen["width"], screen["height"], len(screen["pixels"])))

    def test_eight_pixel_display_has_no_layout_registration(self):
        self.app.stop()
        self.app.height = 8
        self.app.start()
        caps = self.app.json("GET", "/api/v1/capabilities")
        self.assertNotIn("layout", caps)
        self.assertNotIn("layouts", caps)
        status, body = self.app.request("PUT", "/api/v1/apps/pushed/unsupported",
            {"layout": {"version": 1, "regions": []}})
        self.assertEqual(422, status, body)
        self.assertFalse(any(app["name"] == "unsupported" for app in self.app.json("GET", "/api/v1/apps")))

    def test_script_update_preserves_config_and_rolls_back_invalid_replacements(self):
        app = self.app
        name = "HubUpdateRegression"
        source_path = "/api/v1/apps/script/" + name
        config_path = "/api/v1/apps/" + name + "/config"
        original = "class App\n def draw() end\nend\nreturn App()\n"
        app.install("Unchanged", original)
        self.assertTrue(app.json("GET", "/api/v1/capabilities")["scriptUpdates"])
        v1 = "# @config city text default=Rom\n" + original
        v2 = v1 + "# release 2\n"

        def update(before, after, expected=200):
            status, response = app.request("PUT", "/api/v1/apps/script-update/" + name,
                                          {"expected_source": before, "source": after})
            self.assertEqual(expected, status, response)

        update(None, v1)
        self.assertEqual(v1, app.json("GET", source_path))
        update(None, v2, 409)
        app.json("PATCH", config_path, {"city": "Berlin"})
        update(v1, v2)
        self.assertEqual(v2, app.json("GET", source_path))
        self.assertIn("Berlin", json.dumps(app.json("GET", config_path)))
        for candidate, before, status in (
                (v1, v1, 409),
                ("class Broken\n def draw( end", v2, 422),
                ("class App\n def init() raise 'test', 'broken setup' end\n"
                 " def draw() end\nend\nreturn App()", v2, 422)):
            update(before, candidate, status)
            self.assertEqual(v2, app.json("GET", source_path))
            self.assertIn("Berlin", json.dumps(app.json("GET", config_path)))
            entry = next(a for a in app.json("GET", "/api/v1/apps") if a["name"] == name)
            self.assertFalse(entry.get("error"))
        app.json("DELETE", "/api/v1/apps/" + name)
        self.assertFalse(any(a["name"] == name for a in app.json("GET", "/api/v1/apps")))
        self.assertEqual(original, app.json("GET", "/api/v1/apps/script/Unchanged"))

    def test_script_data_round_trip_leaves_settings_to_config(self):
        app = self.app
        name = "DataRoundTrip"
        data_path = "/api/v1/apps/" + name + "/data"
        app.install(name,
                    "# @config city text default=Rom\n"
                    "class App\n"
                    " def setup()\n"
                    "  if store.get('hits') == nil store.set('hits', 1) store.set('best', [3, 1]) end\n"
                    "  shared.set('hits', store.get('hits'))\n"
                    " end\n"
                    " def draw() end\n"
                    "end\n"
                    "return App()\n")
        eventually(lambda: app.json("GET", data_path), lambda data: data == {"hits": 1, "best": [3, 1]})
        app.json("PATCH", data_path, {"hits": 9, "best": None})
        self.assertEqual({"hits": 9}, app.json("GET", data_path))
        eventually(lambda: app.json("GET", "/api/v1/scripts/shared"),
                   lambda rows: any(row["owner"] == name and row["key"] == "hits" and row["value"] == 9
                                    for row in rows))
        self.assertIn("Rom", json.dumps(app.json("GET", "/api/v1/apps/" + name + "/config")))
        for path in (data_path, "/api/v1/apps/" + name + "/config"):
            status, error = app.request("PATCH", path, '{"hits":')
            self.assertEqual(400, status, error)
            self.assertEqual("invalidJson", error["error"]["code"])
        app.json("DELETE", "/api/v1/apps/" + name)

    def test_brightness_reports_the_value_submitted_to_the_board(self):
        self.app.json("PATCH", "/api/v1/settings", {"brightness": 37})
        eventually(lambda: self.app.json("GET", "/api/v1/display"), lambda state: state["brightness"] == 37)
        self.assertEqual(37, self.app.json("GET", "/api/v1/device")["brightness"])
        self.app.json("PUT", "/api/v1/display/moodlight", {"color": "#123456", "brightness": 23})
        eventually(lambda: self.app.json("GET", "/api/v1/display"), lambda state: state["brightness"] == 23)
        eventually(self.app.screen, lambda frame: all(pixel == 0x123456 for pixel in frame["pixels"]))
        self.app.json("DELETE", "/api/v1/display/moodlight")
        eventually(lambda: self.app.json("GET", "/api/v1/display"), lambda state: state["brightness"] == 37)
        self.app.json("PATCH", "/api/v1/settings", {"brightness": 0, "autoBrightness": True})
        eventually(lambda: self.app.json("GET", "/api/v1/display"), lambda state: state["brightness"] == 0)

    def test_panel_sized_gifs_and_asset_replacement_share_the_contract(self):
        fixture_path = Path(__file__).resolve().parents[2] / "scripts/test_dynamic_gif_api.py"
        spec = importlib.util.spec_from_file_location("gif_fixture", fixture_path)
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        app = self.app
        screen = app.screen()
        width, height = screen["width"], screen["height"]
        app.json("PATCH", "/api/v1/settings", {"autoTransition": False})

        def upload(colors):
            data = fixture.animated_gif(width, height, colors=colors)
            body = (b'--gif-contract\r\nContent-Disposition: form-data; '
                    b'name="file"; filename="nativegif.gif"\r\n'
                    b'Content-Type: image/gif\r\n\r\n' + data + b'\r\n--gif-contract--\r\n')
            status, response = app.request("POST", "/api/v1/files?dir=/ICONS", body,
                "multipart/form-data; boundary=gif-contract")
            self.assertEqual(200, status, response)

        def shows(color):
            eventually(app.screen, lambda frame: all(p == color for p in frame["pixels"]))

        upload((0x123456, 0xABCDEF))
        app.json("PUT", "/api/v1/apps/pushed/gifcontract", {"icon": "nativegif"})
        app.json("PUT", "/api/v1/apps/active", {"name": "gifcontract", "fast": True})
        shows(0x123456)
        shows(0xABCDEF)
        upload((0x13579B, 0x2468AC))
        shows(0x13579B)
        app.json("POST", "/api/v1/notifications", {"icon": "nativegif", "hold": True})
        shows(0x2468AC)
        app.json("DELETE", "/api/v1/notifications/active")
        app.install("gifscript", "class Gif\n def draw()\n clear()\n"
                    " icon('nativegif',0,0)\n end\nend\nreturn Gif()\n")
        app.json("PUT", "/api/v1/apps/active", {"name": "gifscript", "fast": True})
        shows(0x13579B)
        shows(0x2468AC)
        upload((0x112233, 0x445566))
        shows(0x445566)

    def test_streamed_logs_share_the_contract(self):
        app = self.app
        app.install("logcontract", 'class Logger\n def init()\n'
                    ' log("merge-log-check")\n end\n def draw() end\nend\nreturn Logger()\n')
        logs = app.json("GET", "/api/v1/logs")
        self.assertTrue(any("merge-log-check" in line for line in logs["lines"]), logs)
        self.assertEqual([], app.json("GET", f'/api/v1/logs?after={logs["next"]}')["lines"])

    def test_modbus_and_timers_use_real_sockets(self):
        requests = []

        class Device(socketserver.BaseRequestHandler):
            def handle(self):
                self.request.settimeout(3)
                request = bytearray()
                while len(request) < 12:
                    chunk = self.request.recv(12 - len(request))
                    if not chunk:
                        return
                    request.extend(chunk)
                transaction, protocol, length, unit, function, address, count = struct.unpack(
                    ">HHHBBHH", request)
                requests.append((protocol, length, unit, function, address, count))
                response = struct.pack(">HHHBBBH", transaction, 0, 5, unit, function, 2, 235)
                for offset in range(0, len(response), 3):
                    self.request.sendall(response[offset:offset + 3])
                    time.sleep(0.002)

        with socketserver.TCPServer(("127.0.0.1", 0), Device) as server:
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            try:
                app = self.app
                app.install("modbuscontract", f"""
import modbus
class Probe
 def init()
  timer.after(50, / -> self.read())
 end
 def read()
  modbus.readHoldingRegisters('127.0.0.1',7,1,
   / values,error -> shared.set('ok',error == 0 && values[0] == 235),
   {{'port':{server.server_address[1]},'unit':3}})
 end
 def draw() end
end
return Probe()
""")
                eventually(lambda: app.json("GET", "/api/v1/scripts/shared"),
                    lambda rows: any(row["owner"] == "modbuscontract" and
                                     row["key"] == "ok" and row["value"] is True
                                     for row in rows))
                self.assertEqual([(0, 6, 3, 3, 7, 1)], requests)
            finally:
                server.shutdown()
                worker.join(timeout=5)

    def test_berry_reads_leading_dot_reals_after_longer_tokens(self):
        app = self.app
        app.install("dotreals", "a = 1.5\nb = .5\nc = 12345\nd = .5\n"
                                "shared.set('b', b)\nshared.set('d', d)\n"
                                "class App\n def draw() end\nend\nreturn App()\n")
        rows = eventually(lambda: app.json("GET", "/api/v1/scripts/shared"),
                          lambda rows: sum(row["owner"] == "dotreals" for row in rows) == 2)
        self.assertEqual({"b": 0.5, "d": 0.5},
                         {row["key"]: row["value"] for row in rows if row["owner"] == "dotreals"})

    def test_berry_syntax_errors_name_the_expected_keyword(self):
        status, reply = self.app.request("PUT", "/api/v1/apps/script/missingend",
                                         "if true\n  x = 1\n", "text/plain")
        self.assertEqual(200, status, reply)
        self.assertEqual({"message": "syntax_error: expected 'end' before 'EOS'", "line": 3},
                         reply["error"])

    def test_sigterm_cancels_an_active_modbus_request(self):
        received, release = threading.Event(), threading.Event()

        class SilentDevice(socketserver.BaseRequestHandler):
            def handle(self):
                self.request.settimeout(3)
                if self.request.recv(12):
                    received.set()
                    release.wait(5)

        with socketserver.TCPServer(("127.0.0.1", 0), SilentDevice) as server:
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            try:
                self.app.install("silentmodbus", f"""
import modbus
class Probe
 def init()
  modbus.readHoldingRegisters('127.0.0.1',0,1,/ values,error -> nil,
   {{'port':{server.server_address[1]}}})
 end
 def draw() end
end
return Probe()
""")
                self.assertTrue(received.wait(3), self.app.logs())
                start = time.monotonic()
                self.app.stop()
                self.assertLess(time.monotonic() - start, 1.5)
            finally:
                release.set()
                server.shutdown()
                worker.join(timeout=5)

    def test_pushed_apps_are_enlarged_by_default(self):
        self.assertTrue(self.app.json("GET", "/api/v1/capabilities")["enlargeApps"])
        self.app.json("PATCH", "/api/v1/settings", {"autoTransition": False})
        self.app.json("PUT", "/api/v1/apps/pushed/enlarged", {"draw": [["pixel", 25, 7, "#123456"]]})
        self.app.json("PUT", "/api/v1/apps/active", {"name": "enlarged", "fast": True})
        screen = eventually(self.app.screen, lambda s: s["pixels"][-1] == 0x123456)
        self.assertEqual([0x123456] * 2, screen["pixels"][14 * 52 + 50:14 * 52 + 52])

    def test_json_and_berry_reach_the_last_native_pixel(self):
        self.app.json("PATCH", "/api/v1/settings", {"autoTransition": False, "enlargeApps": False})
        self.app.json("PUT", "/api/v1/apps/pushed/pixels", {
            "draw": [["pixel", 51, 15, "#123456"], ["pixel", 52, 16, "#FFFFFF"]]})
        self.app.json("PUT", "/api/v1/apps/active", {"name": "pixels", "fast": True})
        screen = eventually(self.app.screen, lambda s: s["pixels"][-1] == 0x123456)
        self.assertEqual(832, len(screen["pixels"]))
        source = ("class NativePixels\n def draw()\n"
                  "  pixel(width()-1, height()-1, 0xABCDEF)\n"
                  "  pixel(0, 0, width()*height())\n end\nend\nreturn NativePixels()\n")
        self.app.install("nativepixels", source)
        self.app.json("PUT", "/api/v1/apps/active", {"name": "nativepixels", "fast": True})
        screen = eventually(self.app.screen, lambda s: s["pixels"][-1] == 0xABCDEF)
        self.assertEqual(832, screen["pixels"][0])

    def test_native_layout_json_berry_and_rejected_updates_share_the_contract(self):
        app = self.app
        app.json("PATCH", "/api/v1/settings", {"autoTransition": False})
        screen = app.screen()
        width, height = screen["width"], screen["height"]
        spec = {"version": 1, "regions": [{"id": "meter",
            "box": [width - 10, height - 4, 10, 4], "progress": 50,
            "color": 0x00FF00, "trackColor": 0x0000FF}]}
        app.json("PUT", "/api/v1/apps/pushed/layoutcontract", {"layout": spec})
        app.json("PUT", "/api/v1/apps/active", {"name": "layoutcontract", "fast": True})
        reference = eventually(app.screen, lambda s: s["pixels"][-1] == 0x0000FF)["pixels"]
        self.assertEqual(0x00FF00, reference[(height - 3) * width + width - 9])
        status, body = app.request("PUT", "/api/v1/apps/pushed/layoutcontract",
            {"layout": {"version": 1, "regions": [{"id": "bad",
             "box": [width, 0, 1, 1], "progress": 50}]}})
        self.assertEqual(422, status, body)
        self.assertEqual(reference, app.screen()["pixels"])
        source = ("import json\nclass NativeLayout\n var handle\n def setup()\n"
                  " self.handle = layout.prepare(json.load('" + json.dumps(spec) + "'))\n"
                  " end\n def draw() layout.draw(self.handle) end\nend\nreturn NativeLayout()\n")
        app.install("layoutscript", source)
        app.json("PUT", "/api/v1/apps/active", {"name": "layoutscript", "fast": True})
        self.assertEqual(reference, eventually(app.screen,
            lambda s: s["pixels"] == reference)["pixels"])

    def test_pictures_from_urls_reach_icons_layouts_and_scripts(self):
        fixture_path = Path(__file__).resolve().parents[2] / "scripts/test_dynamic_gif_api.py"
        spec = importlib.util.spec_from_file_location("gif_fixture", fixture_path)
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        # 64x32, quadrants red and green on top, blue and white below.
        quadrants = base64.b64decode(
        "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYFBgYGBwkIBgcJ"
        "BwYGCAsICQoKCgoKBggLDAsKDAkKCgr/2wBDAQICAgICAgUDAwUKBwYHCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoK"
        "CgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAAgAEADAREAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAA"
        "AAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAk"
        "M2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKT"
        "lJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QA"
        "HwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdh"
        "cRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hp"
        "anN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk"
        "5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD4vr+Uz/fwKACgAoA/Siv5bP8AhXCgAoAKAPzXr+pD/uoCgAoA"
        "KAP0or+Wz/hXCgAoAKAPwbr/ALTD/VgKACgAoA/uIr/AM/VAoAKACgD+Hev9/D8rCgAoAKAP7iK/wDP1QKACgAoA"
        "/9k=")
        files = {"/cover.jpg": quadrants,
                 "/anim.gif": fixture.animated_gif(8, 8, colors=(0x123456, 0xABCDEF))}
        hits = {}

        class Pictures(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                path = self.path.split("?")[0]
                hits[path] = hits.get(path, 0) + 1
                content = files.get(path)
                self.send_response(200 if content else 404)
                self.send_header("Content-Length", str(len(content or b"")))
                self.end_headers()
                self.wfile.write(content or b"")

            def log_message(self, *args):
                pass

        def near(actual, expected):
            return all(abs((actual >> s & 0xFF) - (expected >> s & 0xFF)) <= 16 for s in (16, 8, 0))

        app = self.app
        app.json("PATCH", "/api/v1/settings", {"autoTransition": False})
        with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Pictures) as server:
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            try:
                origin = f"http://127.0.0.1:{server.server_address[1]}"
                cover = origin + "/cover.jpg?token=secret"

                app.json("PUT", "/api/v1/apps/pushed/cover", {"icon": cover})
                app.json("PUT", "/api/v1/apps/active", {"name": "cover", "fast": True})
                frame = eventually(app.screen, lambda s: near(s["pixels"][2 * 52 + 2], 0xFF0000))
                pixel = lambda x, y: frame["pixels"][y * frame["width"] + x]
                self.assertTrue(near(pixel(13, 2), 0x00FF00), hex(pixel(13, 2)))
                self.assertTrue(near(pixel(2, 13), 0x0000FF), hex(pixel(2, 13)))
                self.assertTrue(near(pixel(13, 13), 0xFFFFFF), hex(pixel(13, 13)))

                app.json("PUT", "/api/v1/apps/pushed/coverregion", {"layout": {"version": 1, "regions": [
                    {"id": "art", "box": [20, 0, 32, 16], "icon": cover}]}})
                app.json("PUT", "/api/v1/apps/active", {"name": "coverregion", "fast": True})
                frame = eventually(app.screen, lambda s: near(s["pixels"][2 * 52 + 22], 0xFF0000))
                self.assertTrue(near(pixel(49, 2), 0x00FF00), hex(pixel(49, 2)))
                self.assertTrue(near(pixel(22, 13), 0x0000FF), hex(pixel(22, 13)))
                self.assertTrue(near(pixel(49, 13), 0xFFFFFF), hex(pixel(49, 13)))
                self.assertEqual(2, hits["/cover.jpg"])

                app.json("PUT", "/api/v1/apps/active", {"name": "cover", "fast": True})
                eventually(app.screen, lambda s: near(s["pixels"][2 * 52 + 2], 0xFF0000))
                self.assertEqual(2, hits["/cover.jpg"])

                app.install("urlgif", "class Gif\n def draw()\n clear()\n"
                            f" icon('{origin}/anim.gif',0,0)\n end\nend\nreturn Gif()\n")
                app.json("PUT", "/api/v1/apps/active", {"name": "urlgif", "fast": True})
                eventually(app.screen, lambda s: s["pixels"][4 * 52 + 4] == 0x123456)
                frame = eventually(app.screen, lambda s: s["pixels"][4 * 52 + 4] == 0xABCDEF)
                self.assertEqual(0, pixel(3, 3))
                self.assertEqual(0xABCDEF, pixel(11, 11))
                self.assertEqual(0, pixel(12, 12))

                app.json("PUT", "/api/v1/apps/pushed/nocover", {"icon": origin + "/gone.jpg?token=secret"})
                app.json("PUT", "/api/v1/apps/active", {"name": "nocover", "fast": True})
                logs = eventually(app.logs, lambda text: "HTTP 404" in text)
                self.assertIn(f"picture from 127.0.0.1:{server.server_address[1]}: HTTP 404", logs)
                self.assertNotIn("secret", logs)
            finally:
                server.shutdown()
                worker.join(timeout=5)

    def test_palette_names_resolve_without_case_sensitivity(self):
        (self.app.data / "PALETTES/ContractMix.TXT").write_text("#223344\n#223344\n")
        self.app.json("PATCH", "/api/v1/settings", {"autoTransition": False})
        for name in ("contractmix", "CONTRACTMIX"):
            self.app.json("PUT", "/api/v1/apps/pushed/palette", {
                "effect": "Plasma", "palette": name, "paletteBlend": False})
            self.app.json("PUT", "/api/v1/apps/active", {"name": "palette", "fast": True})
            screen = eventually(self.app.screen, lambda s: s["pixels"][-1] == 0x223344)
            self.assertTrue(all(pixel == 0x223344 for pixel in screen["pixels"]))

    def test_sigterm_flushes_settings_and_restores_scripts(self):
        self.check_persistence_roundtrip(self.app)

    def check_persistence_roundtrip(self, app):
        source = ("class Saved\n"
                  " def init() store.set('boots', store.get('boots', 0) + 1) end\n"
                  " def draw()\n"
                  "  pixel(width()-1,height()-1,0x246810)\n"
                  "  pixel(0,0,store.get('boots',0))\n"
                  " end\nend\nreturn Saved()\n")
        app.install("saved", source)
        app.json("PATCH", "/api/v1/settings", {"brightness": 37, "autoTransition": False})
        identity = (app.data / "identity").read_text()
        app.stop()
        app.start()
        self.assertEqual(37, app.json("GET", "/api/v1/settings")["brightness"])
        self.assertEqual(source, app.json("GET", "/api/v1/apps/script/saved"))
        self.assertEqual(identity, (app.data / "identity").read_text())
        app.json("PUT", "/api/v1/apps/active", {"name": "saved", "fast": True})
        screen = eventually(app.screen, lambda s: s["pixels"][-1] == 0x246810)
        self.assertEqual(2, screen["pixels"][0], "script store was not restored before init()")

    def test_configuration_write_failure_is_visible_and_retried(self):
        target = self.app.data / "device.json"
        target.mkdir()
        self.addCleanup(lambda: target.rmdir() if target.is_dir() else None)
        status, value = self.app.request("PUT", "/api/v1/system", {"hostname": "retry-contract"})
        self.assertEqual(507, status, value)
        self.assertEqual("insufficientStorage", value["error"]["code"])
        self.assertEqual("retry-contract", self.app.json("GET", "/api/v1/system")["hostname"])
        target.rmdir()
        eventually(lambda: json.loads(target.read_text()) if target.is_file() else {},
                   lambda config: config.get("hostname") == "retry-contract", timeout=5)
        self.app.stop()
        self.app.start()
        self.assertEqual("retry-contract", self.app.json("GET", "/api/v1/system")["hostname"])

    def test_document_save_failures_report_applied_state_and_retry(self):
        app = self.app
        stations = {"stations": [{"name": "Storage probe", "url": "http://example.test/radio.mp3"}]}
        order = {"order": ["pendingfixture"], "disabled": []}
        for filename, route, value in [("apploop.json", "/api/v1/apps/order", order),
                                        ("radio.json", "/api/v1/audio/stations", stations)]:
            with self.subTest(route=route):
                path = app.data / filename
                path.unlink(missing_ok=True)
                path.mkdir()
                status, error = app.request("PUT", route, value)
                self.assertEqual(507, status, error)
                self.assertEqual("insufficientStorage", error["error"]["code"])
                if filename == "radio.json":
                    self.assertEqual(stations, app.json("GET", route))
                else:
                    self.assertTrue(any(item["name"] == "pendingfixture" and item["enabled"]
                                        for item in app.json("GET", "/api/v1/apps")))
                path.rmdir()
                self.assertEqual(200, app.request("PUT", route, value)[0])
                self.assertEqual(value, json.loads(path.read_text()))
        app.stop()
        app.start()
        self.assertEqual(stations, app.json("GET", "/api/v1/audio/stations"))
        self.assertTrue(any(item["name"] == "pendingfixture" and item["enabled"]
                            for item in app.json("GET", "/api/v1/apps")))

    def test_factory_reset_removes_all_application_data(self):
        self.app.install("resetfixture", "class Saved\n def draw() end\nend\nreturn Saved()\n")
        self.assertEqual(200, self.upload("resetfixture.txt")[0])
        self.app.json("PUT", "/api/v1/system", {"hostname": "reset-contract"})
        self.app.json("PATCH", "/api/v1/settings", {"brightness": 37})
        (self.app.data / "unrecognized-data").mkdir()
        (self.app.data / "unrecognized-data/nested.txt").write_text("owned by this instance")
        self.app.json("POST", "/api/v1/device/factory-reset", {})
        self.app.process.wait(timeout=5)
        self.app.stop()
        self.assertEqual({".lock"}, {path.name for path in self.app.data.iterdir()})
        self.app.start()
        self.assertEqual(404, self.app.request("GET", "/api/v1/apps/script/resetfixture")[0])
        self.assertEqual(404, self.app.request("GET", "/PALETTES/resetfixture.txt")[0])
        self.assertEqual(120, self.app.json("GET", "/api/v1/settings")["brightness"])

    @staticmethod
    def replace_file(path, data):
        temporary = path.with_name(path.name + ".migrating")
        with open(temporary, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)

    def test_settings_survive_a_forward_and_backward_version_round_trip(self):
        self.app.json("PATCH", "/api/v1/settings", {"brightness": 37, "autoTransition": False})
        self.app.stop()
        settings = self.app.data / "settings.json"
        original = settings.read_bytes()
        members = json.loads(original)
        self.assertEqual(1, members["schemaVersion"])
        # The migrating actor keeps a byte-exact copy outside --data and adds members the
        # current version does not know, as the documented migration rules require.
        backup = self.root / "backup" / "settings.json"
        backup.parent.mkdir()
        self.replace_file(backup, original)
        migrated = {**members, "schemaVersion": 2, "futureSetting": "kept",
                    "vendorExtension": {"nested": [1, 2]}}
        self.replace_file(settings, json.dumps(migrated).encode())
        self.app.start()
        loaded = self.app.json("GET", "/api/v1/settings")
        self.assertEqual(37, loaded["brightness"])
        self.assertIs(False, loaded["autoTransition"])
        self.app.json("PATCH", "/api/v1/settings", {"brightness": 41})
        self.app.stop()
        saved = json.loads(settings.read_bytes())
        self.assertEqual(41, saved["brightness"])
        self.assertIs(False, saved["autoTransition"])
        self.assertEqual(1, saved["schemaVersion"])
        self.assertNotIn("futureSetting", saved)
        self.assertNotIn("vendorExtension", saved)
        self.assertEqual(set(members), set(saved))
        self.replace_file(settings, backup.read_bytes())
        self.assertEqual(original, settings.read_bytes())
        self.app.start()
        restored = self.app.json("GET", "/api/v1/settings")
        self.assertEqual(37, restored["brightness"])
        self.assertIs(False, restored["autoTransition"])

    def test_invalid_requests_keep_the_contract(self):
        for method, path, body, content_type, expected, code in self.invalid_cases():
            with self.subTest(path=path, body=body):
                status, value = self.app.request(method, path, body, content_type)
                self.assertEqual(expected, status, value)
                self.assertEqual(code, value["error"]["code"])

    @staticmethod
    def invalid_cases():
        return [
            ("PATCH", "/api/v1/settings", "{", "application/json", 400, "invalidJson"),
            ("PATCH", "/api/v1/settings", "{}", "text/plain", 415, "unsupportedMediaType"),
            ("PUT", "/api/v1/apps/pushed/bad!", {"text": "bad"}, "application/json", 400, "invalidName"),
            ("PUT", "/api/v1/apps/pushed/bad", {"draw": [["unknown", 1]]}, "application/json", 422, "validationFailed"),
            ("POST", "/api/v1/settings", {}, "application/json", 405, "methodNotAllowed"),
            ("PUT", "/api/v1/apps/pushed/spaced", "{ }", "application/json", 422, "validationFailed"),
            ("PUT", "/api/v1/display/moodlight", "{ }", "application/json", 422, "validationFailed"),
            ("PUT", "/api/v1/indicators/1", "{\n}", "application/json", 422, "validationFailed"),
            ("PUT", "/api/v1/apps/pushed/stops", {"palette": ["#FF0000"] * 17}, "application/json", 422,
             "validationFailed"),
        ]

    def test_system_secrets_and_rejected_edits_share_the_contract(self):
        secrets = {"wifiPass": "wifi-contract-secret", "mqttPass": "mqtt-contract-secret",
                   "authPass": "auth-contract-secret"}
        app = self.app
        response = app.json("PUT", "/api/v1/system", {"hostname": "stable-contract", **secrets})
        public = app.json("GET", "/api/v1/system")
        private = app.json("GET", "/api/v1/system?secrets=1")
        for name, value in secrets.items():
            self.assertNotIn(name, response)
            self.assertNotIn(name, public)
            self.assertEqual(value, private[name])
            self.assertNotIn(value, app.logs())
        status, error = app.request("PUT", "/api/v1/system", {"hostname": "changed", "mqttPort": 0})
        self.assertEqual(422, status, error)
        self.assertEqual("validationFailed", error["error"]["code"])
        self.assertEqual(private, app.json("GET", "/api/v1/system?secrets=1"))
        app.stop()
        app.start()
        self.assertEqual(private, app.json("GET", "/api/v1/system?secrets=1"))

    def test_media_type_and_method_override_share_the_contract(self):
        app = self.app
        for content_type in ("application/jsonp", "application/json-extra", "text/plain"):
            status, error = app.request("PATCH", "/api/v1/settings", {}, content_type)
            self.assertEqual(415, status, error)
            self.assertEqual("unsupportedMediaType", error["error"]["code"])
        status, value = app.request("PATCH", "/api/v1/settings", {"brightness": 41},
                                    "Application/JSON; charset=utf-8")
        self.assertEqual(200, status, value)
        status, value = app.request("POST", "/api/v1/settings", {"brightness": 42},
            headers={"X-HTTP-Method-Override": "patch"})
        self.assertEqual(200, status, value)
        self.assertEqual(42, value["brightness"])
        for method, path, override in [
            ("PATCH", "/api/v1/settings", "PATCH"),
            ("POST", "/api/v1/settings", "GET"),
            ("POST", "/api/v1/apps/script/blocked", "PUT"),
        ]:
            status, error = app.request(method, path, {},
                headers={"X-HTTP-Method-Override": override})
            self.assertEqual(400, status, error)
            self.assertEqual("invalidMethodOverride", error["error"]["code"])
        self.assertEqual(42, app.json("GET", "/api/v1/settings")["brightness"])
        protected_file = app.data / "PALETTES/keep.txt"
        protected_file.write_text("#123456\n", encoding="utf-8")
        for path in ("/api/v1/files?path=/PALETTES/keep.txt",
                     "/api/v1/audio/mp3", "/api/v1/restore", "/update"):
            with self.subTest(multipart_path=path):
                status, error = app.request("POST", path, {},
                    headers={"X-HTTP-Method-Override": "DELETE"})
                self.assertEqual(405, status, error)
                self.assertEqual("methodNotAllowed", error["error"]["code"])
        self.assertEqual("#123456\n", protected_file.read_text(encoding="utf-8"))

    @staticmethod
    def backup_bytes(entries):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
            archive.writestr("manifest.json", json.dumps({"app": "awtrix-ng", "backupFormat": 1}))
            for name, content in entries:
                archive.writestr(name, content)
        return output.getvalue()

    def test_restore_counts_every_skipped_entry(self):
        archive = self.backup_bytes([
            ("zz/unknown.bin", "data"),
            ("ICONS/zz.png", "not an image"),
            ("PALETTES/zz.txt", "hello world"),
        ])
        status, result = self.app.request("POST", "/api/v1/restore", archive, "application/zip")
        self.assertEqual(200, status, result)
        self.assertEqual(3, result["applied"]["skipped"], result)
        self.assertEqual(3, len(result["warnings"]), result)

    def test_malformed_restore_reports_errors_without_applying_system_config(self):
        # Keep the manifest and staged system entry intact, then truncate the next entry.
        archive = self.backup_bytes([
            ("config/system.json", '{"hostname":"must-not-be-applied"}'),
            ("unknown.txt", "unfinished-entry"),
        ])
        truncated = archive[:archive.index(b"unfinished-entry") + 3]
        app = self.app
        before = app.json("GET", "/api/v1/system?secrets=1")
        for body in (truncated,):
            status, result = app.request("POST", "/api/v1/restore", body, "application/zip")
            self.assertEqual(400, status, result)
            self.assertIs(result["ok"], False)
            self.assertTrue(result.get("error"), result)
            self.assertEqual(before, app.json("GET", "/api/v1/system?secrets=1"))
        self.assertEqual(405, app.request("GET", "/api/v1/restore")[0])

    def test_radio_stations_read_matches_persisted_commands(self):
        stations = {"stations": [{"name": "Contract", "url": "https://example.invalid/radio"}]}
        app = self.app
        app.json("PUT", "/api/v1/audio/stations", stations)
        self.assertEqual(stations, app.json("GET", "/api/v1/audio/stations"))
        self.assertEqual(405, app.request("POST", "/api/v1/audio/stations", stations)[0])
        app.stop()
        app.start()
        self.assertEqual(stations, app.json("GET", "/api/v1/audio/stations"))

    def test_stored_script_source_and_config_remain_readable_after_compile_failure(self):
        source = '# @config city text "City" default="Berlin"\nthis is invalid Berry source\n'
        app = self.app
        app.stop()
        (app.data / "SCRIPTS/brokenfixture.ax").write_text(source, encoding="utf-8")
        app.start()
        self.assertEqual(source, app.json("GET", "/api/v1/apps/script/brokenfixture"))
        config = app.json("GET", "/api/v1/apps/brokenfixture/config")
        self.assertEqual("Berlin", {field["key"]: field["value"] for field in config["fields"]}["city"])
        for path, expected, code in [
            ("/api/v1/apps/script/unknownfixture", 404, "notFound"),
            ("/api/v1/apps/unknownfixture/config", 404, "notFound"),
            ("/api/v1/apps/script/bad!", 400, "invalidName"),
            ("/api/v1/apps/bad!/config", 400, "invalidName"),
        ]:
            status, error = app.request("GET", path)
            self.assertEqual(expected, status, error)
            self.assertEqual(code, error["error"]["code"])

    @staticmethod
    def multipart_upload(app, path, filename, content):
        boundary = "awtrix-contract-boundary"
        body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
                f"filename=\"{filename}\"\r\nContent-Type: application/octet-stream\r\n\r\n").encode()
        body += content + f"\r\n--{boundary}--\r\n".encode()
        return app.request("POST", path, body, "multipart/form-data; boundary=" + boundary)

    def test_mp3_aliases_validate_names_and_preserve_rejected_replacements(self):
        content = b"ID3\x04\x00\x00\x00\x00\x00\x00"
        app = self.app
        status, result = self.multipart_upload(app, "/api/v1/audio/mp3", "contract.mp3", content)
        self.assertEqual(200, status, result)
        listing = app.json("GET", "/api/v1/audio/mp3")
        self.assertEqual([{"name": "contract.mp3", "size": len(content)}], listing["files"])
        self.assertEqual([], listing.pop("scripts"))
        self.assertEqual(listing, app.json("GET", "/api/v1/files?dir=/MP3"))
        status, error = self.multipart_upload(app, "/api/v1/audio/mp3", "contract.mp3", b"bad audio")
        self.assertEqual(415, status, error)
        self.assertEqual(content, (app.data / "MP3/contract.mp3").read_bytes())
        self.assertEqual(405, app.request("PUT", "/api/v1/audio/mp3", {})[0])
        self.assertEqual(405, app.request("GET", "/api/v1/audio/mp3/contract")[0])
        self.assertEqual(400, app.request("DELETE", "/api/v1/audio/mp3/bad!")[0])
        self.assertEqual(200, app.request("DELETE", "/api/v1/audio/mp3/contract")[0])
        self.assertEqual(404, app.request("DELETE", "/api/v1/audio/mp3/contract")[0])
        self.assertFalse((app.data / "MP3/contract.mp3").exists())

    def test_an_upload_without_a_file_part_is_a_bad_request(self):
        app = self.app
        app.install("racer", "class Racer\n def draw() end\nend\nreturn Racer()\n")
        form = b'--B\r\nContent-Disposition: form-data; name="x"\r\n\r\nv\r\n--B--\r\n'
        for path in ("/api/v1/files?dir=/ICONS", "/api/v1/audio/mp3", "/api/v1/apps/script/racer/sounds",
                     "/api/v1/restore"):
            with self.subTest(path=path):
                status, error = app.request("POST", path, form, "multipart/form-data; boundary=B")
                self.assertEqual(400, status, error)
                self.assertEqual({"code": "badRequest", "message": "no file received"}, error["error"])
        status, error = app.request("POST", "/api/v1/restore", b"", "application/zip")
        self.assertEqual((400, "badRequest"), (status, error["error"]["code"]))

    def raw_get(self, path):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.app.port}{path}", timeout=4) as response:
            return response.status, response.headers.get("Content-Type"), response.read()

    def test_script_sounds_belong_to_their_script(self):
        app = self.app
        boost = b"ID3\x04\x00\x00\x00\x00\x00\x00boost"
        lap = b"\xff\xfb\x90\x00lap"
        sounds = "/api/v1/apps/script/racer/sounds"
        folder = app.data / "SCRIPTS/racer"

        status, error = self.multipart_upload(app, sounds, "boost.mp3", boost)
        self.assertEqual((404, "notFound"), (status, error["error"]["code"]))
        self.assertFalse(folder.exists())
        self.assertEqual(404, app.request("GET", sounds)[0])

        source = "# @name AWTRIX GP\nclass Racer\n def draw() end\nend\nreturn Racer()\n"
        app.install("racer", source)
        self.assertEqual([], app.json("GET", sounds)["files"])
        # A first sound that does not fit leaves no empty folder behind.
        full = app.data / "full.bin"
        full.write_bytes(bytes(8 * 1024 * 1024))
        status, error = self.multipart_upload(app, sounds, "boost.mp3", boost)
        self.assertEqual((507, "insufficientStorage"), (status, error["error"]["code"]))
        self.assertFalse(folder.exists())
        full.unlink()
        for name, content in (("boost.mp3", boost), ("lap.mp3", lap)):
            status, result = self.multipart_upload(app, sounds, name, content)
            self.assertEqual(200, status, result)
        self.assertEqual(boost, (folder / "boost.mp3").read_bytes())

        listing = app.json("GET", sounds)
        self.assertEqual([{"name": "boost.mp3", "size": len(boost), "sha256": hashlib.sha256(boost).hexdigest()},
                          {"name": "lap.mp3", "size": len(lap), "sha256": hashlib.sha256(lap).hexdigest()}],
                         sorted(listing["files"], key=lambda item: item["name"]))
        self.assertGreater(listing["totalBytes"], 0)
        self.assertGreaterEqual(listing["usedBytes"], len(boost) + len(lap))

        # The same name again replaces the sound.
        self.assertEqual(200, self.multipart_upload(app, sounds, "lap.mp3", boost)[0])
        replaced = {item["name"]: item for item in app.json("GET", sounds)["files"]}
        self.assertEqual(hashlib.sha256(boost).hexdigest(), replaced["lap.mp3"]["sha256"])

        self.assertEqual({"playing": False, "name": "", "error": ""}, app.json("GET", "/api/v1/audio")["app"])
        grouped = app.json("GET", "/api/v1/audio/mp3")
        self.assertEqual([], grouped["files"])
        for group in grouped["scripts"]:
            group["files"].sort(key=lambda item: item["name"])
        self.assertEqual([{"name": "racer", "title": "AWTRIX GP", "orphan": False,
                           "files": [{"name": "boost.mp3", "size": len(boost)},
                                     {"name": "lap.mp3", "size": len(boost)}]}], grouped["scripts"])
        self.assertEqual((200, "audio/mpeg", boost), self.raw_get("/SCRIPTS/racer/boost.mp3"))

        app.install("racer", source + "# release 2\n")
        self.assertEqual(2, len(list(folder.iterdir())))

        self.assertEqual(405, app.request("PUT", sounds, {})[0])
        self.assertEqual(405, app.request("GET", sounds + "/boost")[0])
        self.assertEqual(405, app.request("POST", sounds, {}, headers={"X-HTTP-Method-Override": "DELETE"})[0])
        self.assertEqual(200, app.request("DELETE", sounds + "/boost")[0])
        self.assertTrue(folder.is_dir())
        self.assertEqual(200, app.request("POST", sounds + "/lap", headers={"X-HTTP-Method-Override": "DELETE"})[0])
        self.assertFalse(folder.exists())
        self.assertEqual([], app.json("GET", sounds)["files"])
        self.assertEqual([], app.json("GET", "/api/v1/audio/mp3")["scripts"])

        # Deleting the script keeps its sounds: they stay listed, marked, until they are deleted.
        self.assertEqual(200, self.multipart_upload(app, sounds, "boost.mp3", boost)[0])
        self.assertEqual(200, app.request("DELETE", "/api/v1/apps/racer")[0])
        self.assertEqual(["boost.mp3"], [p.name for p in folder.iterdir()])
        self.assertEqual(["boost.mp3"], [f["name"] for f in app.json("GET", sounds)["files"]])
        self.assertEqual([{"name": "racer", "title": "racer", "orphan": True,
                           "files": [{"name": "boost.mp3", "size": len(boost)}]}],
                         app.json("GET", "/api/v1/audio/mp3")["scripts"])
        status, error = self.multipart_upload(app, sounds, "lap.mp3", lap)
        self.assertEqual((404, "notFound"), (status, error["error"]["code"]), "no uploads without the script")
        self.assertEqual(200, app.request("DELETE", sounds)[0])
        self.assertFalse(folder.exists())
        self.assertEqual(404, app.request("GET", sounds)[0])
        self.assertEqual(200, app.request("DELETE", sounds)[0], "deleting nothing is not an error")

    def test_restore_brings_script_sounds_back_into_their_folder(self):
        app = self.app
        boost = b"ID3\x04\x00\x00\x00\x00\x00\x00boost"
        archive = self.backup_bytes([
            ("SCRIPTS/racer.ax", "class Racer\n def draw() end\nend\nreturn Racer()\n"),
            ("SCRIPTS/racer/boost.mp3", boost),
        ])
        status, result = self.multipart_upload(app, "/api/v1/restore", "backup.zip", archive)
        self.assertEqual(200, status, result)
        self.assertEqual((True, 1, 1, 0), (result["ok"], result["applied"]["scripts"],
                                           result["applied"]["mp3"], result["applied"]["skipped"]))
        folder = app.data / "SCRIPTS/racer"
        self.assertEqual(["boost.mp3"], [p.name for p in folder.iterdir()])
        self.assertEqual(boost, (folder / "boost.mp3").read_bytes())
        app.stop()
        app.start()
        self.assertEqual(["boost.mp3"],
                         [f["name"] for f in app.json("GET", "/api/v1/apps/script/racer/sounds")["files"]])

    def test_body_over_the_ceiling_gets_the_documented_error(self):
        app = self.app
        status, error = self.multipart_upload(app, "/api/v1/audio/mp3", "large.mp3",
                                              b"ID3" + bytes(8 * 1024 * 1024))
        self.assertEqual(413, status, error)
        self.assertEqual("payloadTooLarge", error["error"]["code"])
        self.assertFalse((app.data / "MP3/large.mp3").exists())
        self.assertEqual(200, app.request("GET", "/api/v1/version")[0])

    def test_empty_file_upload_is_rejected(self):
        app = self.app
        status, error = app.request("POST", "/api/v1/files", b"", "application/octet-stream")
        self.assertEqual(400, status, error)
        self.assertEqual("badRequest", error["error"]["code"])

    def upload(self, filename, content="#FF0000\n#00FF00"):
        boundary = "awtrix-contract-boundary"
        body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; "
                f"filename=\"{filename}\"\r\nContent-Type: text/plain\r\n\r\n"
                f"{content}\r\n--{boundary}--\r\n").encode()
        return self.app.request("POST", "/api/v1/files?dir=/PALETTES", body,
                                "multipart/form-data; boundary=" + boundary)

    def test_palette_upload_must_parse(self):
        status, error = self.upload("words.txt", "hello world")
        self.assertEqual(415, status, error)
        self.assertEqual("unsupportedMediaType", error["error"]["code"])
        self.assertFalse((self.app.data / "PALETTES/words.txt").exists())

    def test_file_errors_and_traversal_do_not_escape_data(self):
        self.assertEqual(200, self.upload("valid.txt")[0])
        self.assertTrue((self.app.data / "PALETTES/valid.txt").is_file())
        (self.app.data / "PALETTES/blocked.txt").mkdir()
        status, error = self.upload("blocked.txt")
        self.assertEqual(507, status, error)
        self.assertEqual("insufficientStorage", error["error"]["code"])
        status, error = self.upload("../../escape.txt")
        self.assertEqual(400, status, error)
        self.assertEqual("invalidPath", error["error"]["code"])
        self.assertFalse((self.root / "escape.txt").exists())
        outside = self.root / "outside.txt"
        outside.write_text("private sentinel")
        (self.app.data / "PALETTES/link.txt").symlink_to(outside)
        self.assertEqual(404, self.app.request("GET", "/PALETTES/link.txt")[0])
        self.assertEqual(507, self.upload("link.txt")[0])
        self.assertEqual("private sentinel", outside.read_text())

    def test_same_data_directory_is_exclusive(self):
        result = subprocess.run(self.app.command(port=free_port()), capture_output=True,
                                text=True, timeout=8)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("already in use", result.stderr)
        self.assertTrue(self.app.json("GET", "/api/v1/version")["version"])

    def test_loopback_origin_and_host_are_checked(self):
        host = f"127.0.0.1:{self.app.port}"
        for headers, expected in [
            ({"Host": host, "Origin": "https://foreign.example"}, 403),
            ({"Host": f"rebinding.example:{self.app.port}"}, 403),
            ({"Host": host, "Sec-Fetch-Site": "cross-site"}, 403),
            ({"Host": host, "Origin": "http://" + host}, 200),
            ({"Host": host}, 200),
        ]:
            with self.subTest(headers=headers):
                connection = http.client.HTTPConnection("127.0.0.1", self.app.port, timeout=3)
                try:
                    connection.request("GET", "/api/v1/version", headers=headers)
                    response = connection.getresponse()
                    body = response.read()
                    self.assertEqual(expected, response.status, body)
                    self.assertNotEqual("*", response.getheader("Access-Control-Allow-Origin"))
                    self.assertNotEqual("*", response.getheader("Access-Control-Allow-Headers"))
                    self.assertNotEqual("*", response.getheader("Access-Control-Allow-Methods"))
                finally:
                    connection.close()

    def test_sigterm_cancels_an_active_slow_http_response(self):
        received, release = threading.Event(), threading.Event()
        class SlowHandler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                try:
                    self.wfile.write(b"HTTP/1.1 200 OK\r\nX-Slow: ")
                    self.wfile.flush()
                    received.set()
                    # Deliberately incomplete headers: the body callback has not run yet.
                    while not release.wait(0.1):
                        self.wfile.write(b"x")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    pass
            def log_message(self, *_):
                pass
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), SlowHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        self.addCleanup(release.set)
        self.app.install("slowrequest", f'''# @headless true
class Slow
 var sent
 def init() self.sent = false end
 def loop()
  if self.sent return end
  self.sent = true
  http.get("http://127.0.0.1:{server.server_port}/", /body,status -> nil)
 end
end
return Slow()
''')
        self.assertTrue(received.wait(5), "script never reached slow HTTP endpoint")
        started = time.monotonic()
        self.app.stop()
        self.assertLess(time.monotonic() - started, 2.0, "SIGTERM waited for the response timeout")

    def stall_http_clients(self, app):
        host = f"Host: 127.0.0.1:{app.port}\r\n".encode()
        for request in (b"GET /api/v1/version HTTP/1.1\r\n" + host,
                        b"POST /api/v1/files HTTP/1.1\r\n" + host +
                        b"Content-Type: application/octet-stream\r\n"
                        b"Content-Length: 100000\r\n\r\npartial"):
            client = socket.create_connection(("127.0.0.1", app.port), timeout=5)
            self.addCleanup(client.close)
            client.sendall(request)
        time.sleep(0.3)

    def test_sigterm_does_not_wait_for_stalled_http_clients(self):
        app = self.app
        self.stall_http_clients(app)
        started = time.monotonic()
        app.stop()
        self.assertLess(time.monotonic() - started, 2.0, "SIGTERM waited for a stalled client")

    def test_a_request_that_stops_the_runtime_still_gets_its_answer(self):
        self.stall_http_clients(self.app)
        self.app.json("POST", "/api/v1/device/factory-reset", {})
        self.app.process.wait(timeout=2)

    def test_script_http_follows_redirects_for_reads_only(self):
        posted = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def answer(self, status, body=b"", location=None):
                self.send_response(status)
                if location:
                    self.send_header("Location", location)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                if self.path == "/moved":
                    self.answer(302, location="/target")
                else:
                    self.answer(200, b"redirected")

            def do_POST(self):
                self.rfile.read(int(self.headers.get("Content-Length", 0)))
                if self.path == "/moved":
                    self.answer(302, location="/target")
                else:
                    posted.append(self.path)
                    self.answer(200, b"posted")

            def log_message(self, *_):
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        url = f"http://127.0.0.1:{server.server_port}/moved"
        self.app.install("redirectcontract", f'''# @headless true
class Fetch
 var sent
 def init() self.sent = false end
 def loop()
  if self.sent return end
  self.sent = true
  http.get("{url}", /body,status -> shared.set("get", body == "redirected" && status == 200))
  http.post("{url}", "x", /body,status -> shared.set("post", status == 302))
 end
end
return Fetch()
''')

        def values():
            return {row["key"]: row["value"] for row in
                    self.app.json("GET", "/api/v1/scripts/shared") if row["owner"] == "redirectcontract"}
        result = eventually(values, lambda v: "get" in v and "post" in v, timeout=12)
        self.assertIs(result["get"], True)
        self.assertIs(result["post"], True)
        self.assertEqual([], posted)

    def test_http_client_and_self_signed_tls_rejection(self):
        openssl = self.tool("openssl", OPTIONS.require_tls)
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body = b"contract-ok"
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *_):
                pass
        key, cert = self.root / "key.pem", self.root / "cert.pem"
        subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
            "-keyout", str(key), "-out", str(cert)], check=True, capture_output=True, timeout=15)
        servers = []
        for tls in (False, True):
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            if tls:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.load_cert_chain(cert, key)
                server.socket = context.wrap_socket(server.socket, server_side=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            self.addCleanup(server.server_close)
            self.addCleanup(server.shutdown)
            servers.append(server)
        http_url = f"http://127.0.0.1:{servers[0].server_port}/"
        tls_url = f"https://127.0.0.1:{servers[1].server_port}/"
        source = f'''# @headless true
class Fetch
 var sent
 def init() self.sent = false end
 def loop()
  if self.sent return end
  self.sent = true
  http.get("{http_url}", /body,status -> shared.set("http", body == "contract-ok" && status == 200))
  http.get("{tls_url}", /body,status -> shared.set("tlsRejected", body == nil && status == 0))
 end
end
return Fetch()
'''
        self.app.install("fetchcontract", source)
        def values():
            return {row["key"]: row["value"] for row in
                    self.app.json("GET", "/api/v1/scripts/shared") if row["owner"] == "fetchcontract"}
        result = eventually(values, lambda v: "http" in v and "tlsRejected" in v, timeout=12)
        self.assertIs(result["http"], True)
        self.assertIs(result["tlsRejected"], True)
        # Trust only this fixture certificate for a new process. The same HTTPS endpoint must
        # now work, so an implementation that rejects every HTTPS URL cannot pass this test.
        self.app.stop()
        self.app.start({"SSL_CERT_FILE": str(cert)})
        self.app.install("fetchtrusted", source.replace('"tlsRejected", body == nil && status == 0',
            '"tlsTrusted", body == "contract-ok" && status == 200'))
        def trusted():
            return {row["key"]: row["value"] for row in
                    self.app.json("GET", "/api/v1/scripts/shared") if row["owner"] == "fetchtrusted"}
        result = eventually(trusted, lambda v: "tlsTrusted" in v, timeout=12)
        self.assertIs(result["tlsTrusted"], True)

    def test_mqtt_notify_and_screen_use_a_real_broker(self):
        self.check_mqtt_contract(self.app)

    def check_mqtt_contract(self, app):
        broker_exe = self.tool("mosquitto", OPTIONS.require_mqtt)
        original_screen = app.screen()
        port = free_port()
        config = self.root / "mosquitto.conf"
        config.write_text(f"listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
        log = open(self.root / "mosquitto.log", "wb")
        self.addCleanup(log.close)
        broker = subprocess.Popen([broker_exe, "-c", str(config)], stdout=log, stderr=log)
        def stop_broker():
            if broker.poll() is None:
                broker.terminate()
                try:
                    broker.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    broker.kill()
                    broker.wait(timeout=2)
        self.addCleanup(stop_broker)
        def broker_ready():
            if broker.poll() is not None:
                self.fail((self.root / "mosquitto.log").read_text())
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return True
        eventually(broker_ready)
        client = MqttClient(port)
        self.addCleanup(client.close)
        client.subscribe("contract/#")
        app.json("PUT", "/api/v1/system", {"mqttEnabled": True, "mqttHost": "127.0.0.1",
            "mqttPort": port, "mqttPrefix": "contract"})
        app.stop()
        app.start()
        self.assertEqual("online", client.wait_topic("contract/availability"))
        client.publish("contract/cmd/notify", json.dumps({"backgroundColor": "#13579B", "hold": True}))
        self.assertEqual({"ok": True}, json.loads(client.wait_topic("contract/cmd/notify/result")))
        eventually(app.screen, lambda s: s["pixels"][-1] == 0x13579B)
        client.publish("contract/cmd/screen/get", "")
        screen = json.loads(client.wait_topic("contract/state/screen"))
        self.assertEqual((original_screen["width"], original_screen["height"], len(original_screen["pixels"])),
                         (screen["width"], screen["height"], len(screen["pixels"])))
        self.assertEqual(0x13579B, screen["pixels"][-1])
        self.assertEqual({"ok": True}, json.loads(client.wait_topic("contract/cmd/screen/get/result")))
        app.json("DELETE", "/api/v1/notifications/active")
        client.publish("contract/cmd/notify", json.dumps({"hold": True, "layout": {
            "version": 1, "regions": [{"id": "native", "box": [0, 0,
                original_screen["width"], original_screen["height"]],
                "progress": 100, "color": 0x2468AC}]}}))
        self.assertEqual({"ok": True}, json.loads(client.wait_topic("contract/cmd/notify/result")))
        eventually(app.screen, lambda s: s["pixels"][-1] == 0x2468AC)

        rejected = {"text": "x", "layout": {"version": 1, "regions": []}}
        client.publish("contract/cmd/apps/pushed/probe", json.dumps(rejected))
        result = json.loads(client.wait_topic("contract/cmd/apps/pushed/probe/result"))
        self.assertIs(False, result["ok"])
        self.assertEqual({"source": "mqtt", "request": "contract/cmd/apps/pushed/probe",
                          "error": result["error"]},
                         json.loads(client.wait_topic("contract/event/error")))
        status, body = app.request("PUT", "/api/v1/apps/pushed/probe", rejected)
        self.assertEqual(422, status)
        self.assertEqual({"source": "http", "request": "PUT /api/v1/apps/pushed/probe",
                          "error": body["error"]},
                         json.loads(client.wait_topic("contract/event/error")))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    parser.add_argument("--require-mqtt", action="store_true")
    parser.add_argument("--require-tls", action="store_true")
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.binary = str(Path(OPTIONS.binary).resolve())
    OPTIONS.webui = str(Path(OPTIONS.webui).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
