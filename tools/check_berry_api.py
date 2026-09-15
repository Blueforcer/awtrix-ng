#!/usr/bin/env python3
"""Checks the web UI editor's generated Berry API table.

Two failures this catches, both of which are silent otherwise:

1. The extraction patterns in scripts/berry_api.py stopped matching -- after a
   refactor moves a registration, renames a file, or reformats a call. The build
   would still succeed and the editor would just quietly lose its highlighting
   and completion, which reads as a styling choice rather than a broken build.

2. The table checked into webui/index.html is stale. The build regenerates it in
   place, so a commit that adds a binding without building leaves the simulator
   -- which serves that file straight from disk -- offering an API that no longer
   matches the firmware.

Run: python tools/check_berry_api.py
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import berry_api

BEGIN = berry_api.BEGIN
END = berry_api.END

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def main():
    table = berry_api.extract(ROOT)
    api, mods, core = table["api"], table["mods"], table["core"]
    names = {entry.split("(", 1)[0] for entry in api}

    check(api, "no device API entries extracted from ScriptBindings.cpp/Prelude.h")
    check(mods, "no modules extracted from Prelude.h")
    check(core, "no Berry language builtins extracted from be_baselib.c/berry_conf.h")

    for want in ("pixel", "text", "log", "http.get", "mqtt.subscribe", "store.get", "notify",
                 "scroll_text"):
        check(want in names, "expected %r in the device API, got %d entries" % (want, len(api)))

    check("pixel(x, y, color)" in api, "pixel lost its signature comment")
    check("store.get(k, dflt?)" in api,
          "store.get lost its vararg-to-optional rendering")

    for entry in api + mods + core:
        check(not entry.startswith("_"), "internal name leaked into the table: %r" % entry)

    for want in ("str", "size", "isinstance", "json", "math"):
        check(want in core, "expected %r among the Berry language builtins" % want)
    check("os" not in core, "'os' is disabled in berry_conf.h but appears in the table")

    src = os.path.join(ROOT, "webui", "index.html")
    with open(src, "r", encoding="utf-8", newline="") as f:
        html = f.read()
    start, end = html.find(BEGIN), html.find(END)
    if start < 0 or end < 0 or end < start:
        failures.append("the %s / %s markers are missing from webui/index.html" % (BEGIN, END))
    else:
        current = html[start:end + len(END)]
        check(current == berry_api.block(ROOT),
              "the Berry API table in webui/index.html is stale -- run `pio run -e awtrix` "
              "(or `python scripts/berry_api.py`) and commit the result")

    if failures:
        for f in failures:
            print("FAIL: %s" % f)
        return 1
    print("berry api: %d device entries, %d modules, %d language builtins, table current"
          % (len(api), len(mods), len(core)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
