#!/usr/bin/env python3
"""Assemble the Web UI's ordered sources into its shipped HTML file."""

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def assemble(root=ROOT):
    source = Path(root) / "webui/src"
    names = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
    if not isinstance(names, list) or not names or any(not isinstance(name, str) for name in names):
        raise ValueError("invalid Web UI source manifest")
    if len(set(names)) != len(names):
        raise ValueError("duplicate Web UI source")
    parts = []
    for name in names:
        path = (source / name).resolve()
        if not path.is_relative_to(source.resolve()):
            raise ValueError("invalid Web UI source path")
        parts.append(path.read_bytes())
    return b"".join(parts)


def write(root=ROOT):
    import berry_api

    berry_api.inject(root)
    output = Path(root) / "webui/index.html"
    data = assemble(root)
    if output.exists() and output.read_bytes() == data:
        return False
    output.write_bytes(data)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check", action="store_true", help="reject a stale generated HTML file")
    args = parser.parse_args()
    if args.check:
        if (args.root / "webui/index.html").read_bytes() != assemble(args.root):
            parser.exit(1, "webui/index.html is stale; run python scripts/webui_source.py\n")
    elif write(args.root):
        print("webui: assembled webui/index.html")


if __name__ == "__main__":
    main()
