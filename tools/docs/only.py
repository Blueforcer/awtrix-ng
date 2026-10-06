"""Which clock a docs page or passage belongs to.

Pure Python without MkDocs, so the MkDocs hook (variants.py), the picture tool (panels.py)
and the reader test share one set of rules:

* a page: front matter `only: [tc002]` (a flow list) or `only: tc002`; without it, every
  clock;
* a passage: `<!-- only tc002 -->` ... `<!-- /only -->`, several names separated by spaces,
  on lines of their own or inline, nested where a passage narrows further. Markers inside
  fenced code are text.

Mistakes raise OnlyError; the hook reports them as build errors.
"""

import re

VARIANTS = {"esp32": "ESP32 / TC001", "esp32-s3": "ESP32-S3", "tc002": "TC002"}
ROOT = "root"
NAMES = set(VARIANTS) | {ROOT}

MARKER = re.compile(r"<!--\s*(/?)only\b([^>]*?)-->")
FENCE = re.compile(r"^\s*(`{3,}|~{3,})")
FRONT_MATTER = re.compile(r"\A---[ \t]*\r?\n(.*?)\r?\n---[ \t]*(?:\r?\n|\Z)", re.S)
ONLY = re.compile(r"^only:(.*)$", re.M)


class OnlyError(ValueError):
    pass


def names(text, where):
    found = text.split()
    if not found:
        raise OnlyError("%s: marker names no clock" % where)
    unknown = [n for n in found if n not in NAMES]
    if unknown:
        raise OnlyError("%s: unknown clock %s" % (where, ", ".join(unknown)))
    return set(found)


def page_only(text, where):
    front = FRONT_MATTER.match(text.lstrip("﻿"))
    found = ONLY.search(front.group(1)) if front else None
    if not found:
        return set(VARIANTS)
    value = found.group(1).strip()
    if value.startswith("[") and value.endswith("]"):
        value = value[1:-1]
    return names(value.replace(",", " "), where)


def filter_markdown(markdown, variant, where, origin=None):
    """The text of one clock. `origin`, a list, receives the source line of every line kept."""
    out = []
    stack = []
    keep = True
    fence = None

    def emit(text, number):
        out.append(text)
        if origin is not None:
            origin.append(number)

    for number, line in enumerate(markdown.splitlines(keepends=True), 1):
        match = FENCE.match(line)
        if fence:
            if match and match.group(1)[0] == fence[0] and len(match.group(1)) >= len(fence):
                fence = None
            if keep:
                emit(line, number)
            continue
        if match:
            fence = match.group(1)
            if keep:
                emit(line, number)
            continue

        markers = list(MARKER.finditer(line))
        if not markers:
            if keep:
                emit(line, number)
            continue

        alone = len(markers) == 1 and line.strip() == markers[0].group(0)
        pos = 0
        piece = []
        for m in markers:
            if keep:
                piece.append(line[pos:m.start()])
            pos = m.end()
            here = "%s:%d" % (where, number)
            if m.group(1):
                if not stack:
                    raise OnlyError("%s: <!-- /only --> without an opening marker" % here)
                if m.group(2).strip():
                    raise OnlyError("%s: the closing marker takes no names" % here)
                stack.pop()
            else:
                stack.append((number, variant in names(m.group(2), here)))
            keep = all(inside for _, inside in stack)
        if keep:
            piece.append(line[pos:])
        if not alone:
            emit("".join(piece), number)

    if stack:
        raise OnlyError("%s:%d: marker is never closed" % (where, stack[-1][0]))
    if fence:
        raise OnlyError("%s: code fence is never closed" % where)
    return "".join(out)
