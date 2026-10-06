#!/usr/bin/env python3
"""Build a deterministic public TC002 browser-install ZIP from a verified bundle."""
import argparse
import hashlib
import json
import os
import re
import stat
import sys
import zipfile
from pathlib import Path
from urllib.parse import urlsplit

import bundle

from constants import MAX_FILE_BYTES, MAX_ARCHIVE_BYTES, MAX_FILES, PRODUCTION_TARGET
PUBLIC_FILES = frozenset((*bundle.REQUIRED, "share/boot.mp3", *bundle.HOST_ONLY, *bundle.MCU_PATCH,
                          bundle.SPEECH_VOICE, bundle.IMAGE))
HELPER = "bin/awtrix-tc002-flash"
MANIFEST = "browser-manifest.json"
TARGET = PRODUCTION_TARGET
SOURCE_NOTICE = """Corresponding sources for this TC002 release

Release: {release}
AWTRIX commit: {commit}
Archive: {url}
SHA-256: {sha256}

The companion archive supplies the pinned sources, local patches and build recipes for
this binary release, including BusyBox, libnl, wpa_supplicant, the AIC Wi-Fi drivers and
awtrix_pcm. Its README describes rebuilding and replacing the LGPL libnl portion.
The installed licence texts are in share/licenses.txt.gz inside bundle/release.img. AWTRIX's own licence
and third-party notices accompany this package under notices/.

This installer package contains the AWTRIX release image and its installation helpers.
It contains no stock partition image, device backup or device credentials.
"""


class PackageError(ValueError):
    pass


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise PackageError(f"duplicate JSON member {key}")
        result[key] = value
    return result


def encoded(value):
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def read_file(path):
    path = Path(path)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode):
        raise PackageError(f"not a regular file: {path}")
    if not 0 < info.st_size <= MAX_FILE_BYTES:
        raise PackageError(f"file size outside 1..{MAX_FILE_BYTES}: {path}")
    with path.open("rb") as stream:
        data = stream.read(MAX_FILE_BYTES + 1)
    if len(data) != info.st_size:
        raise PackageError(f"file changed while reading: {path}")
    return data


def source_reference(url, sha256):
    if not isinstance(url, str) or not isinstance(sha256, str):
        raise PackageError("sources require a URL and a SHA-256 string")
    parsed = urlsplit(url)
    if (len(url) > 2048 or not url.isascii() or any(ord(c) <= 32 or ord(c) == 127 for c in url) or
            parsed.scheme != "https" or not parsed.hostname or parsed.username is not None or
            parsed.password is not None or parsed.query or parsed.fragment or not parsed.path or
            "\\" in url or not re.fullmatch(r"[0-9a-f]{64}", sha256)):
        raise PackageError("sources require an HTTPS archive URL without credentials/query/fragment and a SHA-256")
    return {"url": url, "sha256": sha256}


def package_entries(directory, license_path, notices_path, sources, allow_dirty):
    directory = Path(directory)
    if not stat.S_ISDIR(directory.lstat().st_mode):
        raise PackageError("bundle must be a directory, not a symlink")
    directories = {"bin", "share", "lib", "lib/modules", "share/mcu", "share/speech"}
    for path in directory.rglob("*"):
        relative = path.relative_to(directory).as_posix()
        info = path.lstat()
        if stat.S_ISDIR(info.st_mode) and relative in directories:
            continue
        if relative not in PUBLIC_FILES | {"manifest.json"} or not stat.S_ISREG(info.st_mode):
            raise PackageError(f"not a public bundle file: {relative}")
    original = read_file(directory / "manifest.json")
    manifest = json.loads(original, object_pairs_hook=unique_object)
    fields = {"schema_version", "version", "commit", "dirty", "release", "counter", "image", "files", "hostOnly"}
    if not isinstance(manifest, dict) or not fields <= manifest.keys() or manifest.keys() - fields - {"compilers"}:
        raise PackageError("unsupported bundle manifest members")
    if not isinstance(manifest["dirty"], bool) or (manifest["dirty"] and not allow_dirty):
        raise PackageError("dirty bundle requires --allow-dirty for a local preview")
    if not isinstance(manifest["commit"], str) or not re.fullmatch(r"[0-9a-f]{7,40}", manifest["commit"]):
        raise PackageError("bundle requires a source commit")
    if (not isinstance(manifest["version"], str) or not bundle.VERSION.fullmatch(manifest["version"]) or
            not bundle.valid_release_name(manifest["release"])):
        raise PackageError("invalid bundle release identity")
    bundle.check_counter(manifest["counter"])
    if manifest["counter"] is None:
        raise PackageError("bundle requires an update counter")
    if not isinstance(manifest["image"], dict) or set(manifest["image"]) != {"size", "sha256"}:
        raise PackageError("bundle requires its release image")
    verified = bundle.verify(directory)
    if verified != manifest or read_file(directory / "manifest.json") != original:
        raise PackageError("bundle manifest changed while reading")
    installed = {entry["path"] for entry in manifest["files"]}
    hosts = {entry["path"] for entry in manifest["hostOnly"]}
    if hosts != set(bundle.HOST_ONLY):
        raise PackageError("bundle must contain both host helpers")
    try:
        bundle.public_files(installed | hosts)
    except bundle.BundleError as error:
        raise PackageError(str(error)) from error
    if ([entry["path"] for entry in manifest["files"]] != sorted(installed) or
            [entry["path"] for entry in manifest["hostOnly"]] != sorted(hosts)):
        raise PackageError("bundle file lists must be sorted")
    for entry in manifest["files"] + manifest["hostOnly"]:
        if set(entry) != {"path", "size", "sha256", "mode"} or type(entry["size"]) is not int:
            raise PackageError(f"unsupported bundle file members: {entry['path']}")
    shipped = [("helper", next(e for e in manifest["files"] if e["path"] == HELPER))]
    shipped += [("host", entry) for entry in manifest["hostOnly"]]
    shipped.append(("image", {"path": bundle.IMAGE, "mode": "0644", **manifest["image"]}))
    entries = {}
    for role, entry in shipped:
        data = read_file(directory / entry["path"])
        if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise PackageError(f"bundle file changed: {entry['path']}")
        entries["bundle/" + entry["path"]] = (data, entry["mode"], role)
    entries["bundle/manifest.json"] = (original, "0644", "manifest")
    entries["notices/LICENSE.md"] = (read_file(license_path), "0644", "notice")
    entries["notices/THIRD-PARTY-NOTICES.md"] = (read_file(notices_path), "0644", "notice")
    entries["notices/SOURCE-NOTICE.txt"] = (
        SOURCE_NOTICE.format(release=manifest["release"], commit=manifest["commit"], **sources).encode(),
        "0644", "notice")
    browser = {"schemaVersion": 2, "target": TARGET, "version": manifest["version"],
               "release": manifest["release"], "commit": manifest["commit"],
               "counter": str(manifest["counter"]), "dirty": manifest["dirty"], "bundlePrefix": "bundle/",
               "bundleManifest": "bundle/manifest.json",
               "image": {"path": "bundle/" + bundle.IMAGE, **manifest["image"]},
               "flashHelper": "bundle/" + HELPER, "loader": "bundle/lib/libawtrix-loader.so",
               "loop": "bundle/lib/modules/loop.ko", "sources": sources,
               "files": [{"path": path, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                          "mode": mode, "role": role}
                         for path, (data, mode, role) in sorted(entries.items())]}
    entries[MANIFEST] = (encoded(browser), "0644", "manifest")
    return browser, entries


def create(directory, output, *, license_path, notices_path, source_url, source_sha256, allow_dirty=False):
    sources = source_reference(source_url, source_sha256)
    browser, entries = package_entries(directory, license_path, notices_path, sources, allow_dirty)
    if len(entries) > MAX_FILES or any(not 0 < len(data) <= MAX_FILE_BYTES for data, _, _ in entries.values()):
        raise PackageError("too many files or an oversized entry")
    archive_size = 22 + sum(76 + 2 * len(path.encode("ascii")) + len(data)
                            for path, (data, _, _) in entries.items())
    if archive_size > MAX_ARCHIVE_BYTES:
        raise PackageError("archive exceeds 32 MiB")
    output = Path(output)
    created = False
    try:
        with output.open("xb") as stream:
            created = True
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_STORED, allowZip64=False) as archive:
                for path in [MANIFEST, *sorted(set(entries) - {MANIFEST})]:
                    data, mode, _ = entries[path]
                    info = zipfile.ZipInfo(path, (1980, 1, 1, 0, 0, 0))
                    info.create_system = 3
                    info.external_attr = (stat.S_IFREG | int(mode, 8)) << 16
                    archive.writestr(info, data)
            stream.flush()
            os.fsync(stream.fileno())
        if output.stat().st_size != archive_size:
            raise PackageError("unexpected ZIP metadata")
    except BaseException:
        if created:
            output.unlink()
        raise
    return browser


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--license", required=True, type=Path)
    parser.add_argument("--notices", required=True, type=Path)
    parser.add_argument("--source-url", required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args(argv)
    try:
        manifest = create(args.bundle, args.output, license_path=args.license, notices_path=args.notices,
                          source_url=args.source_url, source_sha256=args.source_sha256,
                          allow_dirty=args.allow_dirty)
    except (OSError, ValueError, bundle.BundleError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"{args.output}: {manifest['release']}, release image {manifest['image']['size']} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
