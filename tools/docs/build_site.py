"""Build the published documentation site: one build per clock plus the landing site.

    python tools/docs/build_site.py [--out site]

Builds `root`, `esp32`, `esp32-s3` and `tc002` with `mkdocs build --strict` through the
hook in tools/docs/variants.py, then assembles:

    site/                 landing site (root build)
    site/<variant>/       one documentation per clock
    site/versions.json    the clock selector in the header
    site/firmware/        browser flasher, browser update and TC002 installer files
    site/<page path>/     the clock chooser for every clock page, from the root build

Fails on broken anchors, on `site:` links to paths that do not exist, on a clock page
without a chooser at its path, on text that names another clock, and on words that have
one name only (the terms in docs/developers/writing-docs.md, outside code).
tools/docs/lint-allow.txt lists the exceptions; the developer pages and the AI prompt
cover every clock and are not checked, and release notes keep the words of their day.

    python tools/docs/build_site.py --only tc002

checks one clock without assembling anything and lists every problem at once.
"""

import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from only import ROOT, VARIANTS  # noqa: E402

FOREIGN = {
    "esp32": [r"TC002", r"ESP32-S3", r"\bS3\b"],
    "esp32-s3": [r"TC002", r"TC001"],
    "tc002": [r"ESP32", r"\bS3\b", r"TC001"],
}

UNLINTED = ("developers/", "guides/ai-prompt/")

DIY = ("advanced/diy-build/", "reference/gpio/", "reference/system/")
TERMS = [
    (re.compile(r"\b(?:the|app) loop\b", re.I), "the rotation", ()),
    (re.compile(r"\bmatrix\b(?!-)(?! fonts?\b)"), "display", DIY),
    (re.compile(r"\b(?:on|the) screens?\b", re.I), "display, or shown", ()),
    (re.compile(r"\bpanels?\b"), "display", DIY),
    (re.compile(r"\be\.g\.", re.I), "for example", ()),
]
TERMS_UNLINTED = UNLINTED + ("releases/",)
CODE = re.compile(r"<(code|pre)\b.*?</\1>", re.S)

ARTICLE = re.compile(r'<article class="md-content__inner[^"]*">(.*?)</article>', re.S)
NAV = re.compile(r'md-sidebar--primary(.*?)(?:md-sidebar--secondary|class="md-content")', re.S)
TAG = re.compile(r"<[^>]+>")
SKIP = re.compile(r"<(script|style)\b.*?</\1>", re.S)


def build(variant, out):
    env = dict(os.environ, AWTRIX_DOCS_VARIANT=variant)
    cmd = [sys.executable, "-m", "mkdocs", "build", "--strict", "-d", out]
    result = subprocess.run(cmd, cwd=REPO, env=env, capture_output=True, text=True)
    log = result.stdout + result.stderr
    if result.returncode != 0:
        sys.stderr.write(log)
        raise SystemExit("%s: mkdocs build failed" % variant)
    anchors = [l for l in log.splitlines() if "does not contain an anchor" in l]
    if anchors:
        sys.stderr.write("\n".join(anchors) + "\n")
        raise SystemExit("%s: broken in-page anchors (a linked heading was renamed or removed)"
                         % variant)
    print("built %s" % variant)


def check_one(variant):
    work = tempfile.mkdtemp(prefix="awtrix-docs-")
    try:
        env = dict(os.environ, AWTRIX_DOCS_VARIANT=variant)
        cmd = [sys.executable, "-m", "mkdocs", "build", "-d", work]
        result = subprocess.run(cmd, cwd=REPO, env=env, capture_output=True, text=True)
        log = [l for l in (result.stdout + result.stderr).splitlines()
               if l.startswith(("WARNING", "ERROR")) or "does not contain an anchor" in l]
        if result.returncode != 0:
            sys.stderr.write("\n".join(log) + "\n")
            raise SystemExit("%s: mkdocs build failed" % variant)
        problems = log
        if variant != ROOT:
            problems += lint(variant, work, load_allow())
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("\n".join(problems))
    raise SystemExit("%d problem(s)" % len(problems) if problems else 0)


def pages(site):
    found = []
    for top, _, names in os.walk(site):
        if "index.html" not in names:
            continue
        rel = os.path.relpath(top, site).replace(os.sep, "/")
        rel = "" if rel == "." else rel + "/"
        if rel.startswith(("assets/", "search/")):
            continue
        found.append(rel)
    return found


def load_allow():
    allow = {}
    path = os.path.join(HERE, "lint-allow.txt")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                key, _, phrase = line.partition(": ")
                allow.setdefault(key, []).append(phrase)
    return allow


def page_text(path, part=ARTICLE, code=True):
    with open(path, encoding="utf-8") as f:
        doc = f.read()
    m = part.search(doc)
    body = SKIP.sub(" ", m.group(1) if m else "")
    if not code:
        body = CODE.sub(" ", body)
    return html.unescape(TAG.sub(" ", body))


def term_problems(variant, rel, text):
    problems = []
    for pattern, word, pages in TERMS:
        if rel.startswith(pages):
            continue
        for m in pattern.finditer(text):
            start = max(0, m.start() - 50)
            context = " ".join(text[start:m.end() + 50].split())
            problems.append('%s/%s: %r, write "%s" in "...%s..."'
                            % (variant, rel, m.group(0), word, context))
    return problems


def lint(variant, site, allow):
    problems = []
    patterns = [re.compile(p, re.I) for p in FOREIGN[variant]]
    nav = page_text(os.path.join(site, "index.html"), NAV)
    for title in re.split(r"\s{2,}", nav):
        if title.strip() in ("AWTRIX NG",):
            continue
        for pattern in patterns:
            if pattern.search(title):
                problems.append("%s/nav: %r" % (variant, " ".join(title.split())))
    for rel in pages(site):
        if rel.startswith(UNLINTED):
            continue
        text = page_text(os.path.join(site, rel, "index.html"))
        if "<!-- only" in text or "<!-- /only" in text:
            problems.append("%s%s: leftover only marker" % (variant + "/", rel))
        allowed = allow.get(variant + "/" + rel, []) + allow.get("*/" + rel, [])
        for phrase in allowed:
            text = text.replace(phrase, " ")
        for pattern in patterns:
            for m in pattern.finditer(text):
                start = max(0, m.start() - 50)
                context = " ".join(text[start:m.end() + 50].split())
                problems.append("%s/%s: %r in \"...%s...\"" % (variant, rel, m.group(0), context))
        if not rel.startswith(TERMS_UNLINTED):
            problems += lint_terms(variant, rel, os.path.join(site, rel, "index.html"), allowed)
    return problems


def lint_terms(variant, rel, path, allowed):
    plain = " ".join(page_text(path, code=False).split())
    for phrase in allowed:
        plain = plain.replace(phrase, " ")
    return term_problems(variant, rel, plain)


def check_site_links(out, builds):
    problems = []
    for variant, site in builds.items():
        manifest = os.path.join(site, ".site-links.json")
        with open(manifest, encoding="utf-8") as f:
            links = json.load(f)["links"]
        os.remove(os.path.join(out if variant == ROOT else os.path.join(out, variant),
                               ".site-links.json"))
        for link in links:
            target = link["target"]
            path = os.path.join(out, target)
            if target.endswith("/") or target == "":
                path = os.path.join(path, "index.html")
            if not os.path.exists(path):
                problems.append("%s/%s: site:%s does not exist" % (variant, link["page"], target))
    return problems


def main():
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=os.path.join(REPO, "site"))
    parser.add_argument("--no-lint", action="store_true",
                        help="skip the text checks (other clocks, terms)")
    parser.add_argument("--only", choices=[ROOT] + list(VARIANTS),
                        help="build and check one clock, print every problem, assemble nothing")
    args = parser.parse_args()
    if args.only:
        check_one(args.only)
    out = os.path.abspath(args.out)

    work = tempfile.mkdtemp(prefix="awtrix-docs-")
    try:
        builds = {}
        for variant in [ROOT] + list(VARIANTS):
            builds[variant] = os.path.join(work, variant)
            build(variant, builds[variant])

        problems = []
        if not args.no_lint:
            allow = load_allow()
            for variant in VARIANTS:
                problems += lint(variant, builds[variant], allow)

        if os.path.exists(out):
            shutil.rmtree(out)
        shutil.copytree(builds[ROOT], out)
        for variant in VARIANTS:
            shutil.copytree(builds[variant], os.path.join(out, variant))

        firmware = os.path.join(REPO, "docs", "firmware")
        if os.path.isdir(firmware):
            shutil.copytree(firmware, os.path.join(out, "firmware"))

        with open(os.path.join(out, "versions.json"), "w", encoding="utf-8") as f:
            json.dump([{"version": v, "title": t, "aliases": []} for v, t in VARIANTS.items()],
                      f, indent=1)

        root_pages = set(pages(builds[ROOT]))
        clock_pages = set()
        for variant in VARIANTS:
            clock_pages.update(pages(builds[variant]))
        for path in sorted(clock_pages - root_pages):
            problems.append("root/%s: no clock chooser for this page" % path)
        choosers = len(clock_pages & root_pages)

        problems += check_site_links(out, builds)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    if problems:
        sys.stderr.write("\n".join(problems) + "\n")
        raise SystemExit("%d problem(s)" % len(problems))
    print("site assembled in %s (%d clock choosers)" % (out, choosers))


if __name__ == "__main__":
    main()
