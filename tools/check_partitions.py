"""Check every partition table scripts/gen_partitions.py can produce.

Partition arithmetic is the kind of thing that looks right in review and bricks a
device: an overlap or a misaligned app offset only shows up when someone flashes
it. This runs the generator over every (SoC, flash size) combination the project
ships and asserts the invariants that make a table bootable.

Where the ESP-IDF partition tool is available it also compiles each table to its
binary form, which is the authoritative check -- it enforces rules this script
does not restate.

    python tools/check_partitions.py
"""

import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import gen_partitions as gp  # noqa: E402

SUPPORTED = [
    ("esp32", "4MB"),
    ("esp32", "8MB"),
    ("esp32", "16MB"),
    ("esp32s3", "4MB"),
    ("esp32s3", "8MB"),
    ("esp32s3", "16MB"),
]

REJECTED = [("esp32", "2MB")]

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)


def find_idf_tool():
    pattern = os.path.join(
        os.path.expanduser("~"), ".platformio", "packages",
        "framework-arduinoespressif32*", "tools", "gen_esp32part.py",
    )
    hits = sorted(glob.glob(pattern))
    return hits[0] if hits else None


idf_tool = find_idf_tool()

for soc, flash_text in SUPPORTED:
    flash = gp.parse_flash_size(flash_text)
    label = "%s/%s" % (soc, flash_text)

    try:
        parts = gp.layout(soc, flash)
    except gp.LayoutError as e:
        failures.append("%s: generator refused a supported combination: %s" % (label, e))
        continue

    by_name = {p[0]: p for p in parts}
    check("app0" in by_name and "app1" in by_name, "%s: missing an app partition" % label)
    check("spiffs" in by_name, "%s: missing the spiffs partition" % label)
    check("coredump" in by_name, "%s: missing the coredump partition" % label)
    if failures:
        continue

    app0, app1 = by_name["app0"], by_name["app1"]
    check(app0[4] == app1[4], "%s: app slots differ (0x%X vs 0x%X)" % (label, app0[4], app1[4]))
    check(app0[4] == gp.APP_SLOT[soc], "%s: app slot does not match the policy table" % label)
    for name in ("app0", "app1"):
        off = by_name[name][3]
        check(off % gp.ALIGN == 0, "%s: %s starts at 0x%X, not 64 KB aligned" % (label, name, off))

    ordered = sorted(parts, key=lambda p: p[3])
    for prev, nxt in zip(ordered, ordered[1:]):
        end = prev[3] + prev[4]
        check(
            end <= nxt[3],
            "%s: %s ends at 0x%X, overlapping %s at 0x%X" % (label, prev[0], end, nxt[0], nxt[3]),
        )

    last = ordered[-1]
    check(last[0] == "coredump", "%s: %s sits after the coredump region" % (label, last[0]))
    check(
        last[3] + last[4] == flash,
        "%s: the table ends at 0x%X, not at the 0x%X flash end" % (label, last[3] + last[4], flash),
    )

    spiffs = by_name["spiffs"]
    check(
        spiffs[4] >= gp.MIN_SPIFFS,
        "%s: SPIFFS is %d KB, under the %d KB floor" % (label, spiffs[4] // 1024, gp.MIN_SPIFFS // 1024),
    )

    if idf_tool:
        with tempfile.TemporaryDirectory() as tmp:
            csv_path = os.path.join(tmp, "partitions.csv")
            with open(csv_path, "w") as f:
                f.write(gp.render(soc, flash))
            proc = subprocess.run(
                [sys.executable, idf_tool, "--flash-size", flash_text, csv_path,
                 os.path.join(tmp, "partitions.bin")],
                capture_output=True, text=True,
            )
            if proc.returncode != 0:
                failures.append(
                    "%s: gen_esp32part.py rejected the table:\n%s"
                    % (label, (proc.stderr or proc.stdout).strip())
                )

for soc, sizes in gp.FACTORY_FLASH_SIZES.items():
    for flash_text in sizes:
        check(
            (soc, flash_text) in SUPPORTED,
            "%s/%s ships a factory image but is not in the checked combinations"
            % (soc, flash_text),
        )

for soc, flash_text in REJECTED:
    try:
        gp.layout(soc, gp.parse_flash_size(flash_text))
    except gp.LayoutError:
        pass
    else:
        failures.append(
            "%s/%s: generator produced a table for a combination that does not fit"
            % (soc, flash_text)
        )

if failures:
    print("partition tables: %d problem(s)" % len(failures))
    for f in failures:
        print("  - %s" % f)
    raise SystemExit(1)

note = "" if idf_tool else " (ESP-IDF partition tool not found, skipped binary check)"
print("partition tables: %d combinations OK%s" % (len(SUPPORTED), note))
