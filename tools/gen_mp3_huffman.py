"""Recover the Layer III Huffman code tables and emit them in this project's form.

Where the data comes from
-------------------------
The tables are fixed by ISO/IEC 11172-3 Annex B (Table 3-B.7). They are not
derivable from a rule -- they are hand-designed in the standard -- and the
standard is not freely available: every public copy of ISO 11172-3 that
circulates carries the body text without the normative annex.

So the values are recovered from **minimp3** (github.com/lieff/minimp3), which
is released under **CC0 1.0** -- a public domain dedication whose text reads
"waives, abandons, and surrenders all of Affirmer's Copyright and Related
Rights", with no attribution requirement. CC0 material can be used under any
licence, including this project's PolyForm Noncommercial.

What is taken and what is not
-----------------------------
Only the *data* is taken, and not even in the form minimp3 stores it. minimp3
holds a multi-level lookup structure tuned for its own decoding loop; this
script walks that structure to recover the underlying (code, length, value)
triples the standard defines, and re-emits them in a different representation
chosen for small flash use. None of minimp3's decoding logic is carried over --
that is written separately against the format description.

Running it
----------
    python tools/gen_mp3_huffman.py path/to/minimp3.h

Writes src/core/audio/Mp3HuffmanTables.h. The generated header is checked in, so
this only needs re-running if the representation changes.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "core", "audio", "Mp3HuffmanTables.h")

UNUSED_TABLES = {4, 14}


def parse_array(text, name, signed=True):
    """Pull a C array initialiser out of the header by name.

    Handles one or more dimensions; the row braces of a 2-D initialiser are
    flattened away, so the caller reshapes. Values may be integer or float
    literals, with or without a C float suffix.
    """
    m = re.search(r"\b%s\s*(?:\[[^\]]*\])+\s*=\s*\{(.*?)\};" % re.escape(name), text, re.S)
    if not m:
        raise SystemExit("could not find array %r in the source" % name)
    values = []
    for tok in m.group(1).replace("{", " ").replace("}", " ").split(","):
        tok = tok.strip()
        if not tok:
            continue
        if tok.endswith(("f", "F")) and not tok.lower().startswith("0x"):
            values.append(float(tok[:-1]))
        elif "." in tok or "e" in tok.lower() and not tok.lower().startswith("0x"):
            values.append(float(tok))
        else:
            values.append(int(tok, 0))
    if not signed:
        for v in values:
            if v < 0:
                raise SystemExit("array %r was expected to be unsigned" % name)
    return values


def walk_big_values(tabs, base):
    """Recover {(code, length): (x, y)} for one big_values table.

    minimp3's structure: peek `w` bits and index the table. A negative entry is
    an internal node -- its low three bits are the next peek width and the rest
    is the negated child offset. A non-negative entry is a leaf: bits 8+ hold
    how many bits the codeword actually consumed at this level, and the low byte
    holds the two values as nibbles.
    """
    out = {}

    def walk(relative, width, prefix, prefix_len):
        for pattern in range(1 << width):
            leaf = tabs[base + relative + pattern]
            if leaf < 0:
                walk(-(leaf >> 3), leaf & 7, (prefix << width) | pattern, prefix_len + width)
                continue
            used = leaf >> 8
            code = (prefix << used) | (pattern >> (width - used))
            out[(code, prefix_len + used)] = (leaf & 0x0F, (leaf >> 4) & 0x0F)

    walk(0, 5, 0, 0)
    return out


def walk_count1(tab):
    """Recover {(code, length): (v, w, x, y)} for a count1 table.

    Same idea, narrower: peek four bits; bit 3 clear marks an internal node
    whose low two bits give the next peek width and whose top bits give the
    child offset. The value nibble holds four magnitude flags.
    """
    out = {}

    def emit(leaf, code, length):
        values = tuple(1 if (leaf & (128 >> i)) else 0 for i in range(4))
        out[(code, length)] = values

    for pattern in range(16):
        leaf = tab[pattern]
        if leaf & 8:
            used = leaf & 7
            emit(leaf, pattern >> (4 - used), used)
            continue
        width = leaf & 3
        base = leaf >> 3
        for sub in range(1 << width):
            child = tab[base + sub]
            used = child & 7
            tail = used - 4
            emit(child, (pattern << tail) | (sub >> (width - tail)), used)
    return out


def check_prefix_code(name, codes):
    """A table that is not a valid prefix code would decode to plausible noise."""
    kraft = 0.0
    for _, length in codes:
        if not 1 <= length <= 19:
            raise SystemExit("%s: codeword length %d out of range" % (name, length))
        kraft += 2.0 ** -length
    if kraft > 1.0 + 1e-9:
        raise SystemExit("%s: Kraft sum %.6f exceeds 1 -- not a prefix code" % (name, kraft))
    seen = {}
    for code, length in codes:
        for other_code, other_len in seen.items():
            shorter, longer = (other_len, length) if other_len < length else (length, other_len)
            a = other_code if other_len < length else code
            b = code if other_len < length else other_code
            if (b >> (longer - shorter)) == a:
                raise SystemExit("%s: %r is a prefix of %r" % (name, (a, shorter), (b, longer)))
        seen[code] = length


def flatten(codes):
    """Sort into (length, code) order and split out the per-length index."""
    items = sorted(codes.items(), key=lambda kv: (kv[0][1], kv[0][0]))
    first = [0] * 21
    count = [0] * 21
    for (_, length), _value in items:
        count[length] += 1
    running = 0
    for length in range(21):
        first[length] = running
        running += count[length]
    return items, first, count


def emit_table(out, name, items, first, count, symbol_of):
    codes = ", ".join(str(code) for (code, _), _ in items)
    symbols = ", ".join(str(symbol_of(value)) for _, value in items)
    out.write("inline constexpr uint16_t %s_codes[] = {%s};\n" % (name, codes or "0"))
    out.write("inline constexpr uint8_t %s_symbols[] = {%s};\n" % (name, symbols or "0"))
    out.write("inline constexpr uint16_t %s_first[] = {%s};\n"
              % (name, ", ".join(str(v) for v in first)))
    out.write("inline constexpr uint8_t %s_count[] = {%s};\n\n"
              % (name, ", ".join(str(v) for v in count)))


def main(argv):
    if len(argv) != 1:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    with open(argv[0], "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    tabs = parse_array(text, "tabs")
    tabindex = parse_array(text, "tabindex")
    linbits = parse_array(text, "g_linbits", signed=False)
    tab32 = parse_array(text, "tab32", signed=False)
    tab33 = parse_array(text, "tab33", signed=False)

    tables = []
    for number in range(32):
        if number in UNUSED_TABLES:
            tables.append(None)
            continue
        base = tabindex[number]
        if base == 0 and number == 0:
            tables.append({})
            continue
        codes = walk_big_values(tabs, base)
        check_prefix_code("table %d" % number, codes)
        tables.append(codes)

    count1 = []
    for name, tab in (("A", tab32), ("B", tab33)):
        codes = walk_count1(tab)
        check_prefix_code("count1 table %s" % name, codes)
        count1.append(codes)

    total = sum(len(t) for t in tables if t) + sum(len(c) for c in count1)

    with open(OUT, "w", encoding="utf-8") as out:
        out.write("// GENERATED by tools/gen_mp3_huffman.py -- DO NOT EDIT.\n")
        out.write("//\n")
        out.write("// Layer III Huffman code tables, ISO/IEC 11172-3 Table 3-B.7.\n")
        out.write("// Values recovered from minimp3 (CC0 1.0, public domain dedication) and\n")
        out.write("// re-emitted in this project's own representation; see the generator for\n")
        out.write("// why that source and what was and was not taken from it.\n")
        out.write("//\n")
        out.write("// Each table is stored as its codewords sorted by (length, code), with a\n")
        out.write("// per-length index. Decoding walks one bit at a time and searches only the\n")
        out.write("// run for the current length, which costs a little speed and saves the\n")
        out.write("// kilobytes a flat lookup would spend.\n")
        out.write("//\n")
        out.write("// %d codewords across %d big_values tables and 2 count1 tables.\n"
                  % (total, sum(1 for t in tables if t)))
        out.write("#pragma once\n\n#include <cstdint>\n\nnamespace awtrix {\nnamespace mp3 {\n\n")

        out.write("inline constexpr uint8_t kLinbits[32] = {%s};\n\n"
                  % ", ".join(str(v) for v in linbits))

        emitted = {}
        owner = [None] * 32
        for number, codes in enumerate(tables):
            if codes is None or not codes:
                continue
            key = tuple(sorted(codes.items()))
            if key in emitted:
                owner[number] = emitted[key]
                continue
            emitted[key] = number
            owner[number] = number
            items, first, count = flatten(codes)
            emit_table(out, "kHuff%d" % number, items, first, count,
                       lambda v: (v[0] << 4) | v[1])

        for name, codes in zip(("A", "B"), count1):
            items, first, count = flatten(codes)
            emit_table(out, "kCount1%s" % name, items, first, count,
                       lambda v: (v[0] << 3) | (v[1] << 2) | (v[2] << 1) | v[3])

        out.write("struct HuffTable {\n")
        out.write("  const uint16_t* codes;\n")
        out.write("  const uint8_t* symbols;\n")
        out.write("  const uint16_t* first;\n")
        out.write("  const uint8_t* count;\n")
        out.write("};\n\n")

        out.write("inline constexpr HuffTable kBigValueTables[32] = {\n")
        for number in range(32):
            if owner[number] is None:
                out.write("    {nullptr, nullptr, nullptr, nullptr},  // %d: unused\n" % number)
                continue
            n = "kHuff%d" % owner[number]
            shared = "" if owner[number] == number else "  // %d shares %d" % (number, owner[number])
            out.write("    {%s_codes, %s_symbols, %s_first, %s_count},%s\n" % (n, n, n, n, shared))
        out.write("};\n\n")

        out.write("inline constexpr HuffTable kCount1Tables[2] = {\n")
        for name in ("A", "B"):
            n = "kCount1%s" % name
            out.write("    {%s_codes, %s_symbols, %s_first, %s_count},\n" % (n, n, n, n))
        out.write("};\n\n")

        out.write("}  // namespace mp3\n}  // namespace awtrix\n")

    print("wrote %s (%d codewords, %d bytes)" % (OUT, total, os.path.getsize(OUT)))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
