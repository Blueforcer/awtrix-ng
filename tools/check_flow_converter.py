#!/usr/bin/env python3
"""Fail if the flow converter's maps drift away from the docs they mirror.

The converter on guides/migrating-from-awtrix3.md applies the same renames the
page's key-map tables teach by hand. A row edited in the page without touching
maps.js -- or the other way round -- would have the page and the widget
disagreeing at the same scroll position. The maps also point warnings at
anchors across the docs and translate TEFF numbers by the row order of the
transitions table, both of which silently rot when a heading or a row moves.

maps.js keeps every table as a strict-JSON literal for exactly this check:
each `export const NAME = <literal>;` is extracted with a balanced scan and
json.loads, so a stray comment or trailing comma inside a literal fails here
before it confuses anyone in the browser.

Run: python tools/check_flow_converter.py     (exit 1 on drift)
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPS = os.path.join(ROOT, "docs", "assets", "flow-converter", "maps.js")
MIGRATION = os.path.join(ROOT, "docs", "guides", "migrating-from-awtrix3.md")
SETTINGS = os.path.join(ROOT, "docs", "reference", "settings.md")
VISUALS = os.path.join(ROOT, "docs", "reference", "visuals.md")

# The NG keys the engine's TFORMAT/DFORMAT parsers emit; they are spelled in
# engine.js, so their existence in settings.md is asserted here.
TFORMAT_TARGETS = ["time24h", "timeLeadingZero", "timeShowSeconds",
                   "timeShowAmPm", "timeSeparatorMode"]
DFORMAT_TARGETS = ["dateOrder", "dateSeparator", "dateYearMode"]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def extract_literals(js):
    """Every `export const NAME = <json>;` as {NAME: parsed}."""
    out = {}
    for m in re.finditer(r"^export const (\w+) = ", js, re.M):
        start = m.end()
        opener = js[start]
        closer = {"{": "}", "[": "]"}[opener]
        depth, i, in_str = 0, start, False
        while True:
            c = js[i]
            if in_str:
                if c == "\\":
                    i += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == opener:
                depth += 1
            elif c == closer:
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out[m.group(1)] = json.loads(js[start:i + 1])
    return out


def slugify(heading):
    """mkdocs' toc slug: drop punctuation, lower-case, collapse to dashes."""
    text = re.sub(r"[^\w\s-]", "", heading.strip().lower())
    return re.sub(r"[-\s]+", "-", text.strip())


def headings(md):
    return {slugify(m.group(1)) for m in re.finditer(r"^#{1,6} +(.+?)\s*$", md, re.M)}


def table_rows(md, start_heading, stop_re):
    """First-column and second-column cell text of every table row between
    start_heading and the next heading matching stop_re."""
    m = re.search(r"^#+ +" + re.escape(start_heading) + r"\s*$", md, re.M)
    if not m:
        return None
    stop = re.search(stop_re, md[m.end():], re.M)
    section = md[m.end():m.end() + stop.start()] if stop else md[m.end():]
    rows = []
    for line in section.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 2 and not set(cells[0]) <= {"-", " ", ":"}:
            rows.append(cells)
    return rows


def first_code(cell):
    m = re.search(r"`([^`]+)`", cell)
    return m.group(1) if m else None


def check_key_map(problems, maps, page):
    key_map = maps["KEY_MAP"]
    rows = table_rows(page, "The key map", r"^## (?!#)")
    if rows is None:
        problems.append("migrating-from-awtrix3.md: heading 'The key map' not found")
        return
    seen = set()
    for cells in rows:
        if cells[0] in ("AWTRIX 3", ""):
            continue
        old = first_code(cells[0])
        if old is None:
            continue
        base = old.split(".")[0]
        spec = key_map.get(base)
        if spec is None:
            problems.append(f"maps.js KEY_MAP: page row `{old}` has no entry")
            continue
        seen.add(base)
        new = first_code(cells[1]) if len(cells) > 1 else None
        if spec["kind"] in ("rename", "seconds", "enum"):
            if new != spec["to"]:
                problems.append(
                    f"maps.js KEY_MAP: `{base}` maps to `{spec['to']}` but the page row says `{new}`")
        elif spec["kind"] == "dead":
            # A dead key's NG cell is "-" or points at a replacement call --
            # anything that reads as a plain payload key is a contradiction.
            if new is not None and re.fullmatch(r"\w+", new):
                problems.append(
                    f"maps.js KEY_MAP: `{base}` is dead in maps.js but the page maps it to `{new}`")
    for key in key_map:
        if key not in seen:
            problems.append(f"migrating-from-awtrix3.md: KEY_MAP key `{key}` has no row in the key map")


def check_draw_map(problems, maps, page):
    pairs = dict(re.findall(r"`(d[a-z]{1,2})` → `(\w+)`", page))
    if pairs != maps["DRAW_MAP"]:
        problems.append(
            f"maps.js DRAW_MAP {maps['DRAW_MAP']} != page prose {pairs}")


def check_endpoints(problems, maps, page):
    ng_paths = set()
    for e in maps["ENDPOINT_MAP"]:
        if "ng" in e:
            ng_paths.add(e["ng"]["path"])
        if "emptyBody" in e:
            ng_paths.add(e["emptyBody"]["path"])
    for m in re.finditer(r"`(?:GET|POST|PUT|PATCH|DELETE) (/api/v1/[^\s`]+)`", page):
        path = re.sub(r"/(weather|x)\b", "/{name}", m.group(1))
        if path not in ng_paths and "{name}" not in path:
            path_name = re.sub(r"/api/v1/apps/(pushed/)?\w+$",
                               lambda mm: "/api/v1/apps/" + (mm.group(1) or "") + "{name}",
                               path)
            if path_name not in ng_paths:
                problems.append(
                    f"maps.js ENDPOINT_MAP: page endpoint `{m.group(1)}` has no NG path in the map")

    topic_pairs = {(t["a3"], t["ng"]) for t in maps["TOPIC_MAP"] if "ng" in t}
    where = table_rows(page, "Where to send", r"^## (?!#)") or []
    for cells in where:
        for a3, ng in re.findall(r"`\[?[<\[]?prefix[>\]]?\]?/([\w/]+)`.*?`<prefix>/([\w/]+)`",
                                 "|".join(cells)):
            a3n = re.sub(r"\bweather\b", "{name}", a3)
            ngn = re.sub(r"\bweather\b", "{name}", ng)
            if (a3n, ngn) not in topic_pairs:
                problems.append(
                    f"maps.js TOPIC_MAP: page topic pair {a3n} -> {ngn} missing from the map")


def check_settings(problems, maps, settings_md):
    known = {m.group(1) for m in re.finditer(r"`(\w+)`", settings_md)}
    for a3, spec in maps["SETTINGS_MAP"].items():
        targets = []
        if spec["kind"] in ("rename", "seconds", "teff", "colorOrNull"):
            targets = [spec["to"]]
        elif spec["kind"] == "nested":
            targets = spec["to"].split(".")
        elif spec["kind"] == "tformat":
            targets = TFORMAT_TARGETS
        elif spec["kind"] == "dformat":
            targets = DFORMAT_TARGETS
        for t in targets:
            if t not in known:
                problems.append(
                    f"maps.js SETTINGS_MAP: `{a3}` targets `{t}`, which settings.md does not document")


def check_teff(problems, maps, visuals_md):
    rows = table_rows(visuals_md, "Transitions", r"^## (?!#)")
    names = [first_code(c[0]) for c in (rows or [])
             if first_code(c[0]) and c[0] != "Name" and first_code(c[0])[0].isupper()]
    if names[:len(maps["TEFF_NAMES"])] != maps["TEFF_NAMES"]:
        problems.append(
            "maps.js TEFF_NAMES no longer matches the first rows of the transitions table "
            f"({names[:11]}) -- the TEFF index map depends on that order")


def check_anchors(problems, maps):
    targets = []
    for code, spec in maps["WARNINGS"].items():
        if "anchor" in spec:
            targets.append((f"WARNINGS.{code}", spec["page"], spec["anchor"]))
    for k, spec in maps["KEY_MAP"].items():
        if "anchor" in spec:
            targets.append((f"KEY_MAP.{k}", "guides/migrating-from-awtrix3.md", spec["anchor"]))
    for k, spec in maps["SETTINGS_MAP"].items():
        if "anchor" in spec:
            targets.append((f"SETTINGS_MAP.{k}", "guides/migrating-from-awtrix3.md", spec["anchor"]))
    cache = {}
    for owner, page, anchor in targets:
        path = os.path.join(ROOT, "docs", *page.split("/"))
        if page not in cache:
            if not os.path.exists(path):
                problems.append(f"maps.js {owner}: page {page} does not exist")
                cache[page] = set()
                continue
            cache[page] = headings(read(path))
        if anchor not in cache[page]:
            problems.append(f"maps.js {owner}: anchor #{anchor} not found in {page}")


def main():
    problems = []
    maps = extract_literals(read(MAPS))
    for name in ("KEY_MAP", "DRAW_MAP", "TEFF_NAMES", "SETTINGS_MAP",
                 "ENDPOINT_MAP", "TOPIC_MAP", "WARNINGS"):
        if name not in maps:
            problems.append(f"maps.js: export {name} not found or not strict JSON")
    if not problems:
        page = read(MIGRATION)
        check_key_map(problems, maps, page)
        check_draw_map(problems, maps, page)
        check_endpoints(problems, maps, page)
        check_settings(problems, maps, read(SETTINGS))
        check_teff(problems, maps, read(VISUALS))
        check_anchors(problems, maps)
    for p in problems:
        print(p)
    if problems:
        print(f"\n{len(problems)} problem(s). maps.js and the docs must tell the same story.")
        return 1
    print("flow converter maps match the docs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
