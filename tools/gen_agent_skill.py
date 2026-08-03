#!/usr/bin/env python3
"""Packs the Berry app skill into the zip the docs offer for download.

The skill is two files that live apart in the tree: its instructions in
skills/awtrix-berry-app/SKILL.md, and its reference - the same AI system prompt
docs/guides/ai-prompt.md publishes - in docs/examples/berry-app-system-prompt.md.
Keeping one copy of the prompt is the point: it is edited whenever a binding
changes, and a second copy checked in beside the skill would go stale silently.

The zip is committed rather than built by the docs workflow because
`mkdocs build --strict` resolves the download link against docs_dir, so a local
`mkdocs serve` with no zip present is a broken link. `--check` fails when the
committed zip no longer matches its sources; CI runs it.

Timestamps and permissions are fixed so the same sources always produce the same
bytes - otherwise every run shows up as a diff.

Run: python tools/gen_agent_skill.py            (write the zip)
     python tools/gen_agent_skill.py --check    (exit 1 when it is stale)
"""

import io
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SKILL = "awtrix-berry-app"
OUT = os.path.join(ROOT, "docs", "examples", "awtrix-berry-app-skill.zip")

MEMBERS = [
    (os.path.join(ROOT, "skills", SKILL, "SKILL.md"), SKILL + "/SKILL.md"),
    (
        os.path.join(ROOT, "docs", "examples", "berry-app-system-prompt.md"),
        SKILL + "/references/awtrix-api.md",
    ),
]

FIXED_TIME = (2026, 1, 1, 0, 0, 0)


def build():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, name in MEMBERS:
            try:
                with open(src, "rb") as f:
                    data = f.read()
            except OSError as e:
                raise SystemExit("gen_agent_skill: cannot read a source: %s" % e)
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    return buf.getvalue()


if __name__ == "__main__":
    packed = build()
    if "--check" in sys.argv[1:]:
        try:
            with open(OUT, "rb") as f:
                current = f.read()
        except OSError:
            sys.exit(
                "gen_agent_skill: %s is missing. Run: python tools/gen_agent_skill.py"
                % os.path.relpath(OUT, ROOT)
            )
        if current != packed:
            sys.exit(
                "gen_agent_skill: %s is stale - a source changed after it was packed.\n"
                "Run: python tools/gen_agent_skill.py" % os.path.relpath(OUT, ROOT)
            )
        print("gen_agent_skill: %s is current" % os.path.relpath(OUT, ROOT))
    else:
        with open(OUT, "wb") as f:
            f.write(packed)
        print(
            "gen_agent_skill: wrote %s (%d bytes)"
            % (os.path.relpath(OUT, ROOT), len(packed))
        )
