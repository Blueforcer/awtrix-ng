"""MkDocs hook: build the documentation for one clock.

AWTRIX_DOCS_VARIANT picks the build: `esp32`, `esp32-s3`, `tc002`, or `root` for the
small landing site that sits above the three. tools/docs/build_site.py runs all four and
assembles them; a single `mkdocs serve` needs the variable set.

Authors mark what belongs to which clock:

* a page: front matter `only: [tc002]`. Pages without `only` are in all three clock builds,
  never in `root`.
* a passage: `<!-- only tc002 -->` ... `<!-- /only -->`, several names separated by spaces,
  on lines of their own or inline, nested where a passage narrows further. Markers inside
  fenced code are text.
* a link out of the current build: `site:<path>`, relative to the published root, for
  example `site:tc002/` or `site:` for the clock chooser.
* OpenAPI: `x-only: [tc002]` on any operation, parameter, property or enum entry.
* the clock chooser: `<!-- clocks -->` on a line of its own.

In the root build every clock page becomes a chooser page at the same path. It opens the
page for the clock the browser remembers, or for the only clock that has it, and otherwise
asks which clock the reader has.
"""

import html
import json
import os
import re
import sys

import yaml
from mkdocs.exceptions import PluginError
from mkdocs.structure.files import File, Files, InclusionLevel
from mkdocs.utils import meta as meta_utils

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import only  # noqa: E402
import panel_examples  # noqa: E402
from only import FENCE, NAMES, ROOT, VARIANTS  # noqa: E402

CLOCKS = [
    {"name": "Ulanzi TC001", "variant": "esp32", "panel": "32 × 8",
     "text": "Three buttons, battery, light and temperature sensor."},
    {"name": "Ulanzi TC002", "variant": "tc002", "panel": "52 × 16",
     "text": "The most advanced AWTRIX NG: speaker with synthesizer, internet radio, games with "
             "Bluetooth gamepads, a knob, voice assistant and iPhone notifications."},
    {"name": "ESP32 DIY", "variant": "esp32", "panel": "32–128 × 8",
     "text": "Your own WS2812 panel on an ESP32 board. AWTRIX 2 builds work too."},
    {"name": "ESP32-S3 DIY", "variant": "esp32-s3", "panel": "32–128 × 8",
     "text": "Your own panel on an ESP32-S3 board. With PSRAM and an amplifier board it also "
             "plays MP3s and internet radio."},
]

ROOT_ONLY_STATIC = ("firmware/",)
TC002_ONLY_STATIC = ("assets/tc002/image/",)

CLOCKS_MARKER = re.compile(r"^<!--\s*clocks\s*-->[ \t]*$", re.M)
HEADING = re.compile(r"^#\s+(.+?)(?:\s+\{[^}]*\})?\s*#*\s*$")
SITE_LINK = re.compile(r'(href|src)="site:([^"]*)"')

CHOOSER_PAGE = """---
title: Choose your clock
hide: [navigation, toc]
search: {exclude: true}
clocks: %(clocks)s
---

# Which clock do you have?

%(lead)s

%(chooser)s
"""

_state = {}


def _variant():
    value = os.environ.get("AWTRIX_DOCS_VARIANT", "")
    if value not in NAMES:
        raise PluginError(
            "AWTRIX_DOCS_VARIANT must be one of %s (got %r). "
            "Build the whole site with python tools/docs/build_site.py."
            % (", ".join(sorted(NAMES)), value))
    return value


def _names(text, where):
    try:
        return only.names(text, where)
    except only.OnlyError as error:
        raise PluginError(str(error))


def page_variants(path, where):
    with open(path, encoding="utf-8-sig") as f:
        text = f.read()
    try:
        return only.page_only(text, where)
    except only.OnlyError as error:
        raise PluginError(str(error))


def on_config(config):
    variant = _variant()
    docs_dir = config["docs_dir"]
    dropped = set()
    have = {}
    for top, _, names in os.walk(docs_dir):
        for name in names:
            if not name.endswith(".md"):
                continue
            full = os.path.join(top, name)
            rel = os.path.relpath(full, docs_dir).replace(os.sep, "/")
            variants = page_variants(full, rel)
            have[rel] = [v for v in VARIANTS if v in variants]
            if variant not in variants:
                dropped.add(rel)

    _state.clear()
    _state.update(variant=variant, dropped=dropped, have=have, links=[])

    config["nav"] = _prune(config["nav"], dropped, variant)
    base = config["site_url"].rstrip("/") + "/"
    extra = config["extra"]
    extra["variant"] = variant
    extra["variants"] = VARIANTS
    if variant != ROOT:
        config["site_url"] = base + variant + "/"
        extra["version"] = {"provider": "mike"}
        extra["homepage"] = "../"
    return config


def _prune(nav, dropped, variant):
    out = []
    for item in nav:
        if isinstance(item, str):
            if item not in dropped:
                out.append(item)
            continue
        (title, value), = item.items()
        title = filter_markdown(title, variant, "mkdocs.yml nav %r" % title)
        if isinstance(value, list):
            value = _prune(value, dropped, variant)
            if value:
                out.append({title: value})
        elif value not in dropped:
            out.append({title: value})
    return out


def on_files(files, config):
    variant = _state["variant"]
    kept = []
    for f in files:
        path = f.src_uri
        if path in _state["dropped"]:
            if variant == ROOT and _state["have"][path] and not path.startswith("assets/") \
                    and not f.inclusion.is_excluded():
                kept.append(chooser_page(f, config))
            continue
        if path.startswith(ROOT_ONLY_STATIC):
            continue
        if variant != "tc002" and path.startswith(TC002_ONLY_STATIC):
            continue
        if variant == ROOT and path.startswith("api/"):
            continue
        kept.append(f)
    return Files(kept)


def chooser(path, have):
    cards = []
    for clock in CLOCKS:
        if clock["variant"] not in have:
            continue
        cards.append(
            '<a class="clock-card" href="site:%s/%s" data-variant="%s">'
            '<span class="clock-card__panel">%s</span>'
            '<span class="clock-card__name">%s</span>'
            '<span class="clock-card__text">%s</span></a>'
            % (clock["variant"], html.escape(path), clock["variant"],
               html.escape(clock["panel"]), html.escape(clock["name"]),
               html.escape(clock["text"])))
    return '<nav class="clock-chooser" aria-label="Clocks">\n%s\n</nav>' % "\n".join(cards)


def _heading(f):
    with open(f.abs_src_path, encoding="utf-8-sig") as source:
        body, meta = meta_utils.get_data(source.read())
    if meta.get("title"):
        return meta["title"]
    fence = False
    for line in filter_markdown(body, _state["have"][f.src_uri][0], f.src_uri).splitlines():
        if FENCE.match(line):
            fence = not fence
            continue
        m = None if fence else HEADING.match(line)
        if m:
            return m.group(1)
    return None


def chooser_page(f, config):
    have = _state["have"][f.src_uri]
    title = _heading(f)
    lead = ("Pick yours to open **%s**." % title) if title else "Pick yours to open this page."
    content = CHOOSER_PAGE % {
        "clocks": json.dumps({"path": f.url, "have": have}),
        "lead": lead,
        "chooser": chooser(f.url, have),
    }
    return File.generated(config, f.src_uri, content=content, inclusion=InclusionLevel.NOT_IN_NAV)


def filter_markdown(markdown, variant, where, origin=None):
    try:
        return only.filter_markdown(markdown, variant, where, origin)
    except only.OnlyError as error:
        raise PluginError(str(error))


def on_page_markdown(markdown, page, config, files):
    variant = _state["variant"]
    if variant == ROOT and page.is_homepage:
        page.meta["hide"] = list(page.meta.get("hide", [])) + ["navigation"]
    origin = []
    markdown = filter_markdown(markdown, variant, page.file.src_uri, origin)
    if variant != ROOT:
        docs = config["docs_dir"]
        try:
            markdown = panel_examples.insert_figures(
                markdown, variant, page.file.src_uri,
                lambda path: os.path.exists(os.path.join(docs, path)), origin)
        except panel_examples.PanelError as error:
            raise PluginError(str(error))
    return CLOCKS_MARKER.sub(lambda m: chooser("", list(VARIANTS)), markdown)


def on_page_content(html, page, config, files):
    depth = page.url.count("/")
    if _state["variant"] != ROOT:
        depth += 1
    up = "../" * depth or "./"

    def resolve(m):
        target = m.group(2)
        path = target.split("#", 1)[0].split("?", 1)[0]
        _state["links"].append({"page": page.file.src_uri, "target": path})
        return '%s="%s%s"' % (m.group(1), up, target)

    return SITE_LINK.sub(resolve, html)


def on_page_context(context, page, config, nav):
    if _state["variant"] != ROOT:
        config["extra"]["scope"] = context["base_url"] + "/../"
    return context


def on_post_page(output, page, config):
    config["extra"].pop("scope", None)
    return output


def _filter_api(node, variant, where):
    if isinstance(node, dict):
        out = {}
        for key, value in node.items():
            if isinstance(value, (dict, list)) and _excluded(value, variant, where):
                continue
            if key == "x-only":
                continue
            out[key] = _filter_api(value, variant, where)
        props = out.get("properties")
        if isinstance(out.get("required"), list) and isinstance(props, dict):
            out["required"] = [r for r in out["required"] if r in props]
            if not out["required"]:
                del out["required"]
        return out
    if isinstance(node, list):
        return [_filter_api(v, variant, where) for v in node
                if not _excluded(v, variant, where)]
    return node


def _excluded(node, variant, where):
    if isinstance(node, dict) and "x-only" in node:
        only = node["x-only"]
        if isinstance(only, str):
            only = [only]
        return variant not in _names(" ".join(only), where)
    return False


def on_post_build(config):
    variant = _state["variant"]
    site = config["site_dir"]
    source = os.path.join(config["docs_dir"], "api", "openapi.yaml")
    if variant != ROOT and os.path.exists(source):
        with open(source, encoding="utf-8") as f:
            spec = yaml.safe_load(f)
        spec = _filter_api(spec, variant, "api/openapi.yaml")
        target = os.path.join(site, "api", "openapi.yaml")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            yaml.safe_dump(spec, f, sort_keys=False, allow_unicode=True, width=100)

    with open(os.path.join(site, ".site-links.json"), "w", encoding="utf-8") as f:
        json.dump({"variant": variant, "links": _state["links"]}, f)
