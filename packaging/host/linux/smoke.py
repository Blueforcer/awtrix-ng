import argparse
import json
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def request(url, body=None, key=None, timeout=5):
    headers = {"Content-Type": "application/json"} if body is not None else {}
    if key:
        headers["Authorization"] = "Bearer " + key
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers=headers, method="POST" if data else "GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def wait_for(probe, seconds, what):
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        try:
            result = probe()
            if result:
                return result
        except (OSError, ValueError) as error:
            last = error
        time.sleep(0.5)
    raise RuntimeError(f"{what} within {seconds} s ({last})")


def main():
    parser = argparse.ArgumentParser(description="Starts awtrix-host, pairs a made-up clock and checks its runner.")
    parser.add_argument("--bin", required=True, type=Path)
    parser.add_argument("--share", required=True, type=Path)
    parser.add_argument("--emulator")
    parser.add_argument("--version")
    parser.add_argument("--port", type=int, default=18300)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()

    work = Path(tempfile.mkdtemp(prefix="awtrix-smoke-"))
    state = work / "state"
    runner = args.bin / "awtrix-script-runner"
    prefix = []
    if args.emulator:
        prefix = [args.emulator]
        wrapper = work / "runner"
        wrapper.write_text(f'#!/bin/sh\nexec "{args.emulator}" "{runner}" "$@"\n')
        wrapper.chmod(0o755)
        runner = wrapper
    command = prefix + [str(args.bin / "awtrix-host"), "--state", str(state), "--port", str(args.port),
                        "--no-discovery", "--binary", str(runner), "--webui", str(args.share / "index.html"),
                        "--ca-file", str(args.share / "ca-certificates.crt")]
    log = (work / "host.log").open("wb")
    host = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    base = f"http://127.0.0.1:{args.port}"
    try:
        def status():
            code, text = request(base + "/api/v1/status", timeout=2)
            return json.loads(text) if code == 200 else None

        first = wait_for(status, args.timeout, "no status page")
        if args.version and first.get("version") != args.version:
            raise RuntimeError(f"status page reports version {first.get('version')}, not {args.version}")
        if not str(first.get("code", "")).isdigit():
            raise RuntimeError("status page from 127.0.0.1 shows no pairing code")
        page_status, page = request(base + "/")
        if page_status != 200 or "AWTRIX script host" not in page:
            raise RuntimeError("no status page at /")
        pair_status, pair_text = request(base + "/api/v1/pair", {
            "uid": "00112233aabbccdd", "name": "smoke", "clock": "http://127.0.0.1:9", "width": 32, "height": 8,
            "board": "tc001", "code": first["code"]})
        if pair_status != 200:
            raise RuntimeError(f"pairing answered {pair_status}: {pair_text}")
        paired = json.loads(pair_text)
        runner_url = f"http://127.0.0.1:{paired['port']}"

        def device():
            code, text = request(runner_url + "/api/v1/device", key=paired["key"], timeout=2)
            return json.loads(text) if code == 200 else None

        wait_for(device, args.timeout, f"runner on port {paired['port']} did not answer /api/v1/device")
        denied, _ = request(runner_url + "/api/v1/device", timeout=2)
        if denied != 401:
            raise RuntimeError(f"runner answered {denied} without the key")
        running = wait_for(lambda: [c for c in status()["clocks"] if c["uid"] == "00112233aabbccdd" and c["running"]],
                           args.timeout, "status page does not list the clock as running")
        host.send_signal(signal.SIGTERM)
        code = host.wait(timeout=30)
        if code != 0:
            raise RuntimeError(f"awtrix-host stopped with {code}")
        print(f"smoke: awtrix-host {first.get('version')} answered, clock paired on port {running[0]['port']}, "
              f"runner answered with its key and refused without, both stopped")
        return 0
    except (RuntimeError, OSError, subprocess.TimeoutExpired, KeyError, ValueError) as error:
        print(f"smoke failed: {error}", file=sys.stderr)
        log.flush()
        sys.stderr.write((work / "host.log").read_text(errors="replace")[-4000:])
        return 1
    finally:
        if host.poll() is None:
            host.kill()
            host.wait()
        log.close()
        subprocess.run(["rm", "-rf", str(work)], check=False)


if __name__ == "__main__":
    sys.exit(main())
