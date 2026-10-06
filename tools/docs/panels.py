"""Render the pictures of the display that the docs show under marked examples.

    python3 tools/docs/panels.py [--only CLOCK] [--force] [--check] [--binary PATH]

Finds every <!-- panel --> marker in docs/ for each clock (see panel_examples.py), renders the
example on awtrix-linux at that clock's display size and writes docs/assets/panels/<hash>.svg.
A picture that exists stays as it is unless --force. --check renders every marker again and
compares the first frame with the picture on disk; run it before a release, since a renderer
change does not change any example. A picture that differs is rendered once more, a fraction of
a second later, so that content repeating every second shows up too; one whose first frame
changes from render to render (an effect, the time) is reported and not compared. Pictures no
marker uses are deleted (not with --only or --check). Needs a host build (cmake --build --preset
host); on Windows, run it in WSL.
"""

import argparse
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DOCS = REPO / "docs"
sys.path.insert(0, str(HERE))

import only  # noqa: E402
import panel_examples as examples  # noqa: E402
import panel_host  # noqa: E402
import panel_svg  # noqa: E402

MAX_BYTES = 60000
PHASE_SHIFT_S = 0.45
FRAME0 = re.compile(r'data-frame0="([0-9a-f]+)"')


@dataclass
class Job:
    size: tuple
    panel: examples.Panel
    page: str


def collect(docs, clocks):
    jobs = {}
    for path in sorted(Path(docs).rglob("*.md")):
        rel = path.relative_to(docs).as_posix()
        text = path.read_text(encoding="utf-8-sig")
        for clock in sorted(only.page_only(text, rel) & set(clocks)):
            size = examples.SIZES[clock]
            origin = []
            filtered = only.filter_markdown(text, clock, rel, origin)
            for panel in examples.find_panels(filtered, rel, origin):
                jobs.setdefault(examples.picture_hash(panel, size), Job(size, panel, rel))
    return jobs


def columns(option, width):
    if not option:
        return None
    first, last = option
    return [first, width - 1 if last == "W" else last]


def boxes(panel):
    if not panel.options.get("boxes") or not isinstance(panel.example.body, dict):
        return []
    regions = panel.example.body.get("layout", {}).get("regions", [])
    return [region["box"] for region in regions if "box" in region]


def frame0(svg):
    found = FRAME0.search(svg)
    return found.group(1) if found else None


def verdict(on_disk, fresh, render_again):
    """same, differs, or unsteady: a picture whose first frame changes from render to render."""
    if on_disk == fresh:
        return "same"
    return "differs" if render_again() == fresh else "unsteady"


def render_again(host, panel):
    """The first frame of one more render, out of phase with anything that repeats each second."""
    time.sleep(PHASE_SHIFT_S)
    return frame0(picture(host, panel))


def picture(host, panel):
    options = panel.options
    frames = host.frames(panel.example, options.get("at", 600), options.get("motion"))
    extra = dict(mark=options.get("mark"), cols=columns(options.get("cols"), host.width),
                 boxes=boxes(panel))
    svg = panel_svg.render(frames, host.width, host.height, options["style"],
                           panel_host.FRAME_MS, **extra)
    if len(svg) > MAX_BYTES and len(frames) > 1:
        svg = panel_svg.render(frames[::2], host.width, host.height, options["style"],
                               2 * panel_host.FRAME_MS, **extra)
    return svg


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", choices=list(only.VARIANTS))
    parser.add_argument("--force", action="store_true", help="render existing pictures, too")
    parser.add_argument("--check", action="store_true",
                        help="render everything again and compare, write nothing")
    parser.add_argument("--binary", default=str(REPO / "build" / "host" / "awtrix-linux"))
    args = parser.parse_args()

    target = DOCS / examples.ASSET_DIR
    target.mkdir(parents=True, exist_ok=True)
    try:
        jobs = collect(DOCS, [args.only] if args.only else list(only.VARIANTS))
    except (only.OnlyError, examples.PanelError) as error:
        print(error, file=sys.stderr)
        return 1
    todo = {name: job for name, job in jobs.items()
            if args.force or args.check or not (target / (name + ".svg")).exists()}
    problems = []
    for size in sorted({job.size for job in todo.values()}):
        try:
            with panel_host.PanelHost(args.binary, *size) as host:
                for name, job in sorted(todo.items()):
                    if job.size != size:
                        continue
                    where = "%s:%d" % (job.page, job.panel.source)
                    try:
                        svg = picture(host, job.panel)
                    except panel_host.HostError as error:
                        problems.append("%s: %s" % (where, error))
                        continue
                    path = target / (name + ".svg")
                    if args.check:
                        old = frame0(path.read_text(encoding="utf-8")) if path.exists() else None
                        result = verdict(old, frame0(svg),
                                         lambda: render_again(host, job.panel))
                        if result == "differs":
                            problems.append("%s: the picture differs from %s" % (where, path.name))
                        elif result == "unsteady":
                            print("%s: changes on its own (an effect or the time), not compared"
                                  % where)
                    else:
                        path.write_text(svg, encoding="utf-8", newline="\n")
                        print("wrote %s for %s" % (path.relative_to(REPO).as_posix(), where))
        except (OSError, AssertionError) as error:
            problems.append("awtrix-linux %dx%d: %s" % (size[0], size[1], error))
    if not args.only and not args.check:
        for stale in sorted(target.glob("*.svg")):
            if stale.stem not in jobs:
                stale.unlink()
                print("removed %s" % stale.relative_to(REPO).as_posix())
    for problem in problems:
        print(problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
