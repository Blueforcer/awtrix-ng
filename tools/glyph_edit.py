#!/usr/bin/env python3
"""Dumps glyphs as 8x8 panel cells and writes edited ones back into the BDF.

The generated font header is derived, so a letter is fixed in the BDF under
assets/fonts/. A bitmap glyph is faster to correct by looking at it than by
editing hex, and it has to be judged where it actually sits: a cell shows the
full eight panel rows, with the glyph placed on the baseline the firmware draws
it on. Row 7 is the bottom of the panel, where the progress bar goes.

    python tools/glyph_edit.py --dump MatrixChunky6.bdf 0x63 0x423 > fix.txt
    python tools/glyph_edit.py --dump MatrixChunky6.bdf --cyrillic > fix.txt
    ...edit fix.txt, moving pixels anywhere in the cell...
    python tools/glyph_edit.py --apply MatrixChunky6.bdf < fix.txt
    python scripts/gen_font.py

Only the ink matters: blank rows and columns around it are trimmed on the way
back, so the glyph's own box follows what you drew. `adv=` is the pen movement
and is the one number to set by hand - it is what spaces the letters.

Run: python tools/glyph_edit.py --dump <bdf> <codepoints...>
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import gen_font as gen

CELL_W, CELL_H = 8, 8
BASELINE = 6


def cap_shift(path, glyphs):
    """How far this font is moved to put its capitals on the panel's cap line."""
    for _name, base_file, fill_file, cap_top in gen.FONTS:
        if os.path.basename(path) in (base_file, fill_file):
            return cap_top - glyphs[0x41].top
    return 0


def to_cell(glyph, shift):
    """Paints a glyph into an 8x8 cell at the position the panel draws it."""
    cell = [["."] * CELL_W for _ in range(CELL_H)]
    top = glyph.top + shift
    for i, row in enumerate(glyph.rows):
        y = BASELINE - top + i
        if not 0 <= y < CELL_H:
            continue
        for x, ch in enumerate(row[:CELL_W]):
            if ch == "#":
                cell[y][x] = "#"
    return ["".join(r) for r in cell]


def from_cell(cell, shift):
    """Trims a cell back to a glyph, returning (rows, top) in BDF terms."""
    inked = [y for y, r in enumerate(cell) if "#" in r]
    if not inked:
        return [], 0
    first, last = inked[0], inked[-1]
    width = max(len(r.rstrip(".")) for r in cell[first:last + 1])
    rows = [r[:width].ljust(width, ".") for r in cell[first:last + 1]]
    return rows, (BASELINE - first) - shift


def render(glyphs, shift, codepoints):
    out = []
    for cp in codepoints:
        g = glyphs.get(cp)
        if g is None:
            print(f"U+{cp:04X} is not in this font", file=sys.stderr)
            continue
        try:
            label = chr(cp)
        except ValueError:
            label = "?"
        out.append(f"U+{cp:04X} adv={g.advance}    {label}")
        out += to_cell(g, shift)
        out.append("")
    return "\n".join(out)


def parse_edits(text):
    edits, cp, adv, cell = {}, None, None, []
    for raw in text.split("\n"):
        line = raw.strip()
        m = re.match(r"^U\+([0-9A-Fa-f]{4,6})\s+adv=(\d+)", line)
        if m:
            if cp is not None:
                edits[cp] = (cell, adv)
            cp, adv, cell = int(m.group(1), 16), int(m.group(2)), []
        elif line and set(line) <= set("#."):
            cell.append(line.ljust(CELL_W, "."))
    if cp is not None:
        edits[cp] = (cell, adv)
    return edits


def read_bdf_lines(path):
    with open(path, encoding="latin-1") as fh:
        lines = fh.read().split("\n")
    spans, start, encoding = {}, None, None
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith("STARTCHAR"):
            start, encoding = i, None
        elif s.startswith("ENCODING "):
            encoding = int(s.split()[1])
        elif s == "ENDCHAR" and start is not None and encoding is not None:
            spans[encoding] = (start, i)
            start = encoding = None
    return lines, spans


def apply(path, edits, shift):
    lines, spans = read_bdf_lines(path)
    written = 0
    for cp in sorted(edits, reverse=True):
        span = spans.get(cp)
        if span is None:
            print(f"U+{cp:04X} is not in this font, skipped", file=sys.stderr)
            continue
        cell, adv = edits[cp]
        if len(cell) != CELL_H:
            print(f"U+{cp:04X} has {len(cell)} rows, expected {CELL_H}, skipped", file=sys.stderr)
            continue
        rows, top = from_cell(cell, shift)
        if not rows:
            print(f"U+{cp:04X} is empty, skipped", file=sys.stderr)
            continue
        width = len(rows[0])
        pad = (width + 7) // 8 * 8
        body = [
            f"STARTCHAR U+{cp:04X}",
            f"ENCODING {cp}",
            "SWIDTH 500 0",
            f"DWIDTH {adv} 0",
            f"BBX {width} {len(rows)} 0 {top - len(rows)}",
            "BITMAP",
        ]
        for row in rows:
            value = 0
            for i, ch in enumerate(row):
                if ch == "#":
                    value |= 1 << (pad - 1 - i)
            body.append(f"{value:0{pad // 4}X}")
        body.append("ENDCHAR")
        lines[span[0]:span[1] + 1] = body
        written += 1
    with open(path, "w", encoding="latin-1", newline="\n") as fh:
        fh.write("\n".join(lines))
    return written


def main():
    ap = argparse.ArgumentParser()
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dump", action="store_true", help="print glyphs as 8x8 cells")
    mode.add_argument("--apply", action="store_true", help="read edited cells from stdin")
    ap.add_argument("bdf")
    ap.add_argument("codepoints", nargs="*")
    ap.add_argument("--cyrillic", action="store_true", help="U+0400..U+045F")
    ap.add_argument("--lower", action="store_true", help="a..z")
    ap.add_argument("--upper", action="store_true", help="A..Z")
    args = ap.parse_args()

    path = args.bdf if os.path.sep in args.bdf else os.path.join(gen.FONT_DIR, args.bdf)
    glyphs = gen.parse_bdf(path)
    shift = cap_shift(path, glyphs)

    if args.apply:
        n = apply(path, parse_edits(sys.stdin.read()), shift)
        print(f"{path}: {n} glyphs rewritten -- now run python scripts/gen_font.py",
              file=sys.stderr)
        return 0

    cps = [int(c, 0) for c in args.codepoints]
    if args.cyrillic:
        cps += list(range(0x400, 0x460))
    if args.lower:
        cps += list(range(ord("a"), ord("z") + 1))
    if args.upper:
        cps += list(range(ord("A"), ord("Z") + 1))
    if not cps:
        print("nothing to dump", file=sys.stderr)
        return 1
    sys.stdout.reconfigure(encoding="utf-8")
    print(render(glyphs, shift, cps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
