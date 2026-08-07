"""Merge a full-flash image for one (env, flash size) pair.

`pio run` produces an app image and a bootloader, but the artefact a user flashes
over USB has to carry the partition table too -- and that table depends on the
flash size, while the app image does not. One build therefore yields several
factory images, differing only in their table and in the trailing free space.

    python scripts/factory_image.py --env awtrix_s3_octal --flash-size 8MB \\
        -o usb-awtrix-ng-s3-octal-8mb.bin

Or every flash variant that SoC ships in, named the way the release assets are
(gen_partitions.FACTORY_FLASH_SIZES is the list):

    python scripts/factory_image.py --env awtrix_s3_octal --all

The plain firmware.bin stays the OTA payload: OTA writes only the app slot, so it
needs no table and works across every flash size of its SoC.
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import gen_partitions as gp  # noqa: E402

BOOTLOADER_OFFSET = {"esp32": 0x1000, "esp32s3": 0x0000}

PARTITION_TABLE_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
APP_OFFSET = 0x10000


def packages_dir():
    core = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.join(
        os.path.expanduser("~"), ".platformio")
    return os.path.join(core, "packages")


def find_one(pattern, what):
    hits = sorted(glob.glob(pattern))
    if not hits:
        raise SystemExit("factory_image: no %s found (looked for %s)" % (what, pattern))
    return hits[0]


# Both S3 images name their PSRAM type. The octal one was plain -s3- up to v1.0.15, when it was
# the only one; naming just its counterpart would read as if -s3- were the general case and quad a
# variant of it, and they are two equal halves.
ASSET_NAME = {
    "awtrix": "awtrix-ng",
    "awtrix_s3_octal": "awtrix-ng-s3-octal",
    "awtrix_s3_quad": "awtrix-ng-s3-quad",
}


def asset_base(env):
    return ASSET_NAME.get(env, env.replace("_", "-"))


def default_name(env, flash_size):
    """The release-asset name for one image, e.g. usb-awtrix-ng-s3-octal-8mb.bin."""
    return "usb-%s-%s.bin" % (asset_base(env), flash_size.lower())


def build_one(env, soc, flash_size, output):
    flash = gp.parse_flash_size(flash_size)

    build = os.path.join(ROOT, ".pio", "build", env)
    firmware = os.path.join(build, "firmware.bin")
    bootloader = os.path.join(build, "bootloader.bin")
    for path in (firmware, bootloader):
        if not os.path.isfile(path):
            raise SystemExit("factory_image: %s missing -- run `pio run -e %s` first"
                             % (path, env))

    boot_app0 = find_one(
        os.path.join(packages_dir(), "framework-arduinoespressif32*", "tools", "partitions",
                     "boot_app0.bin"),
        "boot_app0.bin",
    )
    part_tool = find_one(
        os.path.join(packages_dir(), "framework-arduinoespressif32*", "tools",
                     "gen_esp32part.py"),
        "gen_esp32part.py",
    )

    with tempfile.TemporaryDirectory() as tmp:
        csv_path = os.path.join(tmp, "partitions.csv")
        bin_path = os.path.join(tmp, "partitions.bin")
        with open(csv_path, "w") as f:
            f.write(gp.render(soc, flash))
        subprocess.run([sys.executable, part_tool, "--flash-size", flash_size,
                        csv_path, bin_path], check=True)

        subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", soc, "merge_bin",
             "-o", output, "--flash_mode", "dio", "--flash_size", flash_size,
             hex(BOOTLOADER_OFFSET[soc]), bootloader,
             hex(PARTITION_TABLE_OFFSET), bin_path,
             hex(BOOT_APP0_OFFSET), boot_app0,
             hex(APP_OFFSET), firmware],
            check=True,
        )

    print("factory_image: %s (%s, %s flash)" % (output, soc, flash_size))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. awtrix_s3_octal")
    ap.add_argument("--soc", help="defaults to esp32s3 for envs carrying _s3, else esp32")
    ap.add_argument("--flash-size", help="e.g. 4MB, 8MB, 16MB")
    ap.add_argument("--all", action="store_true",
                    help="build every flash variant this SoC ships in")
    ap.add_argument("-o", "--output", help="output file, or output directory with --all")
    args = ap.parse_args(argv)

    # Matched anywhere in the name, not at the end: the S3 has variant envs
    # behind it (awtrix_s3_quad, awtrix_s3_octal_probe), and a suffix test would call
    # every one of them an esp32 and merge an image no S3 can boot.
    soc = args.soc or ("esp32s3" if "_s3" in args.env else "esp32")
    if soc not in BOOTLOADER_OFFSET:
        raise SystemExit("factory_image: unknown SoC %r" % soc)

    if args.all:
        if args.flash_size:
            raise SystemExit("factory_image: --all and --flash-size are exclusive")
        out_dir = args.output or "."
        os.makedirs(out_dir, exist_ok=True)
        for flash_size in gp.FACTORY_FLASH_SIZES[soc]:
            build_one(args.env, soc, flash_size,
                      os.path.join(out_dir, default_name(args.env, flash_size)))
        return

    if not args.flash_size:
        raise SystemExit("factory_image: need --flash-size or --all")
    build_one(args.env, soc, args.flash_size,
              args.output or default_name(args.env, args.flash_size))


if __name__ == "__main__":
    main(sys.argv[1:])
