#!/usr/bin/env python3
"""MQTT for scripts on a script host: the host takes the clock's broker, connects with its own
client id, carries the scripts' subscriptions and publishes, also ones larger than the client's
buffer, and leaves the clock's command topics alone."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from runtime import eventually, free_port
from mini_broker import Broker
from test_script_host_e2e import AUTH, KEY, HostRuntime, LanRuntime

SCRIPT = """class Echo
  def init()
    mqtt.subscribe("sh/test/in", / topic, payload -> mqtt.publish("sh/test/out", "echo:" + payload))
    var large = ""
    for i: 0..999 large = large + "0123456789" end
    mqtt.subscribe("sh/test/big", / topic, payload -> mqtt.publish("sh/test/large", large))
  end
  def draw()
    clear()
  end
end
return Echo()
"""


def run(binary, webui):
    broker = Broker()
    with tempfile.TemporaryDirectory(prefix="awtrix-shm-clock-") as clock_root, \
         tempfile.TemporaryDirectory(prefix="awtrix-shm-host-") as host_root:
        clock = LanRuntime(clock_root, binary, webui)
        clock.width, clock.height = 32, 8
        clock.start()
        host_port = free_port()
        try:
            uid = clock.json("GET", "/api/v1/device")["uid"]
            clock.json("PUT", "/api/v1/system", {"scriptHost": f"127.0.0.1:{host_port}", "scriptHostKey": KEY,
                                                 "mqttEnabled": True, "mqttHost": "127.0.0.1",
                                                 "mqttPort": broker.port, "mqttPrefix": "clock1"})
        finally:
            clock.stop()
        clock.start()
        host = HostRuntime(host_root, binary, webui, uid, clock.port)
        host.port = host_port
        try:
            host.start()
            code, body = host.request("PUT", "/api/v1/apps/script/Echo", SCRIPT, "text/plain", headers=AUTH)
            assert code == 200, (code, body)
            host_id = uid + "-sh"
            eventually(lambda: broker.subscriptions(host_id), lambda subs: "sh/test/in" in subs, timeout=15)
            assert not any(f.endswith("/cmd/#") for f in broker.subscriptions(host_id)), broker.subscriptions(host_id)

            def echoed():
                broker.publish("sh/test/in", b"hi")
                return [p for t, p in broker.published if t == "sh/test/out"]
            eventually(echoed, lambda out: b"echo:hi" in out, timeout=10)

            def large():
                broker.publish("sh/test/big", b"go")
                return [p for t, p in broker.published if t == "sh/test/large"]
            eventually(large, lambda out: out and out[0] == b"0123456789" * 1000, timeout=10)
            assert not any(t.startswith("homeassistant/") and host_id in t for t, _ in broker.published)
            assert any(t == "clock1/scripthost/availability" and p == b"online" for t, p in broker.published)
        finally:
            host.stop()
            clock.stop()
            broker.close()
    print("PASS: scripts on the host use the clock's broker with their own client id")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    args = parser.parse_args()
    run(args.binary.resolve(), args.webui.resolve())


if __name__ == "__main__":
    main()
