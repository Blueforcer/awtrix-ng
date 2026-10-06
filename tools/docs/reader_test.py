"""Reader test: can someone who reads only the docs build what users ask for?

    python3 tools/docs/reader_test.py bundle --clock CLOCK --out DIR
    python3 tools/docs/reader_test.py check --clock CLOCK --answers FILE [--binary PATH]

`bundle` writes the clock's pages as its readers get them (clock markers applied; developer
pages, the AI prompt and the release notes left out) plus TASKS.md. A fresh agent that knows
nothing else answers the tasks from that folder: one fenced code block under `## <task id>`
per task, in one Markdown file. `check` runs each answer on awtrix-linux and tests what the
display shows. docs/developers/writing-docs.md describes a whole run.
"""

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
DOCS = REPO / "docs"
TASKS = HERE / "reader_tasks.json"
sys.path.insert(0, str(HERE))

import only  # noqa: E402
import panel_examples  # noqa: E402

SKIP = ("developers/", "releases/", "examples/", "assets/",
        "guides/ai-prompt.md")
SECTION = re.compile(r"^## +(\S+)[ \t]*$", re.M)
BLOCK = re.compile(r"^```[ \t]*(\w+)[^\n]*\n(.*?)^```[ \t]*$", re.M | re.S)
PANEL = re.compile(r"^[ \t]*<!--\s*panel\b.*?-->[ \t]*\r?\n", re.M)
CHECK_SECONDS = 3.0

INSTRUCTIONS = """# Tasks

You know nothing about AWTRIX except the documentation in this folder. Read it, then solve
every task below for the {title} clock.

Answer in one Markdown file. For each task write a heading `## <task id>` and under it exactly
one fenced code block:

- a script: a complete Berry script in a `berry` block;
- a pushed app or a notification: one `curl` command to `http://<awtrix-ip>` in a `bash` block.

Write nothing else into the file.
"""


def load_tasks(clock):
    tasks = json.loads(Path(TASKS).read_text(encoding="utf-8"))["tasks"]
    return [task for task in tasks if clock in task["clocks"]]


def bundle(clock, out):
    out = Path(out)
    count = 0
    for path in sorted(Path(DOCS).rglob("*.md")):
        rel = path.relative_to(DOCS).as_posix()
        if rel.startswith(SKIP):
            continue
        text = path.read_text(encoding="utf-8-sig")
        if clock not in only.page_only(text, rel):
            continue
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(PANEL.sub("", only.filter_markdown(text, clock, rel)), encoding="utf-8")
        count += 1
    lines = [INSTRUCTIONS.format(title=only.VARIANTS[clock])]
    for task in load_tasks(clock):
        lines.append("## %s\n\n%s\n" % (task["id"], task["ask"]))
    (out / "TASKS.md").write_text("\n".join(lines), encoding="utf-8")
    return count


def read_answers(text):
    answers = {}
    marks = list(SECTION.finditer(text))
    for i, mark in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        block = BLOCK.search(text, mark.end(), end)
        if block:
            answers[mark.group(1)] = (block.group(1), block.group(2))
    return answers


def area(spec, width, height):
    if spec == "all":
        return 0, width - 1, 0, height - 1
    columns, rows = spec.split(",")

    def bounds(text, last):
        first, _, end = text.partition("-")
        return [last if v in ("W", "H") else int(v) for v in (first, end or first)]

    x0, x1 = bounds(columns, width - 1)
    y0, y1 = bounds(rows, height - 1)
    return x0, x1, y0, y1


def _cut(frame, box, width):
    x0, x1, y0, y1 = box
    return [frame[y * width + x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]


def evaluate(check, frames, width, height, payload=None, source=""):
    name, _, arg = check.partition(" ")
    if name == "payload":
        return _payload(arg, payload)
    if name == "source":
        return arg.startswith("!") and arg[1:] not in source
    if name == "colors":
        return len({pixel for pixel in frames[0] if pixel}) >= int(arg)
    box = area(arg, width, height)
    if name in ("moves", "still"):
        first = _cut(frames[0], box, width)
        moved = any(_cut(frame, box, width) != first for frame in frames[1:])
        return moved if name == "moves" else not moved
    if name == "lit":
        return any(any(_cut(frame, box, width)) for frame in frames)
    if name == "dark":
        return not any(any(_cut(frame, box, width)) for frame in frames)
    if name == "centered":
        x0, x1, y0, y1 = box
        used = [x for x in range(x0, x1 + 1)
                if any(frames[0][y * width + x] for y in range(y0, y1 + 1))]
        return bool(used) and abs((used[0] - x0) - (x1 - used[-1])) <= 1
    raise ValueError("unknown check %r" % check)


def _payload(arg, payload):
    match = re.match(r"^([\w.]+)(?:(>=|=)(.+))?$", arg)
    if not match or not isinstance(payload, dict):
        return False
    key, op, want = match.groups()
    value = payload
    for part in key.split("."):
        if not isinstance(value, dict) or part not in value:
            return False
        value = value[part]
    if op is None:
        return True
    if op == ">=":
        return (isinstance(value, (int, float)) and not isinstance(value, bool)
                and value >= float(want))
    return value == want or json.dumps(value) == want


def check(clock, answers_path, binary, webui=None):
    import panel_host
    width, height = panel_examples.SIZES[clock]
    answers = read_answers(Path(answers_path).read_text(encoding="utf-8"))
    results = []
    with panel_host.PanelHost(binary, width, height, webui or panel_host.WEBUI) as host:
        for task in load_tasks(clock):
            answer = answers.get(task["id"])
            if not answer:
                results.append((task["id"], False, "no answer"))
                continue
            try:
                example = panel_examples.example(answer[0], answer[1], task["id"])
                frames = host.frames(example, motion_s=CHECK_SECONDS)
            except (panel_examples.PanelError, panel_host.HostError) as error:
                results.append((task["id"], False, str(error)))
                continue
            payload = example.body if isinstance(example.body, dict) else None
            failed = [c for c in task["checks"]
                      if not evaluate(c, frames, width, height, payload, answer[1])]
            results.append((task["id"], not failed,
                            "failed: " + "; ".join(failed) if failed else ""))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    make = sub.add_parser("bundle")
    make.add_argument("--clock", required=True, choices=list(only.VARIANTS))
    make.add_argument("--out", required=True)
    test = sub.add_parser("check")
    test.add_argument("--clock", required=True, choices=list(only.VARIANTS))
    test.add_argument("--answers", required=True)
    test.add_argument("--binary", default=str(REPO / "build" / "host" / "awtrix-linux"))
    args = parser.parse_args()
    if args.command == "bundle":
        print("%d pages and TASKS.md in %s" % (bundle(args.clock, args.out), args.out))
        return 0
    results = check(args.clock, args.answers, args.binary)
    for task_id, ok, note in results:
        print("%-4s %-28s %s" % ("pass" if ok else "FAIL", task_id, note))
    passed = sum(ok for _, ok, _ in results)
    print("%d of %d passed" % (passed, len(results)))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
