"""Independent native inspection of images produced by the JavaScript engine."""
import os
import re
import struct
import subprocess

BuildError = ValueError
COMPRESSORS = {1: "gzip", 2: "lzma", 3: "lzo", 4: "xz", 5: "lz4", 6: "zstd"}

def parse_superblock(data):
    if len(data) < 96 or data[:4] != b"hsqs":
        raise BuildError("not a squashfs image")
    (inodes, mkfs_time, block_size, fragments, compression, block_log, flags, id_count,
     major, minor) = struct.unpack_from("<IIIIHHHHHH", data, 4)
    bytes_used = struct.unpack_from("<Q", data, 40)[0]
    if (major, minor) != (4, 0):
        raise BuildError(f"unsupported squashfs version {major}.{minor}")
    if compression not in COMPRESSORS:
        raise BuildError(f"unknown squashfs compressor id {compression}")
    if block_size != 1 << block_log or not 4096 <= block_size <= 1 << 20:
        raise BuildError("inconsistent squashfs block size")
    if not 96 <= bytes_used <= len(data):
        raise BuildError("squashfs is truncated")
    return {"inodes": inodes, "mkfs_time": mkfs_time, "block_size": block_size,
            "fragments": fragments, "compression": COMPRESSORS[compression], "flags": flags,
            "id_count": id_count, "bytes_used": bytes_used}


def run(command):
    environment = dict(os.environ, LC_ALL="C", TZ="UTC")
    result = subprocess.run(command, capture_output=True, text=True, env=environment,
                            preexec_fn=lambda: os.umask(0))
    if result.returncode:
        raise BuildError(f"{command[0]} failed: {(result.stderr or result.stdout).strip()}")
    return result.stdout


LISTING_LINE = re.compile(r"(?P<mode>[-dlcbps][-rwxsStT]{9}) (?P<owner>\d+/\d+) +(?P<size>\d+) "
                          r"(?P<time>\d{4}-\d\d-\d\d \d\d:\d\d) squashfs-root(?P<path>/.*)?$")


def listing(image):
    """Mode, owner, size and minute-resolution time of every entry, as the image stores them."""
    entries = {}
    for line in run(["unsquashfs", "-lln", "-UTC", str(image)]).splitlines():
        if not line.startswith(("-", "d", "l", "c", "b", "p", "s")) or " squashfs-root" not in line:
            continue
        match = LISTING_LINE.match(line)
        if not match:
            raise BuildError(f"unsupported entry in {image.name}: {line}")
        path = (match["path"] or "/").lstrip("/") or "."
        entry = {"mode": match["mode"], "owner": match["owner"], "size": int(match["size"]),
                 "time": match["time"]}
        if entry["mode"].startswith("l"):
            path, _, entry["target"] = path.partition(" -> ")
        entries[path] = entry
    if not entries:
        raise BuildError(f"cannot list {image}")
    return entries


def owners(image):
    return {entry["owner"] for entry in listing(image).values()}
