#!/usr/bin/env python3
"""The TC002 release bundle: layout, manifest.json and its verification.

  bundle.py manifest BUNDLE_DIR --version V --commit C [--counter N] [--dirty]
                    [--compiler NAME=GCC]...      write BUNDLE_DIR/manifest.json
  bundle.py verify BUNDLE_DIR                     check the files against it
  bundle.py release BUNDLE_DIR TARGET_DIR         write the installed release: manifest.json
                                                  and the listed files, without host-only files
  bundle.py image BUNDLE_DIR                      write BUNDLE_DIR/release.img, the release image:
                                                  the squashfs of that release, which the TC002
                                                  mounts from its release slot in res and update
                                                  packages carry, and list it in manifest.json
  bundle.py webui REPO OUTPUT                     write share/index.html.gz: the minified, gzipped
                                                  web UI with its Linux-only parts, from WebUiAsset.h

A bundle is bin/, share/ and lib/ plus manifest.json and release.img. "files" lists the installed
release with path, size, SHA-256 and mode. "hostOnly" lists the files the installer puts into res
next to the vendor app, the loader and loop.ko; no release image or update package contains them.
"image" names size and SHA-256 of release.img, made from the installed release, so installers
without mksquashfs can write it; a bundle without a counter has none. "counter"
is the update counter, the committer time of the release commit. "compilers" names the version
of each cross compiler the release was built with and a SHA-256 over what its -v output says
about its build. The release name, which the release slot and every update package carry, is
the version followed by the first twelve hex digits of a digest over every installed file's
path, mode and SHA-256, so different content never shares a name.

The release image is made by mksquashfs 4.6 or later with fixed options (xz, 128 KiB blocks, no
xattrs, owner root, every time the release counter), so equal releases give equal images with
the same mksquashfs.
"""
import argparse
import fnmatch
import hashlib
import json
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts"))
from webui_source import assemble as assemble_webui

WEBUI_SOURCE = "webui/index.html"
WEBUI_ASSET = "src/transport/http/WebUiAsset.h"
SCHEMA_VERSION = 3
TOP_LEVEL = ("bin", "share", "lib")
REQUIRED = ("bin/awtrix-linux", "bin/awtrix-tc002d", "bin/udhcpc", "bin/dhcp-callback",
            "bin/wpa_supplicant", "bin/awtrix-tc002-audio-pcm", "bin/awtrix-tc002-flash",
            "lib/modules/aic8800_bsp.ko", "lib/modules/aic8800_fdrv.ko", "lib/modules/awtrix_pcm.ko",
            "share/index.html.gz", "share/ca-certificates.crt", "share/licenses.txt.gz")
HOST_ONLY = ("lib/libawtrix-loader.so", "lib/modules/loop.ko")
MCU_PATCH = frozenset(("share/mcu/manifest.json", "share/mcu/extension.bin", "share/mcu/LICENSE.txt"))
# The voice that reads speech aloud; a release without it has no speech.
SPEECH_VOICE = "share/speech/voice.atts"
EXECUTABLE_DIRS = ("bin",)
# A path component and a release directory name: src/platform/tc002/contract/ReleaseName.h.
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,63}")
# Short enough that version, "-" and twelve hex digits stay one NAME.
VERSION = re.compile(r"[0-9A-Za-z][0-9A-Za-z.+_-]{0,50}")
MAX_COUNTER = (1 << 63) - 1
COMPILER_LINES = ("Target:", "Configured with:", "Thread model:", "gcc version ")
SQUASHFS_MAGIC = b"hsqs"
SQUASHFS_PADDING = 4096
IMAGE = "release.img"
# What the bundle's manifest.json carries beyond the release's own.
BUNDLE_ONLY = ("hostOnly", "image")
IMAGE_OPTIONS = ("-noappend", "-comp", "xz", "-b", "131072", "-no-xattrs", "-all-root", "-root-mode", "0755",
                 "-no-progress", "-quiet")


class BundleError(Exception):
    pass


def file_digest(path, length=None):
    digest, size = hashlib.sha256(), 0
    with open(path, "rb") as source:
        while length is None or size < length:
            block = source.read(1 << 20 if length is None else min(1 << 20, length - size))
            if not block:
                break
            digest.update(block)
            size += len(block)
    if length is not None and size != length:
        raise BundleError(f"{path} is shorter than {length} bytes")
    return size, digest.hexdigest()


def sha256_file(path, length=None):
    return file_digest(path, length)[1]


def public_files(paths):
    """Exact public layout, retaining support for releases before local MCU patching."""
    paths = set(paths)
    expected = {*REQUIRED, *HOST_ONLY, "share/boot.mp3", *(paths & {SPEECH_VOICE})}
    if paths & MCU_PATCH:
        if not MCU_PATCH <= paths:
            raise BundleError('incomplete MCU patch')
        expected |= MCU_PATCH
    if paths != expected:
        raise BundleError('bundle differs from public file allowlist')
    return expected


def valid_release_name(name):
    """A NAME that does not end in .partial, the suffix of a release still being written."""
    return isinstance(name, str) and bool(NAME.fullmatch(name)) and not name.endswith(".partial")


def file_mode(relative):
    return "0755" if relative.split("/")[0] in EXECUTABLE_DIRS or relative.endswith(".so") else "0644"


def collect(bundle):
    """Every file of the bundle with size, SHA-256 and mode, sorted by path."""
    bundle = Path(bundle)
    files = []
    for path in sorted(bundle.rglob("*"), key=lambda p: p.relative_to(bundle).as_posix()):
        relative = path.relative_to(bundle).as_posix()
        if relative in ("manifest.json", IMAGE):
            continue
        info = path.lstat()
        if stat.S_ISDIR(info.st_mode):
            continue
        parts = relative.split("/")
        if parts[0] not in TOP_LEVEL or len(parts) < 2:
            raise BundleError(f"unexpected entry {relative}")
        if not stat.S_ISREG(info.st_mode):
            raise BundleError(f"{relative} is not a regular file")
        if not all(NAME.fullmatch(part) for part in parts):
            raise BundleError(f"unsupported file name {relative}")
        data = path.read_bytes()
        if relative.startswith("share/mcu/") and relative not in MCU_PATCH:
            raise BundleError("manufacturer MCU images do not belong in a release bundle")
        files.append({"path": relative, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                      "mode": file_mode(relative)})
    return files


def release_name(version, files):
    digest = hashlib.sha256()
    for entry in files:
        digest.update(f"{entry['path']} {entry['mode']} {entry['sha256']}\n".encode())
    return f"{version}-{digest.hexdigest()[:12]}"


def compiler_identity(compiler):
    """The version of a GCC and a SHA-256 over the lines of its -v output that describe the build
    of the compiler: target, configure options, thread model and version, not the path it was
    called by."""
    result = subprocess.run([str(compiler), "-v"], capture_output=True, text=True, check=False)
    lines = [line.rstrip() for line in result.stderr.splitlines() if line.startswith(COMPILER_LINES)]
    version = next((line[len("gcc version "):] for line in lines if line.startswith("gcc version ")), "")
    if result.returncode or not version:
        raise BundleError(f"{compiler} -v does not describe a GCC")
    return {"version": version, "sha256": hashlib.sha256("\n".join(lines).encode()).hexdigest()}


def check_compilers(compilers):
    if not isinstance(compilers, dict) or not all(
            NAME.fullmatch(name) and isinstance(entry, dict) and set(entry) == {"version", "sha256"} and
            isinstance(entry["version"], str) and entry["version"] and
            re.fullmatch(r"[0-9a-f]{64}", str(entry["sha256"])) for name, entry in compilers.items()):
        raise BundleError(f"invalid compilers {compilers!r}")


def check_counter(counter):
    if counter is not None and (not isinstance(counter, int) or isinstance(counter, bool) or
                                not 0 < counter <= MAX_COUNTER):
        raise BundleError(f"unsupported counter {counter!r}")


def write_manifest(bundle, version, commit, dirty, counter=None, host_only=HOST_ONLY,
                   required=REQUIRED, compilers=None):
    """Writes BUNDLE/manifest.json. host_only names the paths listed apart from the installed
    files; required holds paths or glob patterns the installed files must match; compilers maps
    a name to a compiler_identity()."""
    if not VERSION.fullmatch(version):
        raise BundleError(f"unsupported version string {version!r}")
    if not re.fullmatch(r"[0-9a-f]{7,40}|unknown", commit):
        raise BundleError(f"unsupported commit {commit!r}")
    check_counter(counter)
    if compilers is not None:
        check_compilers(compilers)
    found = collect(bundle)
    files = [e for e in found if e["path"] not in host_only]
    host = [{key: e[key] for key in ("path", "size", "sha256", "mode")}
            for e in found if e["path"] in host_only]
    installed = [e["path"] for e in files]
    missing = [pattern for pattern in required if not fnmatch.filter(installed, pattern)]
    if missing:
        raise BundleError(f"bundle lacks {', '.join(missing)}")
    if not files:
        raise BundleError("bundle holds no installed files")
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "version": version,
        "commit": commit,
        "dirty": bool(dirty),
        "release": release_name(version, files),
    }
    if not valid_release_name(manifest["release"]):
        raise BundleError(f"release name {manifest['release']!r} is not usable")
    if counter is not None:
        manifest["counter"] = counter
    if compilers:
        manifest["compilers"] = compilers
    manifest["files"] = files
    manifest["hostOnly"] = host
    (Path(bundle) / "manifest.json").write_bytes((json.dumps(manifest, indent=2) + "\n").encode())
    return manifest


def check_entry(entry):
    if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
        raise BundleError(f"invalid manifest entry {entry!r}")
    parts = entry["path"].split("/")
    if (set(entry) != {"path", "size", "sha256", "mode"} or parts[0] not in TOP_LEVEL or len(parts) < 2 or
            not all(NAME.fullmatch(part) for part in parts) or
            not re.fullmatch(r"[0-9a-f]{64}", str(entry.get("sha256", ""))) or
            entry.get("mode") not in ("0644", "0755") or
            not isinstance(entry.get("size"), int) or isinstance(entry["size"], bool) or entry["size"] < 0):
        raise BundleError(f"invalid manifest entry {entry!r}")


def load_manifest(bundle):
    manifest = json.loads((Path(bundle) / "manifest.json").read_text())
    if not isinstance(manifest, dict):
        raise BundleError("bundle manifest is not an object")
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise BundleError("unsupported bundle manifest schema; rebuild the bundle with build_bundle.sh")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise BundleError("bundle manifest lists no files")
    host = manifest.get("hostOnly", [])
    if not isinstance(host, list):
        raise BundleError("invalid hostOnly list")
    for entry in files + host:
        check_entry(entry)
    paths = [entry["path"] for entry in files + host]
    if len(paths) != len(set(paths)):
        raise BundleError("bundle manifest lists a file twice")
    if not VERSION.fullmatch(manifest.get("version", "")):
        raise BundleError("invalid bundle version")
    check_counter(manifest.get("counter"))
    check_compilers(manifest.get("compilers", {}))
    if manifest.get("release") != release_name(manifest["version"], files):
        raise BundleError("bundle release name does not match its files")
    image = manifest.get("image")
    if image is not None and not (
            isinstance(image, dict) and set(image) == {"size", "sha256"} and isinstance(image["size"], int) and
            not isinstance(image["size"], bool) and image["size"] > 0 and
            re.fullmatch(r"[0-9a-f]{64}", str(image["sha256"]))):
        raise BundleError(f"invalid image entry {image!r}")
    return manifest


def verify(bundle, manifest=None):
    manifest = manifest or load_manifest(bundle)
    actual = {entry["path"]: entry for entry in collect(bundle)}
    listed = {entry["path"]: entry for entry in manifest["files"] + manifest.get("hostOnly", [])}
    if actual.keys() != listed.keys():
        raise BundleError("bundle files differ from manifest.json: "
                          f"{sorted(actual.keys() ^ listed.keys())}")
    for path, entry in listed.items():
        if any(actual[path].get(key) != value for key, value in entry.items()):
            raise BundleError(f"{path} differs from manifest.json")
    image = Path(bundle) / IMAGE
    if "image" not in manifest:
        if image.exists() or image.is_symlink():
            raise BundleError(f"manifest.json does not list {IMAGE}")
        return manifest
    if image.is_symlink() or not image.is_file():
        raise BundleError(f"the bundle lacks {IMAGE}")
    data = image.read_bytes()
    if (len(data) != manifest["image"]["size"] or hashlib.sha256(data).hexdigest() != manifest["image"]["sha256"]
            or not image_valid(data[:96], len(data))):
        raise BundleError(f"{IMAGE} differs from manifest.json")
    return manifest


def write_release(bundle, target):
    """Copies the installed files of a verified bundle to the new directory TARGET with a
    manifest.json that lists no host-only files: the tree the release image is made from."""
    manifest = verify(bundle)
    target = Path(target)
    target.mkdir()
    for entry in manifest["files"]:
        destination = target / entry["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Path(bundle) / entry["path"], destination)
        destination.chmod(int(entry["mode"], 8))
    release = {key: value for key, value in manifest.items() if key not in BUNDLE_ONLY}
    (target / "manifest.json").write_bytes((json.dumps(release, indent=2) + "\n").encode())
    return verify(target)


def image_valid(data, size):
    """Whether size bytes starting with data are a release image: a squashfs of that size once
    mksquashfs padded it to 4 KiB (release_slot_image_valid in src/platform/tc002/contract)."""
    if len(data) < 96 or data[:4] != SQUASHFS_MAGIC:
        return False
    used = int.from_bytes(data[40:48], "little")
    return 96 <= used <= size <= (used + SQUASHFS_PADDING - 1) // SQUASHFS_PADDING * SQUASHFS_PADDING


def mksquashfs_version():
    result = subprocess.run(["mksquashfs", "-version"], capture_output=True, text=True, check=False)
    match = re.search(r"mksquashfs version (\d+)\.(\d+)", result.stdout + result.stderr)
    if not match or (int(match[1]), int(match[2])) < (4, 6):
        raise BundleError("the release image needs mksquashfs 4.6 or later (squashfs-tools)")
    return f"{match[1]}.{match[2]}"


def release_image(tree, output):
    """Writes the release image of the installed release tree TREE (manifest.json and the files
    it lists) to OUTPUT; every time is the release counter and every mode the one the manifest
    lists."""
    manifest = verify(tree)
    if any(key in manifest for key in BUNDLE_ONLY):
        raise BundleError(f"{tree} is a bundle, not an installed release (bundle.py release)")
    counter = manifest.get("counter")
    if not isinstance(counter, int) or isinstance(counter, bool) or not 0 < counter < 1 << 32:
        raise BundleError("the release image needs a counter that is a 32-bit time")
    mksquashfs_version()
    tree = Path(tree)
    for directory in [tree, *(path for path in tree.rglob("*") if path.is_dir())]:
        directory.chmod(0o755)
    (tree / "manifest.json").chmod(0o644)
    for entry in manifest["files"]:
        (tree / entry["path"]).chmod(int(entry["mode"], 8))
    output = Path(output)
    output.unlink(missing_ok=True)
    subprocess.run(["mksquashfs", str(tree), str(output), *IMAGE_OPTIONS, "-mkfs-time", str(counter),
                    "-all-time", str(counter)], check=True, capture_output=True)
    data = output.read_bytes()
    if not image_valid(data[:96], len(data)):
        raise BundleError(f"mksquashfs wrote no release image to {output}")
    return {"release": manifest["release"], "counter": counter, "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def add_image(bundle):
    """Writes BUNDLE/release.img, the release image of the verified bundle made through
    write_release(), and lists it in the bundle's manifest.json."""
    bundle = Path(bundle)
    manifest = verify(bundle)
    manifest.pop("image", None)
    partial = bundle / f"{IMAGE}.partial"
    with tempfile.TemporaryDirectory(prefix="awtrix-release-") as work:
        tree = Path(work) / "release"
        write_release(bundle, tree)
        result = release_image(tree, partial)
    (bundle / IMAGE).unlink(missing_ok=True)
    partial.rename(bundle / IMAGE)
    files = manifest.pop("files")
    host = manifest.pop("hostOnly", [])
    manifest["image"] = {"size": result["bytes"], "sha256": result["sha256"]}
    manifest["files"] = files
    manifest["hostOnly"] = host
    (bundle / "manifest.json").write_bytes((json.dumps(manifest, indent=2) + "\n").encode())
    return verify(bundle)


def embedded_webui(repo):
    """The full gzip stream src/transport/http/WebUiAsset.h carries under AWTRIX_WEBUI_FULL:
    webui/index.html minified by scripts/build_webui.py, which needs Node.js and runs with every
    ESP32 build, with the //linux:begin ... //linux:end regions the ESP32 stream leaves out. The
    header names the MD5 of the webui/index.html it was made from; a stale header is refused."""
    repo = Path(repo)
    header = (repo / WEBUI_ASSET).read_text(encoding="utf-8")
    source = re.search(r"SRC-MD5 ([0-9a-f]{32})", header)
    if not source:
        raise BundleError(f"{WEBUI_ASSET} is not a generated web UI asset")
    if source[1] != hashlib.md5((repo / WEBUI_SOURCE).read_bytes()).hexdigest():
        raise BundleError(f"{WEBUI_ASSET} was not generated from this {WEBUI_SOURCE}; "
                          "build the ESP32 firmware (pio run) to regenerate it")
    if (repo / WEBUI_SOURCE).read_bytes() != assemble_webui(repo):
        raise BundleError(f"{WEBUI_SOURCE} is stale; run python scripts/webui_source.py and build the ESP32 firmware")
    full = re.search(r"^#if defined\(AWTRIX_WEBUI_FULL\)$(.*?)^#endif$", header, re.M | re.S)
    if not full:
        raise BundleError(f"{WEBUI_ASSET} has no AWTRIX_WEBUI_FULL stream; "
                          "build the ESP32 firmware (pio run) to regenerate it")
    section = full[1]
    length = re.search(r"WEBUI_FULL_GZ_LEN = (\d+);", section)
    etag = re.search(r'WEBUI_FULL_ETAG\[\] = "\\"([0-9a-f]{32})\\"";', section)
    array = re.search(r"WEBUI_FULL_GZ\[\] PROGMEM = \{(.*?)\};", section, re.S)
    if not (length and etag and array):
        raise BundleError(f"{WEBUI_ASSET} is not a generated web UI asset")
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2}),", array[1]))
    if len(data) != int(length[1]) or hashlib.md5(data).hexdigest() != etag[1] or data[:2] != b"\x1f\x8b":
        raise BundleError(f"{WEBUI_ASSET} holds a damaged gzip stream")
    return data


def summary(manifest):
    files = manifest["files"]
    host = manifest.get("hostOnly", [])
    text = (f"{manifest['release']}: {len(files)} files, {sum(e['size'] for e in files)} bytes")
    if host:
        text += f"; host-only {', '.join(e['path'] for e in host)}"
    if "image" in manifest:
        text += f"; release image {manifest['image']['size']} bytes"
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("manifest")
    make.add_argument("bundle", type=Path)
    make.add_argument("--version", required=True)
    make.add_argument("--commit", required=True)
    make.add_argument("--counter", type=int)
    make.add_argument("--dirty", action="store_true")
    make.add_argument("--compiler", action="append", default=[], metavar="NAME=GCC")
    check = commands.add_parser("verify")
    check.add_argument("bundle", type=Path)
    release = commands.add_parser("release")
    release.add_argument("bundle", type=Path)
    release.add_argument("target", type=Path)
    image = commands.add_parser("image")
    image.add_argument("bundle", type=Path)
    webui = commands.add_parser("webui")
    webui.add_argument("repo", type=Path)
    webui.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "webui":
            data = embedded_webui(args.repo)
            args.output.write_bytes(data)
            print(f"{args.output}: {len(data)} bytes")
            return 0
        if args.command == "image":
            manifest = add_image(args.bundle)
            print(f"{manifest['release']}: {args.bundle / IMAGE}, {manifest['image']['size']} bytes, "
                  f"SHA-256 {manifest['image']['sha256']}")
            return 0
        if args.command == "manifest":
            compilers = {}
            for option in args.compiler:
                name, _, compiler = option.partition("=")
                if not NAME.fullmatch(name) or not compiler or name in compilers:
                    raise BundleError(f"--compiler {option!r} is not NAME=GCC")
                compilers[name] = compiler_identity(compiler)
            manifest = write_manifest(args.bundle, args.version, args.commit, args.dirty,
                                      args.counter, compilers=compilers)
        elif args.command == "release":
            manifest = write_release(args.bundle, args.target)
        else:
            manifest = verify(args.bundle)
    except (BundleError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(summary(manifest))
    return 0


if __name__ == "__main__":
    sys.exit(main())
