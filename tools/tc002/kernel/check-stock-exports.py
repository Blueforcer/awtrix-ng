#!/usr/bin/env python3
"""Check that symbols appear in the export name table of the stock kernel Image.

The table (__ksymtab_strings) is located as the densest run of names that stock modules
import, which the stock kernel must export. A requested name that sits inside that run is
exported by the stock kernel."""

import argparse
import sys

MAX_GAP = 0x4000


def offsets(image, name):
    key = b"\0" + name.encode() + b"\0"
    found = []
    at = image.find(key)
    while at >= 0:
        found.append(at + 1)
        at = image.find(key, at + 1)
    return found


def export_table(image, reference):
    points = sorted(o for name in reference for o in offsets(image, name))
    best, start = (0, 0, 0), 0
    for i in range(1, len(points) + 1):
        if i == len(points) or points[i] - points[i - 1] > MAX_GAP:
            if i - start > best[0]:
                best = (i - start, points[start], points[i - 1])
            start = i
    return best


def read_names(path):
    with open(path) as f:
        return [line.split()[0] for line in f if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="decompressed stock kernel Image")
    parser.add_argument("reference", help="names imported by stock modules, one per line")
    parser.add_argument("symbols", help="names to check, one per line")
    args = parser.parse_args()
    with open(args.image, "rb") as f:
        image = f.read()
    count, low, high = export_table(image, read_names(args.reference))
    print("export name table: 0x%x-0x%x (%d reference names)" % (low, high, count))
    missing = []
    for name in read_names(args.symbols):
        if not any(low <= o <= high for o in offsets(image, name)):
            missing.append(name)
    if missing:
        print("not in the stock export table: " + " ".join(missing))
        return 1
    print("all %d names are in the stock export table" % len(read_names(args.symbols)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
