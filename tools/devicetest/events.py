import argparse
import json
from pathlib import Path
import time
import uuid

from run import Api


def checked(api, method, path, body=None, ctype=None):
    status, data = api.call(method, path, body, ctype)
    if status != 200:
        raise RuntimeError(f"{method} {path}: {status} {data}")
    return data


def values(api, owner):
    return {row["key"]: row["value"]
            for row in checked(api, "GET", "/api/v1/scripts/shared")
            if row["owner"] == owner}


def wait_value(api, owner, key, expected, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = values(api, owner)
        if result.get(key) == expected:
            return result
        apps = checked(api, "GET", "/api/v1/apps")
        errors = [a.get("error") for a in apps if a["name"] == owner and a.get("error")]
        if errors:
            raise RuntimeError(str(errors))
        time.sleep(0.1)
    raise AssertionError(f"{owner}.{key}: expected {expected!r}, got {result}")


TIMER_SOURCE = """
# @headless true
class Probe
  var repeat_id, count, last, minimum, maximum
  def setup()
    for ms : [-1, 0, 24, 86400001, 25.5, '25', nil]
      assert(timer.after(ms, / -> nil) == nil, 'delay validation')
    end
    assert(timer.after(25, nil) == nil, 'callback validation')
    var ids = []
    for n : 1..8
      var id = timer.after(60000, / -> nil)
      assert(id != nil, 'timer slot')
      ids.push(id)
    end
    assert(timer.after(25, / -> nil) == nil, 'timer limit')
    for id : ids assert(timer.cancel(id), 'cancel') end
    assert(!timer.cancel(ids[0]) && !timer.cancel(nil), 'cancel twice')
    shared.set('validation', true)
    var cancelled = timer.after(25, / -> shared.set('cancelled_fired', true))
    assert(timer.cancel(cancelled), 'cancel pending')
    self.count = 0
    self.last = now_ms()
    self.minimum = 86400000
    self.maximum = 0
    self.repeat_id = timer.every(50, / -> self.step())
    assert(self.repeat_id != nil, 'repeat start')
    assert(timer.after(125, / -> shared.set('once', true)) != nil, 'once start')
  end
  def step()
    var elapsed = now_ms() - self.last
    self.last = now_ms()
    self.minimum = min(self.minimum, elapsed)
    self.maximum = max(self.maximum, elapsed)
    self.count += 1
    shared.set('count', self.count)
    if self.count == 10
      assert(timer.cancel(self.repeat_id), 'self cancel')
      shared.set('minimum_ms', self.minimum)
      shared.set('maximum_ms', self.maximum)
      shared.set('done', true)
    end
  end
end
return Probe()
"""

BUTTON_SOURCE = """
class Buttons
  var events
  def init() self.events = '' end
  def record(value)
    self.events += value + ','
    shared.set('events', self.events)
    log(str(now_ms()) + ' ' + value)
  end
  def on_button_event(btn, event)
    self.record(btn + ':' + event)
    return btn == 'select'
  end
  def on_button(btn)
    self.record('legacy:' + btn)
    return btn == 'left'
  end
  def duration() return 60000 end
  def draw() text(0, 6, 'BUTTON') end
end
return Buttons()
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--auth")
    parser.add_argument("--sim-buttons", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    api = Api(args.host, args.auth)
    before = checked(api, "GET", "/api/v1/device")
    apps = checked(api, "GET", "/api/v1/apps")
    order = [a["name"] for a in sorted(apps, key=lambda a: a.get("slot") or 0)
             if a.get("slot") is not None]
    disabled = [a["name"] for a in apps if a.get("enabled") is False]
    suffix = uuid.uuid4().hex[:6]
    timer_name, button_name = "tt" + suffix, "tb" + suffix
    installed = []
    report = {"before": before, "checks": []}

    def passed(message):
        report["checks"].append(message)
        print("PASS", message, flush=True)

    def install(name, source):
        if name not in installed:
            installed.append(name)
        checked(api, "PUT", "/api/v1/apps/script/" + name, source, "text/plain")

    def set_order(names, off):
        checked(api, "PUT", "/api/v1/apps/order",
                json.dumps({"order": names, "disabled": off}), "application/json")

    def show(name):
        checked(api, "PUT", "/api/v1/apps/active", json.dumps({"name": name, "fast": True}), "application/json")
        time.sleep(0.7)

    def press(button, duration=100, settle=0.4):
        checked(api, "POST", "/sim/button/" + button,
                json.dumps({"durationMs": duration}), "application/json")
        time.sleep(settle)

    try:
        install(timer_name, TIMER_SOURCE)
        result = wait_value(api, timer_name, "done", True)
        assert result["validation"] and result["once"]
        assert not result.get("cancelled_fired")
        assert result["minimum_ms"] >= 50
        report["timer_measurements"] = result
        time.sleep(0.3)
        assert values(api, timer_name)["count"] == 10
        passed("timer validation, limit, cancellation, one-shot and repeating callbacks")
        passed("hidden app timers and self-cancellation; no early callbacks")

        install(timer_name, """
# @headless true
class Probe
  var count
  def setup()
    self.count = 0
    timer.every(100, / -> self.step())
  end
  def step()
    self.count += 1
    shared.set('count', self.count)
  end
end
return Probe()
""")
        time.sleep(0.5)
        set_order(order + [timer_name], disabled + [timer_name])
        time.sleep(0.3)
        stopped = values(api, timer_name)["count"]
        time.sleep(0.5)
        assert values(api, timer_name)["count"] == stopped
        set_order(order + [timer_name], disabled)
        time.sleep(0.35)
        resumed = values(api, timer_name)["count"]
        assert 1 <= resumed - stopped <= 5, (stopped, resumed)
        passed("deactivated timers stop and resume without a catch-up burst")

        install(timer_name, """
# @headless true
class Probe
  def setup()
    timer.every(25, / -> self.fail())
  end
  def fail()
    shared.set('failed_at', now_ms())
    raise 'timer_test'
  end
end
return Probe()
""")
        time.sleep(0.4)
        errors = [a.get("error") for a in checked(api, "GET", "/api/v1/apps")
                  if a["name"] == timer_name]
        assert errors and errors[0], errors
        failed_at = values(api, timer_name)["failed_at"]
        time.sleep(0.3)
        assert values(api, timer_name)["failed_at"] == failed_at
        passed("callback error is reported once and stops the app's timers")

        install(timer_name, TIMER_SOURCE)
        wait_value(api, timer_name, "done", True)
        passed("replacing a failed app clears its old timers and starts cleanly")

        if args.sim_buttons:
            assert before.get("boardType") != "awtrixng", "simulator required"
            install(button_name, BUTTON_SOURCE)
            set_order([button_name, "Time", "Date"],
                      [name for name in disabled if name not in ("Time", "Date")])
            show(button_name)
            power = checked(api, "GET", "/api/v1/device")["matrixPower"]
            press("select")
            assert values(api, button_name)["events"] == "select:press,select:release,"
            passed("debounced short press produces one press and one release")
            press("select", 1050, 1.4)
            events = values(api, button_name)["events"]
            assert events.count("select:long,") == 1 and "select:repeat," in events
            assert events.endswith("select:release,")
            assert "legacy:select" not in events
            passed("holding produces long/repeat/release without a legacy duplicate")
            press("left")
            assert values(api, button_name)["events"].endswith("left:press,legacy:left,")
            assert checked(api, "GET", "/api/v1/device")["currentApp"] == button_name
            passed("unclaimed press falls back to the legacy handler")
            press("select", settle=0.18)
            press("select", settle=0.4)
            assert checked(api, "GET", "/api/v1/device")["matrixPower"] == power
            passed("captured double press does not toggle display power")
            press("right", settle=1.3)
            assert checked(api, "GET", "/api/v1/device")["currentApp"] == "Time"
            passed("unclaimed right press retains normal app navigation")
            press("select", settle=0.18)
            press("select", settle=0.4)
            assert checked(api, "GET", "/api/v1/device")["matrixPower"] != power
            checked(api, "PATCH", "/api/v1/display", json.dumps({"power": power}), "application/json")
            passed("built-in double press still toggles display power")
            show(button_name)
            press("select", duration=1800, settle=0.2)
            captured = values(api, button_name)["events"]
            show("Time")
            show(button_name)
            time.sleep(0.5)
            assert values(api, button_name)["events"] == captured
            passed("switching away ends a hold; returning does not inherit its release")
            report["button_events"] = events
        else:
            report["button_test"] = "not run: requires physical buttons or --sim-buttons"
    finally:
        if args.sim_buttons:
            checked(api, "PATCH", "/api/v1/display",
                    json.dumps({"power": before["matrixPower"]}), "application/json")
        for name in reversed(installed):
            checked(api, "DELETE", "/api/v1/apps/" + name)
        set_order(order, disabled)
        if before.get("currentApp"):
            show(before["currentApp"])
        report["after"] = checked(api, "GET", "/api/v1/device")
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(report['checks'])} checks passed", flush=True)


if __name__ == "__main__":
    main()
