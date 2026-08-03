#!/usr/bin/env python3
"""Headless script-subsystem test run against a real AWTRIX NG device.

Installs the .ax files next to this script, then drives them over the REST API
only: no web UI, no serial, no eyes on the panel.

    python tools/devicetest/run.py --host awtrix-ng.local
    python tools/devicetest/run.py --host awtrix-ng.local --slow --keep
"""

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "scripts")

MODULES = ["tfmt"]
APPS = ["tcfg", "tbg", "tuse", "thttp", "twarn", "tapi", "tskip"]
TEMP = "ttmp"

PASS, FAIL, SKIP = [], [], []


class Api:
    def __init__(self, host, auth=None, timeout=10):
        self.base = "http://%s" % host
        self.timeout = timeout
        self.auth = auth

    def call(self, method, path, body=None, ctype=None):
        data = None
        if body is not None:
            data = body if isinstance(body, bytes) else body.encode("utf-8")
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if ctype:
            req.add_header("Content-Type", ctype)
        if self.auth:
            req.add_header("Authorization", "Basic " + base64.b64encode(
                self.auth.encode("utf-8")).decode("ascii"))
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                raw = r.read().decode("utf-8", "replace")
                status = r.status
        except urllib.error.HTTPError as e:
            raw = e.read().decode("utf-8", "replace")
            status = e.code
        except Exception as e:                                   # noqa: BLE001
            return 0, {"error": {"message": str(e)}}
        try:
            return status, json.loads(raw)
        except ValueError:
            return status, raw

    def get(self, path):
        return self.call("GET", path)

    def put_json(self, path, obj):
        return self.call("PUT", path, json.dumps(obj), "application/json")

    def patch_json(self, path, obj):
        return self.call("PATCH", path, json.dumps(obj), "application/json")

    def put_script(self, name, source):
        return self.call("PUT", "/api/v1/apps/script/" + name, source, "text/plain")

    def delete(self, path):
        return self.call("DELETE", path)


def check(name, ok, detail=""):
    (PASS if ok else FAIL).append(name)
    print("%s %s%s" % ("PASS" if ok else "FAIL", name, ("  -> " + detail) if detail else ""))
    return ok


def skip(name, why):
    SKIP.append(name)
    print("SKIP %s  -> %s" % (name, why))


def apps_by_name(api):
    st, body = api.get("/api/v1/apps")
    if st != 200 or not isinstance(body, list):
        return {}
    return {a.get("name"): a for a in body}


def shared_map(api):
    st, body = api.get("/api/v1/scripts/shared")
    out = {}
    if st == 200 and isinstance(body, list):
        for e in body:
            out["%s.%s" % (e.get("owner"), e.get("key"))] = e
    return out


def wait_for(fn, seconds, step=1.0):
    deadline = time.time() + seconds
    last = None
    while True:
        last = fn()
        if last:
            return last
        if time.time() >= deadline:
            return last
        time.sleep(step)


def fields_by_key(cfg):
    return {f["key"]: f for f in cfg.get("fields", [])} if isinstance(cfg, dict) else {}


# --------------------------------------------------------------------------- install

def install(api):
    """Delete first: that erases the saved store, so every run starts on the
    declared defaults even after a --keep run left settings patched."""
    print("\n== install ==")
    ok = True
    for name in MODULES + APPS:
        path = os.path.join(SCRIPTS, name + ".ax")
        with open(path, "r", encoding="utf-8") as fh:
            src = fh.read()
        api.delete("/api/v1/apps/" + name)
        st, body = api.put_script(name, src)
        err = body.get("error") if isinstance(body, dict) else body
        ok &= check("install %s" % name, st == 200 and err is None,
                    "HTTP %s %s" % (st, json.dumps(err)[:160] if err else ""))
    return ok


# --------------------------------------------------------------------------- checks

def test_inventory(api):
    print("\n== inventory ==")
    apps = apps_by_name(api)
    check("tfmt listed as module", apps.get("tfmt", {}).get("origin") == "module",
          json.dumps(apps.get("tfmt")))
    check("tfmt import name", apps.get("tfmt", {}).get("import") == "tfmt")
    check("module config flag (docs say scripts and modules)",
          apps.get("tfmt", {}).get("config") is True,
          "key absent" if "config" not in apps.get("tfmt", {}) else
          str(apps["tfmt"].get("config")))
    for name in APPS:
        a = apps.get(name, {})
        check("%s origin script" % name, a.get("origin") == "script", str(a.get("origin")))
        check("%s error null" % name, a.get("error") is None, json.dumps(a.get("error")))
    check("tbg headless", apps.get("tbg", {}).get("headless") is True)
    check("tbg not in rotation", apps.get("tbg", {}).get("inLoop") is False
          and apps.get("tbg", {}).get("position") is None,
          "inLoop=%s position=%s" % (apps.get("tbg", {}).get("inLoop"),
                                     apps.get("tbg", {}).get("position")))
    check("tbg meta from headers", apps.get("tbg", {}).get("meta", {}).get("name") == "T-Background")
    check("tcfg config flag", apps.get("tcfg", {}).get("config") is True)
    check("tapi config flag false", apps.get("tapi", {}).get("config") is False)


def test_schema(api):
    print("\n== config schema ==")
    st, cfg = api.get("/api/v1/apps/tcfg/config")
    if not check("GET tcfg config", st == 200, "HTTP %s" % st):
        return
    f = fields_by_key(cfg)
    check("six fields", len(cfg.get("fields", [])) == 6, str(list(f)))
    check("no warnings", cfg.get("warnings") == [], json.dumps(cfg.get("warnings")))
    check("bool default", f.get("flag", {}).get("type") == "bool"
          and f.get("flag", {}).get("default") is True)
    check("text maxlen", f.get("label", {}).get("maxlen") == 12
          and f.get("label", {}).get("default") == "cfg")
    check("number min/max/unit", (f.get("every", {}).get("min"), f.get("every", {}).get("max"),
                                  f.get("every", {}).get("unit")) == (1, 60, "min"),
          json.dumps(f.get("every")))
    check("slider bounds", (f.get("level", {}).get("min"), f.get("level", {}).get("max")) == (0, 100))
    check("select options", f.get("mode", {}).get("options") == ["now", "today", "week"],
          json.dumps(f.get("mode", {}).get("options")))
    check("color default is a number", f.get("tint", {}).get("default") == 0xFF8800,
          str(f.get("tint", {}).get("default")))
    check("value equals default before any patch",
          all(x.get("value") == x.get("default") for x in f.values()))

    st, cfg = api.get("/api/v1/apps/tapi/config")
    check("script without settings answers empty, not 404",
          st == 200 and cfg.get("fields") == [], "HTTP %s" % st)
    st, _ = api.get("/api/v1/apps/nosuchscript/config")
    check("unknown script config 404", st == 404, "HTTP %s" % st)
    st, _ = api.get("/api/v1/apps/not%20a%20name/config")
    check("invalid name config 400", st == 400, "HTTP %s" % st)


def test_patch_roundtrip(api):
    print("\n== config patch -> restart -> shared ==")
    want = {"flag": False, "label": "patched", "every": 999, "level": 5,
            "mode": "week", "tint": "#00FF00"}
    st, body = api.patch_json("/api/v1/apps/tcfg/config", want)
    check("PATCH tcfg accepted", st == 200 and body.get("ok") is True
          and body.get("error") is None, "HTTP %s %s" % (st, json.dumps(body)[:160]))

    st, cfg = api.get("/api/v1/apps/tcfg/config")
    f = fields_by_key(cfg)
    check("text stored", f.get("label", {}).get("value") == "patched")
    check("bool stored", f.get("flag", {}).get("value") is False)
    check("number clamped to max", f.get("every", {}).get("value") == 60,
          str(f.get("every", {}).get("value")))
    check("slider stored", f.get("level", {}).get("value") == 5)
    check("select stored", f.get("mode", {}).get("value") == "week")
    check("color from #RRGGBB", f.get("tint", {}).get("value") == 0x00FF00,
          str(f.get("tint", {}).get("value")))
    check("defaults untouched", f.get("tint", {}).get("default") == 0xFF8800)

    got = wait_for(lambda: (lambda s: s if s.get("tcfg.label", {}).get("value") == "patched"
                            else None)(shared_map(api)), 10)
    check("script restarted and re-read its settings",
          got.get("tcfg.label", {}).get("value") == "patched",
          json.dumps({k: v.get("value") for k, v in got.items() if k.startswith("tcfg.")}))
    check("clamped value reached the script", got.get("tcfg.every", {}).get("value") == 60)
    check("color reached the script as an int", got.get("tcfg.tint", {}).get("value") == 0x00FF00)
    check("bool reached the script", got.get("tcfg.flag", {}).get("value") is False)
    check("shared types", got.get("tcfg.mode", {}).get("type") == "string"
          and got.get("tcfg.every", {}).get("type") == "int",
          "%s/%s" % (got.get("tcfg.mode", {}).get("type"), got.get("tcfg.every", {}).get("type")))


def test_patch_rejects(api):
    print("\n== config validation ==")
    cases = [
        ("unknown key", {"nope": 1}, 422),
        ("select off the list", {"mode": "yesterday"}, 422),
        ("bool given a string", {"flag": "yes"}, 422),
        ("text over maxlen", {"label": "x" * 13}, 422),
        ("colour garbage", {"tint": "not-a-colour"}, 422),
    ]
    for title, body, want in cases:
        st, res = api.patch_json("/api/v1/apps/tcfg/config", body)
        check("reject: %s" % title, st == want,
              "HTTP %s %s" % (st, json.dumps(res)[:120]))

    st, res = api.call("PATCH", "/api/v1/apps/tcfg/config", "", "application/json")
    check("reject: empty body", st == 422, "HTTP %s %s" % (st, json.dumps(res)[:120]))
    st, res = api.call("PATCH", "/api/v1/apps/tcfg/config", "{ not json", "application/json")
    check("reject: malformed JSON", st == 422, "HTTP %s %s" % (st, json.dumps(res)[:120]))
    st, res = api.patch_json("/api/v1/apps/tcfg/config", {})
    check("empty object changes nothing, 200", st == 200, "HTTP %s %s" % (st, json.dumps(res)[:120]))

    st, res = api.call("PATCH", "/api/v1/apps/tcfg/config", '{"flag":true}', "text/plain")
    check("reject: wrong content type", st == 415, "HTTP %s" % st)

    st, cfg = api.get("/api/v1/apps/tcfg/config")
    f = fields_by_key(cfg)
    check("nothing changed by the rejected calls",
          f.get("label", {}).get("value") == "patched" and f.get("flag", {}).get("value") is False)

    st, res = api.patch_json("/api/v1/apps/tapi/config", {"x": 1})
    check("script without settings rejects a patch", st in (422,),
          "HTTP %s %s" % (st, json.dumps(res)[:120]))


def test_module(api):
    print("\n== module ==")
    got = wait_for(lambda: (lambda s: s if "tuse.out" in s else None)(shared_map(api)), 8)
    out = got.get("tuse.out", {}).get("value")
    check("consumer ran module code", isinstance(out, str) and out.startswith("ng"), str(out))
    check("module helper padded", isinstance(out, str) and len(out) == 4, str(out))

    st, body = api.patch_json("/api/v1/apps/tfmt/config", {"tag": "zz", "tint": "#123456"})
    check("PATCH module settings", st == 200 and body.get("error") is None,
          "HTTP %s %s" % (st, json.dumps(body)[:160]))

    got = wait_for(lambda: (lambda s: s if str(s.get("tuse.out", {}).get("value", "")).startswith("zz")
                            else None)(shared_map(api)), 15)
    check("saving a module restarts its importers",
          str(got.get("tuse.out", {}).get("value", "")).startswith("zz"),
          str(got.get("tuse.out", {}).get("value")))

    st, cfg = api.get("/api/v1/apps/tfmt/config")
    f = fields_by_key(cfg)
    check("module config readable", f.get("tint", {}).get("value") == 0x123456,
          json.dumps(cfg)[:160])

    st, src = api.get("/api/v1/apps/script/tfmt")
    check("module source served back", st == 200 and "module(\"tfmt\")" in str(src),
          "HTTP %s" % st)


def test_headless(api):
    print("\n== headless ==")
    a = shared_map(api).get("tbg.ticks", {}).get("value")
    if not check("headless app publishes", isinstance(a, int), str(a)):
        return
    time.sleep(4)
    b = shared_map(api).get("tbg.ticks", {}).get("value")
    check("headless loop() runs while out of the rotation", isinstance(b, int) and b > a,
          "%s -> %s" % (a, b))
    st, stats = api.get("/api/v1/device")
    check("headless app never becomes the current app", stats.get("currentApp") != "tbg",
          str(stats.get("currentApp")))
    check("shared ageMs moves", shared_map(api).get("tbg.ticks", {}).get("ageMs") is not None)


def test_http(api, host):
    print("\n== http from a script ==")
    st, body = api.patch_json("/api/v1/apps/thttp/config", {"host": host, "secs": 3})
    check("PATCH http host", st == 200 and body.get("error") is None, "HTTP %s" % st)
    got = wait_for(lambda: (lambda s: s if s.get("thttp.up", {}).get("value", -1) >= 0
                            else None)(shared_map(api)), 30, 2.0)
    up = got.get("thttp.up", {}).get("value")
    check("script fetched the device's own API", isinstance(up, int) and up >= 0, str(up))
    check("status reached the callback", got.get("thttp.status", {}).get("value") == 200,
          str(got.get("thttp.status", {}).get("value")))
    check("no transport failures", got.get("thttp.fails", {}).get("value") == 0,
          str(got.get("thttp.fails", {}).get("value")))


def test_warnings(api):
    print("\n== config warnings ==")
    st, cfg = api.get("/api/v1/apps/twarn/config")
    if not check("GET twarn config", st == 200, "HTTP %s" % st):
        return
    warns = " | ".join(cfg.get("warnings", []))
    f = fields_by_key(cfg)
    for needle, title in [
        ("'1bad' is not a usable key", "bad key reported"),
        ("declared twice", "duplicate key reported"),
        ("unknown type", "unknown type reported"),
        ("select needs options", "select without options reported"),
        ("min is above max", "min above max reported"),
        ("unknown attribute", "unknown attribute reported"),
        ("default is not a number", "bad default reported"),
    ]:
        check(title, needle in warns, warns[:200])
    check("every warning carries a line number",
          all(w.startswith("line ") for w in cfg.get("warnings", [])), warns[:200])
    check("only the usable fields survive", sorted(f) == ["attr", "numd", "ok", "rng"],
          str(sorted(f)))
    check("min/max dropped when inverted", "min" not in f.get("rng", {}), json.dumps(f.get("rng")))
    check("broken config does not break the app",
          apps_by_name(api).get("twarn", {}).get("error") is None)


def test_errors(api):
    print("\n== error reporting ==")
    st, body = api.put_script(TEMP, "class T\n  def draw()\n    text(1, 6, \"x\"\n  end\nend\nreturn T()\n")
    err = body.get("error") if isinstance(body, dict) else {}
    check("syntax error still installs with 200", st == 200, "HTTP %s" % st)
    check("compile error reported", isinstance(err, dict) and err.get("message"),
          json.dumps(err)[:160])
    check("compile error carries a line", isinstance(err, dict) and isinstance(err.get("line"), int),
          json.dumps(err)[:160])

    st, body = api.put_script(TEMP, "class T\n  def draw()\n    clear()\n    self.nope()\n  end\nend\nreturn T()\n")
    check("re-PUT of a broken script is accepted", st == 200, "HTTP %s" % st)
    api.put_json("/api/v1/apps/active", {"name": TEMP, "fast": True})
    got = wait_for(lambda: (lambda a: a if a.get(TEMP, {}).get("error") else None)(apps_by_name(api)), 8)
    err = got.get(TEMP, {}).get("error") or {}
    check("runtime error latched on the app", bool(err.get("message")), json.dumps(err)[:160])
    check("runtime error names the hook", err.get("hook") in ("draw", "loop", "setup", "init"),
          json.dumps(err)[:160])

    st, body = api.put_script(TEMP, "class T\n  def draw()\n    clear()\n    text(0, 6, \"ok\", 0x00FF00)\n  end\nend\nreturn T()\n")
    check("a good PUT clears the error",
          st == 200 and (body.get("error") if isinstance(body, dict) else 1) is None,
          json.dumps(body)[:160])
    st, _ = api.delete("/api/v1/apps/" + TEMP)
    check("temp script deleted", st == 200, "HTTP %s" % st)
    st, _ = api.delete("/api/v1/apps/" + TEMP)
    check("deleting twice is safe", st == 200, "HTTP %s" % st)

    st, body = api.put_script("tfmt2", "# @module tfmt\nvar m = module(\"tfmt\")\nreturn m\n")
    check("duplicate module import name refused", st == 422, "HTTP %s %s" % (st, json.dumps(body)[:120]))
    if st != 422:
        api.delete("/api/v1/apps/tfmt2")
    st, body = api.put_script("tjson", "# @module json\nvar m = module(\"json\")\nreturn m\n")
    check("built-in module name refused", st == 422, "HTTP %s %s" % (st, json.dumps(body)[:120]))
    if st != 422:
        api.delete("/api/v1/apps/tjson")


def test_pixels(api):
    print("\n== pixels ==")
    api.put_json("/api/v1/apps/active", {"name": "tcfg", "fast": True})
    time.sleep(0.4)
    api.put_json("/api/v1/apps/active", {"name": "tcfg", "fast": True})
    st, fb = api.get("/api/v1/display/screen")
    if not check("framebuffer readable", st == 200 and isinstance(fb, dict), "HTTP %s" % st):
        return
    px = fb.get("pixels", [])
    lit = [p for p in px if p]
    check("canvas size", (fb.get("width"), fb.get("height")) == (32, 8),
          "%sx%s" % (fb.get("width"), fb.get("height")))
    check("pixel count matches", len(px) == fb.get("width", 0) * fb.get("height", 0), str(len(px)))
    check("app drew something", len(lit) > 0, "%d lit" % len(lit))
    check("drawn in the configured colour", 0x00FF00 in lit,
          "unique: %s" % sorted(set(lit))[:6])
    check("nothing in the last column",
          all(px[r * 32 + 31] == 0 for r in range(8)), "right edge lit")


def test_rotation(api):
    print("\n== rotation (slow) ==")
    api.patch_json("/api/v1/apps/tskip/config", {"show": True, "secs": 2})
    got = wait_for(lambda: (lambda s: s if s.get("tskip.turns", {}).get("value", 0) >= 1
                            else None)(shared_map(api)), 120, 2.0)
    check("app took a turn", got.get("tskip.turns", {}).get("value", 0) >= 1,
          str(got.get("tskip.turns", {}).get("value")))
    check("duration() honoured", got.get("tskip.ms", {}).get("value") == 2000)

    api.patch_json("/api/v1/apps/tskip/config", {"show": False})
    got = wait_for(lambda: (lambda a: a if a.get("tskip", {}).get("skipped") else None)(apps_by_name(api)),
                   120, 2.0)
    check("should_show() false shows up as skipped", got.get("tskip", {}).get("skipped") is True,
          json.dumps(got.get("tskip")))
    before = shared_map(api).get("tskip.turns", {}).get("value", 0)
    time.sleep(100)
    after = shared_map(api).get("tskip.turns", {}).get("value", 0)
    check("skipped app gets no turn", after == before, "%s -> %s" % (before, after))
    api.patch_json("/api/v1/apps/tskip/config", {"show": True})


def cleanup(api):
    print("\n== cleanup ==")
    for name in APPS + MODULES + [TEMP]:
        st, _ = api.delete("/api/v1/apps/" + name)
        print("     delete %-6s HTTP %s" % (name, st))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.environ.get("AWTRIX", "awtrix-ng.local"))
    ap.add_argument("--auth", help="user:password if the device has a login")
    ap.add_argument("--slow", action="store_true", help="also test should_show/duration (~4 min)")
    ap.add_argument("--keep", action="store_true", help="leave the scripts installed")
    ap.add_argument("--only", help="comma separated test names to run")
    args = ap.parse_args()

    api = Api(args.host, args.auth)
    st, dev = api.get("/api/v1/device")
    if st != 200:
        print("device %s not reachable: HTTP %s %s" % (args.host, st, dev))
        return 2
    print("device %s  fw %s  soc %s  heap %s free / %s largest  scripts %s" % (
        dev.get("ipAddress"), dev.get("version"), dev.get("soc"), dev.get("freeHeapBytes"),
        dev.get("largestFreeBlockBytes"), dev.get("scriptingRunning")))
    heap_before = dev.get("freeHeapBytes")

    if not install(api):
        print("\ninstall failed - stopping")
        return 1
    time.sleep(2)

    tests = [
        ("inventory", lambda: test_inventory(api)),
        ("schema", lambda: test_schema(api)),
        ("patch", lambda: test_patch_roundtrip(api)),
        ("reject", lambda: test_patch_rejects(api)),
        ("module", lambda: test_module(api)),
        ("headless", lambda: test_headless(api)),
        ("http", lambda: test_http(api, args.host)),
        ("warnings", lambda: test_warnings(api)),
        ("errors", lambda: test_errors(api)),
        ("pixels", lambda: test_pixels(api)),
    ]
    wanted = set(args.only.split(",")) if args.only else None
    for name, fn in tests:
        if wanted and name not in wanted:
            skip(name, "--only")
            continue
        fn()
    if args.slow and (not wanted or "rotation" in wanted):
        test_rotation(api)
    elif not args.slow:
        skip("rotation", "pass --slow")

    st, dev = api.get("/api/v1/device")
    print("\nheap %s -> %s (%+d) over the run, %d scripts installed" % (
        heap_before, dev.get("freeHeapBytes"),
        (dev.get("freeHeapBytes") or 0) - (heap_before or 0), len(APPS) + len(MODULES)))

    if not args.keep:
        cleanup(api)

    print("\n%d passed, %d failed, %d skipped" % (len(PASS), len(FAIL), len(SKIP)))
    for f in FAIL:
        print("  FAIL %s" % f)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
