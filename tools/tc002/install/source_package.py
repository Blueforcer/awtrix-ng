#!/usr/bin/env python3
"""Package the pinned corresponding sources of an exact clean TC002 binary bundle."""
import argparse
import gzip
import hashlib
import json
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

import bundle

HERE = Path(__file__).resolve().parent
PINS = {
    "busybox-1.37.0.tar.bz2": "3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4",
    "wpa_supplicant-2.12.tar.gz": "08e23937e16d0155e55cab2b51f51fbe10d80a1aa91c4e15442645059b737ef6",
    "libnl-3.11.0.tar.gz": "2a56e1edefa3e68a7c00879496736fdbf62fc94ed3232c0baba127ecfa76874d",
    "openssl-3.5.8.tar.gz": "a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2",
    "musl-1.2.6.tar.gz": "d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a",
    "base64_arduino-ac168f5fa2865de855384f6a2d61444ce7d92c27.tar.gz": "acb182019e908a0d21c4a110d87a732da184daa793489a60871ed957c129b616",
    "ca-certificates_20260223.tar.xz": "2fa2b00d4360f0d14ec51640ae8aea9e563956b95ea786e3c3c01c4eead42b56",
    "xz-5.8.4.tar.xz": "4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b",
    "libjpeg-turbo-3.2.0.tar.gz": "6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e",
}
KERNEL_COMMIT = "e53dccbcd926a883a2859695a6b8839e12daf321"
AIC_COMMIT = "dd73e713829ab31029e371bc3fb67b367b8ae2eb"
AIC_PATH = "buildroot-overlay/package/aic8800_sdio/src"
AIC_TREE = "3ff4a299165d4bce65bb91e6f8924a7ca3dbe9fb"
AIC_PARTS = ("aic8800_bsp", "aic8800_fdrv")
RECIPES = (
    "tools/tc002/install/build_bundle.sh", "tools/tc002/busybox/build.sh",
    "tools/tc002/wpa_supplicant/build.sh", "tools/tc002/openssl/build.sh",
    "tools/tc002/ca/build.sh", "tools/tc002/kernel/common.sh", "tools/tc002/kernel/build_aic8800.sh",
    "tools/tc002/kernel/build-loop.sh", "tools/tc002/lzma/build.sh", "tools/tc002/jpeg/build.sh",
)
MUSL_RECIPE_PINS = {
    "0001-avoid-kernel-if_ether.h.patch": "7fb1ec8ed9bda3b670d03b8cb9a46f769276ae066c3b975ee9cc974284bd97fd",
    "0002-package-musl-Make-scheduler-functions-Linux-compatib.patch": "d074724a3edc8f9a86b207ce4baf9a9b24a5f24cb26d1075f3df6bdb8e9495a7",
    "0003-fix-pathological-slowness-incorrect-mappings-in-icon.patch": "98506a81bca5f3807b3ab2b10c7ee2298ec73354e8368ea6eb80080af2bacfd7",
    "0004-qsort-fix-leonardo-heap-corruption-from-bug-in-doubl.patch": "435af2170b2929951e664522b29538b98931048e9be566a7780298b0d654fe09",
    "0005-qsort-hard-preclude-oob-array-writes-independent-of-.patch": "322830abe71d97e3348c35c0f5b806135603121b31555f7536e45ef4964c900c",
    "0006-qsort-fix-shift-UB-in-shl-and-shr.patch": "b9d740558f3770b1659ed489bddedb8e9b092f04319f805756eac90c061720df",
    "Config.in": "2b09cb105627bb7fb3b3d2c93e4807fa515f2b122ed1acf3b5b58862c01a550f",
    "musl.hash": "d2361e618d3ac48b2004f89eaef501f99eb56325bb5ac230549e66c50e20ba70",
    "musl.mk": "bda12a61a87d96453a6ec60bbaec3ad42722c59edbdc7cf01f07e05ec67b23da"
}


class SourceError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise SourceError(message)


def git(source, *arguments):
    return subprocess.check_output(["git", "-C", str(source), *arguments], text=True, encoding="utf-8").strip()


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        require(name not in result, "duplicate JSON member: " + name)
        result[name] = value
    return result


def verify_binary(repo, commit, manifest_path):
    require(isinstance(commit, str) and re.fullmatch(r"[0-9a-f]{40}", commit),
            "a full immutable application commit is required")
    require(git(repo, "cat-file", "-t", commit) == "commit", "application object must be a commit")
    manifest_path = Path(manifest_path)
    require(manifest_path.name == "manifest.json" and stat.S_ISREG(manifest_path.lstat().st_mode),
            "bundle manifest must be a regular manifest.json file")
    original = manifest_path.read_bytes()
    require(len(original) <= 256 << 10, "binary manifest is too large")
    binary = json.loads(original, object_pairs_hook=unique_object)
    require(isinstance(binary, dict) and binary.get("dirty") is False,
            "binary manifest must describe a clean committed build")
    require(binary.get("commit") in (commit, commit[:12]), "binary manifest commit does not match source commit")
    require(type(binary.get("counter")) is int and
            binary["counter"] == int(git(repo, "show", "-s", "--format=%ct", commit)),
            "binary manifest counter does not match source commit time")
    require(bundle.verify(manifest_path.parent) == binary, "bundle verification changed manifest identity")
    installed = {entry["path"] for entry in binary["files"]}
    hosts = {entry["path"] for entry in binary.get("hostOnly", [])}
    bundle.public_files(installed | hosts)
    require(hosts == set(bundle.HOST_ONLY),
            "binary manifest must describe the complete public TC002 bundle")
    require(manifest_path.read_bytes() == original, "binary manifest changed while reading")
    return original


def preflight(*, repo, commit, bundle_manifest, downloads, kernel_source, aic_source, musl_buildroot_recipe):
    original = verify_binary(repo, commit, bundle_manifest)
    verify_sources(repo=repo, commit=commit, downloads=downloads, kernel_source=kernel_source,
                   aic_source=aic_source, musl_buildroot_recipe=musl_buildroot_recipe)
    return original


def verify_sources(*, repo, commit, downloads, kernel_source, aic_source, musl_buildroot_recipe):
    require(isinstance(commit, str) and re.fullmatch(r"[0-9a-f]{40}", commit),
            "a full immutable application commit is required")
    recipes = "\n".join(git(repo, "show", commit + ":" + path) for path in RECIPES)
    for identity in (KERNEL_COMMIT, AIC_COMMIT, AIC_TREE, *PINS.values()):
        require(identity in recipes, "source pin differs from committed build recipes: " + identity)
    require(git(aic_source, "rev-parse", AIC_COMMIT + ":" + AIC_PATH) == AIC_TREE,
            "AIC source tree differs from the build recipe")
    for source, revision, paths in ((repo, commit, ()), (kernel_source, KERNEL_COMMIT, ()),
                                     (aic_source, AIC_COMMIT + ":" + AIC_PATH, AIC_PARTS)):
        listing = git(source, "ls-tree", "-r", revision, *paths).splitlines()
        require(listing and not any(line.startswith("160000 ") for line in listing),
                "archive would omit submodule content: " + str(source))
    for name, expected in PINS.items():
        require(digest(Path(downloads) / name) == expected, "source hash mismatch: " + name)
    recipe = Path(musl_buildroot_recipe)
    require(recipe.is_dir(), "musl Buildroot recipe directory is missing")
    require({path.name for path in recipe.iterdir()} == set(MUSL_RECIPE_PINS),
            "musl Buildroot recipe file set differs from the pinned toolchain")
    for name, expected in MUSL_RECIPE_PINS.items():
        path = recipe / name
        require(stat.S_ISREG(path.lstat().st_mode) and digest(path) == expected,
                "musl Buildroot recipe hash mismatch: " + name)


def git_archive(source, revision, target, prefix, paths=()):
    with Path(target).open("xb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=1) as compressed:
        process = subprocess.Popen(["git", "-c", "tar.umask=0022", "-C", str(source), "archive",
                                    "--format=tar", "--prefix=" + prefix, revision, *paths], stdout=subprocess.PIPE)
        try:
            with process.stdout:
                with tarfile.open(fileobj=process.stdout, mode="r|") as source_tar, \
                        tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as target_tar:
                    for entry in source_tar:
                        entry.uid = entry.gid = entry.mtime = 0
                        entry.uname = entry.gname = ""
                        entry.pax_headers = {}
                        if entry.isfile():
                            with source_tar.extractfile(entry) as stream:
                                target_tar.addfile(entry, stream)
                        else:
                            target_tar.addfile(entry)
            require(process.wait() == 0, "git archive failed: " + str(target))
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


def deterministic_archive(directory, output):
    output = Path(output)
    created = False
    try:
        with output.open("xb") as raw:
            created = True
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=1) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                    for path in sorted(Path(directory).rglob("*")):
                        require((path.is_file() or path.is_dir()) and not path.is_symlink(),
                                "unexpected source staging entry")
                        entry = tarfile.TarInfo("tc002-sources/" + path.relative_to(directory).as_posix())
                        entry.mode = 0o755 if path.is_dir() else 0o644
                        if path.is_dir():
                            entry.type = tarfile.DIRTYPE
                            archive.addfile(entry)
                        else:
                            entry.size = path.stat().st_size
                            with path.open("rb") as stream:
                                archive.addfile(entry, stream)
    except BaseException:
        if created:
            output.unlink()
        raise


def create(output, **inputs):
    original = preflight(**inputs)
    output = Path(output).absolute()
    require(not output.exists() and not output.is_symlink(), "output already exists")
    require(output.parent.is_dir(), "output parent directory must exist")
    commit = inputs["commit"]
    with tempfile.TemporaryDirectory(prefix=".tc002-sources-", dir=output.parent) as temporary:
        destination = Path(temporary)
        require(destination.resolve().parent == output.parent.resolve(), "unexpected temporary staging location")
        destination.chmod(0o700)
        for name in PINS:
            shutil.copyfile(Path(inputs["downloads"]) / name, destination / name)
            require(digest(destination / name) == PINS[name], "source changed during copy: " + name)
        git_archive(inputs["repo"], commit, destination / ("awtrix-ng-" + commit + ".tar.gz"), "awtrix-ng/")
        git_archive(inputs["kernel_source"], KERNEL_COMMIT,
                    destination / ("linux-chenxing-" + KERNEL_COMMIT + ".tar.gz"), "linux-chenxing/")
        git_archive(inputs["aic_source"], AIC_COMMIT + ":" + AIC_PATH,
                    destination / ("aic8800-" + AIC_COMMIT + ".tar.gz"), "aic8800/", AIC_PARTS)
        recipe = destination / "musl-buildroot-recipe"
        recipe.mkdir()
        for name, expected in MUSL_RECIPE_PINS.items():
            shutil.copyfile(Path(inputs["musl_buildroot_recipe"]) / name, recipe / name)
            require(digest(recipe / name) == expected, "musl recipe changed during copy")
        (destination / "binary-manifest.json").write_bytes(original)
        licenses = destination / "licenses"
        licenses.mkdir()
        (licenses / "GPL-2.0-kernel-COPYING").write_bytes(
            (git(inputs["kernel_source"], "show", KERNEL_COMMIT + ":COPYING") + "\n").encode())
        for archive, member, name in (
                ("libnl-3.11.0.tar.gz", "libnl-3.11.0/COPYING", "LGPL-2.1-libnl-COPYING"),
                ("wpa_supplicant-2.12.tar.gz", "wpa_supplicant-2.12/COPYING", "BSD-wpa_supplicant-COPYING")):
            with tarfile.open(destination / archive) as source:
                entry = source.getmember(member)
                require(entry.isfile() and entry.size <= 128 << 10, "invalid source license entry")
                with source.extractfile(entry) as stream:
                    (licenses / name).write_bytes(stream.read())
        for source, target in (("source_package_README.md", "README.md"),
                               ("source_package_AIC_NOTICE.txt", "AIC-SOURCE-NOTICE.txt")):
            (destination / target).write_bytes((HERE / source).read_text(encoding="utf-8").encode("utf-8"))
        files = {path.relative_to(destination).as_posix(): {"sha256": digest(path), "bytes": path.stat().st_size}
                 for path in sorted(destination.rglob("*")) if path.is_file()}
        manifest = {"schema_version": 1, "application_commit": commit,
                    "binary_manifest_sha256": hashlib.sha256(original).hexdigest(),
                    "kernel_commit": KERNEL_COMMIT, "aic_commit": AIC_COMMIT,
                    "aic_original_tree": AIC_TREE, "aic_archive_paths": list(AIC_PARTS),
                    "tc002_stock_firmware_included": False,
                    "upstream_kernel_firmware_tree_retained": True, "binary_files_verified": True,
                    "offline_rebuild_of_source_companion_performed": False, "files": files}
        (destination / "manifest.json").write_bytes((json.dumps(manifest, indent=2) + "\n").encode())
        deterministic_archive(destination, output)
    return {"archive": str(output), "sha256": digest(output), "bytes": output.stat().st_size,
            "application_commit": commit, "binary_manifest_sha256": hashlib.sha256(original).hexdigest()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=HERE.parents[2])
    parser.add_argument("--commit", required=True)
    parser.add_argument("--bundle-manifest", required=True, type=Path)
    parser.add_argument("--downloads", required=True, type=Path)
    parser.add_argument("--kernel-source", required=True, type=Path)
    parser.add_argument("--aic-source", required=True, type=Path)
    parser.add_argument("--musl-buildroot-recipe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--check-only", action="store_true")
    args = vars(parser.parse_args(argv))
    output, check_only = args.pop("output"), args.pop("check_only")
    try:
        if check_only:
            original = preflight(**args)
            result = {"status": "preflight-passed", "application_commit": args["commit"],
                      "binary_manifest_sha256": hashlib.sha256(original).hexdigest()}
        else:
            result = create(output, **args)
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, SourceError, bundle.BundleError, subprocess.SubprocessError, ValueError, tarfile.TarError) as error:
        print("source package: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
