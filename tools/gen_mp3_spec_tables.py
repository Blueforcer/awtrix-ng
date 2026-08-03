"""Emit the Layer III tables from the standard's own normative annex.

Source
------
ISO/IEC 11172-3:1993, 3-Annex B, as distributed by mp3-tech.org:

    http://www.mp3-tech.org/programmer/docs/iso11172-3.zip   ->  ANNEX_AB.DOC

That archive is the only public copy found that carries the *normative annex*
rather than just the body text -- every loose "ISO 11172-3" PDF in circulation
stops before the tables. Convert the document to text first:

    antiword -w 200 ANNEX_AB.DOC > annex_ab.txt
    python tools/gen_mp3_spec_tables.py annex_ab.txt

Writes src/core/audio/Mp3HuffmanTables.h and src/core/audio/Mp3Window.h.

Why this replaced the earlier route
-----------------------------------
The Huffman tables were first recovered from minimp3 (CC0) because the standard
appeared to be unobtainable. With the annex in hand the values come from the
standard directly, and the minimp3-derived header becomes a cross-check: pass
--verify-against to compare the two. They agree, which is what a table
recovered two independent ways should do.

The synthesis window could not be recovered that way at all -- minimp3 stores it
folded together with its own DCT twiddles -- so the annex is the only source for
it. It is not computable: the coefficients came out of an iterative gradient
optimisation at Philips (Koornwinder, "How were the MP3 filter coefficients
produced?", 2007, confirmed by their originator), so a closed form does not
exist and any reconstruction lands on different numbers.
"""

import argparse
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HUFF_OUT = os.path.join(ROOT, "src", "core", "audio", "Mp3HuffmanTables.h")
WINDOW_OUT = os.path.join(ROOT, "src", "core", "audio", "Mp3Window.h")
TABLES_OUT = os.path.join(ROOT, "src", "core", "audio", "Mp3Tables.h")

BAND_SECTIONS = [("3-B.8a", 32000), ("3-B.8b", 44100), ("3-B.8c", 48000)]
RATES = [44100, 48000, 32000]

SPECTRAL_LINES = 576
SHORT_LINES_PER_WINDOW = SPECTRAL_LINES // 3

sys.path.insert(0, os.path.join(ROOT, "tools"))
from gen_mp3_huffman import check_prefix_code, flatten, emit_table  # noqa: E402

PAIR_ROW = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+([01]+)\s*$")
QUAD_ROW = re.compile(r"^\s*([01]{4})\s+(\d+)\s+([01]+)\s*$")
SECTION = re.compile(r"^\s*Huffman code table (?:(\d+)|for quadruples \((A|B)\))\s*$")
SAME_AS = re.compile(r"same as table (\d+), but linbits\s*=\s*(\d+)")
ESC = re.compile(r"ESC table, linbits\s*=\s*(\d+)")
DVALUE = re.compile(r"D\[\s*(\d+)\]\s*=\s*(-?[\d.]+)")


def parse_window(lines):
    """Table 3-B.3: the 512 synthesis window coefficients."""
    values = {}
    for line in lines:
        for index, text in DVALUE.findall(line):
            values[int(index)] = float(text)
    if len(values) != 512:
        raise SystemExit("expected 512 window coefficients, found %d" % len(values))
    return [values[i] for i in range(512)]


def check_window_symmetry(window):
    """The standard's window is antisymmetric except at multiples of 64.

    Koornwinder records C[i] = -C[512-i] unless 64 divides i, and C[i] =
    C[512-i] when it does, with C = D/32. Verifying it here does two jobs: it
    catches a mangled parse, and it licenses storing only half the table.
    """
    for i in range(1, 256):
        mirror = window[512 - i]
        expected = window[i] if (i % 64 == 0) else -window[i]
        if abs(mirror - expected) > 1e-9:
            raise SystemExit(
                "window symmetry broken at %d: D[%d]=%.9f, D[%d]=%.9f"
                % (i, i, window[i], 512 - i, mirror))


BAND_ROW = re.compile(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$")


def parse_scalefactor_bands(lines):
    """Table 3-B.8: scalefactor band widths, per rate, for long and short blocks.

    The tables stop short of the full spectrum -- 44.1 kHz long blocks end at
    line 417 of 576 -- because the top band carries no scalefactor of its own.
    Decoding still has to walk it, so a final band covering the remainder is
    appended and the widths are made to sum to the full spectrum.
    """
    bands = {}
    section = None
    kind = None
    for line in lines:
        for tag, rate in BAND_SECTIONS:
            if "Table %s" % tag in line:
                section, kind = rate, None
                bands.setdefault(rate, {"long": [], "short": []})
        if section is None:
            continue
        stripped = line.strip().lower()
        if stripped.startswith("long blocks"):
            kind = "long"
            continue
        if stripped.startswith("short blocks"):
            kind = "short"
            continue
        if "Table 3-B.9" in line:
            section = None
            continue
        if kind is None:
            continue
        row = BAND_ROW.match(line)
        if row:
            bands[section][kind].append(int(row.group(2)))

    out = {}
    for rate, kinds in bands.items():
        long_widths = kinds["long"]
        short_widths = kinds["short"]
        if len(long_widths) != 21 or len(short_widths) != 12:
            raise SystemExit(
                "%d Hz: expected 21 long and 12 short bands, got %d and %d"
                % (rate, len(long_widths), len(short_widths)))
        long_widths = long_widths + [SPECTRAL_LINES - sum(long_widths)]
        short_widths = short_widths + [SHORT_LINES_PER_WINDOW - sum(short_widths)]
        if sum(long_widths) != SPECTRAL_LINES or sum(short_widths) != SHORT_LINES_PER_WINDOW:
            raise SystemExit("%d Hz: band widths do not cover the spectrum" % rate)
        out[rate] = (long_widths, short_widths)
    return out


def parse_preemphasis(lines):
    """Table 3-B.6: the value added to the upper bands when preflag is set."""
    for index, line in enumerate(lines):
        if "Table 3-B.6" in line:
            for follow in lines[index : index + 6]:
                values = follow.split()
                if len(values) == 21 and all(v.isdigit() for v in values):
                    return [int(v) for v in values]
    raise SystemExit("could not find the preemphasis table")


def parse_alias_coefficients(lines):
    """Table 3-B.9: the eight ci, from which the butterfly pair is computed."""
    ci = []
    started = False
    for line in lines:
        if "Table 3-B.9" in line:
            started = True
            continue
        if not started:
            continue
        parts = line.split()
        if len(parts) == 2 and parts[0].isdigit() and parts[1].startswith("-"):
            ci.append(float(parts[1]))
        if len(ci) == 8:
            return ci
    raise SystemExit("could not find the eight aliasing coefficients")


def parse_huffman(lines):
    """Table 3-B.7: every Layer III Huffman table, plus the two quadruple ones."""
    tables = {}
    linbits = [0] * 32
    aliases = {}
    current = None
    pending_rows = []

    def flush():
        if current is not None:
            tables[current] = pending_rows[:]

    for line in lines:
        section = SECTION.match(line)
        if section:
            flush()
            del pending_rows[:]
            current = int(section.group(1)) if section.group(1) else section.group(2)
            continue
        if current is None:
            continue

        same = SAME_AS.search(line)
        if same:
            aliases[current] = int(same.group(1))
            linbits[current] = int(same.group(2))
            continue
        esc = ESC.search(line)
        if esc:
            linbits[current] = int(esc.group(1))
            continue

        if isinstance(current, str):
            row = QUAD_ROW.match(line)
            if not row:
                continue
            symbol = int(row.group(1), 2)
            length = int(row.group(2))
            code = int(row.group(3), 2)
        else:
            row = PAIR_ROW.match(line)
            if not row:
                continue
            symbol = (int(row.group(1)) << 4) | int(row.group(2))
            length = int(row.group(3))
            code = int(row.group(4), 2)
        pending_rows.append((code, length, symbol))

    flush()

    for target, source in aliases.items():
        tables[target] = tables[source]
    return tables, linbits


def as_dict(rows):
    return {(code, length): symbol for code, length, symbol in rows}


def emit_window(window):
    """Half the table plus the symmetry rule, rather than all 512 values."""
    with open(WINDOW_OUT, "w", encoding="utf-8") as out:
        out.write("// GENERATED by tools/gen_mp3_spec_tables.py -- DO NOT EDIT.\n//\n")
        out.write("// Synthesis window, ISO/IEC 11172-3 Table 3-B.3.\n//\n")
        out.write("// Only D[0..256] is stored. The standard's window obeys\n")
        out.write("//     D[512-i] = -D[i], except D[512-i] = D[i] when 64 divides i,\n")
        out.write("// which halves the table; the generator verifies the identity on the\n")
        out.write("// parsed values before relying on it.\n//\n")
        out.write("// These coefficients are not derivable. They came out of an iterative\n")
        out.write("// gradient optimisation, so there is no closed form to compute them from\n")
        out.write("// and an independently designed window would be a different filter.\n")
        out.write("#pragma once\n\n#include <cstddef>\n\nnamespace awtrix {\nnamespace mp3 {\n\n")
        half = window[:257]
        out.write("inline constexpr float kSynthesisWindowHalf[%d] = {\n" % len(half))
        for i in range(0, len(half), 4):
            row = ", ".join(fmt(v) for v in half[i : i + 4])
            out.write("    %s,\n" % row)
        out.write("};\n\n")
        out.write("// Full-table access, reconstructing the mirrored half on the fly.\n")
        out.write("inline constexpr float synthesisWindow(std::size_t i) {\n")
        out.write("  return i <= 256 ? kSynthesisWindowHalf[i]\n")
        out.write("                  : ((512 - i) % 64 == 0 ? kSynthesisWindowHalf[512 - i]\n")
        out.write("                                         : -kSynthesisWindowHalf[512 - i]);\n")
        out.write("}\n\n}  // namespace mp3\n}  // namespace awtrix\n")


def emit_huffman(tables, linbits):
    with open(HUFF_OUT, "w", encoding="utf-8") as out:
        out.write("// GENERATED by tools/gen_mp3_spec_tables.py -- DO NOT EDIT.\n//\n")
        out.write("// Layer III Huffman code tables, ISO/IEC 11172-3 Table 3-B.7, taken from\n")
        out.write("// the standard's normative annex.\n//\n")
        out.write("// Each table is stored as its codewords sorted by (length, code) with a\n")
        out.write("// per-length index. Decoding walks one bit at a time and searches only the\n")
        out.write("// run for the current length, which costs a little speed and saves the\n")
        out.write("// kilobytes a flat 19-bit lookup would spend.\n")
        out.write("#pragma once\n\n#include <cstdint>\n\nnamespace awtrix {\nnamespace mp3 {\n\n")
        out.write("inline constexpr uint8_t kLinbits[32] = {%s};\n\n"
                  % ", ".join(str(v) for v in linbits))

        emitted = {}
        owner = {}
        for number in range(32):
            rows = tables.get(number)
            if not rows:
                continue
            key = tuple(sorted(rows))
            if key in emitted:
                owner[number] = emitted[key]
                continue
            emitted[key] = number
            owner[number] = number
            items, first, count = flatten(as_dict(rows))
            emit_table(out, "kHuff%d" % number, items, first, count, lambda s: s)

        for name in ("A", "B"):
            items, first, count = flatten(as_dict(tables[name]))
            emit_table(out, "kCount1%s" % name, items, first, count, lambda s: s)

        out.write("struct HuffTable {\n  const uint16_t* codes;\n  const uint8_t* symbols;\n")
        out.write("  const uint16_t* first;\n  const uint8_t* count;\n};\n\n")

        out.write("inline constexpr HuffTable kBigValueTables[32] = {\n")
        for number in range(32):
            if number not in owner:
                out.write("    {nullptr, nullptr, nullptr, nullptr},  // %d: unused\n" % number)
                continue
            n = "kHuff%d" % owner[number]
            note = "" if owner[number] == number else "  // %d shares %d" % (number, owner[number])
            out.write("    {%s_codes, %s_symbols, %s_first, %s_count},%s\n" % (n, n, n, n, note))
        out.write("};\n\n")

        out.write("inline constexpr HuffTable kCount1Tables[2] = {\n")
        for name in ("A", "B"):
            n = "kCount1%s" % name
            out.write("    {%s_codes, %s_symbols, %s_first, %s_count},\n" % (n, n, n, n))
        out.write("};\n\n}  // namespace mp3\n}  // namespace awtrix\n")


def fmt(value):
    text = "%.9g" % value
    if "." not in text and "e" not in text and "E" not in text:
        text += "."
    return text + "f"


def mdct_windows():
    """The four block windows, from the closed forms in the standard."""
    long_window = [math.sin(math.pi / 36.0 * (i + 0.5)) for i in range(36)]
    short = [math.sin(math.pi / 12.0 * (i + 0.5)) for i in range(12)]

    start = list(long_window[:18]) + [1.0] * 6 + [short[i - 18] for i in range(24, 30)] + [0.0] * 6
    stop = [0.0] * 6 + [short[i - 6] for i in range(6, 12)] + [1.0] * 6 + list(long_window[18:])
    short_window = short + [0.0] * 24
    return [long_window, start, short_window, stop]


def emit_tables(bands, preemphasis, ci):
    """Everything that is not Huffman and not the synthesis window."""
    cs = [1.0 / math.sqrt(1.0 + c * c) for c in ci]
    ca = [c / math.sqrt(1.0 + c * c) for c in ci]

    pan = []
    for position in range(7):
        ratio = math.tan(position * math.pi / 12.0)
        pan.append((ratio / (1.0 + ratio), 1.0 / (1.0 + ratio)))

    with open(TABLES_OUT, "w", encoding="utf-8") as out:
        out.write("// GENERATED by tools/gen_mp3_spec_tables.py -- DO NOT EDIT.\n//\n")
        out.write("// Layer III constant tables. Scalefactor band widths come from ISO/IEC\n")
        out.write("// 11172-3 Table 3-B.8, the preemphasis from 3-B.6 and the aliasing\n")
        out.write("// coefficients from 3-B.9. The block windows, the butterfly pair, the\n")
        out.write("// intensity weights and the power table are computed from the standard's\n")
        out.write("// closed forms rather than transcribed.\n//\n")
        out.write("// Band widths are stored so that walking them linearly covers all 576\n")
        out.write("// spectral lines in the order the Huffman data arrives: a short block\n")
        out.write("// repeats each width once per window. Each list ends with a 0.\n")
        out.write("#pragma once\n\n#include <cstdint>\n\nnamespace awtrix {\nnamespace mp3 {\n\n")

        out.write("inline constexpr int kSampleRateCount = %d;\n\n" % len(RATES))
        out.write("// Row index into the rate-dependent tables, or -1 if unsupported.\n")
        out.write("inline constexpr int rateIndex(int hz) {\n  return %s : -1;\n}\n\n"
                  % " : ".join("hz == %d ? %d" % (r, i) for i, r in enumerate(RATES)))

        longs, shorts, mixed = [], [], []
        for rate in RATES:
            long_widths, short_widths = bands[rate]
            longs.append(long_widths + [0])
            shorts.append([w for w in short_widths for _ in range(3)] + [0])
            mixed.append(long_widths[:8] + [w for w in short_widths[3:] for _ in range(3)] + [0])

        for name, table, note in (
            ("kBandsLong", longs, "Long blocks."),
            ("kBandsShort", shorts, "Short blocks, each width repeated per window."),
            ("kBandsMixed", mixed, "Mixed blocks: eight long bands, then short."),
        ):
            width = max(len(row) for row in table)
            out.write("// %s\n" % note)
            out.write("inline constexpr uint8_t %s[%d][%d] = {\n" % (name, len(table), width))
            for rate, row in zip(RATES, table):
                padded = row + [0] * (width - len(row))
                out.write("    {%s},  // %d Hz\n" % (", ".join(str(v) for v in padded), rate))
            out.write("};\n\n")

        out.write("// Added to the upper bands when preflag is set (Table 3-B.6).\n")
        out.write("inline constexpr uint8_t kPreemphasis[%d] = {%s};\n\n"
                  % (len(preemphasis), ", ".join(str(v) for v in preemphasis)))

        out.write("// scalefac_compress selects the two field widths. Transcribed from the\n")
        out.write("// body text of the standard and cross-checked against an independent\n")
        out.write("// implementation's copy.\n")
        slen = [(0, 0), (0, 1), (0, 2), (0, 3), (3, 0), (1, 1), (1, 2), (1, 3),
                (2, 1), (2, 2), (2, 3), (3, 1), (3, 2), (3, 3), (4, 2), (4, 3)]
        out.write("inline constexpr uint8_t kSlen1[16] = {%s};\n"
                  % ", ".join(str(a) for a, _ in slen))
        out.write("inline constexpr uint8_t kSlen2[16] = {%s};\n\n"
                  % ", ".join(str(b) for _, b in slen))

        out.write("// Alias reduction butterflies: cs = 1/sqrt(1+ci^2), ca = ci/sqrt(1+ci^2).\n")
        out.write("// The ci are all negative, so ca is too; the butterfly adds no sign.\n")
        out.write("inline constexpr float kAliasCs[8] = {%s};\n" % ", ".join(fmt(v) for v in cs))
        out.write("inline constexpr float kAliasCa[8] = {%s};\n\n" % ", ".join(fmt(v) for v in ca))

        out.write("// Intensity stereo: weights for is_pos 0..6, as (left, right).\n")
        out.write("inline constexpr float kIntensityPan[7][2] = {\n")
        for left, right in pan:
            out.write("    {%s, %s},\n" % (fmt(left), fmt(right)))
        out.write("};\n\n")

        out.write("// IMDCT block windows by block_type: 0 long, 1 start, 2 short, 3 stop.\n")
        out.write("// The short window holds its 12 coefficients in the low slots.\n")
        out.write("inline constexpr float kBlockWindow[4][36] = {\n")
        for name, window in zip(("long", "start", "short", "stop"), mdct_windows()):
            out.write("    {%s},  // %s\n" % (", ".join(fmt(v) for v in window), name))
        out.write("};\n\n")

        out.write("// x^(4/3) for the magnitudes that fit without an escape, plus enough\n")
        out.write("// headroom that the linbits path rarely has to call powf.\n")
        out.write("inline constexpr int kPow43Size = 256;\n")
        out.write("inline constexpr float kPow43[kPow43Size] = {\n")
        values = [fmt(pow(x, 4.0 / 3.0)) for x in range(256)]
        for i in range(0, 256, 8):
            out.write("    %s,\n" % ", ".join(values[i : i + 8]))
        out.write("};\n\n}  // namespace mp3\n}  // namespace awtrix\n")


def verify_against(path, tables):
    """Compare with the header recovered from minimp3, table by table."""
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    problems = 0
    for number in range(32):
        rows = tables.get(number)
        if not rows:
            continue
        m = re.search(r"kHuff%d_codes\[\] = \{([^}]*)\}" % number, text)
        if not m:
            continue
        codes = [int(v) for v in m.group(1).split(",")]
        mine = sorted(as_dict(rows).items(), key=lambda kv: (kv[0][1], kv[0][0]))
        if [c for (c, _), _ in mine] != codes:
            print("  table %d: codewords differ" % number)
            problems += 1
    print("cross-check against %s: %s" % (os.path.basename(path),
                                          "identical" if not problems else "%d differ" % problems))
    return problems == 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("annex", help="ANNEX_AB.DOC converted to text")
    ap.add_argument("--verify-against", help="an existing Mp3HuffmanTables.h to compare with")
    args = ap.parse_args(argv)

    with open(args.annex, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()

    window = parse_window(lines)
    check_window_symmetry(window)

    tables, linbits = parse_huffman(lines)
    for number, rows in tables.items():
        check_prefix_code("table %s" % number, as_dict(rows))

    ok = True
    if args.verify_against and os.path.exists(args.verify_against):
        ok = verify_against(args.verify_against, tables)

    bands = parse_scalefactor_bands(lines)
    preemphasis = parse_preemphasis(lines)
    ci = parse_alias_coefficients(lines)

    emit_window(window)
    emit_huffman(tables, linbits)
    emit_tables(bands, preemphasis, ci)
    print("wrote %s, %s and %s" % (os.path.basename(WINDOW_OUT), os.path.basename(HUFF_OUT),
                                   os.path.basename(TABLES_OUT)))
    print("window peak %.9f, %d big_values tables, %d codewords"
          % (max(abs(v) for v in window), sum(1 for n in range(32) if tables.get(n)),
             sum(len(r) for n, r in tables.items() if isinstance(n, int))))
    print("bands: %s" % ", ".join(
        "%d Hz long[0..2]=%s short[0]=%d" % (rate, bands[rate][0][:3], bands[rate][1][0])
        for rate in RATES))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
