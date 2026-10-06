#!/usr/bin/env python3
"""Writes share/licenses.txt.gz, every licence text a TC002 release ships.

  compose.py OUTPUT --preamble FILE --text TITLE FILE [FILE ...] [--text TITLE FILE ...]

OUTPUT is gzip (level 9, no file name, no time stamp) of the preamble, a numbered list of the
titles and each title followed by its files, so the same inputs always give the same bytes.
"""
import argparse
import gzip
import io
import sys
from pathlib import Path

RULE = "=" * 78


def read_text(path):
    text = Path(path).read_bytes().decode("utf-8").replace("\r\n", "\n")
    if not text.strip():
        raise ValueError(f"{path} is empty")
    return text.strip("\n")


def compose(preamble, sections):
    lines = [preamble, ""]
    lines += [f"{number:2}. {title}" for number, (title, _) in enumerate(sections, 1)]
    for number, (title, texts) in enumerate(sections, 1):
        lines += ["", "", RULE, f"{number}. {title}", RULE]
        for text in texts:
            lines += ["", text]
    return "\n".join(lines) + "\n"


def gzip_bytes(text):
    buffer = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", compresslevel=9, fileobj=buffer, mtime=0) as out:
        out.write(text.encode("utf-8"))
    return buffer.getvalue()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", type=Path)
    parser.add_argument("--preamble", type=Path, required=True)
    parser.add_argument("--text", nargs="+", action="append", required=True,
                        metavar=("TITLE", "FILE"))
    args = parser.parse_args(argv)
    try:
        if any(len(entry) < 2 or not entry[0].strip() for entry in args.text):
            raise ValueError("every --text needs a title and at least one file")
        sections = [(entry[0], [read_text(path) for path in entry[1:]]) for entry in args.text]
        data = gzip_bytes(compose(read_text(args.preamble), sections))
        args.output.write_bytes(data)
    except (OSError, UnicodeDecodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"{args.output}: {len(sections)} sections, {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
