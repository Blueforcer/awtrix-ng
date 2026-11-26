#!/usr/bin/env python3
"""A clock and its script host, both awtrix-linux, with a relay between them that passes their
datagrams and calls on, records the host's calls to the clock, can add button presses and tells the
host the clock has a melody output, which the headless clock lacks. Scripts on
the host show on the clock with its settings, time zone and icons; what they do to the clock goes
there as an act the clock carries out, a sound only when the clock has an output for it; presses
they leave, switches, on-demand starts and the clock's display state reach them; a hold the clock
let go of is asked for again; an app switched off on the clock while the host was down does not
start when the host comes back; the clock names its own web server in its Hello and the host calls
it there, not where the datagrams come from, and at --remote-clock-url while the Hello names none;
the host takes only the key, from any origin, and the clock drops the host's apps when the host
stops."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import Runtime, eventually, free_port, wait_ready

KEY = bytes(range(1, 33)).hex()
FALLBACK = "127.0.0.8"
AUTH = {"Authorization": "Bearer " + KEY}
ACT = "/api/v1/scripthost/act"

SCRIPT = """class Painter
  def draw()
    var c = settings.get("textColor")
    clear(c == nil ? 0x123456 : c)
    icon("hosticon", 0, 0)
  end
end
return Painter()
"""

KEYS = """class Keys
  var color
  def init() self.color = 0x0000FF end
  def draw()
    rotation.pause()
    clear(self.color)
  end
  def on_button(b)
    if b == "left"
      sound.play({"rtttl": "beep:d=4,o=5,b=100:c"})
      return true
    end
    if b == "select"
      var c = 0
      try
        sound.play({"file": 5})
      except .. as e, m
        c = 0xFF0000
      end
      if !sound.play({"track": 7}) c = c | 0x00FF00 end
      self.color = c
      return true
    end
    return false
  end
end
return Keys()
"""

SESSION = """# @ondemand
class Session
  var loops
  def init() self.loops = 0 end
  def loop()
    self.loops = self.loops + 1
    sound.stop()
    if self.loops == 2 rotation.close() end
  end
  def draw() clear(0x00FF00) end
end
return Session()
"""

BACKGROUND = "# @headless true\nclass Bg def loop() end end\nreturn Bg()\n"
MODULE = "# @module\nvar m = module(\"tools\")\nreturn m\n"

STARTER = """class Starter
  def init() store.set("starts", store.get("starts", 0) + 1) end
  def draw() clear() end
end
return Starter()
"""

TICKER = """class Ticker
  def init() notify({"text": "hi"}) end
  def loop() sound.stop() end
  def draw() clear() end
end
return Ticker()
"""


class LanRuntime(Runtime):
    def command(self, port=None):
        return super().command(port) + ["--lan", "--listen", "127.0.0.1"]


class HostRuntime(Runtime):
    def __init__(self, root, binary, webui, clock_uid, clock_port):
        super().__init__(root, binary, webui)
        self.width, self.height = 32, 8
        self.key_file = Path(root) / "key"
        self.key_file.write_text(KEY + "\n")
        self.clock_uid, self.clock_port = clock_uid, clock_port

    def command(self, port=None):
        return super().command(port) + ["--remote", "--remote-clock", self.clock_uid,
                                        "--remote-key-file", str(self.key_file),
                                        "--remote-clock-url", f"http://{FALLBACK}:{self.clock_port}"]

    def json_auth(self, path):
        code, body = self.request("GET", path, headers=AUTH)
        assert code == 200, (path, code, body)
        return body

    def start(self, environment=None):
        self.output = open(self.root / "runtime.log", "ab")
        self.process = subprocess.Popen(self.command(), stdout=self.output, stderr=self.output)
        wait_ready(self.process, lambda: self.request("GET", "/api/v1/version", headers=AUTH)[0] == 200, self.logs)
        return self


def passing(target_port, calls, lock, rewrite=None):
    class Pass(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def forward(self):
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else b""
            headers = {k: v for k, v in self.headers.items() if k.lower() in ("authorization", "content-type")}
            connection = http.client.HTTPConnection("127.0.0.1", target_port, timeout=8)
            try:
                connection.request(self.command, self.path, body=body or None, headers=headers)
                response = connection.getresponse()
                answer, status = response.read(), response.status
                kind = response.getheader("Content-Type", "application/json")
            finally:
                connection.close()
            if rewrite:
                answer = rewrite(self.command, self.path, answer)
            with lock:
                calls.append((self.command, self.path, body, status, self.server.server_address[0]))
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(answer)))
            self.end_headers()
            self.wfile.write(answer)

        do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = forward

    return Pass


class Relay:
    def __init__(self, host_port, clock_port, address, named):
        self.host_port, self.clock_port = host_port, clock_port
        self.lock = threading.Lock()
        self.calls, self.uploads = [], []
        self.frames = 0
        self.extra = [0, 0, 0]
        self.clock = None
        self.clock_named = None
        self.servers, self.sockets = [], []
        self.to_host = ThreadingHTTPServer(("127.0.0.1", 0), passing(host_port, self.uploads, self.lock))
        self.port = self.to_host.server_address[1]
        self.facing_clock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.facing_clock.bind(("127.0.0.1", self.port))
        self.facing_host = self.named = None
        self.serve(FALLBACK)
        self.move(address, named)
        self.running = True
        threading.Thread(target=self.to_host.serve_forever, daemon=True).start()
        threading.Thread(target=self.relay, daemon=True).start()

    def serve(self, address):
        server = ThreadingHTTPServer((address, self.clock_port),
                                     passing(self.clock_port, self.calls, self.lock, self.melody_output))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.servers.append(server)

    def move(self, address, named):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((address, 0))
        self.sockets.append(sock)
        if named:
            self.serve(named)
        with self.lock:
            self.facing_host, self.named = sock, named

    def rename(self, hello):
        at = 6 + 1 + hello[6] + 4
        address, port = struct.unpack_from("<IH", hello, at)
        with self.lock:
            self.clock_named = (socket.inet_ntoa(struct.pack(">I", address)), port)
            named = self.named
        raw = bytearray(hello[:-16])
        struct.pack_into("<IH", raw, at, struct.unpack(">I", socket.inet_aton(named))[0] if named else 0,
                         self.clock_port if named else 0)
        return bytes(raw) + hmac.new(bytes.fromhex(KEY), bytes(raw), hashlib.sha256).digest()[:16]

    @staticmethod
    def melody_output(method, path, answer):
        if method != "GET" or path != "/api/v1/capabilities":
            return answer
        caps = json.loads(answer)
        caps["audio"]["rtttl"] = True
        return json.dumps(caps).encode()

    def close(self):
        self.running = False
        for server in self.servers:
            server.shutdown()
            server.server_close()
        for sock in self.sockets:
            sock.close()
        self.to_host.shutdown()
        self.to_host.server_close()
        self.facing_clock.close()

    def relay(self):
        while self.running:
            with self.lock:
                facing_host = self.facing_host
            try:
                ready, _, _ = select.select([self.facing_clock, facing_host], [], [], 0.05)
            except (OSError, ValueError):
                continue
            for sock in ready:
                try:
                    data, sender = sock.recvfrom(2048)
                except OSError:
                    continue
                if sock is self.facing_clock:
                    self.clock = sender
                    if data[5:6] == b"\x01":
                        data = self.rename(data)
                    if data[5:6] == b"\x02":
                        with self.lock:
                            raw = bytearray(data)
                            for i in range(3):
                                raw[9 + i] = (raw[9 + i] + self.extra[i]) & 0xFF
                            data = bytes(raw)
                    facing_host.sendto(data, ("127.0.0.1", self.host_port))
                elif self.clock:
                    if data[5:6] == b"\x04":
                        with self.lock:
                            self.frames += 1
                    self.facing_clock.sendto(data, self.clock)

    def press(self, button):
        with self.lock:
            self.extra[button] += 1

    def count(self):
        with self.lock:
            return len(self.calls)

    def frame_count(self):
        with self.lock:
            return self.frames

    def gets(self, path, since=0, at=None):
        with self.lock:
            return sum(1 for method, p, _, _, to in self.calls[since:]
                       if method == "GET" and p == path and at in (None, to))

    def acts(self, since=0, at=None, **match):
        with self.lock:
            calls = list(self.calls[since:])
        found = []
        for method, path, body, status, to in calls:
            if method == "POST" and path == ACT and at in (None, to):
                act = json.loads(body)
                if all(act.get(k) == v for k, v in match.items()):
                    found.append((act, status))
        return found


def quiet(count, window=1.5, timeout=10):
    deadline = time.monotonic() + timeout
    last, since = count(), time.monotonic()
    while time.monotonic() < deadline:
        time.sleep(0.1)
        now = count()
        if now != last:
            last, since = now, time.monotonic()
        elif time.monotonic() - since >= window:
            return now
    raise AssertionError(f"still moving after {timeout}s: {last}")


def pixel(clock, app, index=20):
    clock.request("PUT", "/api/v1/apps/active", {"name": app})
    clock.screen()
    return clock.screen()["pixels"][index]


def upload_icon(clock, name, color):
    fixture_path = Path(__file__).resolve().parents[2] / "scripts/test_dynamic_gif_api.py"
    spec = importlib.util.spec_from_file_location("gif_fixture", fixture_path)
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    data = fixture.animated_gif(8, 8, colors=(color, color))
    body = (b'--icon\r\nContent-Disposition: form-data; name="file"; filename="' + name.encode() + b'.gif"\r\n'
            b'Content-Type: image/gif\r\n\r\n' + data + b'\r\n--icon--\r\n')
    status, response = clock.request("POST", "/api/v1/files?dir=/ICONS", body, "multipart/form-data; boundary=icon")
    assert status == 200, (status, response)


def exchange(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
    try:
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()
    finally:
        connection.close()


def check_web_ui_routes(host, clock_origin):
    page = {"Origin": clock_origin, "Sec-Fetch-Site": "cross-site"}
    for path in ("/api/v1/apps", "/api/v1/version", "/"):
        assert host.request("GET", path)[0] == 401, path
    assert host.request("GET", "/api/v1/version", headers={"Authorization": "Bearer " + "0" * 64})[0] == 401
    status, headers, _ = exchange(host.port, "OPTIONS", "/api/v1/apps/script/Painter",
                                  headers={**page, "Access-Control-Request-Method": "PUT",
                                           "Access-Control-Request-Headers": "authorization,content-type"})
    assert status == 204 and headers.get("access-control-allow-origin") == "*", (status, headers)
    assert "Authorization" in headers.get("access-control-allow-headers", ""), headers
    sound = b"ID3\x04\x00\x00\x00\x00\x00\x00boost"
    upload = (b'--s\r\nContent-Disposition: form-data; name="file"; filename="boost.mp3"\r\n'
              b'Content-Type: application/octet-stream\r\n\r\n' + sound + b'\r\n--s--\r\n')
    calls = [("POST", "/api/v1/apps/script/Painter/sounds", upload, "multipart/form-data; boundary=s", 200),
             ("GET", "/api/v1/files?dir=/SCRIPTS", None, None, 200),
             ("GET", "/api/v1/audio/mp3", None, None, 200),
             ("GET", "/SCRIPTS/Painter/boost.mp3", None, None, 200),
             ("GET", "/api/v1/apps/script/Painter", None, None, 200),
             ("DELETE", "/api/v1/apps/script/Painter/sounds/boost", None, None, 200)]
    for method, path, body, kind, expected in calls:
        headers = {**page, **AUTH, **({"Content-Type": kind} if kind else {})}
        status, answer_headers, answer = exchange(host.port, method, path, body, headers)
        assert status == expected, (method, path, status, answer[:200])
        assert answer_headers.get("access-control-allow-origin") == "*", (method, path, answer_headers)
        if path.endswith(".mp3"):
            assert answer == sound
    for method, path, body in (("POST", "/api/v1/restore", b"{}"), ("PUT", "/api/v1/secrets/Painter", b"{}"),
                               ("POST", "/api/v1/oauth/Painter", b"{}")):
        status, _, answer = exchange(host.port, method, path, body, {**page, **AUTH, "Content-Type": "application/json"})
        assert status != 403 and b"forbiddenOrigin" not in answer, (path, status, answer)


def run(binary, webui):
    with tempfile.TemporaryDirectory(prefix="awtrix-sh-clock-") as clock_root, \
         tempfile.TemporaryDirectory(prefix="awtrix-sh-host-") as host_root:
        clock = LanRuntime(clock_root, binary, webui)
        clock.width, clock.height = 32, 8
        clock.start()
        host_port = free_port()
        relay = Relay(host_port, clock.port, "127.0.0.3", "127.0.0.4")
        try:
            uid = clock.json("GET", "/api/v1/device")["uid"]
            clock.json("PUT", "/api/v1/system", {"scriptHost": f"127.0.0.1:{relay.port}", "scriptHostKey": KEY})
        finally:
            clock.stop()
        clock.start()
        host = HostRuntime(host_root, binary, webui, uid, clock.port)
        host.port = host_port
        try:
            host.start()
        except BaseException:
            clock.stop()
            relay.close()
            raise
        try:
            code, body = host.request("PUT", "/api/v1/apps/script/Painter", SCRIPT, "text/plain", headers=AUTH)
            assert code == 200, (code, body)
            caps = host.request("GET", "/api/v1/capabilities", headers=AUTH)[1]
            assert "tcp" in caps and "oauth" in caps and "ble" not in caps and "gamepad" not in caps, caps
            clock_caps = clock.json("GET", "/api/v1/capabilities")
            assert all(clock_caps.get(module) for module in ("oauth", "secret", "tcp")), clock_caps
            assert not any(clock_caps.get(module) for module in ("crypto", "gamepad", "gamepadRemote")), clock_caps
            check_web_ui_routes(host, f"http://127.0.0.1:{clock.port}")

            eventually(lambda: clock.json("GET", "/api/v1/scripthost")["link"], lambda link: link == "up", timeout=10)
            eventually(lambda: [a["name"] for a in clock.json("GET", "/api/v1/apps")],
                       lambda names: "Painter" in names, timeout=10)
            eventually(lambda: relay.gets("/api/v1/settings") and relay.gets("/api/v1/capabilities"), timeout=5)

            clock.json("PATCH", "/api/v1/settings", {"textColor": [255, 0, 0]})
            eventually(lambda: pixel(clock, "Painter"), lambda p: p == 0xFF0000, timeout=10)
            mark = relay.count()
            clock.json("PATCH", "/api/v1/settings", {"textColor": [0, 0, 255]})
            eventually(lambda: pixel(clock, "Painter"), lambda p: p == 0x0000FF, timeout=10)
            assert relay.gets("/api/v1/settings", mark) and not relay.gets("/api/v1/system?secrets=1", mark), \
                "a settings change fetches the settings alone"
            assert not any(host.json_auth("/api/v1/display/screen")["pixels"]), \
                "the host draws nothing of its own rotation"

            code, body = clock.request("PUT", "/api/v1/apps/script/ViaClock", "class V def draw() clear() end end return V()",
                                       "text/plain")
            assert code == 200, (code, body)
            eventually(lambda: [a["name"] for a in host.json_auth("/api/v1/apps")],
                       lambda names: "ViaClock" in names, timeout=10)

            upload_icon(clock, "hosticon", 0xFF00FF)
            eventually(lambda: pixel(clock, "Painter", 0), lambda p: p == 0xFF00FF, timeout=10)
            upload_icon(clock, "hosticon", 0x00FF00)
            eventually(lambda: pixel(clock, "Painter", 0), lambda p: p == 0x00FF00, timeout=10)

            mark = relay.count()
            clock.json("PUT", "/api/v1/system", {"tz": "EST5EDT,M3.2.0,M11.1.0", "tzName": "America/New_York"})
            eventually(lambda: host.request("GET", "/api/v1/system", headers=AUTH)[1].get("tzName"),
                       lambda name: name == "America/New_York", timeout=10)
            assert relay.gets("/api/v1/system?secrets=1", mark) and not relay.gets("/api/v1/settings", mark), \
                "a system change fetches the system alone"

            check_acts(clock, host, relay)
            check_display(clock, relay)
            check_listing_and_session(clock, host, relay)
            check_switches(clock, host, relay)
            check_restart(clock, host)

            assert relay.clock_named == ("127.0.0.1", clock.port), relay.clock_named
            assert relay.gets("/api/v1/settings", 0, "127.0.0.4") and relay.acts(0, "127.0.0.4", act="press"), \
                "the host calls the address the Hello names, not the relay the datagrams come from"

            mark = relay.count()
            relay.move("127.0.0.6", "127.0.0.5")
            for path in ("/api/v1/settings", "/api/v1/system?secrets=1", "/api/v1/capabilities"):
                eventually(lambda: relay.gets(path, mark, "127.0.0.5"), timeout=10)
            eventually(lambda: relay.acts(mark, "127.0.0.5", script="Ticker"), timeout=10)
            moved = relay.count()
            time.sleep(1.5)
            assert not relay.acts(moved, "127.0.0.4"), "a clock that names a new address is not called at the old one"

            mark = relay.count()
            relay.move("127.0.0.6", None)
            eventually(lambda: relay.acts(mark, FALLBACK, script="Ticker"), timeout=10)
        except BaseException:
            host.stop()
            clock.stop()
            relay.close()
            raise
        host.stop()
        time.sleep(3.5)
        try:
            assert clock.json("GET", "/api/v1/scripthost")["link"] == "down"
        finally:
            clock.stop()
            relay.close()
    print("PASS: scripts on a script host drive their clock through acts, follow it and keep to its switches")


def shown(clock):
    return clock.screen()["pixels"][20]


def check_acts(clock, host, relay):
    code, body = host.request("PUT", "/api/v1/apps/script/Keys", KEYS, "text/plain", headers=AUTH)
    assert code == 200, (code, body)
    eventually(lambda: [a["name"] for a in clock.json("GET", "/api/v1/apps")], lambda names: "Keys" in names,
               timeout=10)
    clock.json("PUT", "/api/v1/apps/active", {"name": "Keys"})
    eventually(lambda: relay.acts(act="hold", hold=True), timeout=5)
    eventually(lambda: shown(clock), lambda p: p == 0x0000FF, timeout=5)
    quiet(relay.count)
    assert len(relay.acts(act="hold")) <= 2, "a hold asked for every frame does not go out every frame"

    relay.press(0)
    eventually(lambda: relay.acts(act="restart", app="Keys"), timeout=5)
    played = eventually(lambda: relay.acts(act="sound", script="Keys", action="play"), timeout=5)
    assert played[0][0]["sound"] == {"rtttl": "beep:d=4,o=5,b=100:c"}, played
    restarted = relay.acts(act="restart", app="Keys")
    assert restarted[0][1] == 200, restarted

    relay.press(1)
    eventually(lambda: shown(clock), lambda p: p == 0xFFFF00, timeout=5)
    assert len(relay.acts(act="sound", action="play")) == 1, \
        "a sound the clock would refuse is refused here, one it has no output for does not go out"

    holds = len(relay.acts(act="hold", hold=True))
    clock.json("PUT", "/api/v1/apps/active", {"name": "Keys"})
    eventually(lambda: len(relay.acts(act="hold", hold=True)), lambda n: n > holds, timeout=6)
    quiet(relay.count)
    assert len(relay.acts(act="hold", hold=True)) <= holds + 2, "a hold the clock let go of is asked for again, once"

    assert clock.json("GET", "/api/v1/device")["currentApp"] == "Keys"
    relay.press(2)
    pressed = eventually(lambda: relay.acts(act="press", app="Keys", button=2), timeout=5)
    assert pressed[0][1] == 200, pressed
    eventually(lambda: clock.json("GET", "/api/v1/device")["currentApp"], lambda app: app != "Keys", timeout=5)


def check_display(clock, relay):
    pixel(clock, "Painter")
    clock.json("PATCH", "/api/v1/display", {"power": False})
    quiet(relay.frame_count, 1.5, 10)
    clock.json("PATCH", "/api/v1/display", {"power": True})
    before = relay.frame_count()
    eventually(relay.frame_count, lambda frames: frames > before, timeout=6)


def check_listing_and_session(clock, host, relay):
    for name, source in (("Session", SESSION), ("Bg", BACKGROUND), ("Tools", MODULE)):
        code, body = host.request("PUT", "/api/v1/apps/script/" + name, source, "text/plain", headers=AUTH)
        assert code == 200, (name, code, body)
    apps = eventually(lambda: {a["name"]: a for a in clock.json("GET", "/api/v1/apps")},
                      lambda apps: "Session" in apps and "Bg" in apps, timeout=10)
    assert not apps["Bg"]["inLoop"] and not apps["Session"]["inLoop"] and "Tools" not in apps, apps

    pixel(clock, "Painter")
    mark = relay.count()
    clock.json("PUT", "/api/v1/apps/active", {"name": "Session"})
    eventually(lambda: shown(clock), lambda p: p == 0x00FF00, timeout=10)
    eventually(lambda: relay.acts(mark, act="endSession", id="Session"), timeout=10)
    eventually(lambda: clock.json("GET", "/api/v1/device")["currentApp"], lambda app: app != "Session", timeout=5)
    assert relay.acts(mark, script="Session"), "the started app runs here"
    quiet(lambda: len(relay.acts(mark, script="Session")), 2.5, 15)


def check_switches(clock, host, relay):
    code, body = host.request("PUT", "/api/v1/apps/script/Ticker", TICKER, "text/plain", headers=AUTH)
    assert code == 200, (code, body)
    eventually(lambda: relay.acts(act="notify", script="Ticker"), timeout=10)
    eventually(lambda: relay.acts(act="sound", script="Ticker", action="stop"), timeout=10)
    clock.json("PUT", "/api/v1/apps/order", {"disabled": ["Ticker"]})
    quiet(lambda: len(relay.acts(script="Ticker")), 2.5, 10)
    assert not next(a for a in host.json_auth("/api/v1/apps") if a["name"] == "Ticker")["enabled"]
    before = len(relay.acts(script="Ticker"))
    clock.json("PUT", "/api/v1/apps/order", {"disabled": []})
    eventually(lambda: len(relay.acts(script="Ticker")), lambda n: n > before, timeout=10)


def check_restart(clock, host):
    code, body = host.request("PUT", "/api/v1/apps/script/Starter", STARTER, "text/plain", headers=AUTH)
    assert code == 200, (code, body)
    starts = lambda: host.json_auth("/api/v1/apps/Starter/data").get("starts")
    eventually(starts, lambda n: n == 1, timeout=10)
    eventually(lambda: [a["name"] for a in clock.json("GET", "/api/v1/apps")], lambda names: "Starter" in names,
               timeout=10)
    host.stop()
    clock.json("PUT", "/api/v1/apps/order", {"disabled": ["Starter"]})
    host.start()
    eventually(lambda: next(a for a in host.json_auth("/api/v1/apps") if a["name"] == "Starter")["enabled"],
               lambda enabled: enabled is False, timeout=10)
    time.sleep(1)
    assert starts() == 1, "an app switched off on the clock while the host was down does not start"
    clock.json("PUT", "/api/v1/apps/order", {"disabled": []})
    eventually(starts, lambda n: n == 2, timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    args = parser.parse_args()
    run(args.binary.resolve(), args.webui.resolve())


if __name__ == "__main__":
    main()
