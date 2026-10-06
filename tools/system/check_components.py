"""Verify the bundled component manifest against notices, license texts, vendored trees and the ARM recipe.

The manifest records reviewed facts about every bundled or vendored application
component. This check binds each entry to the exact bytes in the checkout; it is
not a license opinion or a vulnerability scan.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys

import build


MANIFEST = build.REPO / "tools/system/components.json"
PACKAGE = "firmware/buildroot/package/awtrix-ng"
NOTICES = "THIRD-PARTY-NOTICES.md"
TARGETS = ("esp32", "linux")
DECISIONS = ("accepted", "pending")
ID = re.compile(r"[a-z0-9][a-z0-9-]{0,39}")
SHA256 = re.compile(r"[0-9a-f]{64}")
DATE = re.compile(r"\d{4}-\d{2}-\d{2}")
URL = re.compile(r"https?://[^\s\x00-\x1f\x7f]+")
LINK = re.compile(r"\[([^\]]+)\]\(([^)\s]+)\)")
TABLE_SEPARATOR = re.compile(r"\|(?:\s*:?-+:?\s*\|)+")
MANIFEST_KEYS = {"schema_version", "scope", "notice_exclusions", "components"}
COMPONENT_KEYS = {"id", "name", "version", "origin", "vendored", "bundled_from", "recipe_package", "paths",
                  "files_sha256", "patches", "patch_marker", "license", "notice", "targets", "review"}
REQUIRED_KEYS = {"id", "name", "version", "origin", "vendored", "paths", "patches", "license", "notice",
                 "targets", "review"}


def text(value) -> bool:
    return isinstance(value, str) and bool(value.strip())


def listing(names) -> str:
    return ", ".join(sorted(names))


def relative_path(value) -> bool:
    if not isinstance(value, str) or not value or "\\" in value or ":" in value or value.endswith("/"):
        return False
    parsed = PurePosixPath(value)
    return bool(parsed.parts) and not parsed.is_absolute() and ".." not in parsed.parts and \
        "." not in parsed.parts and parsed.as_posix() == value


def validate_component(component) -> str:
    if not isinstance(component, dict):
        raise build.BuildError("invalid component entry")
    ident = component.get("id")
    if not isinstance(ident, str) or not ID.fullmatch(ident):
        raise build.BuildError(f"invalid component id: {ident!r}")
    for key in sorted(set(component) - COMPONENT_KEYS):
        raise build.BuildError(f"unknown component key: {ident}: {key}")
    for key in sorted(REQUIRED_KEYS - set(component)):
        raise build.BuildError(f"missing component key: {ident}: {key}")
    if not text(component["name"]):
        raise build.BuildError(f"invalid component name: {ident}")
    if component["version"] is not None and not text(component["version"]):
        raise build.BuildError(f"invalid component version: {ident}")
    origin = component["origin"]
    if (not isinstance(origin, dict) or not {"url", "reference"} <= set(origin) <= {"url", "reference", "note"} or
            (origin["reference"] is not None and not text(origin["reference"])) or
            ("note" in origin and not text(origin["note"]))):
        raise build.BuildError(f"invalid component origin: {ident}")
    if not isinstance(origin["url"], str) or not URL.fullmatch(origin["url"]):
        raise build.BuildError(f"invalid origin URL: {ident}")
    if (component["version"] is None or origin["reference"] is None) and "note" not in origin:
        raise build.BuildError(f"origin note required when version or reference is null: {ident}")
    vendored = component["vendored"]
    if not isinstance(vendored, bool):
        raise build.BuildError(f"invalid vendored flag: {ident}")
    if vendored and "bundled_from" in component:
        raise build.BuildError(f"bundled_from not allowed for vendored component: {ident}")
    if not vendored and not text(component.get("bundled_from")):
        raise build.BuildError(f"bundled_from required for non-vendored component: {ident}")
    if "recipe_package" in component and (vendored or not text(component["recipe_package"])):
        raise build.BuildError(f"recipe_package only for non-vendored component: {ident}")
    paths = component["paths"]
    if not isinstance(paths, list):
        raise build.BuildError(f"invalid component paths: {ident}")
    for path in paths:
        if not relative_path(path):
            raise build.BuildError(f"invalid component path: {ident}: {path if isinstance(path, str) else path!r}")
    if vendored and not paths:
        raise build.BuildError(f"vendored component needs paths: {ident}")
    if not vendored and paths:
        raise build.BuildError(f"paths not allowed for non-vendored component: {ident}")
    if "files_sha256" in component:
        if not vendored:
            raise build.BuildError(f"files_sha256 only for vendored component: {ident}")
        if not isinstance(component["files_sha256"], str) or not SHA256.fullmatch(component["files_sha256"]):
            raise build.BuildError(f"invalid files_sha256: {ident}")
    elif vendored:
        raise build.BuildError(f"vendored component needs files_sha256: {ident}")
    if not isinstance(component["patches"], list):
        raise build.BuildError(f"invalid patch entry: {ident}")
    for patch in component["patches"]:
        if (not isinstance(patch, dict) or set(patch) != {"summary", "files"} or not text(patch["summary"]) or
                not isinstance(patch["files"], list) or not patch["files"] or
                not all(relative_path(file) for file in patch["files"])):
            raise build.BuildError(f"invalid patch entry: {ident}")
    if "patch_marker" in component:
        if not vendored:
            raise build.BuildError(f"patch_marker only for vendored component: {ident}")
        if not text(component["patch_marker"]):
            raise build.BuildError(f"invalid patch_marker: {ident}")
    license_ = component["license"]
    if (not isinstance(license_, dict) or set(license_) != {"spdx", "name", "file", "sha256"} or
            (license_["spdx"] is not None and not text(license_["spdx"])) or not text(license_["name"])):
        raise build.BuildError(f"invalid component license: {ident}")
    file = license_["file"]
    if not relative_path(file) or PurePosixPath(file).parts[0] != "LICENSES" or len(PurePosixPath(file).parts) != 2:
        raise build.BuildError(f"invalid license file path: {ident}")
    if not isinstance(license_["sha256"], str) or not SHA256.fullmatch(license_["sha256"]):
        raise build.BuildError(f"invalid license sha256: {ident}")
    if not text(component["notice"]):
        raise build.BuildError(f"invalid component notice: {ident}")
    targets = component["targets"]
    if (not isinstance(targets, list) or not targets or len(set(targets)) != len(targets) or
            not set(targets) <= set(TARGETS)):
        raise build.BuildError(f"invalid component targets: {ident}")
    review = component["review"]
    if not isinstance(review, dict) or set(review) != {"decision", "date", "rationale"}:
        raise build.BuildError(f"invalid component review: {ident}")
    if review["decision"] not in DECISIONS:
        raise build.BuildError(f"invalid review decision: {ident}")
    try:
        if not isinstance(review["date"], str) or not DATE.fullmatch(review["date"]):
            raise ValueError
        datetime.date.fromisoformat(review["date"])
    except ValueError:
        raise build.BuildError(f"invalid review date: {ident}") from None
    if not text(review["rationale"]):
        raise build.BuildError(f"missing review rationale: {ident}")
    return ident


def validate_manifest(manifest: dict) -> dict[str, dict]:
    if not isinstance(manifest, dict):
        raise build.BuildError("invalid component manifest")
    if manifest.get("schema_version") != 1:
        raise build.BuildError("unsupported component manifest schema")
    for key in sorted(set(manifest) - MANIFEST_KEYS):
        raise build.BuildError(f"unknown manifest key: {key}")
    for key in sorted(MANIFEST_KEYS - set(manifest)):
        raise build.BuildError(f"missing manifest key: {key}")
    if not text(manifest["scope"]):
        raise build.BuildError("invalid manifest scope")
    if not isinstance(manifest["notice_exclusions"], list):
        raise build.BuildError("invalid notice exclusions")
    for exclusion in manifest["notice_exclusions"]:
        if not isinstance(exclusion, dict) or set(exclusion) != {"notice", "reason"} or not text(exclusion["notice"]):
            raise build.BuildError("invalid notice exclusion")
        if not text(exclusion["reason"]):
            raise build.BuildError(f"exclusion without reason: {exclusion['notice']}")
    if not isinstance(manifest["components"], list) or not manifest["components"]:
        raise build.BuildError("no components listed")
    validated = {}
    owners = []
    for component in manifest["components"]:
        ident = validate_component(component)
        if ident in validated:
            raise build.BuildError(f"duplicate component id: {ident}")
        validated[ident] = component
        for path in component["paths"]:
            for owner, other in owners:
                if path == other or path.startswith(other + "/") or other.startswith(path + "/"):
                    raise build.BuildError(f"overlapping component paths: {owner} {ident}")
            owners.append((ident, path))
    return validated


def load_manifest(path: Path) -> dict:
    if path.is_symlink():
        raise build.BuildError(f"symlinked component manifest: {path}")
    manifest = build.json_file(path)
    validate_manifest(manifest)
    return manifest


def component_path(root: Path, relative: str) -> Path:
    candidate = root
    for part in PurePosixPath(relative).parts:
        candidate = candidate / part
        if candidate.is_symlink():
            raise build.BuildError(f"symlink in component path: {relative}")
    if not candidate.exists() or not candidate.resolve().is_relative_to(root.resolve()):
        raise build.BuildError(f"missing component path: {relative}")
    return candidate


def component_files(root: Path, component: dict) -> dict[str, str]:
    files = {}
    for relative in component["paths"]:
        path = component_path(root, relative)
        entries = [path] if path.is_file() else sorted(path.rglob("*"))
        found = False
        for entry in entries:
            name = entry.relative_to(root).as_posix()
            if entry.is_symlink():
                raise build.BuildError(f"symlink in component path: {name}")
            if entry.is_file():
                files[name] = build.digest(entry)
                found = True
        if not found:
            raise build.BuildError(f"component path contains no files: {component['id']}: {relative}")
    return dict(sorted(files.items()))


def lf_normalized_files(root: Path, files: dict[str, str]) -> dict[str, str]:
    """Digests with CRLF replaced by LF, used only to explain a mismatch."""
    normalized = {}
    for name, checksum in files.items():
        data = (root / name).read_bytes()
        normalized[name] = hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest() if b"\r\n" in data else checksum
    return normalized


def files_digest(files: dict[str, str]) -> str:
    return hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def check_patches(root: Path, component: dict, files: dict[str, str]) -> None:
    ident = component["id"]
    listed = set()
    for patch in component["patches"]:
        for file in patch["files"]:
            if file not in files:
                inside = any(file == path or file.startswith(path + "/") for path in component["paths"])
                kind = "missing patch file" if inside else "patch file outside component paths"
                raise build.BuildError(f"{kind}: {ident}: {file}")
            listed.add(file)
    marker = component.get("patch_marker")
    if marker is None:
        return
    for name in files:
        if name not in listed and marker.encode("utf-8") in (root / name).read_bytes():
            raise build.BuildError(f"unlisted local modification: {ident}: {name}")


def notice_row(line: str, rows: dict[str, dict]) -> None:
    cells = [cell.strip() for cell in line.strip("|").split("|")] if line.startswith("|") else []
    link = LINK.match(cells[0]) if cells else None
    if len(cells) < 4 or link is None:
        raise build.BuildError(f"malformed notice row: {line}")
    name = link[1]
    if name in rows:
        raise build.BuildError(f"duplicate notice row: {name}")
    license_ = next((match[2] for match in LINK.finditer(cells[3]) if match[2].startswith("LICENSES/")), None)
    if license_ is None:
        raise build.BuildError(f"notice row without license link: {name}")
    rows[name] = {"url": link[2], "license_file": license_}


def notice_rows(text: str) -> dict[str, dict]:
    """Every body line of every table, up to the next blank line, must start with a component link."""
    rows = {}
    lines = [line.strip() for line in text.splitlines()]
    index = 0
    while index < len(lines):
        if not lines[index].startswith("|"):
            index += 1
            continue
        if index + 1 == len(lines) or not TABLE_SEPARATOR.fullmatch(lines[index + 1]):
            raise build.BuildError(f"notice table without header separator: {lines[index]}")
        index += 2
        while index < len(lines) and lines[index]:
            notice_row(lines[index], rows)
            index += 1
    return rows


def check_notices(root: Path, components: dict[str, dict], exclusions: list) -> dict[str, dict]:
    rows = notice_rows((root / NOTICES).read_text(encoding="utf-8"))
    claimed = {}
    for ident, component in components.items():
        notice = component["notice"]
        row = rows.get(notice)
        if row is None:
            raise build.BuildError(f"notice row not found: {ident}")
        if notice in claimed:
            raise build.BuildError(f"notice row claimed twice: {notice}")
        claimed[notice] = ident
        if row["url"] != component["origin"]["url"]:
            raise build.BuildError(f"notice URL differs from origin: {ident}")
        if row["license_file"] != component["license"]["file"]:
            raise build.BuildError(f"notice license file differs: {ident}")
    for exclusion in exclusions:
        notice = exclusion["notice"]
        if notice not in rows or notice in claimed:
            raise build.BuildError(f"exclusion names a claimed or missing row: {notice}")
        claimed[notice] = None
    for notice in rows:
        if notice not in claimed:
            raise build.BuildError(f"unclaimed notice row: {notice}")
    return rows


def check_license_files(root: Path, components: dict[str, dict], rows: dict[str, dict]) -> None:
    for ident, component in components.items():
        file = component["license"]["file"]
        candidate = root
        for part in PurePosixPath(file).parts:
            candidate = candidate / part
            if candidate.is_symlink():
                raise build.BuildError(f"symlink in license path: {ident}: {file}")
        if not candidate.is_file():
            raise build.BuildError(f"missing license file: {ident}: {file}")
        if build.digest(candidate) != component["license"]["sha256"]:
            raise build.BuildError(f"license checksum mismatch: {ident}: {file}")
    present = set()
    for entry in (root / "LICENSES").iterdir():
        if entry.is_symlink() or not entry.is_file():
            raise build.BuildError(f"unexpected entry in LICENSES: {entry.name}")
        present.add("LICENSES/" + entry.name)
    linked = {row["license_file"] for row in rows.values()}
    if present != linked:
        raise build.BuildError("LICENSES files differ from notice links: "
                               f"missing [{listing(linked - present)}], unlinked [{listing(present - linked)}]")


def check_recipe_licenses(package: Path, components: dict[str, dict]) -> None:
    expected = {c["license"]["file"] for c in components.values() if "linux" in c["targets"]}
    actual = {file for file in build.recipe_license_files(package) if file.startswith("LICENSES/")}
    if expected != actual:
        raise build.BuildError("recipe license files differ from linux components: "
                               f"missing [{listing(expected - actual)}], unexpected [{listing(actual - expected)}]")


def check(root: Path, manifest: dict, package: Path) -> dict[str, dict]:
    components = validate_manifest(manifest)
    for ident, component in components.items():
        if not component["vendored"]:
            check_patches(root, component, {})
            continue
        files = component_files(root, component)
        if files_digest(files) != component["files_sha256"]:
            if files_digest(lf_normalized_files(root, files)) == component["files_sha256"]:
                raise build.BuildError(f"vendored files differ from manifest only by CRLF line endings: {ident} "
                                       "(the checkout is not normalized to LF as .gitattributes requests)")
            raise build.BuildError(f"vendored files differ from manifest: {ident}")
        check_patches(root, component, files)
    rows = check_notices(root, components, manifest["notice_exclusions"])
    check_license_files(root, components, rows)
    check_recipe_licenses(package, components)
    return components


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=build.REPO, help="checkout to verify (default: this repository)")
    parser.add_argument("--manifest", type=Path, help="manifest file (default: ROOT/tools/system/components.json)")
    parser.add_argument("--print-files-sha256", metavar="ID", help="print the digest of one vendored component and exit")
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        manifest = load_manifest(args.manifest or root / "tools/system/components.json")
        if args.print_files_sha256:
            component = next((c for c in manifest["components"] if c["id"] == args.print_files_sha256), None)
            if component is None or not component["vendored"]:
                raise build.BuildError(f"no vendored component with id {args.print_files_sha256}")
            print(files_digest(component_files(root, component)))
            return 0
        verified = check(root, manifest, root / PACKAGE)
        print(f"{len(verified)} application components verified")
        return 0
    except (build.BuildError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"components: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
