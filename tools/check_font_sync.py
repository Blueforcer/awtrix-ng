#!/usr/bin/env python3
"""Checks that the generated panel fonts match the BDF sources.

src/media/AwtrixFont.h is generated: both panel fonts laid out as glyph tables
and index arrays over one shared bitmap blob. Nothing in the normal build
regenerates it, so a stale header fails quietly rather than loudly:

1. An edit to a BDF under assets/fonts/ does not reach the device. The firmware
   keeps drawing the previously generated glyphs, so a redrawn letter still looks
   the way it did - while the source that was "obviously" changed sits right
   there in the tree.

2. A change to the covered ranges desynchronises the index arrays from the
   FontRange table. Out-of-range entries are caught by test_font, but an entry
   that merely points at the wrong glyph is not: it draws a plausible letter.

3. Glyph slots shift. Code points that draw the same pixels share a slot, so one
   redrawn glyph can renumber every index after it - in both fonts at once,
   because they share the blob.

Fix a failure with:

    python scripts/gen_font.py

and commit the regenerated header.

Run: python tools/check_font_sync.py
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import gen_font as gen


def main():
    for entry in gen.FONTS:
        for filename in entry[1:3]:
            if not filename:
                continue
            path = os.path.join(gen.FONT_DIR, filename)
            if not os.path.exists(path):
                print(f"missing font source: {path}", file=sys.stderr)
                return 1

    text, (stats, blob) = gen.generate()

    with open(gen.OUT_HEADER, encoding="utf-8") as fh:
        if fh.read() != text:
            print("src/media/AwtrixFont.h does not match assets/fonts/ -- "
                  "run python scripts/gen_font.py and commit the result", file=sys.stderr)
            return 1

    detail = "  ".join(f"{n}: {g} glyphs, {s} shared, {f} filled in"
                       for n, g, s, f in stats)
    print(f"font in sync: {detail}, {blob} bitmap bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
