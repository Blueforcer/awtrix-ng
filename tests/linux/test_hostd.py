#!/usr/bin/env python3
"""awtrix-host end to end: it offers itself to a clock on the network, pairs it with the code only
this machine sees, runs the clock's process and starts it again when it dies, hands out a new key
on pairing again, and forgets the clock on unpairing. Other web pages get neither the code nor
CORS outside pairing, bodies are capped and wrong codes lock pairing."""

from __future__ import annotations

import argparse
import http.client
import json
from pathlib import Path
import socket
import stat
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from runtime import eventually, free_port, runner_beside, stop_process, wait_ready
from test_script_host_e2e import LanRuntime

SCRIPT = "class Dot def draw() clear(0x00FF00) end end return Dot()"


def call(method, url, body=None, headers=None):
    data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
    request = urllib.request.Request(url, data=data, method=method,
                                     headers={"Content-Type": "application/json", **(headers or {})})
    try:
        response = urllib.request.urlopen(request, timeout=12)
    except urllib.error.HTTPError as error:
        response = error
    except OSError:
        return 0, None
    with response:
        raw = response.read()
        try:
            return response.status, json.loads(raw)
        except ValueError:
            return response.status, raw.decode(errors="replace")


def exchange(method, url, body=None, headers=None):
    parts = urllib.parse.urlsplit(url)
    connection = http.client.HTTPConnection(parts.hostname, parts.port, timeout=12)
    try:
        connection.request(method, parts.path, body=body, headers=headers or {})
        response = connection.getresponse()
        return response.status, {k.lower(): v for k, v in response.getheaders()}, response.read()
    finally:
        connection.close()


def lan_address():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        try:
            sock.connect(("192.0.2.1", 9))
            address = sock.getsockname()[0]
        except OSError:
            return None
    return None if address.startswith("127.") or address == "0.0.0.0" else address


def run_guards(host_binary, linux_binary, webui):
    with tempfile.TemporaryDirectory(prefix="awtrix-hostd-guards-") as state:
        port = free_port()
        base = f"http://127.0.0.1:{port}"
        log = open(Path(state) / "hostd.log", "ab")
        host = subprocess.Popen([str(host_binary), "--state", state, "--port", str(port),
                                 "--binary", str(runner_beside(linux_binary)),
                                 "--webui", str(webui), "--name", "guards", "--no-discovery",
                                 "--advertise", "192.0.2.7", "--ha-ingress"], stdout=log, stderr=log)
        logs = lambda: (Path(state) / "hostd.log").read_text(errors="replace")[-4000:]
        status = lambda headers=None: exchange("GET", base + "/api/v1/status", headers=headers)
        try:
            wait_ready(host, lambda: call("GET", base + "/api/v1/status")[0] == 200, logs)
            assert len(json.loads(status()[2])["code"]) == 6, "a tool on this machine sees the code"
            assert "code" in json.loads(status({"Origin": base, "Sec-Fetch-Site": "same-origin"})[2]), \
                "so does the host's own page"
            for name, value in (("Origin", "http://evil.example"), ("Sec-Fetch-Site", "cross-site"),
                                ("Host", f"evil.example:{port}")):
                code, headers, body = status({name: value})
                assert code == 200 and "code" not in json.loads(body), (name, body)
                assert "access-control-allow-origin" not in headers, (name, headers)
            assert "code" in json.loads(status({"X-Ingress-Path": "/api/hassio_ingress/x"})[2]), \
                "the Home Assistant switch keeps the code on this machine"
            lan = lan_address()
            if lan:
                for headers in ({}, {"X-Ingress-Path": "/api/hassio_ingress/x"}):
                    assert "code" not in json.loads(exchange("GET", f"http://{lan}:{port}/api/v1/status",
                                                             headers=headers)[2]), \
                        ("another machine never sees the code", headers)
            code, _, page = exchange("GET", base + "/")
            assert code == 200 and b"fetch('api/v1/status')" in page, "the page works under any path prefix"
            code, headers, _ = exchange("OPTIONS", base + "/api/v1/status",
                                        headers={"Origin": "http://evil.example", "Access-Control-Request-Method": "GET"})
            assert code == 404 and "access-control-allow-origin" not in headers, (code, headers)
            for path in ("/api/v1/pair", "/api/v1/pair/c0ffee"):
                code, headers, _ = exchange("OPTIONS", base + path,
                                            headers={"Origin": "http://192.168.1.50",
                                                     "Access-Control-Request-Method": "DELETE",
                                                     "Access-Control-Request-Private-Network": "true"})
                assert code == 204 and headers.get("access-control-allow-origin") == "*", (path, code, headers)
                assert "Authorization" in headers.get("access-control-allow-headers", ""), headers
                assert "access-control-allow-private-network" not in headers, headers

            json_type = {"Content-Type": "application/json", "Origin": "http://192.168.1.50"}
            code, _, _ = exchange("POST", base + "/api/v1/pair", b'{"pad":"' + b"x" * 2048 + b'"}', json_type)
            assert code == 413, code
            assert call("GET", base + "/api/v1/status")[0] == 200, "a refused body leaves the host running"

            pairing = {"uid": "c0ffee", "name": "desk", "width": 32, "height": 8, "board": "awtrixng",
                       "clock": "http://127.0.0.1:9"}
            code, headers, body = exchange("POST", base + "/api/v1/pair",
                                           json.dumps({**pairing, "code": json.loads(status()[2])["code"]}).encode(),
                                           json_type)
            assert code == 200 and headers.get("access-control-allow-origin") == "*", (code, headers, body)
            paired = json.loads(body)
            assert paired["scriptHost"] == f"192.0.2.7:{paired['port']}", paired
            assert paired["url"] == "http://" + paired["scriptHost"], paired
            key_file = Path(state) / "clocks" / "c0ffee" / "key"
            assert stat.S_IMODE(key_file.stat().st_mode) == 0o600, oct(key_file.stat().st_mode)
            assert key_file.read_text().strip() == paired["key"]

            for _ in range(5):
                code, headers, _ = exchange("POST", base + "/api/v1/pair",
                                            json.dumps({**pairing, "code": "x"}).encode(), json_type)
                assert code == 403 and headers.get("access-control-allow-origin") == "*", (code, headers)
            code, headers, body = exchange("POST", base + "/api/v1/pair",
                                           json.dumps({**pairing, "code": json.loads(status()[2])["code"]}).encode(),
                                           json_type)
            assert code == 429 and json.loads(body)["error"]["code"] == "locked", (code, body)

            code, headers, _ = exchange("DELETE", base + "/api/v1/pair/c0ffee",
                                        headers={"Authorization": "Bearer " + paired["key"],
                                                 "Origin": "http://192.168.1.50"})
            assert code == 200 and headers.get("access-control-allow-origin") == "*", (code, headers)
        finally:
            stop_process(host, logs)
            log.close()
    print("PASS: awtrix-host keeps its code and CORS to itself, caps bodies, locks wrong codes and advertises")


def run(host_binary, linux_binary, webui):
    with tempfile.TemporaryDirectory(prefix="awtrix-hostd-clock-") as clock_root, \
         tempfile.TemporaryDirectory(prefix="awtrix-hostd-state-") as state:
        clock = LanRuntime(clock_root, linux_binary, webui)
        clock.width, clock.height = 32, 8
        clock.start()
        port = free_port()
        base = f"http://127.0.0.1:{port}"
        log = open(Path(state) / "hostd.log", "ab")
        host = subprocess.Popen([str(host_binary), "--state", state, "--port", str(port),
                                 "--binary", str(runner_beside(linux_binary)),
                                 "--webui", str(webui), "--name", "testhost"], stdout=log, stderr=log)
        logs = lambda: (Path(state) / "hostd.log").read_text(errors="replace")[-4000:]
        try:
            wait_ready(host, lambda: call("GET", base + "/api/v1/status")[0] == 200, logs)
            status = call("GET", base + "/api/v1/status")[1]
            assert status["clocks"] == [] and len(status["code"]) == 6, status
            uid = clock.json("GET", "/api/v1/device")["uid"]

            offers = eventually(lambda: clock.json("GET", "/api/v1/scripthost")["offers"],
                                lambda o: any(x["name"] == "testhost" and x["port"] == port for x in o), timeout=10)
            assert offers

            pairing = {"uid": uid, "name": "kitchen", "width": 32, "height": 8, "board": "awtrixng",
                       "clock": f"http://127.0.0.1:{clock.port}"}
            assert call("POST", base + "/api/v1/pair", {**pairing, "code": "000000x"})[0] == 403
            assert call("POST", base + "/api/v1/pair", {**pairing, "board": "tc002",
                                                        "code": status["code"]})[0] == 422
            assert call("POST", base + "/api/v1/pair", {**pairing, "height": 16, "code": status["code"]})[0] == 422
            code, paired = call("POST", base + "/api/v1/pair", {**pairing, "code": status["code"]})
            assert code == 200, paired
            key, url = paired["key"], paired["url"]
            auth = {"Authorization": "Bearer " + key}
            assert call("POST", base + "/api/v1/pair", {**pairing, "code": status["code"]})[0] == 403, "a code pairs once"

            assert call("GET", url + "/api/v1/version", headers=auth)[0] == 200, "the process answers once pairing does"
            clock.json("PUT", "/api/v1/system", {"scriptHost": paired["scriptHost"], "scriptHostKey": key})
            clock.stop()
            clock.start()
            code, body = call("PUT", url + "/api/v1/apps/script/Dot", SCRIPT.encode(),
                              {**auth, "Content-Type": "text/plain"})
            assert code == 200, body
            eventually(lambda: [a["name"] for a in clock.json("GET", "/api/v1/apps")], lambda n: "Dot" in n, timeout=15)

            subprocess.run(["pkill", "-f", "--", "--clock " + uid], check=False)
            eventually(lambda: call("GET", url + "/api/v1/version", headers=auth)[0], lambda c: c != 200, timeout=5)
            eventually(lambda: call("GET", url + "/api/v1/version", headers=auth)[0], lambda c: c == 200, timeout=15)
            assert any(a["name"] == "Dot" for a in call("GET", url + "/api/v1/apps", headers=auth)[1]), "the script survived"

            fresh = call("GET", base + "/api/v1/status")[1]["code"]
            code, again = call("POST", base + "/api/v1/pair", {**pairing, "code": fresh})
            assert code == 200 and again["key"] != key and again["port"] == paired["port"], again
            new_auth = {"Authorization": "Bearer " + again["key"]}
            assert call("GET", url + "/api/v1/version", headers=new_auth)[0] == 200, "so does a pairing again"
            assert call("GET", url + "/api/v1/version", headers=auth)[0] == 401, "the old key is gone"

            assert call("DELETE", base + f"/api/v1/pair/{uid}", headers=auth)[0] == 401
            assert call("DELETE", base + f"/api/v1/pair/{uid}?keep=0", headers=new_auth)[0] == 200
            assert call("GET", base + "/api/v1/status")[1]["clocks"] == []
            assert not (Path(state) / "clocks" / uid).exists()
            eventually(lambda: call("GET", url + "/api/v1/version", headers=new_auth)[0], lambda c: c != 200, timeout=8)
        finally:
            stop_process(host, logs)
            log.close()
            clock.stop()
    print("PASS: awtrix-host pairs a clock with its code, keeps its process running and forgets it on request")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    parser.add_argument("--host-binary", type=Path)
    args = parser.parse_args()
    host_binary = args.host_binary or args.binary.parent / "awtrix-host"
    run_guards(host_binary.resolve(), args.binary.resolve(), args.webui.resolve())
    run(host_binary.resolve(), args.binary.resolve(), args.webui.resolve())


if __name__ == "__main__":
    main()
