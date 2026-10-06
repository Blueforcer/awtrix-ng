"""Copy a verified build's records, logs and source archives into one hashed tarball outside temporary storage.

A bundle is retained development evidence bound to one build result. It proves
which bytes were kept, not that any process ran; it is not a hardware, security
or release attestation.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tarfile
import tempfile

import build
import qualify


TEMPORARY_ROOTS = ("/tmp", "/dev/shm")
WORK_FILES = ("inputs.json", "build-result.json", "application.tar", "output/.config", *qualify.ARTIFACTS)
WORK_OPTIONAL_FILES = ("arm-contracts.json",)
WORK_DIRECTORIES = ("output/legal-info", "output/qualification")
DOWNLOADS = "downloads"
REPRODUCTION_FILES = ("reproduction.json", "first.log", "second.log", "host-packages.txt", "runner/build.py",
                      "first/build-result.json", "second/build-result.json",
                      "first/output/images/rootfs.tar", "first/output/target/usr/bin/awtrix-linux",
                      "second/output/images/rootfs.tar", "second/output/target/usr/bin/awtrix-linux")
REPRODUCTION_OPTIONAL_FILES = ("reproduction-verified.json",)
QUALIFICATION = "output/qualification/qualification.json"
LEGAL = "output/legal-info/"
LEGAL_SUMS = LEGAL + "legal-info.sha256"
BUILD_INPUTS = LEGAL + "build-inputs/"
SUPPLEMENT = BUILD_INPUTS + "manifest.json"
EXTERNAL = BUILD_INPUTS + "external.tar"
RECORDS = ("work/inputs.json", "work/build-result.json", "work/" + QUALIFICATION,
           "work/" + LEGAL_SUMS, "work/" + SUPPLEMENT, "work/" + EXTERNAL)
REPRODUCTION = "reproduction/reproduction.json"
ELF = "work/" + qualify.ARTIFACTS[1]
MEMBER_FILES = frozenset({"work/" + name for name in (*WORK_FILES, *WORK_OPTIONAL_FILES)} |
                         {"reproduction/" + name for name in (*REPRODUCTION_FILES, *REPRODUCTION_OPTIONAL_FILES)})
MEMBER_TREES = tuple("work/" + name + "/" for name in (*WORK_DIRECTORIES, DOWNLOADS))
PURPOSE = "Retained development evidence; not a hardware, security or release attestation"


class HashingReader:
    """Feed tarfile from a stream while hashing exactly the bytes it copies."""

    def __init__(self, stream):
        self.stream = stream
        self.digest = hashlib.sha256()

    def read(self, size=-1):
        chunk = self.stream.read(size)
        self.digest.update(chunk)
        return chunk


def evidence_file(root: Path, relative: str) -> Path:
    return qualify.regular_file(root, relative)


def refuse_temporary(path: Path) -> None:
    resolved = path.resolve()
    for root in (*TEMPORARY_ROOTS, tempfile.gettempdir()):
        if resolved.is_relative_to(Path(root).resolve()):
            raise build.BuildError("evidence bundles must live outside temporary storage")


def source_directory(path: Path) -> Path:
    if path.is_symlink() or not path.is_dir():
        raise build.BuildError(f"evidence source must be a real directory: {path}")
    return path.resolve()


def tree_members(root: Path, relative: str, prefix: str) -> list[tuple[str, Path]]:
    directory = root / relative
    if directory.is_symlink() or not directory.is_dir():
        raise build.BuildError(f"missing evidence directory: {relative}")
    members = []
    for path in sorted(directory.rglob("*")):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise build.BuildError(f"symlink in evidence tree: {name}")
        if path.is_file():
            members.append((prefix + "/" + name, path))
    if not members:
        raise build.BuildError(f"empty evidence directory: {relative}")
    return members


def optional_members(root: Path, names: tuple, prefix: str, missing: list) -> list[tuple[str, Path]]:
    members = []
    for name in names:
        if (root / name).exists() or (root / name).is_symlink():
            members.append((prefix + "/" + name, evidence_file(root, name)))
        else:
            missing.append(prefix + "/" + name)
    return members


def evidence_json(root: Path, relative: str) -> tuple[dict, str]:
    """Parse and hash the same bytes, so the copied member can be compared with what was checked."""
    data = evidence_file(root, relative).read_bytes()
    return build.json_bytes(data, relative), hashlib.sha256(data).hexdigest()


# Windows resolves these to another file: a stream suffix, a trailing dot or space, or an 8.3 short name.
WINDOWS_ALIAS = re.compile(r":|[. ]$|~[0-9]")


def check_member_name(name: str) -> None:
    """Accept only the canonical paths create writes, so no member extracts over another path or outside its tree."""
    parts = PurePosixPath(name).parts
    if ("\\" in name or name.startswith("/") or ".." in parts or PurePosixPath(name).as_posix() != name or
            any(WINDOWS_ALIAS.search(part) for part in parts) or
            not (name in MEMBER_FILES or name.startswith(MEMBER_TREES) or (len(parts) == 2 and parts[0] == "logs"))):
        raise build.BuildError(f"invalid evidence member name: {name}")


def check_member_case(name: str, folded: dict[str, str]) -> None:
    """Case-insensitive file systems extract two names that differ only in case into one file."""
    other = folded.setdefault(name.casefold(), name)
    if other != name:
        raise build.BuildError(f"evidence member names collide when case is ignored: {name}")


def check_identity(members: dict, identity: dict[str, str], message: str) -> None:
    for name, sha256 in sorted(identity.items()):
        record = members.get(name)
        if not isinstance(record, dict) or record.get("sha256") != sha256:
            raise build.BuildError(f"{message}: {name}")


def check_build_records(inputs: dict, result: dict) -> None:
    """The record checks build.verify and qualify.verify_build apply to inputs.json and build-result.json."""
    if inputs.get("schema_version") != 1 or inputs.get("profile") != "awtrix_armv7_defconfig":
        raise build.BuildError("unsupported prepared inputs")
    if (result.get("schema_version") != 1 or result.get("status") != "crossbuild_passed" or
            result.get("profile") != inputs["profile"] or result.get("application_commit") != inputs.get("application_commit")):
        raise build.BuildError("build result does not identify a successful build of these inputs")


def bound_reproduction(record: dict, result: dict) -> None:
    if (record.get("status") != "reproduced" or record.get("clean_builds") != 2 or
            record.get("application_commit") != result["application_commit"] or
            record.get("source_inputs_sha256") != result["inputs_sha256"]):
        raise build.BuildError("reproduction record is not bound to this build")


def bound_generated(record: dict, result: dict, result_sha256: str) -> dict:
    generated = record.get("generated_files_sha256")
    if (record.get("inputs_sha256") != result["inputs_sha256"] or
            record.get("application_commit") != result["application_commit"] or
            record.get("build_result_sha256") != result_sha256 or record.get("artifacts") != result["artifacts"] or
            not isinstance(generated, dict) or not generated):
        raise build.BuildError("qualification record is not bound to this build")
    return generated


def legal_identity(record: dict, inputs: dict, result: dict, sums: bytes, supplement: dict) -> dict[str, str]:
    """Map every legal-info file except the two record files to the digest a record bound to this build holds.

    qualify.py records the parsed legal-info.sha256 as legal_evidence_sha256, so the list must be Buildroot's
    sorted rendering of that record. build-inputs is written after Buildroot hashed legal-info; its manifest is
    bound through the build result and inputs, and external.tar by content in check_external_archive.
    """
    recorded = record.get("legal_evidence_sha256")
    if (not isinstance(recorded, dict) or recorded.get("buildroot.config") != result["config_sha256"] or
            any(not isinstance(sha256, str) or not qualify.SHA256.fullmatch(sha256) or
                LEGAL + name == LEGAL_SUMS or (LEGAL + name).startswith(BUILD_INPUTS)
                for name, sha256 in recorded.items())):
        raise build.BuildError("qualification record does not cover this build's legal evidence")
    if sums != "".join(f"{sha256}  {name}\n" for name, sha256 in sorted(recorded.items())).encode():
        raise build.BuildError("legal-info checksum list differs from the qualification record")
    source = inputs["source_lock"]["buildroot"]
    anchored = {"application.tar": inputs["application_archive_sha256"], "inputs.json": result["inputs_sha256"],
                f"buildroot-{source['version']}.tar.xz": source["sha256"]}
    files = supplement.get("files")
    if (supplement.get("schema_version") != 1 or supplement.get("application_commit") != result["application_commit"] or
            not isinstance(files, dict) or set(files) != {*anchored, "external.tar"} or
            any(files[name] != sha256 for name, sha256 in anchored.items()) or
            not isinstance(files["external.tar"], str) or not qualify.SHA256.fullmatch(files["external.tar"])):
        raise build.BuildError("build-inputs manifest is not bound to this build")
    identity = {LEGAL + name: sha256 for name, sha256 in recorded.items()}
    identity.update({BUILD_INPUTS + name: sha256 for name, sha256 in files.items()})
    return identity


def check_external_archive(data: bytes, expected: dict[str, str]) -> None:
    """external.tar has no digest outside its own manifest, so its files must equal the frozen recipes."""
    found = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as archive:
        for member in archive:
            if member.isdir():
                continue
            name = member.name.removeprefix("external/")
            if not member.isreg() or name == member.name or name in found:
                raise build.BuildError(f"unexpected member in build-inputs/external.tar: {member.name}")
            found[name] = hashlib.sha256(archive.extractfile(member).read()).hexdigest()
    if found != expected:
        raise build.BuildError("build-inputs/external.tar differs from the frozen external recipes")


def check_legal_coverage(names, covered: set) -> None:
    """No record vouches for any other legal-info file, so bundling one would pass unchecked bytes as evidence."""
    for name in sorted(names):
        if name.startswith("work/" + LEGAL) and name not in covered:
            raise build.BuildError(f"unrecorded legal evidence: {name}")


def collect_work(work: Path, downloads: bool) -> tuple[dict, dict, dict[str, str], list[tuple[str, Path]], list[str]]:
    """Return the verified build, the member digests it vouches for, and the members to copy."""
    inputs, result = qualify.verify_build(work)
    recorded, result_sha256 = evidence_json(work, "build-result.json")
    if recorded != result:
        raise build.BuildError("evidence changed while bundling: work/build-result.json")
    identity = {"work/inputs.json": result["inputs_sha256"], "work/build-result.json": result_sha256,
                "work/output/.config": result["config_sha256"], "work/application.tar": inputs["application_archive_sha256"]}
    identity.update({"work/" + name: artifact["sha256"] for name, artifact in result["artifacts"].items()})
    if downloads:
        source = inputs["source_lock"]["buildroot"]
        identity[f"work/{DOWNLOADS}/buildroot-{source['version']}.tar.xz"] = source["sha256"]
    record, identity["work/" + QUALIFICATION] = evidence_json(work, QUALIFICATION)
    for name, sha256 in sorted(bound_generated(record, result, result_sha256).items()):
        relative = "output/qualification/" + name
        if build.digest(evidence_file(work, relative)) != sha256:
            raise build.BuildError(f"qualification output differs from its record: {name}")
        identity["work/" + relative] = sha256
    sums = evidence_file(work, LEGAL_SUMS).read_bytes()
    identity["work/" + LEGAL_SUMS] = hashlib.sha256(sums).hexdigest()
    supplement, identity["work/" + SUPPLEMENT] = evidence_json(work, SUPPLEMENT)
    external = b""
    for name, sha256 in sorted(legal_identity(record, inputs, result, sums, supplement).items()):
        path = evidence_file(work, name)
        if name == EXTERNAL:
            external = path.read_bytes()
        if (hashlib.sha256(external).hexdigest() if name == EXTERNAL else build.digest(path)) != sha256:
            raise build.BuildError(f"legal evidence differs from its record: {name}")
        identity["work/" + name] = sha256
    check_external_archive(external, inputs["external_files"])
    missing = []
    members = [("work/" + name, evidence_file(work, name)) for name in WORK_FILES]
    members += optional_members(work, WORK_OPTIONAL_FILES, "work", missing)
    for directory in WORK_DIRECTORIES:
        members += tree_members(work, directory, "work")
    check_legal_coverage([name for name, _ in members], set(identity))
    if downloads:
        members += tree_members(work, DOWNLOADS, "work")
    return inputs, result, identity, members, missing


def collect_reproduction(directory: Path, result: dict, identity: dict[str, str],
                         missing: list) -> list[tuple[str, Path]]:
    record, identity[REPRODUCTION] = evidence_json(directory, "reproduction.json")
    bound_reproduction(record, result)
    members = [("reproduction/" + name, evidence_file(directory, name)) for name in REPRODUCTION_FILES]
    return members + optional_members(directory, REPRODUCTION_OPTIONAL_FILES, "reproduction", missing)


def collect_logs(directory: Path) -> list[tuple[str, Path]]:
    members = []
    for path in sorted(directory.iterdir()):
        if path.is_symlink():
            raise build.BuildError(f"symlink in evidence tree: logs/{path.name}")
        if not path.is_file():
            raise build.BuildError(f"log directory must contain only files: {path.name}")
        members.append(("logs/" + path.name, path))
    if not members:
        raise build.BuildError("empty evidence directory: logs")
    return members


def write_bundle(output: Path, members: list[tuple[str, Path]], epoch: int, identity: dict[str, str]) -> dict[str, dict]:
    """Publish the tar only when the copied bytes are the ones verified; a refused copy stays .partial."""
    partial = output.with_name(output.name + ".partial")
    folded: dict[str, str] = {}
    for name, _ in members:
        check_member_name(name)
        check_member_case(name, folded)
    written = {}
    with tarfile.open(partial, "x:", format=tarfile.PAX_FORMAT) as archive:
        for name, path in sorted(members):
            if name in written:
                raise build.BuildError(f"duplicate evidence member: {name}")
            info = tarfile.TarInfo(name)
            info.size = path.stat().st_size
            info.mtime = epoch
            info.mode = 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with path.open("rb") as stream:
                reader = HashingReader(stream)
                archive.addfile(info, reader)
            written[name] = {"sha256": reader.digest.hexdigest(), "bytes": info.size}
    with partial.open("ab") as stream:
        os.fsync(stream.fileno())
    check_identity(written, identity, "evidence changed while bundling")
    partial.replace(output)
    return written


def write_manifest(path: Path, manifest: dict) -> None:
    """Create the temporary file exclusively, so a file or link already at its name is never written through."""
    partial = path.with_name(path.name + ".partial")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)
    try:
        descriptor = os.open(partial, flags, 0o644)
    except FileExistsError as error:
        raise build.BuildError(f"evidence bundle output already exists: {partial}") from error
    with os.fdopen(descriptor, "wb") as stream:
        stream.write((json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"))
        stream.flush()
        os.fsync(stream.fileno())
    partial.replace(path)


def create(work: Path, output: Path, reproduction: Path | None, logs: Path | None, downloads: bool = True) -> dict:
    if output.suffix != ".tar":
        raise build.BuildError("evidence bundle must end with .tar")
    manifest_path = output.with_name(output.name + ".manifest.json")
    for path in (output, output.with_name(output.name + ".partial"), manifest_path,
                 manifest_path.with_name(manifest_path.name + ".partial")):
        if path.exists() or path.is_symlink():
            raise build.BuildError(f"evidence bundle output already exists: {path}")
    refuse_temporary(output)
    if output.parent.is_symlink() or not output.parent.is_dir():
        raise build.BuildError("evidence bundle parent directory does not exist")
    sources = {"work": source_directory(work),
               "reproduction": source_directory(reproduction) if reproduction else None,
               "logs": source_directory(logs) if logs else None}
    resolved = output.resolve()
    if any(resolved.is_relative_to(source) for source in sources.values() if source):
        raise build.BuildError("evidence bundle must not be written inside its sources")
    inputs, result, identity, members, missing = collect_work(sources["work"], downloads)
    if sources["reproduction"]:
        members += collect_reproduction(sources["reproduction"], result, identity, missing)
    if sources["logs"]:
        members += collect_logs(sources["logs"])
    written = write_bundle(output, members, inputs["source_date_epoch"], identity)
    manifest = {"schema_version": 1, "purpose": PURPOSE,
                "application_commit": result["application_commit"], "inputs_sha256": result["inputs_sha256"],
                "build_result_sha256": identity["work/build-result.json"],
                "artifacts": result["artifacts"],
                "bundle": {"path": str(output), "sha256": build.digest(output), "bytes": output.stat().st_size},
                "members": written, "missing_optional": missing, "downloads_included": downloads,
                "sources": {key: None if value is None else str(value) for key, value in sources.items()},
                "tool_sha256": build.digest(Path(__file__)),
                "tc002_hardware_verified": False, "bootable_tc002_image": False}
    write_manifest(manifest_path, manifest)
    return manifest


def check_bundled_records(seen: dict, records: dict[str, bytes], manifest: dict) -> None:
    """Repeat the checks of create that the bundled members allow, against the records bundled with them."""
    for name in RECORDS:
        if name not in records:
            raise build.BuildError(f"missing member: {name}")
    inputs, result, record, supplement = (build.json_bytes(records[name], name) for name in (
        "work/inputs.json", "work/build-result.json", "work/" + QUALIFICATION, "work/" + SUPPLEMENT))
    check_build_records(inputs, result)
    if (result.get("inputs_sha256") != seen["work/inputs.json"]["sha256"] or
            result.get("artifacts") != manifest["artifacts"] or
            result.get("application_commit") != manifest.get("application_commit")):
        raise build.BuildError("member differs from build identity: work/build-result.json")
    header = records[ELF][:20]
    if not build.is_arm_elf(header):
        raise build.BuildError("application artifact is not ARM 32-bit little-endian ELF")
    downloads = any(name.startswith("work/" + DOWNLOADS + "/") for name in seen)
    if manifest.get("downloads_included") is not downloads:
        raise build.BuildError("evidence manifest does not match the bundled downloads")
    identity = {"work/application.tar": inputs.get("application_archive_sha256"),
                "work/output/.config": result.get("config_sha256")}
    if downloads:
        source = inputs["source_lock"]["buildroot"]
        identity[f"work/{DOWNLOADS}/buildroot-{source['version']}.tar.xz"] = source["sha256"]
    check_identity(seen, identity, "member differs from build identity")
    expected = {"output/qualification/" + name: sha256 for name, sha256 in
                bound_generated(record, result, seen["work/build-result.json"]["sha256"]).items()}
    expected.update(legal_identity(record, inputs, result, records["work/" + LEGAL_SUMS], supplement))
    for name, sha256 in sorted(expected.items()):
        if seen.get("work/" + name, {}).get("sha256") != sha256:
            raise build.BuildError(f"member differs from qualification record: work/{name}")
    check_external_archive(records["work/" + EXTERNAL], inputs["external_files"])
    check_legal_coverage(seen, {"work/" + name for name in (*expected, LEGAL_SUMS, SUPPLEMENT)})
    if manifest["sources"].get("reproduction") is not None or any(name.startswith("reproduction/") for name in seen):
        for name in REPRODUCTION_FILES:
            if "reproduction/" + name not in seen:
                raise build.BuildError(f"missing member: reproduction/{name}")
        bound_reproduction(build.json_bytes(records[REPRODUCTION], REPRODUCTION), result)


def verify(bundle: Path) -> dict:
    manifest = build.json_file(bundle.with_name(bundle.name + ".manifest.json"))
    if (manifest.get("schema_version") != 1 or not isinstance(manifest.get("members"), dict) or
            not isinstance(manifest.get("sources"), dict)):
        raise build.BuildError("unsupported evidence manifest")
    seen, records, folded = {}, {}, {}
    try:
        with tarfile.open(bundle, "r:") as archive:
            for member in archive:
                if not member.isreg():
                    raise build.BuildError(f"unexpected member type: {member.name}")
                check_member_name(member.name)
                if member.name in seen:
                    raise build.BuildError(f"duplicate evidence member: {member.name}")
                check_member_case(member.name, folded)
                digest = hashlib.sha256()
                data = bytearray()
                buffered = member.name in RECORDS or member.name == REPRODUCTION
                with archive.extractfile(member) as stream:
                    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                        digest.update(chunk)
                        if buffered or (member.name == ELF and not data):
                            data.extend(chunk)
                if buffered or member.name == ELF:
                    records[member.name] = bytes(data if buffered else data[:20])
                seen[member.name] = {"sha256": digest.hexdigest(), "bytes": member.size}
    except (tarfile.TarError, EOFError) as error:
        raise build.BuildError(f"evidence bundle is truncated or corrupt: {error}") from error
    for name in sorted(set(manifest["members"]) - set(seen)):
        raise build.BuildError(f"missing member: {name}")
    for name in sorted(set(seen) - set(manifest["members"])):
        raise build.BuildError(f"unexpected member: {name}")
    for name, record in sorted(seen.items()):
        if manifest["members"][name] != record:
            raise build.BuildError(f"member differs from manifest: {name}")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict) or set(artifacts) != set(qualify.ARTIFACTS):
        raise build.BuildError("evidence manifest does not identify both artifacts")
    for name, record in sorted(artifacts.items()):
        if seen.get("work/" + name) != record:
            raise build.BuildError(f"member differs from build identity: work/{name}")
    check_identity(seen, {"work/inputs.json": manifest.get("inputs_sha256"),
                          "work/build-result.json": manifest.get("build_result_sha256")},
                   "member differs from build identity")
    check_bundled_records(seen, records, manifest)
    expected = manifest.get("bundle")
    if (not isinstance(expected, dict) or expected.get("bytes") != bundle.stat().st_size or
            expected.get("sha256") != build.digest(bundle)):
        raise build.BuildError("evidence bundle differs from its manifest")
    return manifest


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    creating = commands.add_parser("create", help="bundle a verified build directory")
    creating.add_argument("--work", type=Path, required=True)
    creating.add_argument("--output", type=Path, required=True, help="new .tar path outside temporary storage")
    creating.add_argument("--reproduction", type=Path, help="reproduction directory bound to the same build")
    creating.add_argument("--logs", type=Path, help="directory whose files are added under logs/")
    creating.add_argument("--no-downloads", action="store_true", help="omit the package download cache")
    verifying = commands.add_parser("verify", help="recheck a bundle against its manifest")
    verifying.add_argument("--bundle", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "create":
            create(args.work.expanduser(), args.output.expanduser(),
                   args.reproduction.expanduser() if args.reproduction else None,
                   args.logs.expanduser() if args.logs else None, not args.no_downloads)
            print(f"Evidence bundle written: {args.output.with_name(args.output.name + '.manifest.json')}")
        else:
            verify(args.bundle.expanduser())
            print(f"Evidence bundle verified: {args.bundle}")
        return 0
    except (build.BuildError, OSError, ValueError, KeyError, TypeError, tarfile.TarError) as error:
        print(f"evidence bundle: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
