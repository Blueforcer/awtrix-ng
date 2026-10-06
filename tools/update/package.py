#!/usr/bin/env python3
"""Create bounded, unsigned AWUPD003 update containers with SHA-256 integrity checks.

  package.py --release-dir DIR --output OUT.awup [--target T] [--release R] [--counter N]
      DIR is an installed release: manifest.json plus exactly the files it lists. The payload is
      its release image (tools/tc002/install/bundle.py), the squashfs the TC002 mounts from its
      release slot.
  package.py --image FILE --output OUT.awup --release R --counter N [--target T]
      Packages a release image built before.
  package.py --payload FILE --output OUT.awup --target experimental:NAME --release R --counter N
      Packages any payload for an experimental target.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tc002" / "install"))
from bundle import BundleError, image_valid, release_image, valid_release_name  # noqa: E402
from constants import PRODUCTION_TARGET  # noqa: E402

MAGIC = b"AWUPD003"
HEADER = struct.Struct(">8sHHIQQHH32s")
MAX_PAYLOAD = 256 * 1024 * 1024
MAX_COUNTER = (1 << 63) - 1
IDENTIFIER = re.compile(r"[A-Za-z0-9._:-]+", re.ASCII)


def allowed_target(target: str) -> bool:
    return (bool(IDENTIFIER.fullmatch(target)) and len(target) <= 128 and
            (target == PRODUCTION_TARGET or
             (target.startswith("experimental:") and target != "experimental:")))


def create(payload: Path, output: Path, target: str, release: str, counter: int) -> dict:
    """Publish without replacing an existing output. Never writes to a device. The payload of an
    awtrix-ng:tc002 package must be a release image."""
    if not allowed_target(target):
        raise ValueError("only awtrix-ng:tc002 or explicit experimental profiles are supported")
    target_bytes = target.encode("ascii")
    if not valid_release_name(release):
        raise ValueError("invalid release identifier")
    release_bytes = release.encode("ascii")
    if not isinstance(counter, int) or isinstance(counter, bool) or not 0 < counter <= MAX_COUNTER:
        raise ValueError("counter must be a positive integer below 2^63")
    if not output.name or output.name in (".", ".."):
        raise ValueError("output filename required")
    # Linux/WSL is required for O_NOFOLLOW, directory fsync and atomic link publication.
    if os.name != "posix":
        raise ValueError("run the packaging tool on Linux or WSL")
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    with os.fdopen(os.open(payload, flags), "rb") as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= MAX_PAYLOAD:
            raise ValueError("payload must be a regular file between 1 byte and 256 MiB")
        directory = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
        published = False
        try:
            directory_info = os.fstat(directory)
            if directory_info.st_uid != os.geteuid() or stat.S_IMODE(directory_info.st_mode) != 0o700:
                raise ValueError("output directory must be caller-owned with mode 0700")
            # Build in a private same-filesystem directory. The source is copied
            # once; the digest covers exactly the copied bytes, even if the input
            # changes concurrently. Only bounded header/digest areas are rewritten.
            with tempfile.TemporaryDirectory(prefix=".awup-build-", dir=output.parent) as work:
                work_path = Path(work)
                candidate = work_path / "container"
                manifest_size = HEADER.size + len(target_bytes) + len(release_bytes)
                digest = hashlib.sha256()
                with candidate.open("x+b") as staged:
                    candidate.chmod(0o600)
                    staged.write(bytes(manifest_size + 32))
                    remaining = info.st_size
                    first = True
                    while remaining:
                        chunk = source.read(min(65536, remaining))
                        if not chunk:
                            raise ValueError("payload truncated during packaging")
                        if first and target == PRODUCTION_TARGET and not image_valid(chunk[:96], info.st_size):
                            raise ValueError("the payload of an awtrix-ng:tc002 package must be a release image")
                        first = False
                        digest.update(chunk)
                        staged.write(chunk)
                        remaining -= len(chunk)
                    if source.read(1):
                        raise ValueError("payload grew during packaging")
                    manifest = HEADER.pack(MAGIC, 3, 0, manifest_size, info.st_size, counter, len(target_bytes),
                                           len(release_bytes), digest.digest()) + target_bytes + release_bytes
                    manifest_digest = hashlib.sha256(manifest).digest()
                    staged.seek(0)
                    staged.write(manifest)
                    staged.write(manifest_digest)
                    staged.flush()
                    os.fsync(staged.fileno())
                # Existing files and symlinks both cause failure; no overwrite.
                os.link(candidate, output.name, dst_dir_fd=directory, follow_symlinks=False)
                published = True
                os.fsync(directory)
            # Persist removal of the private construction directory too.
            os.fsync(directory)
        except OSError as error:
            if published:
                raise ValueError("package published but directory durability is uncertain") from error
            raise
        finally:
            os.close(directory)
    return {"format": 3, "target": target, "release": release, "counter": counter,
            "payload_bytes": info.st_size, "payload_sha256": digest.hexdigest(), "package": str(output)}


def create_release(release_dir: Path, output: Path, target: str = PRODUCTION_TARGET,
                   release: str | None = None, counter: int | None = None) -> dict:
    manifest = json.loads((release_dir / "manifest.json").read_bytes())
    listed_release = manifest.get("release") if isinstance(manifest, dict) else None
    listed_counter = manifest.get("counter") if isinstance(manifest, dict) else None
    release = release if release is not None else listed_release
    counter = counter if counter is not None else listed_counter
    if not isinstance(release, str):
        raise ValueError("release name required (--release or manifest.json release)")
    if listed_release is not None and listed_release != release:
        raise ValueError("--release differs from manifest.json release")
    if not isinstance(counter, int) or isinstance(counter, bool):
        raise ValueError("counter required (--counter or manifest.json counter)")
    if listed_counter is not None and listed_counter != counter:
        raise ValueError("--counter differs from manifest.json counter")
    with tempfile.TemporaryDirectory(prefix=".awup-image-", dir=output.parent) as work:
        image = Path(work) / "release.img"
        release_image(release_dir, image)
        return create(image, output, target, release, counter)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--release-dir", type=Path)
    source.add_argument("--image", type=Path)
    source.add_argument("--payload", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target")
    parser.add_argument("--release")
    parser.add_argument("--counter", type=int)
    args = parser.parse_args()
    try:
        if args.release_dir:
            result = create_release(args.release_dir, args.output, args.target or PRODUCTION_TARGET,
                                    args.release, args.counter)
        elif args.image:
            if args.release is None or args.counter is None:
                parser.error("--image needs --release and --counter")
            result = create(args.image, args.output, args.target or PRODUCTION_TARGET, args.release, args.counter)
        else:
            if args.target is None or args.release is None or args.counter is None:
                parser.error("--payload needs --target, --release and --counter")
            if args.target == PRODUCTION_TARGET:
                parser.error("awtrix-ng:tc002 packages carry a release image: use --image or --release-dir")
            result = create(args.payload, args.output, args.target, args.release, args.counter)
    except (OSError, ValueError, BundleError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"package creation failed: {error}\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
