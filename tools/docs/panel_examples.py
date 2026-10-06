"""Pictures of the display in the docs: markers, examples, file names and figures.

A marker on a line of its own stands directly before a fenced code block:

    <!-- panel style=diagram mark=row:6 -->

The block is the example. tools/docs/panels.py renders it on awtrix-linux; the MkDocs hook
(variants.py) puts the picture under the block. Pure Python: no MkDocs, no runtime.

Options: style=led|diagram, mark=row:N|col:N, cols=A-B (B may be W, the last column),
boxes (outline the layout boxes), at=MS (when a still picture is taken), motion=SECONDS
(record an animation), alt="TEXT". A pictured example is complete: a whole script, a JSON
payload, or one curl request to /api/v1/apps/pushed/<name> or /api/v1/notifications.
"""

import hashlib
import json
import re
import shlex
from dataclasses import dataclass

SIZES = {"esp32": (32, 8), "esp32-s3": (32, 8), "tc002": (52, 16)}
ASSET_DIR = "assets/panels"
STYLES = ("led", "diagram")
ALT = {
    "script": "What the script shows on the display",
    "pushed": "What the app shows on the display",
    "notification": "What the notification shows on the display",
}

MARKER = re.compile(r"^(?P<indent>[ \t]*)<!--\s*panel\b(?P<options>.*?)-->[ \t]*\r?\n?$")
FENCE = re.compile(r"^(?P<indent>[ \t]*)(?P<fence>`{3,}|~{3,})[ \t]*(?P<lang>[^`\s]*)")
MARK = re.compile(r"^(row|col):(\d+)$")
COLS = re.compile(r"^(\d+)-(\d+|W)$")
SECONDS = re.compile(r"^\d+(?:\.\d+)?$")
API = re.compile(r"/api/v1/(?:apps/pushed/[A-Za-z0-9_-]+|(notifications))(?![\w/])")
RETURN = re.compile(r"^return\s+\w+\(\)\s*$", re.M)


class PanelError(ValueError):
    pass


@dataclass
class Example:
    kind: str     # "script", "pushed" or "notification"
    body: object  # the script source, or the JSON payload as a dict


@dataclass
class Panel:
    line: int        # 1-based line of the marker in the text searched
    options: dict
    lang: str
    code: str        # the block without its fences and indentation
    indent: str      # indentation of the marker and the block
    close: int       # 0-based index of the closing fence line
    example: Example
    source: int      # 1-based line of the marker in the page's source file


def parse_options(text, where):
    options = {"style": "led"}
    try:
        tokens = shlex.split(text)
    except ValueError as error:
        raise PanelError("%s: %s" % (where, error))
    for token in tokens:
        key, eq, value = token.partition("=")
        if key == "boxes" and not eq:
            options["boxes"] = True
        elif key == "style" and value in STYLES:
            options["style"] = value
        elif key == "mark" and MARK.match(value):
            axis, number = MARK.match(value).groups()
            options["mark"] = [axis, int(number)]
        elif key == "cols" and COLS.match(value):
            first, last = COLS.match(value).groups()
            if last != "W" and int(first) > int(last):
                raise PanelError("%s: cols=%s runs backwards" % (where, value))
            options["cols"] = [int(first), last if last == "W" else int(last)]
        elif key == "at" and value.isdigit():
            options["at"] = int(value)
        elif key == "motion" and SECONDS.match(value) and 0 < float(value) <= 10:
            options["motion"] = float(value)
        elif key == "alt" and value:
            options["alt"] = value
        else:
            raise PanelError("%s: unknown panel option %r" % (where, token))
    return options


def example(lang, code, where):
    if lang == "berry":
        if not RETURN.search(code):
            raise PanelError("%s: a pictured script must be complete, ending in return YourApp()"
                             % where)
        return Example("script", code)
    if lang == "json":
        return Example("pushed", _object(code, where))
    if lang in ("bash", "sh", "shell"):
        return _curl(code, where)
    raise PanelError("%s: a panel marker needs a berry, json or bash block, not %r"
                     % (where, lang))


def _object(text, where):
    try:
        value = json.loads(text)
    except ValueError as error:
        raise PanelError("%s: the example is not JSON: %s" % (where, error))
    if not isinstance(value, dict):
        raise PanelError("%s: the example must be a JSON object" % where)
    return value


def _curl(code, where):
    try:
        words = shlex.split(code.replace("\\\r\n", " ").replace("\\\n", " "), comments=True)
    except ValueError as error:
        raise PanelError("%s: %s" % (where, error))
    targets = [m for m in (API.search(word) for word in words) if m]
    bodies = [words[i + 1] for i, word in enumerate(words[:-1])
              if word in ("-d", "--data", "--data-raw")]
    if len(targets) != 1 or len(bodies) != 1:
        raise PanelError("%s: a pictured curl example sends one -d body to "
                         "/api/v1/apps/pushed/<name> or /api/v1/notifications" % where)
    kind = "notification" if targets[0].group(1) else "pushed"
    return Example(kind, _object(bodies[0], where))


def _closes(line, fence):
    m = FENCE.match(line)
    return (bool(m) and m.group("fence")[0] == fence[0] and len(m.group("fence")) >= len(fence)
            and not line.strip()[len(m.group("fence")):].strip())


def _dedent(lines, indent):
    if not indent:
        return "".join(lines)
    return "".join(l[len(indent):] if l.startswith(indent) else l.lstrip(" \t") for l in lines)


def find_panels(markdown, where, origin=None):
    """Every marked example. `origin` maps the lines of a filtered text to its source lines."""
    lines = markdown.splitlines(keepends=True)
    found = []
    fence = None
    i = 0
    while i < len(lines):
        line = lines[i]
        if fence:
            if _closes(line, fence):
                fence = None
            i += 1
            continue
        opening = FENCE.match(line)
        if opening:
            fence = opening.group("fence")
            i += 1
            continue
        marker = MARKER.match(line)
        if not marker:
            i += 1
            continue
        source = origin[i] if origin else i + 1
        here = "%s:%d" % (where, source)
        options = parse_options(marker.group("options"), here)
        opening = FENCE.match(lines[i + 1]) if i + 1 < len(lines) else None
        if not opening:
            raise PanelError("%s: a panel marker must stand directly before a code block" % here)
        close = i + 2
        while close < len(lines) and not _closes(lines[close], opening.group("fence")):
            close += 1
        if close == len(lines):
            raise PanelError("%s: the code block is never closed" % here)
        code = _dedent(lines[i + 2:close], opening.group("indent"))
        lang = opening.group("lang")
        found.append(Panel(i + 1, options, lang, code, marker.group("indent"), close,
                           example(lang, code, here), source))
        i = close + 1
    return found


def _normal(code):
    return "\n".join(l.rstrip() for l in code.replace("\r\n", "\n").split("\n")).strip("\n")


def picture_hash(panel, size):
    options = {k: v for k, v in panel.options.items() if k != "alt"}
    key = json.dumps({"code": _normal(panel.code), "lang": panel.lang, "size": list(size),
                      "options": options}, sort_keys=True)
    return hashlib.sha256(key.encode("utf-8")).hexdigest()[:12]


def picture_path(panel, size):
    return "%s/%s.svg" % (ASSET_DIR, picture_hash(panel, size))


def insert_figures(markdown, variant, page, exists, origin=None):
    """Put the picture of each marked example under its block and drop the markers.

    `page` is the Markdown path relative to docs/, `exists(path)` answers whether a path
    relative to docs/ exists, `origin` maps filtered lines to source lines. A missing picture
    is an error naming the page and its source line.
    """
    panels = find_panels(markdown, page, origin)
    if not panels:
        return markdown
    size = SIZES[variant]
    up = "../" * page.count("/")
    lines = markdown.splitlines(keepends=True)
    for panel in reversed(panels):
        path = picture_path(panel, size)
        if not exists(path):
            raise PanelError("%s:%d: picture missing, run python3 tools/docs/panels.py"
                             % (page, panel.source))
        alt = (panel.options.get("alt") or ALT[panel.example.kind]).replace("]", ")")
        end = "\r\n" if lines[panel.close].endswith("\r\n") else "\n"
        if not lines[panel.close].endswith("\n"):
            lines[panel.close] += end
        image = "%s![%s](%s%s){ .awx-panel loading=lazy }" % (panel.indent, alt, up, path)
        lines[panel.close + 1:panel.close + 1] = [end, image + end, end]
        del lines[panel.line - 1]
    return "".join(lines)
