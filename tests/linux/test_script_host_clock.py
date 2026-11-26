#!/usr/bin/env python3
"""A headless clock whose scripts run on a script host: it greets the host with a signed Hello that
names the address and port of its web server, lists the host's apps in its rotation, shows the
pictures the host sends, keeps the sounds of the host's apps, tells the host whether the rotation is
held, offers the scripts the host's modules, takes the pairing key as a Bearer token, takes the
host's acts only with it, hands its own page the key alone, passes script uploads on while the host
is there and refuses them once the host falls silent."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import Runtime, eventually

KEY = bytes(range(32))
KEY_HEX = KEY.hex()
MAGIC = b"AWRH\x01"


def name(text: str) -> bytes:
    raw = text.encode()
    return bytes([len(raw)]) + raw


def apps_packet(names, revision=1, flags=0b10):
    body = struct.pack("<HB", revision, len(names))
    for app in names:
        body += name(app) + bytes([flags]) + struct.pack("<I", 0)
    return MAGIC + b"\x03" + body


def frame_packet(app, width, height, rgb, number=1):
    head = struct.pack("<HHHHH", number, width, height, 0, height)
    pixel = bytes(((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF))
    return MAGIC + b"\x04" + head + name(app) + pixel * (width * height)


def read_name(data, at):
    length = data[at]
    return data[at + 1:at + 1 + length].decode(), at + 1 + length


class FakeHost:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.1)
        self.port = self.sock.getsockname()[1]
        self.clock = None
        self.hellos = 0
        self.state = 0
        self.web = None
        self.last_show = None

    def poll(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                data, sender = self.sock.recvfrom(1500)
            except socket.timeout:
                continue
            if not data.startswith(MAGIC):
                continue
            kind = data[5]
            if kind == 1:
                tag = data[-16:]
                expected = hmac.new(KEY, data[:-16], hashlib.sha256).digest()[:16]
                assert tag == expected, "the Hello tag does not match the pairing key"
                uid, at = read_name(data, 6)
                width, height = struct.unpack_from("<HH", data, at)
                assert (width, height) == (32, 8), (width, height)
                address, port = struct.unpack_from("<IH", data, at + 4)
                self.web = (socket.inet_ntoa(struct.pack(">I", address)), port)
                self.state = data[at + 26]
                self.clock = sender
                self.hellos += 1
            elif kind == 2:
                at = 6 + 2 + 1 + 3
                current, at = read_name(data, at)
                incoming, _ = read_name(data, at)
                self.last_show = (current, incoming)

    def send(self, packet):
        assert self.clock, "no Hello from the clock yet"
        self.sock.sendto(packet, self.clock)


class LanRuntime(Runtime):
    def command(self, port=None):
        return super().command(port) + ["--lan", "--listen", "127.0.0.1"]


def status(clock):
    return clock.json("GET", "/api/v1/scripthost")


def sound_form(filename, data):
    head = b'--s\r\nContent-Disposition: form-data; name="file"; filename="' + filename.encode() + b'"\r\n'
    return head + b"Content-Type: audio/mpeg\r\n\r\n" + data + b"\r\n--s--\r\n", "multipart/form-data; boundary=s"


def run(binary, webui):
    host = FakeHost()
    with tempfile.TemporaryDirectory(prefix="awtrix-script-host-") as root:
        clock = LanRuntime(root, binary, webui)
        clock.width, clock.height = 32, 8
        clock.start()
        try:
            assert status(clock)["mode"] == "off"
            assert "key" not in clock.json("GET", "/api/v1/scripthost?secrets=1"), "an unpaired clock has no key"
            code, _ = clock.request("PUT", "/api/v1/system",
                                    {"scriptHost": f"127.0.0.1:{host.port}", "scriptHostKey": "abc"})
            assert code == 422, "a short key is refused"
            clock.json("PUT", "/api/v1/system", {"scriptHost": f"127.0.0.1:{host.port}", "scriptHostKey": KEY_HEX})
        finally:
            clock.stop()

        clock.start()
        try:
            assert status(clock)["mode"] == "remote"
            host.poll(3)
            assert host.hellos >= 1, "no Hello from the clock"
            assert host.web == ("127.0.0.1", clock.port), f"the Hello names the clock's web server: {host.web}"
            eventually(lambda: status(clock)["link"], lambda link: link == "down")

            host.send(apps_packet(["Weather"]))
            eventually(lambda: status(clock)["link"], lambda link: link == "up")
            apps = clock.json("GET", "/api/v1/apps")
            assert any(app["name"] == "Weather" for app in apps), apps

            clock.json("PUT", "/api/v1/apps/active", {"name": "Weather"})

            def shown():
                host.poll(0.3)
                host.send(apps_packet(["Weather"]))
                if host.last_show and host.last_show[0] == "Weather":
                    host.send(frame_packet("Weather", 32, 8, 0x00FF00))
                return clock.screen()["pixels"][0]
            eventually(shown, lambda pixel: pixel == 0x00FF00, timeout=10)

            caps = clock.json("GET", "/api/v1/capabilities")
            assert all(caps.get(m) for m in ("oauth", "secret", "tcp")), caps
            assert not any(caps.get(m) for m in ("crypto", "gamepad", "gamepadRemote", "layout")), \
                "scripts on the host cannot use the modules of the clock"

            mp3 = b"ID3\x04\x00\x00\x00\x00\x00\x00boost"
            code, body = clock.request("POST", "/api/v1/apps/script/Weather/sounds", *sound_form("boost.mp3", mp3))
            assert code == 200, ("the clock keeps the sounds of a host app", code, body)
            listed = clock.json("GET", "/api/v1/apps/script/Weather/sounds")
            assert [f["name"] for f in listed["files"]] == ["boost.mp3"], listed
            group = next(s for s in clock.json("GET", "/api/v1/audio/mp3")["scripts"] if s["name"] == "Weather")
            assert not group.get("orphan"), group
            assert clock.request("GET", "/SCRIPTS/Weather/boost.mp3")[0] == 200
            code, body = clock.request("POST", "/api/v1/apps/script/Nobody/sounds", *sound_form("x.mp3", mp3))
            assert code == 200, ("a script the host has not listed yet takes sounds", code, body)
            group = next(s for s in clock.json("GET", "/api/v1/audio/mp3")["scripts"] if s["name"] == "Nobody")
            assert group.get("orphan"), ("until the host lists it", group)
            code, _ = clock.request("POST", "/api/v1/apps/script/b@d/sounds", *sound_form("x.mp3", mp3))
            assert code == 400, "an invalid script name is refused"

            shown_key = clock.json("GET", "/api/v1/scripthost?secrets=1")
            assert shown_key.get("key") == KEY_HEX, ("the own page gets the pairing key", shown_key)
            assert "key" not in status(clock), "only when it asks for secrets"
            code, _ = clock.request("GET", "/api/v1/scripthost?secrets=1",
                                    headers={"Origin": "http://evil.example", "Sec-Fetch-Site": "cross-site"})
            assert code == 403, "a page of another site does not"

            code, body = clock.request("PUT", "/api/v1/apps/script/Local", "class A def draw() end end return A()",
                                       "text/plain")
            assert code == 200, ("an upload to the clock is passed on to the host", code, body)
            assert not any(a["name"] == "Local" for a in clock.json("GET", "/api/v1/apps")), \
                "the clock itself keeps no script"
            code, body = clock.request("GET", "/api/v1/apps/Weather/config")
            assert code == 409 and body["error"]["code"] == "scriptsOnHost", (code, body)

            act = "/api/v1/scripthost/act"
            bearer = {"Authorization": "Bearer " + KEY_HEX}
            code, _ = clock.request("POST", act, {"act": "next"})
            assert code == 401, "an act needs the pairing key even without a login"
            code, body = clock.request("POST", act, {"act": "show", "app": "Weather"}, headers=bearer)
            assert code == 200 and body == {"ok": True}, (code, body)
            code, _ = clock.request("POST", act, {"act": "dance"}, headers=bearer)
            assert code == 422

            def state():
                host.poll(0.3)
                host.send(apps_packet(["Weather"]))
                return host.state
            held = 1 << 2
            assert not state() & held
            code, _ = clock.request("POST", act, {"act": "hold", "hold": True}, headers=bearer)
            assert code == 200
            eventually(state, lambda s: s & held, timeout=5)
            clock.json("PUT", "/api/v1/apps/active", {"name": "Weather"})
            eventually(state, lambda s: not s & held, timeout=5)
            code, _ = clock.request("GET", act, headers=bearer)
            assert code == 405
            code, _ = clock.request("POST", "/api/v1/scripthost")
            assert code == 405

            clock.json("PUT", "/api/v1/system", {"authEnabled": True, "authUser": "admin", "authPass": "secret"})
            code, _ = clock.request("GET", "/api/v1/scripthost")
            assert code == 401, "a login is asked for without the key"
            code, _ = clock.request("GET", "/api/v1/scripthost", headers={"Authorization": "Bearer " + KEY_HEX})
            assert code == 200, "the pairing key opens the API"
            code, _ = clock.request("GET", "/api/v1/scripthost", headers={"Authorization": "Bearer " + "0" * 64})
            assert code == 401, "another key does not"

            time.sleep(3.5)
            code, body = clock.request("GET", "/api/v1/scripthost", headers={"Authorization": "Bearer " + KEY_HEX})
            assert code == 200 and body["link"] == "down", body
            code, body = clock.request("PUT", "/api/v1/apps/script/Local", "class A def draw() end end return A()",
                                       "text/plain", headers={"Authorization": "Bearer " + KEY_HEX})
            assert code == 409 and body["error"]["code"] == "scriptsOnHost", ("no host, no upload", code, body)
        finally:
            clock.stop()
    print("PASS: a clock shows the apps of its script host, takes its acts and drops them when the host falls silent")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    args = parser.parse_args()
    run(args.binary.resolve(), args.webui.resolve())


if __name__ == "__main__":
    main()
