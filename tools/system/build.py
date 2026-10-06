"""Prepare a pinned ARM Linux build; this does not produce TC002 boot support.

Python 3.12+, Linux/WSL for configure/build. Application sources come exclusively
from a specified local Git commit. No device, flashing or remote Git write exists.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request


REPO = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("sources.lock.json")
EXTERNAL = REPO / "firmware/buildroot"
APP_PATHS = ["CMakeLists.txt", "cmake", "version", "src", "lib", "packaging/linux", "webui/index.html",
             "webui/src", "scripts/webui_source.py", "scripts/berry_api.py",
             "LICENSE.md", "LICENSES", "THIRD-PARTY-NOTICES.md", "tools/system/components.json"]
MARKER = ".awtrix-system-work.json"
REQUIRED_TOOLS = ("make", "gcc", "g++", "rsync", "patch", "bc", "bison",
                  "flex", "file", "unzip", "cpio", "git", "wget", "xz")


class BuildError(RuntimeError):
    pass


def is_arm_elf(header: bytes) -> bool:
    return header[:6] == b"\x7fELF\x01\x01" and header[18:20] == b"\x28\x00"


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def write_json(path: Path, value) -> None:
    """Publish through a temporary created exclusively, so an existing file or link there is never written."""
    temporary = path.with_suffix(path.suffix + ".partial")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)
    handle = os.open(temporary, flags, 0o666)
    try:
        with open(handle, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def json_bytes(data: bytes, name: str) -> dict:
    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise BuildError(f"duplicate JSON key: {key}")
            result[key] = value
        return result
    result = json.loads(data.decode("utf-8"), object_pairs_hook=unique_pairs)
    if not isinstance(result, dict):
        raise BuildError(f"expected JSON object: {name}")
    return result


def json_file(path: Path) -> dict:
    return json_bytes(path.read_bytes(), path.name)


def load_lock(path: Path = LOCK) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema_version") != 1:
        raise BuildError("unsupported source lock schema")
    for key in ("buildroot", "linux_headers"):
        source = value[key]
        if not re.fullmatch(r"[0-9a-f]{64}", source["sha256"]):
            raise BuildError(f"invalid checksum for {key}")
        if urllib.parse.urlparse(source["url"]).scheme != "https":
            raise BuildError(f"source must use HTTPS: {key}")
    return value


def download_verified(source: dict, destination: Path) -> None:
    """Existing corrupt cache entries fail closed and are never silently reused."""
    if destination.exists():
        if not destination.is_file() or digest(destination) != source["sha256"]:
            raise BuildError(f"cached source checksum mismatch: {destination}")
        return
    temporary = destination.with_suffix(destination.suffix + ".partial")
    created = False
    try:
        with temporary.open("xb") as out:
            created = True
            with urllib.request.urlopen(source["url"], timeout=45) as response:
                count = 0
                while chunk := response.read(1024 * 1024):
                    count += len(chunk)
                    if count > 1024 * 1024 * 1024:
                        raise BuildError("source archive exceeds 1 GiB")
                    out.write(chunk)
                out.flush()
                os.fsync(out.fileno())
        if digest(temporary) != source["sha256"]:
            raise BuildError(f"download checksum mismatch: {destination.name}")
        temporary.replace(destination)
    except BaseException:
        if created:
            temporary.unlink(missing_ok=True)
        raise


def extract_archive(archive: Path, destination: Path, prefix: str, *, allow_absolute_links=False) -> None:
    """Write files before links; no archive entry may be written through a link.

    Buildroot skeletons contain absolute links meaningful inside the future target
    filesystem. Only the checksum-pinned Buildroot source enables those links.
    """
    if destination.exists():
        raise BuildError(f"extraction destination already exists: {destination}")
    with tempfile.TemporaryDirectory(prefix=".extract-", dir=destination.parent) as temporary:
        with tarfile.open(archive) as source:
            members = source.getmembers()
            paths, links, directories = set(), set(), set()
            for member in members:
                path = PurePosixPath(member.name)
                if ("\\" in member.name or path.is_absolute() or ".." in path.parts or
                        not path.parts or path.parts[0] != prefix):
                    raise BuildError(f"unexpected archive path: {member.name}")
                name = path.as_posix()
                if name in paths and not (member.isdir() and name in directories):
                    raise BuildError(f"duplicate archive path: {member.name}")
                paths.add(name)
                if member.isdir():
                    directories.add(name)
                if member.issym() or member.islnk():
                    links.add(name)
            for member in members:
                path = PurePosixPath(member.name)
                if any(parent.as_posix() in links for parent in path.parents):
                    raise BuildError(f"archive entry traverses a link: {member.name}")
                if not member.issym() and not member.islnk():
                    source.extract(member, temporary, filter="data")
            for member in members:
                if not member.issym() and not member.islnk():
                    continue
                path = PurePosixPath(member.name)
                target = member.linkname
                if "\\" in target:
                    raise BuildError(f"invalid archive link: {member.name}")
                if member.islnk():
                    normalized = posixpath.normpath(target)
                    if normalized not in paths or normalized in links or PurePosixPath(target).is_absolute():
                        raise BuildError(f"invalid hardlink: {member.name}")
                    (Path(temporary) / path).parent.mkdir(parents=True, exist_ok=True)
                    os.link(Path(temporary) / normalized, Path(temporary) / path)
                    continue
                if PurePosixPath(target).is_absolute():
                    if not allow_absolute_links:
                        raise BuildError(f"absolute archive symlink: {member.name}")
                else:
                    normalized = PurePosixPath(posixpath.normpath(str(path.parent / target)))
                    if not normalized.parts or normalized.parts[0] != prefix:
                        raise BuildError(f"escaping archive symlink: {member.name}")
                (Path(temporary) / path).parent.mkdir(parents=True, exist_ok=True)
                os.symlink(target, Path(temporary) / path)
        extracted = Path(temporary) / prefix
        if not extracted.is_dir() or extracted.is_symlink():
            raise BuildError("archive does not contain the expected source directory")
        extracted.replace(destination)


def tree_hashes(root: Path, *, allow_links=False) -> dict[str, str]:
    hashes = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            if not allow_links:
                raise BuildError(f"unexpected symlink in frozen input: {path}")
            hashes[path.relative_to(root).as_posix()] = "symlink:" + os.readlink(path)
        elif path.is_file():
            hashes[path.relative_to(root).as_posix()] = digest(path)
    return hashes


def git_output(repo: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def snapshot_git(repo: Path, revision: str, archive: Path) -> tuple[str, int]:
    commit = git_output(repo, "rev-parse", "--verify", revision + "^{commit}")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise BuildError("expected a full Git commit ID")
    epoch = int(git_output(repo, "show", "-s", "--format=%ct", commit))
    paths = [path for path in APP_PATHS
             if git_output(repo, "ls-tree", "--name-only", commit, "--", path)]
    if set(APP_PATHS) - set(paths) - {"packaging/linux"}:
        raise BuildError("application commit is missing required build inputs")
    with archive.open("xb") as stream:
        subprocess.run(["git", "-C", str(repo), "-c", "core.autocrlf=false", "-c", "core.eol=lf",
                        "archive", "--format=tar",
                        "--prefix=application/", commit, *paths], stdout=stream, check=True)
    return commit, epoch


def recipe_license_files(package: Path) -> list[str]:
    """Return the literal AWTRIX_NG_LICENSE_FILES list of the application recipe."""
    recipe = (package / "awtrix-ng.mk").read_text(encoding="utf-8")
    recipe = re.sub(r"\\\r?\n", " ", recipe)
    lists = re.findall(r"^AWTRIX_NG_LICENSE_FILES\s*([:+?]?=)\s*(.*)$", recipe, re.MULTILINE)
    if len(lists) != 1 or lists[0][0] != "=":
        raise BuildError("expected one literal AWTRIX_NG_LICENSE_FILES assignment")
    licenses = lists[0][1].split()
    if not licenses or len(set(licenses)) != len(licenses):
        raise BuildError("empty or duplicate AWTRIX_NG_LICENSE_FILES")
    return licenses


def verify_application_licenses(application: Path, external: Path) -> None:
    """Preflight the recipe's literal license list against exact exported bytes.

    Deliberately do not decode license text: Windows encodings and newline
    conversion must never change the bytes represented by Buildroot's hashes.
    """
    package = external / "package/awtrix-ng"
    try:
        licenses = recipe_license_files(package)
        hashes = {}
        for number, line in enumerate((package / "awtrix-ng.hash").read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.split()
            if (len(fields) != 3 or fields[0] != "sha256" or
                    not re.fullmatch(r"[0-9a-f]{64}", fields[1]) or fields[2] in hashes):
                raise BuildError(f"invalid or duplicate application license hash at line {number}")
            hashes[fields[2]] = fields[1]
        if set(hashes) != set(licenses):
            raise BuildError("application license hashes do not match AWTRIX_NG_LICENSE_FILES")
        for name in licenses:
            path = PurePosixPath(name)
            if (path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or
                    path.as_posix() != name or "$" in name):
                raise BuildError("invalid application license path: " + name)
            source = application
            for part in path.parts:
                source = source / part
                if source.is_symlink():
                    raise BuildError("symlink in application license path: " + name)
            if not source.is_file():
                raise BuildError("missing application license file: " + name)
            actual = digest(source)
            if actual != hashes[name]:
                raise BuildError(f"application license checksum mismatch: {name}; "
                                 f"expected {hashes[name]}, actual {actual}")
    except OSError as error:
        raise BuildError(f"cannot preflight application license files: {error}") from error


def own_work_directory(work: Path) -> None:
    if any(character.isspace() for character in str(work)):
        raise BuildError("Buildroot work paths must not contain whitespace")
    if work == REPO or work in REPO.parents or work == Path(work.anchor):
        raise BuildError("choose a dedicated build directory")
    if work.exists() and any(work.iterdir()) and not (work / MARKER).is_file():
        raise BuildError("work directory is not empty and has no AWTRIX build marker")
    work.mkdir(parents=True, exist_ok=True)
    marker = work / MARKER
    if marker.exists():
        owner = json.loads(marker.read_text())
        if owner.get("schema_version") != 1 or owner.get("purpose") != "awtrix-armv7-build":
            raise BuildError("unsupported work directory marker")
    else:
        write_json(marker, {"schema_version": 1, "purpose": "awtrix-armv7-build"})


def prepare(work: Path, revision: str, *, check_licenses: bool = True) -> dict:
    own_work_directory(work)
    if (work / "inputs.json").exists():
        inputs = verify(work)
        wanted = git_output(REPO, "rev-parse", "--verify", revision + "^{commit}")
        if (inputs["application_commit"] != wanted or inputs["source_lock"] != load_lock() or
                inputs["external_files"] != tree_hashes(EXTERNAL)):
            raise BuildError("prepared inputs differ; use a new dedicated work directory")
        if check_licenses:
            verify_application_licenses(work / "application", work / "external")
        return inputs
    # An interrupted preparation is preserved for inspection, never overwritten.
    for name in ("buildroot", "application", "external", "application.tar"):
        if (work / name).exists():
            raise BuildError("incomplete preparation; use a new dedicated work directory")
    sources = load_lock()
    commit, epoch = snapshot_git(REPO, revision, work / "application.tar")
    extract_archive(work / "application.tar", work / "application", "application")
    # Reject symlinks before copying so this is a frozen, reviewable recipe tree.
    external_hashes = tree_hashes(EXTERNAL)
    shutil.copytree(EXTERNAL, work / "external")
    if check_licenses:
        verify_application_licenses(work / "application", work / "external")
    downloads = work / "downloads"
    downloads.mkdir(exist_ok=True)
    source = sources["buildroot"]
    filename = f"buildroot-{source['version']}.tar.xz"
    download_verified(source, downloads / filename)
    extract_archive(downloads / filename, work / "buildroot", "buildroot-" + source["version"],
                    allow_absolute_links=True)
    inputs = {
        "schema_version": 1, "profile": "awtrix_armv7_defconfig",
        "application_commit": commit, "source_date_epoch": epoch,
        "source_lock": sources, "application_archive_sha256": digest(work / "application.tar"),
        "application_files": tree_hashes(work / "application"),
        "external_files": external_hashes,
        "buildroot_files": tree_hashes(work / "buildroot", allow_links=True),
        "tc002_hardware_verified": False, "bootable_tc002_image": False,
    }
    write_json(work / "inputs.json", inputs)
    return verify(work)


def verify(work: Path) -> dict:
    inputs = json.loads((work / "inputs.json").read_text(encoding="utf-8"))
    if inputs.get("schema_version") != 1 or inputs.get("profile") != "awtrix_armv7_defconfig":
        raise BuildError("unsupported prepared inputs")
    if digest(work / "application.tar") != inputs["application_archive_sha256"]:
        raise BuildError("application archive changed")
    for name, key in (("application", "application_files"), ("external", "external_files")):
        if tree_hashes(work / name) != inputs[key]:
            raise BuildError(f"frozen {name} inputs changed")
    if tree_hashes(work / "buildroot", allow_links=True) != inputs["buildroot_files"]:
        raise BuildError("frozen Buildroot inputs changed")
    source = inputs["source_lock"]["buildroot"]
    if digest(work / "downloads" / f"buildroot-{source['version']}.tar.xz") != source["sha256"]:
        raise BuildError("Buildroot archive changed")
    return inputs


def doctor() -> None:
    if sys.platform != "linux":
        raise BuildError("configure/build requires Linux; use WSL on Windows")
    missing = [name for name in REQUIRED_TOOLS if not shutil.which(name)]
    if missing:
        raise BuildError("missing host tools: " + ", ".join(missing))


def make(work: Path, inputs: dict, *targets: str) -> None:
    environment = os.environ.copy()
    # WSL imports Windows PATH entries containing spaces, which Buildroot rejects.
    environment["PATH"] = os.pathsep.join(part for part in environment.get("PATH", "").split(os.pathsep)
                                           if part and not any(c.isspace() for c in part))
    environment.update(LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH=str(inputs["source_date_epoch"]))
    subprocess.run(["make", "-C", str(work / "buildroot"), "O=" + str(work / "output"),
                    "BR2_EXTERNAL=" + str(work / "external"), "BR2_DL_DIR=" + str(work / "downloads"),
                    "AWTRIX_NG_SNAPSHOT=" + str(work / "application"),
                    "AWTRIX_NG_REVISION=" + inputs["application_commit"], *targets],
                   env=environment, check=True)


def record_result(work: Path, inputs: dict) -> None:
    binary = work / "output/target/usr/bin/awtrix-linux"
    with binary.open("rb") as stream:
        header = stream.read(20)
    if not is_arm_elf(header):
        raise BuildError("expected an ARM 32-bit little-endian application ELF")
    artifacts = [binary, work / "output/images/rootfs.tar"]
    write_json(work / "build-result.json", {
        "schema_version": 1, "status": "crossbuild_passed", "profile": inputs["profile"],
        "application_commit": inputs["application_commit"],
        "inputs_sha256": digest(work / "inputs.json"),
        "config_sha256": digest(work / "output/.config"),
        "artifacts": {path.relative_to(work).as_posix(): {"sha256": digest(path), "bytes": path.stat().st_size}
                      for path in artifacts},
        "tc002_hardware_verified": False, "bootable_tc002_image": False,
        "bit_reproducibility_tested": False,
    })


def collect_legal_evidence(work: Path, inputs: dict) -> None:
    """Supplement the source inputs that Buildroot legal-info asks users to retain."""
    destination = work / "output/legal-info/build-inputs"
    destination.mkdir(parents=True, exist_ok=True)
    version = inputs["source_lock"]["buildroot"]["version"]
    sources = [work / "downloads" / f"buildroot-{version}.tar.xz",
               work / "application.tar", work / "inputs.json"]
    for source in sources:
        shutil.copyfile(source, destination / source.name)
    def metadata(member):
        member.uid = member.gid = 0
        member.uname = member.gname = ""
        member.mtime = inputs["source_date_epoch"]
        return member
    with tarfile.open(destination / "external.tar", "w") as archive:
        archive.add(work / "external", arcname="external", filter=metadata)
    write_json(destination / "manifest.json", {
        "schema_version": 1, "application_commit": inputs["application_commit"],
        "files": {path.name: digest(path) for path in sorted(destination.iterdir())
                  if path.is_file() and path.name != "manifest.json"},
        "purpose": "Supplement Buildroot sources, local application and external recipes; not a license audit",
    })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("doctor", "prepare", "verify", "configure", "build", "toolchain",
                                            "legal-info"))
    parser.add_argument("--work", type=Path, help="dedicated build directory (prefer Linux filesystem)")
    parser.add_argument("--revision", default="HEAD", help="local Git commit for application sources")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    active_build = None
    try:
        if args.command == "doctor":
            doctor()
            print("Host build tools available")
            return 0
        if args.work is None or not 1 <= args.jobs <= 64:
            raise BuildError("--work is required and --jobs must be between 1 and 64")
        work = args.work.expanduser().resolve()
        if args.command == "prepare":
            inputs = prepare(work, args.revision)
        elif args.command == "toolchain":
            inputs = prepare(work, args.revision, check_licenses=False)
        else:
            inputs = verify(work)
        if args.command in ("configure", "build", "toolchain", "legal-info"):
            doctor()
            if args.command == "build":
                active_build = work
                write_json(work / "build-result.json", {
                    "schema_version": 1, "status": "building",
                    "application_commit": inputs["application_commit"],
                    "tc002_hardware_verified": False, "bootable_tc002_image": False,
                })
            if args.command != "legal-info":
                make(work, inputs, inputs["profile"])
            if args.command == "build":
                make(work, inputs, f"-j{args.jobs}", f"BR2_JLEVEL={args.jobs}", "all")
                record_result(work, inputs)
            elif args.command == "toolchain":
                make(work, inputs, f"-j{args.jobs}", f"BR2_JLEVEL={args.jobs}", "toolchain")
            elif args.command == "legal-info":
                make(work, inputs, "legal-info")
                collect_legal_evidence(work, inputs)
        print(f"{args.command}: {inputs['application_commit']} ({work})")
        return 0
    except (BuildError, OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError) as error:
        if active_build is not None:
            try:
                write_json(active_build / "build-result.json", {
                    "schema_version": 1, "status": "failed",
                    "tc002_hardware_verified": False, "bootable_tc002_image": False,
                })
            except OSError:
                pass  # Preserve the original error even if storage cannot record the failure.
        print(f"system build: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
