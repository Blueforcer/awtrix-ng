#!/usr/bin/env python3
"""Generates src/media/AwtrixFont.h from the BDF fonts under assets/fonts/.

The panel carries two fonts, both from trip5's Matrix-Fonts set: a five-row one
and a seven-row one, selectable per app. They are emitted into a single bitmap
blob so a shape either font already has costs nothing the second time.

Within a font the same sharing runs across code points: the dense ASCII span
keeps its slots because the renderer indexes it by subtraction, and every code
point outside it that draws the same pixels as an earlier one reuses that glyph.
A Cyrillic A is a Latin A at this size, so it becomes a reference rather than a
copy - the fold table, derived from the glyphs instead of written by hand.

  python scripts/gen_font.py            regenerate the header
  python scripts/gen_font.py --check    exit 1 if the header is stale

Run: python scripts/gen_font.py
"""


import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "assets", "fonts")
OUT_HEADER = os.path.join(ROOT, "src", "media", "AwtrixFont.h")

FONTS = [
    ("Small", "awtrix.bdf", "MatrixChunky6.bdf", 5),
    ("Large", "MatrixChunky8.bdf", None, 6),
]

DENSE_FIRST, DENSE_LAST = 0x20, 0x7E

BASE_EXCLUDE = {
    "awtrix.bdf": {
        0x00A0,
        0x00B6,
    },
}

ALIASES = {
    0x00A0: 0x0020,
}

COVERAGE = [
    ("Latin1", 0x00A0, 0x00FF),
    ("LatinExtA", 0x0100, 0x017F),
    ("Cyrillic", 0x0400, 0x04FF),
    ("Punctuation", 0x2010, 0x2027),
    ("Currency", 0x20AC, 0x20AC),
]


class Glyph:
    """A glyph as pixel rows, positioned relative to the baseline.

    `top` is how many rows above the baseline row 0 sits, matching the negated
    yOffset the renderer uses.
    """

    __slots__ = ("rows", "advance", "top")

    def __init__(self, rows, advance, top):
        self.rows = rows
        self.advance = advance
        self.top = top

    @property
    def height(self):
        return len(self.rows)

    @property
    def width(self):
        return max((len(r) for r in self.rows), default=0)

    def key(self):
        """Identity for the fold split: shape plus metrics, nothing else."""
        return (tuple(self.rows), self.advance, self.top)

    def trimmed(self):
        """Drops all-blank rows off the top and bottom, adjusting `top`.

        The source font pads every glyph to the full 7-row cell. Storing that
        padding would waste a byte per blank row and, worse, make an unpadded
        AWTRIX glyph compare unequal to an identical padded one.
        """
        first = next((i for i, r in enumerate(self.rows) if "#" in r), None)
        if first is None:
            return Glyph([], self.advance, self.top)
        last = max(i for i, r in enumerate(self.rows) if "#" in r)
        width = max((len(r.rstrip(".")) for r in self.rows), default=0)
        rows = [r[:width].ljust(width, ".") for r in self.rows[first:last + 1]]
        return Glyph(rows, self.advance, self.top - first)


def parse_bdf(path):
    glyphs = {}
    encoding = bbx = dwidth = bits = None

    with open(path, encoding="latin-1") as fh:
        for raw in fh:
            line = raw.strip()
            if line.startswith("ENCODING "):
                encoding = int(line.split()[1])
            elif line.startswith("DWIDTH "):
                dwidth = int(line.split()[1])
            elif line.startswith("BBX "):
                bbx = [int(v) for v in line.split()[1:5]]
            elif line == "BITMAP":
                bits = []
            elif line == "ENDCHAR":
                if None not in (encoding, bbx, bits) and encoding >= 0:
                    w, h, _xoff, yoff = bbx
                    rows = []
                    for hexrow in bits:
                        value = int(hexrow, 16)
                        span = len(hexrow) * 4
                        rows.append("".join(
                            "#" if (value >> (span - 1 - x)) & 1 else "."
                            for x in range(w)))
                    glyphs[encoding] = Glyph(
                        rows, dwidth if dwidth else w, yoff + h).trimmed()
                encoding = bbx = dwidth = bits = None
            elif bits is not None and re.fullmatch(r"[0-9A-Fa-f]+", line):
                bits.append(line)
    return glyphs


def write_bdf(path, glyphs, name):
    """Writes glyphs back out as BDF, so the AWTRIX font has an editable source."""
    height = max((g.height for g in glyphs.values()), default=8)
    width = max((g.width for g in glyphs.values()), default=8)
    out = [
        "STARTFONT 2.1",
        f"FONT -awtrix-{name}-medium-r-normal--{height}-*-*-*-c-*-iso10646-1",
        f"SIZE {height} 75 75",
        f"FONTBOUNDINGBOX {width} {height} 0 0",
        "STARTPROPERTIES 4",
        'FOUNDRY "awtrix"',
        "FONT_ASCENT 6",
        "FONT_DESCENT 2",
        'COPYRIGHT "AWTRIX NG panel font."',
        "ENDPROPERTIES",
        f"CHARS {len(glyphs)}",
    ]
    for cp in sorted(glyphs):
        g = glyphs[cp]
        rows = g.rows or ["." * width]
        w = max(len(r) for r in rows)
        pad = (w + 7) // 8 * 8
        out += [
            f"STARTCHAR U+{cp:04X}",
            f"ENCODING {cp}",
            "SWIDTH 500 0",
            f"DWIDTH {g.advance} 0",
            f"BBX {w} {len(rows)} 0 {g.top - len(rows)}",
            "BITMAP",
        ]
        for row in rows:
            value = 0
            for i, ch in enumerate(row):
                if ch == "#":
                    value |= 1 << (pad - 1 - i)
            out.append(f"{value:0{pad // 4}X}")
        out.append("ENDCHAR")
    out.append("ENDFONT")
    with open(path, "w", encoding="latin-1", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")


class Packer:
    """Collects glyph bitmaps into one blob shared by every font.

    Rows are at most 8 px wide, so a row is one byte and glyphs stay
    byte-aligned, which lets identical bitmaps share storage even when the
    glyphs around them differ.
    """

    def __init__(self):
        self.blob = []
        self._seen = {}

    def add(self, glyph):
        rows = tuple(glyph.rows)
        offset = self._seen.get(rows)
        if offset is None:
            offset = len(self.blob)
            self._seen[rows] = offset
            for row in rows:
                value = 0
                for i, ch in enumerate(row):
                    if ch == "#":
                        value |= 0x80 >> i
                self.blob.append(value)
        return (offset, 8, glyph.height, glyph.advance, 0, -glyph.top)


def narrow(glyphs, lo, hi):
    """Shrinks a declared range to the code points the font actually carries."""
    have = [cp for cp in range(lo, hi + 1)
            if cp in glyphs or (cp in ALIASES and ALIASES[cp] in glyphs)]
    return (have[0], have[-1]) if have else None


def build_font(glyphs, packer):
    """Lays one font out: a dense span, then an index array per sparse range.

    The dense span has to stay contiguous - the renderer indexes it by
    subtraction - so its glyphs keep their slots. Everything outside it is
    deduplicated against them and against each other, which is what turns a
    Cyrillic A or a Greek K into a reference to the Latin letter rather than a
    second copy of the same pixels.
    """
    order = [glyphs[cp] for cp in range(DENSE_FIRST, DENSE_LAST + 1)]
    slot_of = {}
    for i, g in enumerate(order):
        slot_of.setdefault(g.key(), i)

    indices, shared = [], 0
    for name, lo, hi in COVERAGE:
        span = narrow(glyphs, lo, hi)
        if span is None:
            continue
        first, last = span
        entries = []
        for cp in range(first, last + 1):
            g = glyphs.get(cp)
            if g is None and cp in ALIASES:
                g = glyphs.get(ALIASES[cp])
            if g is None:
                entries.append(0)
                continue
            key = g.key()
            slot = slot_of.get(key)
            if slot is None:
                slot = len(order)
                slot_of[key] = slot
                order.append(g)
            else:
                shared += 1
            entries.append(slot + 1)
        indices.append((name, first, last, entries))

    table = [packer.add(g) for g in order]
    return table, indices, shared


def _rows(values, per_line, fmt):
    out = []
    for i in range(0, len(values), per_line):
        out.append("    " + ", ".join(fmt(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(out)


def emit(blob, fonts):
    lines = [
        "#pragma once",
        "",
        "// GENERATED by scripts/gen_font.py -- do not edit by hand.",
        "//",
        "// Glyph data from:",
    ]
    for filename in sources():
        lines.append(f"//   assets/fonts/{filename}  --  {copyright_of(filename)}")
    lines += [
        "//",
        "// The Matrix-Fonts are Copyright (c) 2026 Trip5 and MIT licensed. The full",
        "// notice is in assets/fonts/MatrixFonts.LICENSE and has to ship with any copy",
        "// of this data, this header included.",
        "",
        "#include <cstdint>",
        "",
        '#include "core/render/Font.h"',
        "",
        "namespace awtrix {",
        "",
        "const uint8_t AwtrixBitmaps[] = {",
        _rows(blob, 16, lambda v: f"0x{v:02X}"),
        "};",
        "",
    ]

    for name, table, indices, y_advance in fonts:
        lines.append(f"const FontGlyph AwtrixGlyphs{name}[] = {{")
        for off, w, h, adv, xo, yo in table:
            lines.append(f"    {{{off}, {w}, {h}, {adv}, {xo}, {yo}}},")
        lines += ["};", ""]
        for range_name, first, last, entries in indices:
            lines.append(f"const uint16_t AwtrixIndex{name}{range_name}[] = {{")
            lines.append(_rows(entries, 16, str))
            lines += ["};", ""]
        lines.append(f"const FontRange AwtrixRanges{name}[] = {{")
        for range_name, first, last, _entries in indices:
            lines.append(
                f"    {{0x{first:04X}, 0x{last:04X}, AwtrixIndex{name}{range_name}}},")
        lines += ["};", ""]
        lines.append(f"constexpr uint8_t kAwtrixRangeCount{name} = {len(indices)};")
        lines.append(f"constexpr uint8_t kAwtrixYAdvance{name} = {y_advance};")
        lines.append("")

    lines += [
        f"constexpr uint16_t kAwtrixFontFirst = 0x{DENSE_FIRST:02X};",
        f"constexpr uint16_t kAwtrixFontLast = 0x{DENSE_LAST:02X};",
        "",
        "}",
        "",
    ]
    return "\n".join(lines)


def copyright_of(filename):
    """The COPYRIGHT line a BDF carries, so the notice travels with the glyphs.

    The generated header is a substantial portion of these fonts and reaches the
    firmware image, which is what the MIT notice has to accompany. Reading it
    from the source keeps it honest if a font is ever swapped.
    """
    with open(os.path.join(FONT_DIR, filename), encoding="latin-1") as fh:
        for line in fh:
            if line.startswith("COPYRIGHT "):
                return line.split(" ", 1)[1].strip().strip('"')
    return "no copyright line"


def sources():
    """Every BDF that contributes glyphs, in the order the fonts use them."""
    out = []
    for entry in FONTS:
        for filename in entry[1:3]:
            if filename and filename not in out:
                out.append(filename)
    return out


def aligned(filename, cap_top):
    """Loads a BDF and moves it so its capitals sit on our cap line.

    Two fonts drawn by different hands rarely agree on where a capital starts.
    Lining them up here is what lets one fill the other's gaps without the
    imported letters standing a row proud of their neighbours.
    """
    glyphs = parse_bdf(os.path.join(FONT_DIR, filename))
    shift = cap_top - glyphs[0x41].top
    if not shift:
        return glyphs
    return {cp: Glyph(g.rows, g.advance, g.top + shift) for cp, g in glyphs.items()}


def generate():
    packer = Packer()
    fonts, stats = [], []
    for name, base_file, fill_file, cap_top in FONTS:
        glyphs = aligned(base_file, cap_top)
        for cp in BASE_EXCLUDE.get(base_file, ()):
            glyphs.pop(cp, None)
        filled = 0
        if fill_file:
            for cp, g in aligned(fill_file, cap_top).items():
                have = glyphs.get(cp)
                if have is None:
                    glyphs[cp] = g
                    filled += 1
                elif cp > DENSE_LAST and g.height > have.height:
                    glyphs[cp] = g
                    filled += 1
        missing = [cp for cp in range(DENSE_FIRST, DENSE_LAST + 1) if cp not in glyphs]
        if missing:
            raise SystemExit(
                f"{base_file} has no glyph for {', '.join(hex(c) for c in missing[:8])}"
                " in the dense span")
        y_advance = max(g.height for g in glyphs.values()) + 1
        table, indices, shared = build_font(glyphs, packer)
        fonts.append((name, table, indices, y_advance))
        stats.append((name, len(table), shared, filled))
    return emit(packer.blob, fonts), (stats, len(packer.blob))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if the committed header differs")
    args = ap.parse_args()

    text, (stats, blob) = generate()

    if args.check:
        with open(OUT_HEADER, encoding="utf-8") as fh:
            if fh.read() != text:
                print("src/media/AwtrixFont.h is stale -- run python scripts/gen_font.py",
                      file=sys.stderr)
                return 1
    else:
        with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)

    verb = "in sync" if args.check else "wrote"
    detail = "  ".join(f"{n}: {g} glyphs, {s} shared, {f} filled in"
                       for n, g, s, f in stats)
    print(f"font {verb}: {detail}, {blob} bitmap bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
