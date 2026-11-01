#!/usr/bin/env python3
from __future__ import annotations
import sys

import argparse
import http.server
import json
from pathlib import Path
import stat
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import Runtime, eventually

VALUE = "sk-CONTRACT/4711"
ENCODED = "sk-CONTRACT%2F4711"
HEADER = '# @secret key label="API key" help="Your key"\n# @headless true\n'
PUBLIC = "http://192.0.2.1"


class Recorder(http.server.BaseHTTPRequestHandler):
    seen = []

    def do_GET(self):
        Recorder.seen.append((self.path, self.headers.get("X-Key", "")))
        body = b"ok"
        if self.path.startswith("/moved"):
            self.send_response(302)
            self.send_header("Location", "/after")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def caller(port):
    return f'''import secret
class App
 var sent
 def init() self.sent = false end
 def loop()
  if self.sent return end
  self.sent = true
  shared.set("has", secret.has("key"))
  var filled = {{"headers": {{"X-Key": "{{secret:key}}"}}}}
  http.get("{PUBLIC}/public", /b,s -> shared.set("public", s), filled)
  http.get("http://127.0.0.1:{port}/header", /b,s -> shared.set("header", s), filled)
  http.get("http://127.0.0.1:{port}/query?k={{secret:key}}", /b,s -> shared.set("query", s))
  http.get("http://127.0.0.1:{port}/moved", /b,s -> shared.set("moved", s), filled)
  http.get("http://127.0.0.1:{port}/plain", /b,s -> shared.set("plain", s), {{"headers": {{"X-Key": "plain"}}}})
 end
end
return App()
'''


def check(condition, what):
    if not condition:
        raise AssertionError(what)


def holders(folder):
    return [f for f in folder.rglob("*") if f.is_file() and VALUE.encode() in f.read_bytes()]


def key_set(runtime):
    return runtime.json("GET", "/api/v1/secrets/Keys")["keys"][0]["set"]


def run(runtime, server_port):
    own = {"Origin": f"http://127.0.0.1:{runtime.port}", "X-Awtrix-Secret": "1"}
    private = runtime.root / "script-secrets"
    check(runtime.json("GET", "/api/v1/capabilities").get("secret") is True, "capabilities report secret")

    runtime.install("Keys", HEADER + "class App def loop() end end\nreturn App()\n")
    check(runtime.json("GET", "/api/v1/secrets") == {"apps": ["Keys"]}, "the listing names the app")
    check(runtime.request("PUT", "/api/v1/secrets/Keys", {"key": VALUE})[0] == 403, "no marker: refused")
    status, answer = runtime.request("PUT", "/api/v1/secrets/Keys", {"key": VALUE}, headers=own)
    check(status == 200 and key_set(runtime), f"the own page sets the key: {status} {answer}")

    runtime.install("Keys", HEADER + caller(server_port))
    check(key_set(runtime), "new code under the same @secret line keeps the key")
    shared = lambda: {row["key"]: row["value"] for row in runtime.json("GET", "/api/v1/scripts/shared")
                      if row["owner"] == "Keys"}
    values = eventually(shared, lambda v: len(v) == 6, timeout=40)
    check(values == {"has": True, "public": 0, "header": 200, "query": 200, "moved": 302, "plain": 200},
          f"what the script saw: {values}")
    check(sorted(Recorder.seen) == sorted([("/header", VALUE), (f"/query?k={ENCODED}", ""), ("/moved", VALUE),
                                           ("/plain", "plain")]),
          f"plain http carries the key to a private target only, and no redirect follows it: {Recorder.seen}")

    for path in ("/api/v1/secrets", "/api/v1/secrets/Keys", "/api/v1/apps/script/Keys", "/api/v1/apps/Keys/config",
                 "/api/v1/apps/Keys/data", "/api/v1/files?dir=/", "/api/v1/files?dir=/SCRIPTS",
                 "/api/v1/system?secrets=1", "/api/v1/scripts/shared", "/api/v1/logs"):
        answer = json.dumps(runtime.request("GET", path)[1])
        check(VALUE not in answer and ENCODED not in answer, f"the key leaked through {path}")
    check(not holders(runtime.data), "no file in the data directory holds the key")
    files = holders(private)
    check(len(files) == 1 and stat.S_IMODE(files[0].stat().st_mode) == 0o600, "one private 0600 file holds it")
    check(stat.S_IMODE(private.stat().st_mode) == 0o700, "the private directory is 0700")

    runtime.install("Keys", HEADER.replace("API key", "Key").replace("Your key", "Yours") + caller(server_port))
    check(key_set(runtime), "a new label or help keeps the key")
    runtime.install("Keys", HEADER.replace("help=", "hosts=192.168.1.5 help=") + caller(server_port))
    check(not key_set(runtime) and not holders(private), "changed hosts= erase the key")

    check(runtime.request("PUT", "/api/v1/secrets/Keys", {"key": VALUE}, headers=own)[0] == 200, "set again")
    check(runtime.request("DELETE", "/api/v1/secrets/Keys/key", headers=own)[0] == 200 and not key_set(runtime),
          "DELETE erases the key")
    check(runtime.request("PUT", "/api/v1/secrets/Keys", {"key": VALUE}, headers=own)[0] == 200, "set again")
    check(runtime.request("DELETE", "/api/v1/apps/Keys")[0] == 200, "the app is deleted")
    check(runtime.request("GET", "/api/v1/secrets/Keys")[0] == 404 and not holders(private),
          "a deleted app's keys are erased")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--webui", required=True)
    args = parser.parse_args()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Recorder)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    with tempfile.TemporaryDirectory(prefix="awtrix-secret-") as temp:
        runtime = Runtime(temp, args.binary, args.webui).start()
        try:
            run(runtime, server.server_port)
        finally:
            runtime.stop()
            server.shutdown()
    print("ok")


if __name__ == "__main__":
    main()
