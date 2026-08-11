#!/usr/bin/env python3
"""Fail if the published API docs have drifted from the firmware.

`mkdocs build --strict` validates markdown and links, not whether the API
description is true. That gap let the docs claim things the code did not do:
undocumented fields, wrong ranges, and keys that had been removed from the
firmware but were still presented as live.

This check extracts the real key sets from the source and compares them
against the OpenAPI schemas and the reference pages. It knows about *names*
and, for numeric fields, the accepted *range* - the two things that drifted in
practice. It still cannot tell you a prose description is wrong.

The range check exists because a name-only check passed happily while
`matrixLayout` was documented as `0-2` for as long as it took someone to
notice; the firmware had widened it to `0-3`.

Run: python tools/check_docs_sync.py     (exit 1 on drift)
"""

import os
import re
import sys

try:
    import yaml
except ImportError:
    sys.exit("check_docs_sync: PyYAML is required (pip install pyyaml)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

REMOVED_KEYS = {"bootSound", "updateCheck", "colorTemperature", "clients", "save", "volume",
                "matrixWidth",
                "matrixLayout", "matrixTileWidth", "matrixOrientation", "matrixSerpentine",
                "matrixFlipX", "matrixFlipY"}
HISTORICAL_MENTIONS_OK = {
    "docs/guides/migrating-from-awtrix3.md",
    "docs/guides/sounds.md",
    "docs/guides/updating.md",
    "docs/reference/system.md",
    "docs/reference/visuals.md",
}
UNPUBLISHED = ("docs/examples/",)


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as fh:
        return fh.read()


def ground_truth():
    """The key sets the firmware actually implements."""
    settings_src = read("src/core/Settings.cpp")
    settings = set(re.findall(
        r'mk(?:Bool|Int|Long|Float|Enum|Color|NullColor|Transition)\("([a-zA-Z0-9]+)"',
        settings_src))
    settings |= set(re.findall(r'w\.key\("([a-zA-Z0-9]+)"\)', settings_src))
    system = set(re.findall(r"X\(([a-zA-Z0-9]+),", read("src/persistence/DeviceConfigFields.h")))
    state_json = read("src/core/api/StateJson.cpp")
    start = state_json.index("std::string buildDeviceJson(")
    end = state_json.index("\n}", start)
    body = state_json[start:end]
    arr_start = body.find('w.key("indicators")')
    if arr_start != -1:
        arr_end = body.index("w.endArray();", arr_start) + len("w.endArray();")
        body = body[:arr_start] + body[arr_end:]
    device = set(re.findall(r'\bw\.(?:member|memberNull|key)\("([a-zA-Z0-9]+)"',
                            body)) | {"indicators"}
    router = read("src/core/api/ApiRouter.cpp") + read("src/transport/http/HttpApiServer.cpp")
    errors = set(re.findall(r'errorJson\("([a-zA-Z]+)"', router))
    errors |= set(re.findall(r'sendError\([0-9]+, "([a-zA-Z]+)"', router))
    errors |= set(re.findall(r'return \{"([a-zA-Z]+)", [0-9]+,', router))
    errors |= set(re.findall(r'err = \{[0-9]+, "([a-zA-Z]+)"',
                             read("src/persistence/SystemConfigApply.cpp")))
    return settings, system, device, errors


RANGE_PAGES = ("docs/reference/settings.md", "docs/reference/system.md",
               "docs/reference/errors.md", "docs/reference/gpio.md",
               "docs/reference/http.md", "docs/reference/payload.md",
               "docs/reference/limits.md")

RANGE_EXEMPT = {"transitionDurationMs"}

_DASH = "–—-"
_NUM = r"-?\d+(?:\.\d+)?"


CAPS = (
    ("src/AppConfig.h", r"kMaxPushedApps\s*=\s*([0-9 *]+);", "Pushed apps resident"),
    ("src/AppConfig.h", r"kMaxNotifications\s*=\s*([0-9 *]+);", "Notification queue"),
    ("src/transport/http/HttpApiServer.cpp", r"kMaxBodyBytes\s*=\s*([0-9 *]+);",
     "JSON request body"),
    ("src/transport/mqtt/MqttLink.cpp", r"kMqttBufferBytes\s*=\s*([0-9 *]+);",
     "MQTT command payload"),
    ("src/core/render/Gfx2d.h", r"kMaxChartPoints\s*=\s*([0-9 *]+);", "barChart"),
    ("src/core/radio/StationList.h", r"kMaxStations\s*=\s*([0-9 *]+);", "Radio stations"),
    ("src/core/radio/StationList.h", r"kMaxNameLength\s*=\s*([0-9 *]+);", "Station name"),
    ("src/core/radio/StationList.h", r"kMaxUrlLength\s*=\s*([0-9 *]+);", "Station URL"),
    ("src/core/sound/Rtttl.h", r"kMaxLength\s*=\s*([0-9 *]+);", "Melody source"),
    ("src/core/sound/Rtttl.h", r"kMaxTitle\s*=\s*([0-9 *]+);", "Melody name"),
    ("src/core/script/BerryVM.h", r"kInstructionLimit\s*=\s*([0-9 *]+);",
     "Instructions per entry"),
    ("src/system/ScriptHeapEsp32.cpp", r"kInternalBudgetBytes\s*=\s*([0-9 *]+);",
     "Shared script memory"),
    ("src/core/script/ScriptServices.h", r"kMaxHttpBody\s*=\s*([0-9 *]+);",
     "HTTP response body"),
    ("src/system/ScriptHttpWorker.cpp", r"kConnectTimeoutMs\s*=\s*([0-9 *]+);",
     "HTTP connect and read timeout"),
    ("src/core/script/ScriptServices.h", r"kHttpTimeoutMs\s*=\s*([0-9 *]+);",
     "HTTP request unanswered"),
    ("src/core/script/ScriptServices.h", r"kMaxPendingHttp\s*=\s*([0-9 *]+);",
     "HTTP requests in flight"),
    ("src/core/script/ScriptServices.h", r"AWTRIX_MAX_MQTT_SUBS\s+([0-9 *]+)\n",
     "MQTT subscriptions"),
    ("src/core/script/ScriptHost.h", r"AsyncQueue<MqttMessage,\s*([0-9 *]+)>",
     "MQTT messages waiting"),
    ("src/core/script/ScriptServices.h", r"kInstallReserveBytes\s*=\s*([0-9 *]+);",
     "Memory held back"),
    ("src/core/script/ScriptConfig.cpp", r"kMaxTextLen\s*=\s*([0-9 *]+);", "Setting text value"),
    ("src/core/script/SharedState.h", r"kMaxSharedKeyChars\s*=\s*([0-9 *]+);", "Shared key names"),
)

LIMITS_PAGE = "docs/reference/limits.md"


def cap_value(expr):
    """`96 * 1024` -> 98304. Only products of integers occur in these constants."""
    value = 1
    for part in expr.split("*"):
        value *= int(part.strip())
    return value


def cap_renderings(value):
    """Every way the docs are allowed to write this number."""
    out = {str(value), "{:,}".format(value).replace(",", " ")}
    if value % 1024 == 0:
        out.add("%d KB" % (value // 1024))
    if value % 1000 == 0:
        out.add("%d s" % (value // 1000))
    return out


def check_caps(problems):
    """Each fixed cap must be stated, at its current value, in its own row."""
    page = read(LIMITS_PAGE)
    rows = [line for line in page.splitlines() if line.strip().startswith("|")]
    for path, pattern, label in CAPS:
        found = re.search(pattern, read(path))
        if not found:
            problems.append("check_docs_sync: %s no longer matches /%s/ -- update the CAPS table"
                            % (path, pattern))
            continue
        value = cap_value(found.group(1))
        plain = label.replace("`", "")
        row = next((r for r in rows if r.replace("`", "").strip("| ").startswith(plain)), None)
        if row is None:
            problems.append("%s: no row for `%s` (the firmware caps it at %d)"
                            % (LIMITS_PAGE, label, value))
            continue
        if not any(text in row for text in cap_renderings(value)):
            problems.append("%s: the `%s` row does not state the firmware's value of %d"
                            % (LIMITS_PAGE, label, value))


def stock_palettes():
    """The palette names the firmware advertises in GET /api/v1/capabilities.

    The array is a hand-written literal in the capabilities builder rather than
    generated from the palette table, so it is exactly the kind of list that
    drifts: `Rainbow` was a working palette that five doc pages described as
    missing from the API long after it had been added to the literal.
    """
    src = read("src/core/api/CapabilitiesJson.h")
    m = re.search(r'\\"palettes\\":\[(.*?)\]', src, re.S)
    if not m:
        return None
    return set(re.findall(r'\\"([A-Za-z0-9]+)\\"', m.group(1)))


def check_palettes(problems):
    truth = stock_palettes()
    if truth is None:
        problems.append("check_docs_sync: could not find the palettes literal in "
                        "src/core/api/CapabilitiesJson.h")
        return
    for page in ("docs/reference/http.md", "docs/reference/mqtt.md", "docs/reference/visuals.md",
                 "docs/reference/payload.md", "docs/guides/effects.md"):
        for lineno, line in enumerate(read(page).splitlines(), 1):
            named = {p for p in truth if re.search(r'[`"]%s[`"]' % p, line)}
            if len(named) < len(truth) - 1:
                continue
            missing = truth - named
            if missing:
                problems.append("%s:%d: palette list omits %s (the firmware advertises all %d)"
                                % (page, lineno, ", ".join(sorted(missing)), len(truth)))


def builtin_palette_tables():
    """{name: [16 hex strings]} for the built-in palettes, from Palette.cpp.

    The tables are written with named colour constants (`kDarkBlue`), so the
    constants are resolved first and the table bodies mapped through them.
    """
    src = read("src/core/render/Palette.cpp")
    consts = {m[0]: m[1].upper().zfill(6)
              for m in re.findall(r'constexpr uint32_t (k\w+)\s*=\s*0x([0-9A-Fa-f]+)u?;', src)}
    out = {}
    for name, body in re.findall(r'constexpr Palette k(\w+)\s*=\s*\{\{(.*?)\}\};', src, re.S):
        entries = []
        for tok in (t.strip() for t in body.split(',')):
            if not tok:
                continue
            if tok in consts:
                entries.append(consts[tok])
            elif re.fullmatch(r'0x[0-9A-Fa-f]+u?', tok):
                entries.append(tok[2:].rstrip('uU').upper().zfill(6))
            else:
                return None
        out[name] = entries
    return out


def check_webui_palettes(problems):
    """The web UI carries its own copy of the built-in tables.

    It has to: the editor draws every palette as the ramp the device would paint,
    and no route reports a built-in's entries. A copy is a copy, so it is checked
    rather than trusted - a table edited in the firmware alone would leave the
    editor drawing colours the panel never shows.
    """
    truth = builtin_palette_tables()
    if not truth:
        problems.append("check_docs_sync: could not parse the palette tables in Palette.cpp")
        return
    ui = read("webui/index.html")
    m = re.search(r'const PAL_BUILTIN=\[(.*?)\n\];', ui, re.S)
    if not m:
        problems.append("check_docs_sync: could not find PAL_BUILTIN in webui/index.html")
        return
    copied = {n: v.split(' ') for n, v in re.findall(r"\['(\w+)','([0-9A-F ]+)'\]", m.group(1))}
    for name, entries in sorted(truth.items()):
        got = copied.get(name)
        if got is None:
            problems.append("webui/index.html: PAL_BUILTIN is missing the built-in palette %s"
                            % name)
        elif got != entries:
            problems.append("webui/index.html: PAL_BUILTIN's %s does not match Palette.cpp"
                            % name)
    for name in sorted(set(copied) - set(truth)):
        problems.append("webui/index.html: PAL_BUILTIN carries %s, which the firmware does not have"
                        % name)


def payload_keys():
    """The app/notification payload keys the firmware's allow-list accepts.

    Anything else in a payload body is refused outright with
    `unknown key "X"`, so a doc example or a web-UI request carrying a legacy
    AWTRIX 3 name is not merely stale - it is a 422 the moment anyone runs it.
    """
    src = read("src/core/payload/PayloadParser.cpp")
    out = set()
    for array in ("kAppKeys", "kNotificationKeys"):
        m = re.search(r"%s\[\]\s*=\s*\{(.*?)\};" % array, src, re.S)
        if not m:
            return None
        out |= set(re.findall(r'"([a-zA-Z0-9]+)"', m.group(1)))
    return out


PAYLOAD_ENDPOINT = re.compile(r"/api/v1/(?:notifications|apps/pushed/)[^\s'\"`,)]*"
                              r"['\"`]?\s*(?:,\s*)?")

_BODY_VAR = re.compile(r"^([A-Za-z_$][A-Za-z0-9_$]*)\s*[,)]")

_KEY_AT_DEPTH1 = re.compile(r"""(?:"([A-Za-z0-9_]+)"|'([A-Za-z0-9_]+)'|([A-Za-z_][A-Za-z0-9_]*))"""
                            r"""\s*:""")


def object_literal(text, i):
    """Top-level keys of the object literal at text[i] == '{', and its end.

    One brace-matching pass that understands quoting, so a `:` inside a string
    is not read as a key separator, and nesting, so only depth-1 keys come
    back. JSON, JS object literals and the body inside `curl -d '...'` all
    parse the same way here.
    """
    depth, j, keys, spans, start, quote = 0, i, set(), [], i + 1, None
    while j < len(text):
        c = text[j]
        if quote:
            if c == "\\":
                j += 2
                continue
            if c == quote:
                quote = None
        elif c in "\"'`":
            quote = c
        elif c in "{[(":
            depth += 1
        elif c in "}])":
            depth -= 1
            if depth == 0:
                spans.append((start, j))
                break
        elif c == "," and depth == 1:
            spans.append((start, j))
            start = j + 1
        j += 1
    for a, b in spans:
        m = _KEY_AT_DEPTH1.match(text[a:b].strip())
        if m:
            keys.add(m.group(1) or m.group(2) or m.group(3))
    return keys, j


def payload_bodies(text):
    """Yield (offset, keys) for each payload body sent to a payload endpoint."""
    for m in PAYLOAD_ENDPOINT.finditer(text):
        tail = text[m.end():m.end() + 200]
        var = _BODY_VAR.match(tail)
        if var:
            assign = None
            for a in re.finditer(r"(?:const|let|var)\s+%s\s*=" % re.escape(var.group(1)), text):
                if a.end() <= m.start():
                    assign = a
            if assign is None:
                continue
            region = text[assign.end():assign.end() + 400]
            k = 0
            while True:
                brace = region.find("{", k)
                if brace < 0:
                    break
                keys, end = object_literal(region, brace)
                if keys:
                    yield assign.end() + brace, keys
                k = end + 1
                if not re.match(r"^\s*[:?]", region[k:]):
                    break
            continue
        brace = tail.find("{")
        if brace < 0 or tail[:brace].strip(" \t\n\r"):
            continue
        keys, _ = object_literal(text, m.end() + brace)
        if keys:
            yield m.end() + brace, keys


def check_payload_keys(problems):
    truth = payload_keys()
    if truth is None:
        problems.append("check_docs_sync: could not read kAppKeys/kNotificationKeys "
                        "from src/core/payload/PayloadParser.cpp")
        return

    body = read("webui/index.html")
    for offset, keys in payload_bodies(body):
        for key in sorted(keys - truth):
            problems.append(
                "webui/index.html:%d: payload sent to the device carries `%s`, which the "
                "firmware refuses with `unknown key` (allowed keys: "
                "src/core/payload/PayloadParser.cpp)"
                % (body.count("\n", 0, offset) + 1, key))

    spec = yaml.safe_load(read("docs/api/openapi.yaml"))
    for path, method in (("/api/v1/apps/pushed/{name}", "put"),
                         ("/api/v1/notifications", "post")):
        schema = (((spec.get("paths", {}).get(path, {}).get(method, {})
                    .get("requestBody") or {}).get("content") or {})
                  .get("application/json") or {}).get("schema") or {}
        for variant in schema.get("oneOf", [schema]):
            for key in sorted(set((variant.get("properties") or {}).keys()) - truth):
                problems.append("openapi.yaml: %s %s declares payload field `%s`, which the "
                                "firmware refuses with `unknown key`" % (method.upper(), path, key))


def numeric_ranges():
    """{key: (lo, hi)} for every numeric field the firmware range-checks."""
    out = {}
    for key, lo, hi in re.findall(
            r'\{"([a-zA-Z0-9]+)",\s*(%s),\s*(%s),' % (_NUM, _NUM),
            read("src/core/ConfigRules.h")):
        out[key] = (float(lo), float(hi))
    for key, lo, hi in re.findall(
            r'mkInt\("([a-zA-Z0-9]+)",\s*&Settings::[a-zA-Z0-9]+,\s*(%s),\s*([A-Z_]+|%s)\)'
            % (_NUM, _NUM), read("src/core/Settings.cpp")):
        if hi.isupper():
            continue
        out.setdefault(key, (float(lo), float(hi)))
    return out


def stated_ranges(text):
    """Every (lo, hi) pair a doc cell states, in any of the shapes we write."""
    text = text.replace("`", "").replace("−", "-")
    pairs = []
    for n in re.findall(r"±\s*(\d+(?:\.\d+)?)", text):
        pairs.append((-float(n), float(n)))
    for lo, hi in re.findall(r"(%s)\s*[%s]\s*(%s)" % (_NUM, _DASH, _NUM), text):
        pairs.append((float(lo), float(hi)))
    return pairs


def check_ranges(problems):
    """Compare the range stated in a field's own table row against the code.

    Only table rows are checked, and only against the key named in the row's
    first cell. A field mentioned inside *another* row's description ("only when
    `matrixLayout` is 3") carries no claim about its own range.

    Prose is deliberately not checked. A number pair next to a field name in a
    sentence almost never states that field's bounds - it names a subset ("the
    narrow layouts, `timeMode` 1-4, reserve the left nine columns") or the modes
    a setting applies to. Extending this check over prose was tried: it produced
    two hits, both of them sentences of exactly that kind, and no real drift.
    """
    truth = numeric_ranges()
    for page in RANGE_PAGES:
        for lineno, line in enumerate(read(page).splitlines(), 1):
            stripped = line.strip()
            if not stripped.startswith("|"):
                continue
            cells = [c.strip() for c in stripped.strip("|").split("|")]
            if len(cells) < 2:
                continue
            subjects = set(re.findall(r"`([a-zA-Z0-9]+)`", cells[0]))
            rest = " | ".join(cells[1:])
            for key in sorted(subjects):
                if key in RANGE_EXEMPT or key not in truth:
                    continue
                lo, hi = truth[key]
                pairs = stated_ranges(rest)
                if not pairs:
                    continue
                if not all(abs(a - lo) < 1e-9 and abs(b - hi) < 1e-9 for a, b in pairs):
                    problems.append(
                        "%s:%d: `%s` is documented as %s but the firmware accepts %g-%g"
                        % (page, lineno, key,
                           ", ".join("%g-%g" % p for p in pairs), lo, hi))


def main():
    settings, system, device, errors = ground_truth()
    spec = yaml.safe_load(read("docs/api/openapi.yaml"))
    schemas = spec.get("components", {}).get("schemas", {})
    problems = []

    def props(name):
        return set((schemas.get(name) or {}).get("properties", {}).keys())

    for schema, truth in (("Settings", settings), ("SystemConfig", system),
                          ("DeviceState", device)):
        have = props(schema)
        for key in sorted(truth - have):
            problems.append("openapi.yaml: schema %s is missing `%s` (the firmware accepts it)"
                            % (schema, key))
        for key in sorted(have - truth):
            problems.append("openapi.yaml: schema %s documents `%s`, which the firmware does not have"
                            % (schema, key))

    enum = set(((((schemas.get("Error") or {}).get("properties") or {}).get("error") or {})
                .get("properties") or {}).get("code", {}).get("enum") or [])
    for code in sorted(errors - enum):
        problems.append("openapi.yaml: Error enum is missing the `%s` code" % code)
    for code in sorted(enum - errors):
        problems.append("openapi.yaml: Error enum lists `%s`, which the firmware never returns" % code)

    for page, truth, label in (("docs/reference/settings.md", settings, "settings"),
                               ("docs/reference/system.md", system, "system"),
                               ("docs/reference/device.md", device, "device-state")):
        body = read(page)
        for key in sorted(k for k in truth if not re.search(r"`%s`" % re.escape(k), body)):
            problems.append("%s: %s key `%s` is never mentioned" % (page, label, key))

    for dirpath, _, files in os.walk(os.path.join(ROOT, "docs")):
        for name in files:
            if not name.endswith((".md", ".yaml")):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), ROOT).replace(os.sep, "/")
            if rel.startswith(UNPUBLISHED) or rel in HISTORICAL_MENTIONS_OK:
                continue
            body = read(rel)
            for key in sorted(REMOVED_KEYS):
                if re.search(r"[`\"']%s[`\"']" % re.escape(key), body):
                    problems.append(
                        "%s: `%s` was removed from the firmware but is still documented "
                        "(add the page to HISTORICAL_MENTIONS_OK if the mention is deliberate history)"
                        % (rel, key))

    check_ranges(problems)
    check_payload_keys(problems)
    check_palettes(problems)
    check_webui_palettes(problems)
    check_caps(problems)

    if problems:
        print("API documentation has drifted from the firmware:\n")
        for p in problems:
            print("  - " + p)
        print("\n%d problem(s). Update the docs, or the ground-truth extraction in %s."
              % (len(problems), os.path.relpath(__file__, ROOT)))
        return 1

    print("API docs in sync: %d settings, %d system, %d device-state keys, %d error codes, "
          "%d numeric ranges."
          % (len(settings), len(system), len(device), len(errors),
             len(set(numeric_ranges()) - RANGE_EXEMPT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
