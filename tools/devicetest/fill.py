#!/usr/bin/env python3
"""Fill a device with throwaway scripts and modules, to look at a full UI.

The content is meaningless on purpose: each file is the smallest thing that
compiles, so the only thing that grows is the number of rows in the sidebar.

    python tools/devicetest/fill.py --host awtrix-ng.local --scripts 10 --modules 6
    python tools/devicetest/fill.py --host awtrix-ng.local --clean
"""

import argparse
import json
import sys
import urllib.error
import urllib.request

PREFIX = "fill"


def call(base, method, path, body=None, ctype=None):
    data = None if body is None else body.encode("utf-8")
    req = urllib.request.Request(base + path, data=data, method=method)
    if ctype:
        req.add_header("Content-Type", ctype)
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            raw, status = r.read().decode("utf-8", "replace"), r.status
    except urllib.error.HTTPError as e:
        raw, status = e.read().decode("utf-8", "replace"), e.code
    except Exception as e:                                       # noqa: BLE001
        return 0, {"error": {"message": str(e)}}
    try:
        return status, json.loads(raw)
    except ValueError:
        return status, raw


def script_src(i):
    return ("# @name    Filler %d\n"
            "# @desc    Throwaway app %d\n"
            "class F%d\n"
            "  def draw()\n"
            "    clear()\n"
            "    text(0, 6, \"%d\", hsv(%d, 90, 60))\n"
            "  end\n"
            "end\n"
            "return F%d()\n" % (i, i, i, i, (i * 37) % 360, i))


def module_src(i):
    return ("# @module  fillmod%d\n"
            "# @desc    Throwaway module %d\n"
            "var m = module(\"fillmod%d\")\n"
            "m.n = %d\n"
            "return m\n" % (i, i, i, i))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="awtrix-ng.local")
    ap.add_argument("--scripts", type=int, default=10)
    ap.add_argument("--modules", type=int, default=6)
    ap.add_argument("--clean", action="store_true", help="delete every fill* file instead")
    args = ap.parse_args()
    base = "http://" + args.host

    st, apps = call(base, "GET", "/api/v1/apps")
    if st != 200:
        print("device not reachable: HTTP %s" % st)
        return 2
    mine = [a["name"] for a in apps if a.get("name", "").startswith(PREFIX)]

    if args.clean:
        for name in mine:
            s, _ = call(base, "DELETE", "/api/v1/apps/" + name)
            print("delete %-16s HTTP %s" % (name, s))
        print("%d removed" % len(mine))
        return 0

    plan = [("%smod%d" % (PREFIX, i), module_src(i)) for i in range(1, args.modules + 1)]
    plan += [("%s%02d" % (PREFIX, i), script_src(i)) for i in range(1, args.scripts + 1)]

    ok = 0
    for name, src in plan:
        st, res = call(base, "PUT", "/api/v1/apps/script/" + name, src, "text/plain")
        err = res.get("error") if isinstance(res, dict) else res
        print("%-16s HTTP %s %s" % (name, st, "" if err is None else json.dumps(err)[:90]))
        if st != 200:
            print("stopped: the device refused this one")
            break
        ok += 1

    st, dev = call(base, "GET", "/api/v1/device")
    st, apps = call(base, "GET", "/api/v1/apps")
    files = [a for a in apps if a.get("origin") in ("script", "module")]
    print("\n%d installed, %d files on the device (%d scripts, %d modules)" % (
        ok, len(files),
        len([a for a in files if a["origin"] == "script"]),
        len([a for a in files if a["origin"] == "module"])))
    print("heap %s free, largest block %s" % (
        dev.get("freeHeapBytes"), dev.get("largestFreeBlockBytes")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
