#!/usr/bin/env python3
"""Build private TC002 res images with the shared JavaScript installation engine.

The output contains stock-res.img, awtrix-res.img and res-images.json. The stock
image remains byte-identical. Node.js and the pinned image tools from
tools/tc002/browser-image/build.py are required on Windows, Linux and macOS.
Keep the output private and outside the repository.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tc002_install as installer  # noqa: E402
from bundle import sha256_file  # noqa: E402
from constants import RES_PARTITION_BYTES as DEFAULT_PARTITION_SIZE, RES_ERASE_BYTES as DEFAULT_ERASE_SIZE  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
RES_INDEX, RES_NAME = 3, "res"
BUNDLE_LOADER, BUNDLE_LOOP = "lib/libawtrix-loader.so", "lib/modules/loop.ko"


class BuildError(Exception):
    pass


def read_backup(backup):
    manifest = json.loads((backup / "manifest.json").read_text())
    if manifest.get("status") != "complete":
        raise BuildError("backup manifest is not complete")
    records = [p for p in manifest.get("partitions", []) if p.get("index") == RES_INDEX]
    if len(records) != 1 or records[0].get("status") != "complete":
        raise BuildError(f"backup manifest has no complete mtd{RES_INDEX} record")
    record = records[0]
    size, erase = DEFAULT_PARTITION_SIZE, DEFAULT_ERASE_SIZE
    profile_path = backup / manifest.get("profile", {}).get("file", "profile.json")
    if profile_path.exists():
        profile = json.loads(profile_path.read_text())
        parts = [p for p in profile.get("partitions", []) if p.get("index") == RES_INDEX]
        if len(parts) != 1 or parts[0].get("name") != RES_NAME:
            raise BuildError(f"backup profile does not name mtd{RES_INDEX} {RES_NAME!r}")
        size, erase = parts[0]["size"], parts[0]["erase_size"]
    return backup / record["file"], record["sha256"], record["bytes"], size, erase


def load_bundle(bundle):
    try:
        manifest = installer.load_bundle(bundle)
        image = installer.bundle_image(bundle, manifest)
    except (installer.InstallError, installer.bundle_format.BundleError) as error:
        raise BuildError(f"bundle {bundle}: {error}")
    return Path(bundle), manifest, image


def inside_repository(path):
    try:
        path.resolve().relative_to(REPO)
        return True
    except ValueError:
        return False


def build(dump_path, expected_sha256, loader_path, output, partition_size=DEFAULT_PARTITION_SIZE,
          erase_size=DEFAULT_ERASE_SIZE, work_parent=None, replace=False, source=None,
          bundle=None):
    if bundle is None:
        raise BuildError("name the release bundle with --bundle")
    bundle, release, release_image = load_bundle(bundle)
    loader_path = Path(loader_path) if loader_path else bundle / BUNDLE_LOADER
    dump_path, output = Path(dump_path), Path(output)
    actual = sha256_file(dump_path)
    if actual != expected_sha256.lower():
        raise BuildError("res dump SHA-256 does not match the expected value")
    if inside_repository(output):
        raise BuildError("choose a private output directory outside the repository")
    names = ("stock-res.img", "awtrix-res.img", "res-images.json")
    if not replace and any((output / name).exists() for name in names):
        raise BuildError("output already holds images; pass --replace to overwrite them")
    node = shutil.which("node")
    if not node:
        raise BuildError("Node.js is required for the shared installation engine")
    with tempfile.TemporaryDirectory(prefix="awtrix-res-", dir=work_parent) as work:
        built = Path(work) / "awtrix-res.img"
        request = dict(stock=str(dump_path.resolve()), sha256=actual,
                       loader=str(loader_path.resolve()), loop=str((bundle / BUNDLE_LOOP).resolve()),
                       output=str(built.resolve()), partitionSize=partition_size,
                       eraseSize=erase_size, releaseBytes=release_image["size"])
        result = subprocess.run([node, str(Path(__file__).with_name("image_cli.mjs"))],
                                input=json.dumps(request), capture_output=True, text=True,
                                encoding="utf-8", timeout=600)
        if result.returncode:
            raise BuildError(result.stderr.strip() or "shared image engine failed")
        info = json.loads(result.stdout)
        if info["stockSha256"] != actual or sha256_file(built) != info["imageSha256"]:
            raise BuildError("image engine output failed verification")
        stock = info["stock"]
        manifest = {
            "schema_version": 1,
            "partition": {"index": RES_INDEX, "name": RES_NAME, "size": partition_size,
                          "erase_size": erase_size},
            "squashfs": {"compression": stock["compression"], "block_size": stock["blockSize"], "flags": stock["flags"],
                         "mkfs_time": stock["time"], "owner": stock["owner"]},
            "stock": {"file": names[0], "bytes": dump_path.stat().st_size, "sha256": actual,
                      "squashfs_bytes": stock["used"], "startup_library": "/res/lib/libzkgui.so",
                      "source": source or {}},
            "awtrix": {"file": names[1], "bytes": info["imageBytes"], "sha256": info["imageSha256"],
                       "squashfs_bytes": info["built"]["used"], "entries": info["built"]["entries"],
                       "startup_library": "/res/" + BUNDLE_LOADER, "removed": info["removed"],
                       "loader": {"bytes": loader_path.stat().st_size, "sha256": sha256_file(loader_path)},
                       "loop": {"path": "awtrix-ng/loop.ko", "bytes": (bundle / BUNDLE_LOOP).stat().st_size,
                                "sha256": sha256_file(bundle / BUNDLE_LOOP)},
                       "slot": dict(info["slot"], release=release["release"],
                                    release_image_bytes=release_image["size"])},
        }
        output.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(output, 0o700)
        for name, source_path in ((names[0], dump_path), (names[1], built)):
            destination = output / name
            with open(source_path, "rb") as src, open(destination, "wb") as dst:
                os.chmod(destination, 0o600)
                shutil.copyfileobj(src, dst)
                dst.flush()
                os.fsync(dst.fileno())
        if sha256_file(output / names[0]) != actual or sha256_file(output / names[1]) != info["imageSha256"]:
            raise BuildError("output images failed verification")
        installer.flash_backup.atomic_json(output / names[2], manifest)
        installer.flash_backup.sync_directory(output.parent)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--backup", type=Path, help="private backup directory (manifest.json, mtd3.bin)")
    source.add_argument("--res-dump", type=Path, help="raw dump of the whole res partition")
    parser.add_argument("--expect-sha256", help="SHA-256 of --res-dump")
    parser.add_argument("--bundle", type=Path, required=True,
                        help="release bundle: its loader and loop.ko go into res, its release image "
                             "must fit the release slot")
    parser.add_argument("--loader", type=Path,
                        help="ARM libawtrix-loader.so (default: the bundle's)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, help="parent for private temporary images")
    parser.add_argument("--replace", action="store_true", help="overwrite images in --output")
    args = parser.parse_args(argv)
    try:
        if args.backup:
            dump, expected, _, size, erase = read_backup(args.backup)
            origin = {"backup_manifest_sha256": sha256_file(args.backup / "manifest.json")}
        else:
            if not args.expect_sha256:
                parser.error("--res-dump needs --expect-sha256")
            dump, expected, size, erase = args.res_dump, args.expect_sha256, None, None
            origin = {}
        manifest = build(dump, expected, args.loader, args.output,
                         size or DEFAULT_PARTITION_SIZE, erase or DEFAULT_ERASE_SIZE,
                         args.work_dir, args.replace, origin, args.bundle)
    except (BuildError, OSError, KeyError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
