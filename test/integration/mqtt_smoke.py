#!/usr/bin/env python3
"""Real-broker MQTT contract check; run only against a disposable broker and simulator.

Requires paho-mqtt==2.1.0. Example inside the build container on a Podman network:
  python test/integration/mqtt_smoke.py --broker mqtt-smoke-broker
The simulator uses a temporary data directory; all command writes are undone.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request

import paho.mqtt.client as mqtt


class Observer:
    def __init__(self, broker):
        self.messages = []
        self.condition = threading.Condition()
        self.ready = threading.Event()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = self.on_connect
        self.client.on_subscribe = lambda *args: self.ready.set()
        self.client.on_message = self.on_message
        self.client.connect(broker, 1883, 10)
        self.client.loop_start()
        assert self.ready.wait(10), "broker subscription timed out"

    def on_connect(self, client, userdata, flags, reason, properties):
        assert reason == 0, reason
        client.subscribe([("homeassistant/#", 0), ("mqttsmoke/#", 0)])

    def on_message(self, client, userdata, message):
        with self.condition:
            self.messages.append({"topic": message.topic,
                                  "payload": message.payload.decode(),
                                  "retain": message.retain})
            self.condition.notify_all()

    def wait(self, topic, payload=None, start=0, timeout=15):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for message in self.messages[start:]:
                    if message["topic"] == topic and (payload is None or message["payload"] == payload):
                        return message
                remaining = deadline - time.monotonic()
                assert remaining > 0, f"missing {topic}: {payload}"
                self.condition.wait(remaining)

    def wait_retained(self, topic, timeout=15):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for message in self.messages:
                    if message["topic"] == topic and message["retain"]:
                        return message
                remaining = deadline - time.monotonic()
                assert remaining > 0, f"no retained copy of {topic}"
                self.condition.wait(remaining)

    def command(self, suffix, payload):
        start = len(self.messages)
        topic = "mqttsmoke/cmd/" + suffix
        self.client.publish(topic, payload).wait_for_publish(5)
        reply = self.wait(topic + "/result", start=start)
        assert json.loads(reply["payload"]) == {"ok": True}, reply
        assert not reply["retain"], reply
        return reply

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker", required=True)
    parser.add_argument("--program", default=".pio/build/native_sim/program")
    parser.add_argument("--report", default=".pio/mqtt-smoke-report.json")
    args = parser.parse_args()
    program = str(Path(args.program).resolve())
    port = 18087
    base = f"http://127.0.0.1:{port}"
    report = {"commands": [], "checks": []}
    observer = Observer(args.broker)
    sim = None

    def http(path, body=None):
        request = urllib.request.Request(base + path,
            data=None if body is None else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=2) as response:
            return json.load(response)

    try:
        with tempfile.TemporaryDirectory(prefix="awtrix-mqtt-") as data:
            Path(data, "device.json").write_text(json.dumps({
                "mqttEnabled": True, "mqttHost": args.broker, "mqttPort": 1883,
                "mqttPrefix": "mqttsmoke", "haDiscovery": True, "hostname": "mqtt-smoke-sim",
                "statsInterval": 1, "panelWidth": 53, "panelHeight": 11, "panels": 1,
                "scriptingEnabled": False}))
            with tempfile.TemporaryFile(mode="w+") as log:
                def start_sim():
                    return subprocess.Popen([program, "--no-matrix", "--port", str(port),
                                             "--data", data], stdout=log, stderr=subprocess.STDOUT)
                sim = start_sim()
                observer.wait("mqttsmoke/availability", "online", timeout=30)
                deadline = time.monotonic() + 15
                while True:
                    try:
                        http("/api/v1/device")
                        break
                    except (urllib.error.URLError, TimeoutError):
                        assert time.monotonic() < deadline, "simulator HTTP not ready"
                        time.sleep(0.1)

                discovery = observer.wait("homeassistant/device/simulator/config")
                report["discovery"] = json.loads(discovery["payload"])
                doc = report["discovery"]
                assert doc["dev"]["mf"] == "Blueforcer"
                assert doc["dev"]["mdl"] == "AWTRIX NG"
                assert doc["cmps"]["prefix"]["name"] == "MQTT prefix"
                assert doc["cmps"]["prefix"]["stat_t"] == "~/state/prefix"
                for key, name in zip(("btnl", "btnm", "btnr"), ("left", "select", "right")):
                    comp = doc["cmps"][key]
                    assert comp["name"] == "Button " + name and comp["p"] == "binary_sensor"
                    assert comp["stat_t"] == "~/state/buttons/" + name
                    assert comp["pl_on"] == "1" and comp["pl_off"] == "0"
                report["checks"].append("Blueforcer, MQTT prefix and stock button discovery")

                before = http("/api/v1/device")["messageCount"]
                report["commands"].append(observer.command("notify", '{"name":"smoke-note","text":"MQTT smoke","hold":true}'))
                report["commands"].append(observer.command("apps/pushed/test", '{"text":"Pico MQTT"}'))
                assert http("/api/v1/device")["messageCount"] == before + 2
                screen_start = len(observer.messages)
                observer.command("screen/get", "")
                screen = observer.wait("mqttsmoke/state/screen", start=screen_start)
                assert not screen["retain"]
                report["checks"].append("notify/app results, inbound-only count and non-retained screen")

                report["button_edges"] = []
                for name in ("left", "select", "right"):
                    start = len(observer.messages)
                    http("/sim/button/" + name, {"durationMs": 200})
                    topic = "mqttsmoke/state/buttons/" + name
                    press = observer.wait(topic, "1", start=start)
                    after = next(i for i, m in enumerate(observer.messages) if m is press) + 1
                    report["button_edges"].append(press)
                    report["button_edges"].append(observer.wait(topic, "0", start=after))
                report["checks"].append("left/select/right press and release edges")

                # Retain is observed on a NEW subscription, not the live delivery.
                fresh = Observer(args.broker)
                try:
                    topics = ["availability", "state/capabilities", "state/prefix", "state/device",
                              "state/settings", "state/audio", "state/apps/active"]
                    topics += ["state/buttons/" + name for name in ("left", "select", "right")]
                    retained = [fresh.wait_retained("mqttsmoke/" + topic) for topic in topics]
                    retained.append(fresh.wait_retained("homeassistant/device/simulator/config"))
                    assert all(message["retain"] for message in retained), retained
                    time.sleep(0.3)
                    assert not any(m["topic"].endswith("/result") or m["topic"] == "mqttsmoke/state/screen"
                                   for m in fresh.messages)
                    report["retained_topics"] = [m["topic"] for m in retained]
                finally:
                    fresh.close()
                report["checks"].append("state/discovery retained; replies and screen not retained")

                # Undo command writes even though the whole simulator store is disposable.
                observer.command("apps/pushed/test", "")
                observer.command("notify/dismiss", "")
                report["checks"].append("test app and notification removed")
                start = len(observer.messages)
                sim.kill()  # no MQTT DISCONNECT: the broker must deliver the will
                sim.wait(timeout=5)
                report["will"] = observer.wait("mqttsmoke/availability", "offline", start=start)
                fresh = Observer(args.broker)
                try:
                    assert fresh.wait("mqttsmoke/availability", "offline")["retain"]
                finally:
                    fresh.close()
                report["checks"].append("abrupt exit publishes retained offline LWT")

                start = len(observer.messages)
                sim = start_sim()
                observer.wait("mqttsmoke/availability", "online", start=start, timeout=30)
                observer.wait("homeassistant/device/simulator/config", start=start)
                observer.wait("mqttsmoke/state/prefix", "mqttsmoke", start=start)
                report["checks"].append("restart reannounces online, discovery and prefix")
                sim.terminate()
                sim.wait(timeout=5)
                sim = None
                log.seek(0)
                report["simulator_log"] = log.read()
        Path(args.report).write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
        print(f"PASS MQTT integration: {len(report['checks'])} contract checks")
    finally:
        if sim is not None and sim.poll() is None:
            sim.kill()
            sim.wait(timeout=5)
        observer.close()


if __name__ == "__main__":
    main()
