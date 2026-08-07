#!/usr/bin/env python3
"""Fail if the matrix screenshots in docs/guides/ no longer match their examples.

`tools/gen_docshots.py` needs a built simulator, so it cannot run in CI and the
pictures are committed instead. That leaves the usual gap for generated files:
an example can be edited and the picture beneath it left showing the old
result, which is worse than no picture at all - the reader trusts it.

Every generated block records the hash of the code it illustrates:

    <!-- shot:begin id=hello hash=02ac930e -->

This checks that the hash still matches the block above it, that the file it
points at exists, and that no image is left behind after an example is deleted.

Run: python tools/check_docshots.py     (exit 1 on drift)
"""

import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIDES = os.path.join(ROOT, "docs", "guides")
SHOTS = os.path.join(ROOT, "docs", "assets", "shots")

FENCE = re.compile(r"^```([a-zA-Z0-9]*)[ \t]*\n(.*?)^```[ \t]*$", re.S | re.M)
BEGIN = re.compile(r"^<!--\s*shot:begin\b([^>]*)-->[ \t]*\n(.*?)^<!--\s*shot:end\s*-->[ \t]*$",
                   re.S | re.M)
IMAGE = re.compile(r"^!\[.*?\]\(([^)]+)\)", re.M)


def digest(body):
    return hashlib.sha256(body.encode("utf-8")).hexdigest()[:8]


def check():
    problems = []
    referenced = set()

    for name in sorted(os.listdir(GUIDES)):
        if not name.endswith(".md"):
            continue
        rel = "docs/guides/" + name
        path = os.path.join(GUIDES, name)
        with open(path, encoding="utf-8") as fh:
            text = fh.read()

        fences = [(m.end(), m.group(2)) for m in FENCE.finditer(text)]
        for m in BEGIN.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            attrs = dict(re.findall(r"(\w+)=(\S+)", m.group(1)))

            # The block this picture belongs to is the fence that ends closest
            # above it - the directive line may sit in between.
            above = [body for end, body in fences if end <= m.start()]
            if not above:
                problems.append("%s:%d: a shot block with no code above it" % (rel, line))
                continue
            want = digest(above[-1])
            if attrs.get("hash") != want:
                problems.append(
                    "%s:%d: the example changed since the picture was made "
                    "(hash %s, now %s) - rerun tools/gen_docshots.py"
                    % (rel, line, attrs.get("hash"), want))

            images = IMAGE.findall(m.group(2))
            if not images:
                problems.append("%s:%d: shot block contains no image" % (rel, line))
            for src in images:
                target = os.path.normpath(os.path.join(GUIDES, src))
                referenced.add(target)
                if not os.path.exists(target):
                    problems.append("%s:%d: missing image %s" % (rel, line, src))

    if os.path.isdir(SHOTS):
        for folder, _, files in os.walk(SHOTS):
            for f in files:
                full = os.path.join(folder, f)
                if full not in referenced:
                    problems.append("%s is not used by any page - delete it"
                                    % os.path.relpath(full, ROOT).replace("\\", "/"))

    return problems


def main():
    problems = check()
    if not problems:
        print("check_docshots: screenshots match their examples")
        return 0
    for p in problems:
        print("check_docshots: " + p)
    print("\n%d problem(s)" % len(problems))
    return 1


if __name__ == "__main__":
    sys.exit(main())
