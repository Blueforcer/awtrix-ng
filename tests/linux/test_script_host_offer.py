#!/usr/bin/env python3
"""awtrix-host tells each paired clock where its process listens, in an offer signed with the
clock's key: a clock that still looks for its script host at an old address moves there, keeps the
new address and its link comes up."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from runtime import eventually, free_port, stop_process, wait_ready
from test_hostd import call
from test_script_host_e2e import LanRuntime


def start_host(host_binary, linux_binary, webui, state, port):
    log = open(Path(state) / "hostd.log", "ab")
    process = subprocess.Popen([str(host_binary), "--state", state, "--port", str(port), "--binary", str(linux_binary),
                                "--webui", str(webui), "--name", "offerhost"], stdout=log, stderr=log)
    logs = lambda: (Path(state) / "hostd.log").read_text(errors="replace")[-4000:]
    wait_ready(process, lambda: call("GET", f"http://127.0.0.1:{port}/api/v1/status")[0] == 200, logs)
    return process, log, logs


def run(host_binary, linux_binary, webui):
    with tempfile.TemporaryDirectory(prefix="awtrix-offer-clock-") as clock_root, \
         tempfile.TemporaryDirectory(prefix="awtrix-offer-state-") as state:
        clock = LanRuntime(clock_root, linux_binary, webui)
        clock.width, clock.height = 32, 8
        clock.start()
        port = free_port()
        base = f"http://127.0.0.1:{port}"
        host = log = None
        try:
            host, log, logs = start_host(host_binary, linux_binary, webui, state, port)
            uid = clock.json("GET", "/api/v1/device")["uid"]
            code = call("GET", base + "/api/v1/status")[1]["code"]
            status, paired = call("POST", base + "/api/v1/pair",
                                  {"uid": uid, "name": "moved", "width": 32, "height": 8, "board": "awtrixng",
                                   "clock": f"http://127.0.0.1:{clock.port}", "code": code})
            assert status == 200, paired
            stop_process(host, logs)
            log.close()
            host = None

            old = f"127.0.0.1:{free_port()}"
            clock.json("PUT", "/api/v1/system", {"scriptHost": old, "scriptHostKey": paired["key"]})
            clock.stop()
            clock.start()
            assert clock.json("GET", "/api/v1/scripthost")["link"] != "up"

            host, log, logs = start_host(host_binary, linux_binary, webui, state, port)
            eventually(lambda: clock.json("GET", "/api/v1/system")["scriptHost"],
                       lambda at: at == f"127.0.0.1:{paired['port']}", timeout=15)
            eventually(lambda: clock.json("GET", "/api/v1/scripthost")["link"], lambda link: link == "up", timeout=15)
            clock.stop()
            clock.start()
            assert clock.json("GET", "/api/v1/system")["scriptHost"] == f"127.0.0.1:{paired['port']}", \
                "the clock keeps the address it moved to"
        finally:
            if host:
                stop_process(host, logs)
                log.close()
            clock.stop()
    print("PASS: a signed offer from awtrix-host moves its clock to where the clock's process listens")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    parser.add_argument("--host-binary", type=Path)
    args = parser.parse_args()
    host_binary = args.host_binary or args.binary.parent / "awtrix-host"
    run(host_binary.resolve(), args.binary.resolve(), args.webui.resolve())


if __name__ == "__main__":
    main()
