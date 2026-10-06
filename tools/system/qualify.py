"""Verify a local ARM development build and export a scoped CycloneDX inventory.

No network access, flashing, publication, or hardware attestation is performed.
Exit 2 means a requested public release is ineligible; exit 1 means invalid input.
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys


import build
import check_components as components

BuildError = build.BuildError

ARTIFACTS = ("output/images/rootfs.tar", "output/target/usr/bin/awtrix-linux")
MANIFEST_PATH = "tools/system/components.json"
TRIAGE_STATUSES = ("no_known_advisories", "not_affected", "affected", "under_investigation", "fixed_locally")
TRIAGE_SCOPE = "application components from the manifest with a linux target; Buildroot packages are not covered"
TRIAGE_KEYS = {"schema_version", "subject", "data_source", "reviewer", "components"}
CHECKS = {
    "hardware_identity": "Board revision, SoC, memory and flash identified on physical hardware",
    "maintained_kernel_boot": "Supported kernel, board sources and boot chain verified on hardware",
    "peripherals": "Required display, controls, audio, sensors and network tested on hardware",
    "install_without_opening": "End-user installation verified without opening the enclosure",
    "recovery_without_opening": "Recovery from an unusable installation verified without opening",
    "power_loss_and_rollback": "Update interruption, power loss and rollback tested",
    "security_review": "Threat model, service isolation, credentials and vulnerability review completed",
    "signed_updates": "Update authenticity, key handling and rollback policy verified",
    "redistribution_review": "All source, license and any necessary proprietary redistribution reviewed",
    "bit_reproducibility": "Independent clean builds compared with recorded environment and artifacts",
}
SHA256 = re.compile(r"[0-9a-f]{64}")
INVENTORY_SCOPE = (
    "Configured Buildroot source packages and build dependencies, plus two verified output files. "
    "Vendored application components with a Linux target are nested under the application recipe "
    "from the reviewed component manifest; ESP32-only components are listed in the manifest but not "
    "in this inventory. Not a complete installed-file inventory. The vulnerability-triage record "
    "states whether a triage was performed and against which data source. Linux headers do not "
    "establish the presence, version or support of a running kernel."
)
TRIAGE_LIMITATION = ("Vulnerability triage status not_performed means no advisory data source was consulted; "
                     "a performed status covers only the listed application components.")


json_file = build.json_file


def regular_file(root: Path, relative: str) -> Path:
    """Reject traversal and symlinks even when a link points back into the tree."""
    if not isinstance(relative, str) or not relative or "\\" in relative or ":" in relative:
        raise BuildError("invalid evidence path")
    parsed = PurePosixPath(relative)
    if parsed.is_absolute() or ".." in parsed.parts or parsed.as_posix() != relative:
        raise BuildError(f"invalid evidence path: {relative}")
    candidate = root
    for part in parsed.parts:
        candidate = candidate / part
        if candidate.is_symlink():
            raise BuildError(f"symlink in evidence path: {relative}")
    if not candidate.is_file() or not candidate.resolve().is_relative_to(root.resolve()):
        raise BuildError(f"missing evidence file: {relative}")
    return candidate


def verified_file(root: Path, relative: str, expected: str) -> Path:
    if not isinstance(expected, str) or not SHA256.fullmatch(expected):
        raise BuildError(f"invalid SHA-256 for {relative}")
    path = regular_file(root, relative)
    if build.digest(path) != expected:
        raise BuildError(f"checksum mismatch: {relative}")
    return path


def verify_build(work: Path) -> tuple[dict, dict]:
    inputs = build.verify(work)
    result = json_file(regular_file(work, "build-result.json"))
    if (result.get("schema_version") != 1 or result.get("status") != "crossbuild_passed" or
            result.get("profile") != inputs["profile"] or
            result.get("application_commit") != inputs["application_commit"]):
        raise BuildError("build result does not identify a successful build of these inputs")
    verified_file(work, "inputs.json", result.get("inputs_sha256"))
    verified_file(work, "output/.config", result.get("config_sha256"))
    if not isinstance(result.get("artifacts"), dict) or set(result["artifacts"]) != set(ARTIFACTS):
        raise BuildError("build result must identify both expected development artifacts")
    for name, record in result["artifacts"].items():
        if not isinstance(record, dict):
            raise BuildError(f"invalid artifact record: {name}")
        path = verified_file(work, name, record.get("sha256"))
        if type(record.get("bytes")) is not int or path.stat().st_size != record["bytes"]:
            raise BuildError(f"artifact size mismatch: {name}")
    with regular_file(work, ARTIFACTS[1]).open("rb") as stream:
        header = stream.read(20)
    if not build.is_arm_elf(header):
        raise BuildError("application artifact is not ARM 32-bit little-endian ELF")
    return inputs, result


def application_manifest(work: Path) -> tuple[dict, dict, str]:
    """Verify the component manifest against the frozen application tree, not the checkout."""
    application = work / "application"
    if not (application / MANIFEST_PATH).is_file():
        raise BuildError(f"application snapshot lacks the component manifest: {MANIFEST_PATH}")
    path = regular_file(application, MANIFEST_PATH)
    manifest = components.load_manifest(path)
    validated = components.check(application, manifest, work / "external/package/awtrix-ng")
    return manifest, validated, build.digest(path)


def nested_components(validated: dict) -> list[dict]:
    nested = []
    for ident, component in sorted(validated.items()):
        if not component["vendored"] or "linux" not in component["targets"]:
            continue
        origin, license_ = component["origin"], component["license"]
        metadata = {"manifest-id": ident, "origin-reference": origin["reference"], "paths": component["paths"],
                    "files-sha256": component["files_sha256"],
                    "patches": [patch["summary"] for patch in component["patches"]],
                    "license-file": license_["file"], "license-file-sha256": license_["sha256"],
                    "notice": component["notice"], "targets": component["targets"],
                    "review-decision": component["review"]["decision"], "review-date": component["review"]["date"]}
        if "note" in origin:
            metadata["origin-note"] = origin["note"]
        entry = {"type": "library", "bom-ref": "application-component:" + ident, "name": component["name"],
                 "licenses": [{"license": {"id": license_["spdx"]} if license_["spdx"] else {"name": license_["name"]}}],
                 "externalReferences": [{"type": "vcs" if origin["url"].startswith("https://github.com/") else "website",
                                         "url": origin["url"]}],
                 "properties": properties(metadata)}
        if component["version"]:
            entry["version"] = component["version"]
        nested.append(entry)
    return nested


def triage_template(manifest_sha256: str, validated: dict) -> dict:
    return {"schema_version": 1, "subject": {"component_manifest_sha256": manifest_sha256},
            "data_source": {"name": "", "reference": "", "retrieved": ""}, "reviewer": "",
            "components": {ident: {"status": "under_investigation", "note": ""}
                           for ident, component in sorted(validated.items()) if "linux" in component["targets"]}}


def evaluate_triage(manifest_sha256: str, validated: dict, path: Path | None) -> dict:
    """A triage counts only when a record names its data source and covers every Linux component."""
    if path is None:
        return {"status": "not_performed", "data_source": None, "record_sha256": None, "reviewer": None,
                "findings": {}, "reason": "no vulnerability data source was supplied", "scope": TRIAGE_SCOPE}
    record = json_file(path)
    if record.get("schema_version") != 1 or set(record) != TRIAGE_KEYS:
        raise BuildError("unsupported triage record schema")
    if record["subject"] != {"component_manifest_sha256": manifest_sha256}:
        raise BuildError("triage record is not bound to this component manifest")
    source = record["data_source"]
    if (not isinstance(source, dict) or set(source) != {"name", "reference", "retrieved"} or
            not all(isinstance(value, str) and value.strip() for value in source.values()) or
            not re.fullmatch(r"\d{4}-\d{2}-\d{2}", source["retrieved"])):
        raise BuildError("triage record names no data source")
    if not isinstance(record["reviewer"], str) or not record["reviewer"].strip():
        raise BuildError("triage record names no reviewer")
    expected = {ident for ident, component in validated.items() if "linux" in component["targets"]}
    entries = record["components"]
    if not isinstance(entries, dict):
        raise BuildError("triage record lists no components")
    for ident in sorted(expected - set(entries)):
        raise BuildError(f"triage record omits component: {ident}")
    for ident in sorted(set(entries) - expected):
        raise BuildError(f"triage record names unknown component: {ident}")
    findings = {}
    for ident, entry in sorted(entries.items()):
        if not isinstance(entry, dict) or set(entry) != {"status", "note"} or entry["status"] not in TRIAGE_STATUSES:
            raise BuildError(f"invalid triage status: {ident}")
        if not isinstance(entry["note"], str) or not entry["note"].strip():
            raise BuildError(f"missing triage note: {ident}")
        findings[ident] = entry["status"]
    return {"status": "performed", "data_source": source, "record_sha256": build.digest(path),
            "reviewer": record["reviewer"], "findings": findings, "scope": TRIAGE_SCOPE}


def collect_show_info(work: Path, inputs: dict) -> dict:
    """Evaluate metadata locally from the verified frozen recipes and config."""
    environment = os.environ.copy()
    environment["PATH"] = os.pathsep.join(p for p in environment.get("PATH", "").split(os.pathsep)
                                           if p and not any(c.isspace() for c in p))
    environment.update(LC_ALL="C", TZ="UTC", SOURCE_DATE_EPOCH=str(inputs["source_date_epoch"]))
    raw = subprocess.check_output([
        "make", "-s", "--no-print-directory", "-C", str(work / "buildroot"),
        "O=" + str(work / "output"), "BR2_EXTERNAL=" + str(work / "external"),
        "BR2_DL_DIR=" + str(work / "downloads"), "AWTRIX_NG_SNAPSHOT=" + str(work / "application"),
        "AWTRIX_NG_REVISION=" + inputs["application_commit"], "show-info",
    ], env=environment, text=True)
    info = json.loads(raw)
    if not isinstance(info, dict) or not info:
        raise BuildError("Buildroot show-info returned no package objects")
    return info


def legal_metadata(work: Path, inputs: dict, result: dict, info: dict) -> tuple[dict, dict]:
    legal = work / "output/legal-info"
    sums = regular_file(legal, "legal-info.sha256")
    checked = {}
    for line in sums.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match or match[2] in checked:
            raise BuildError("invalid or duplicate legal-info checksum entry")
        verified_file(legal, match[2], match[1])
        checked[match[2]] = match[1]
    required = {"README", "buildroot.config", "manifest.csv", "host-manifest.csv"}
    if not required.issubset(checked):
        raise BuildError("legal-info checksum manifest omits required evidence")
    if checked["buildroot.config"] != result["config_sha256"]:
        raise BuildError("legal-info belongs to a different build configuration")
    rows = {}
    columns = {"PACKAGE", "VERSION", "LICENSE", "LICENSE FILES", "SOURCE ARCHIVE", "SOURCE SITE"}
    for host, filename in ((False, "manifest.csv"), (True, "host-manifest.csv")):
        reader = csv.DictReader(io.StringIO(regular_file(legal, filename).read_text(encoding="utf-8")))
        if not columns.issubset(reader.fieldnames or []):
            raise BuildError(f"unsupported legal manifest columns: {filename}")
        for row in reader:
            if any(not isinstance(row.get(key), str) for key in columns):
                raise BuildError(f"malformed legal manifest row: {filename}")
            key = ("host-" if host else "") + row["PACKAGE"]
            if key in rows:
                raise BuildError(f"duplicate legal package: {key}")
            if key == "host-buildroot":
                if row["VERSION"] != inputs["source_lock"]["buildroot"]["version"]:
                    raise BuildError("legal manifest Buildroot version differs from source lock")
            else:
                package = info.get(key)
                if (not isinstance(package, dict) or package.get("version") != row["VERSION"] or
                        package.get("licenses", "") != row["LICENSE"] or
                        " ".join(package.get("license_files", [])) != row["LICENSE FILES"]):
                    raise BuildError(f"legal manifest differs from configured package: {key}")
            row["manifest"] = filename
            rows[key] = row
    if "awtrix-ng" not in rows or rows["awtrix-ng"]["VERSION"] != inputs["application_commit"]:
        raise BuildError("legal manifest does not identify the built application commit")
    return rows, checked


def properties(values: dict) -> list[dict]:
    return [{"name": "awtrix:" + key, "value": value if isinstance(value, str) else
             json.dumps(value, sort_keys=True, separators=(",", ":"))} for key, value in sorted(values.items())]


def inventory(inputs: dict, result: dict, info: dict, rows: dict, checked: dict, validated: dict,
              manifest_sha256: str, triage_status: str) -> dict:
    recipe_ids = {}
    for ident, component in sorted(validated.items()):
        if component["vendored"] or "recipe_package" not in component or "linux" not in component["targets"]:
            continue
        package = info.get(component["recipe_package"])
        if not isinstance(package, dict) or package.get("version") != component["version"]:
            raise BuildError(f"recipe package differs from component manifest: {ident}")
        recipe_ids[component["recipe_package"]] = ident
    entries = []
    application_seen = False
    for key, package in sorted(info.items()):
        if not isinstance(package, dict) or package.get("type") not in ("target", "host", "rootfs"):
            raise BuildError(f"unsupported show-info package: {key}")
        if package["type"] == "rootfs" or package.get("virtual"):
            continue
        if not isinstance(package.get("name"), str) or not package["name"]:
            raise BuildError(f"missing package name: {key}")
        metadata = {"buildroot-key": key, "package-role": package["type"],
                    "source-downloads": package.get("downloads", []),
                    "license-files": package.get("license_files", []),
                    "build-dependencies": package.get("dependencies", []),
                    "recipe-patches": package.get("patches", [])}
        for flag in ("install_target", "install_staging", "install_images"):
            if flag in package:
                metadata[flag.replace("_", "-")] = package[flag]
        if key in rows:
            row = rows[key]
            metadata["legal-manifest"] = row["manifest"]
            metadata["source-site"] = row["SOURCE SITE"]
            metadata["source-archive"] = row["SOURCE ARCHIVE"]
            prefix = ("host-sources/" if package["type"] == "host" else "sources/")
            prefix += package["name"] + "-" + package.get("version", "") + "/"
            metadata["saved-source-sha256"] = {p: h for p, h in checked.items() if p.startswith(prefix)}
        else:
            metadata["legal-manifest"] = "not listed; consult recipe and Buildroot legal-info README"
        if key in recipe_ids:
            metadata["component-manifest-id"] = recipe_ids[key]
        component = {"type": "library", "bom-ref": "buildroot:" + key,
                     "name": package["name"], "properties": properties(metadata)}
        if package.get("version"):
            component["version"] = package["version"]
        if package.get("licenses"):
            # Keep Buildroot's original wording; do not guess an SPDX expression.
            component["licenses"] = [{"license": {"name": package["licenses"]}}]
        if key == "awtrix-ng":
            component["components"] = nested_components(validated)
            application_seen = True
        entries.append(component)
    if not application_seen:
        raise BuildError("application recipe missing from show-info")
    if "host-buildroot" not in rows:
        raise BuildError("legal manifest omits Buildroot itself")
    source = inputs["source_lock"]["buildroot"]
    entries.append({"type": "application", "bom-ref": "build-tool:buildroot", "name": "Buildroot",
                    "version": source["version"],
                    "licenses": [{"license": {"name": rows["host-buildroot"]["LICENSE"]}}],
                    "externalReferences": [{"type": "source-distribution", "url": source["url"],
                                            "hashes": [{"alg": "SHA-256", "content": source["sha256"]}]}],
                    "properties": properties({"package-role": "build-tool"})})
    for name, artifact in sorted(result["artifacts"].items()):
        entries.append({"type": "file", "bom-ref": "artifact:" + name, "name": name,
                        "hashes": [{"alg": "SHA-256", "content": artifact["sha256"]}]})
    return {"$schema": "https://cyclonedx.org/schema/bom-1.6.schema.json", "bomFormat": "CycloneDX",
            "specVersion": "1.6", "version": 1,
            "metadata": {"component": {"type": "application", "bom-ref": "awtrix-development-build",
                                       "name": "AWTRIX NG ARM userspace development build",
                                       "version": inputs["application_commit"]},
                         "properties": properties({"scope": INVENTORY_SCOPE, "release-channel": "development-only",
                                                   "inputs-sha256": result["inputs_sha256"],
                                                   "config-sha256": result["config_sha256"],
                                                   "component-manifest-sha256": manifest_sha256,
                                                   "vulnerability-triage": triage_status})},
            "components": entries,
            "compositions": [{"aggregate": "incomplete"}]}


def evidence_template(result: dict) -> dict:
    return {"schema_version": 1,
            "subject": {"inputs_sha256": result["inputs_sha256"],
                        "artifacts": {p: r["sha256"] for p, r in sorted(result["artifacts"].items())}},
            "checks": {key: {"state": "unmet", "note": description, "files": []}
                       for key, description in CHECKS.items()}}


def evaluate_evidence(result: dict, path: Path | None) -> dict:
    template = evidence_template(result)
    if path is None:
        return template["checks"]
    evidence = json_file(path)
    if evidence.get("schema_version") != 1 or evidence.get("subject") != template["subject"]:
        raise BuildError("release evidence is not bound to these inputs and artifacts")
    checks = evidence.get("checks")
    if not isinstance(checks, dict) or set(checks) != set(CHECKS):
        raise BuildError("release evidence must list every required check exactly once")
    for key, check in checks.items():
        if not isinstance(check, dict) or check.get("state") not in ("passed", "unmet"):
            raise BuildError(f"invalid evidence state: {key}")
        if not isinstance(check.get("note"), str) or not check["note"].strip():
            raise BuildError(f"missing evidence explanation: {key}")
        files = check.get("files")
        if not isinstance(files, list):
            raise BuildError(f"invalid evidence file list: {key}")
        if check["state"] == "passed" and (not files or not isinstance(check.get("reviewer"), str) or
                                           not check["reviewer"].strip()):
            raise BuildError(f"passed check needs a reviewer and hashed evidence: {key}")
        for record in files:
            if not isinstance(record, dict):
                raise BuildError(f"invalid evidence file record: {key}")
            verified_file(path.parent, record.get("path"), record.get("sha256"))
    return checks


def qualify(work: Path, evidence: Path | None = None,
            triage: Path | None = None) -> tuple[dict, dict, dict, dict, dict]:
    inputs, result = verify_build(work)
    _, validated, manifest_sha256 = application_manifest(work)
    info = collect_show_info(work, inputs)
    rows, checked = legal_metadata(work, inputs, result, info)
    checks = evaluate_evidence(result, evidence)
    triaged = evaluate_triage(manifest_sha256, validated, triage)
    triage_status = triaged["status"] if triaged["record_sha256"] is None else "performed:" + triaged["record_sha256"]
    sbom = inventory(inputs, result, info, rows, checked, validated, manifest_sha256, triage_status)
    # Current artifacts contain no kernel, board support, bootloader or installer.
    # No external assertion may promote this userspace-only profile to firmware.
    reasons = ["experimental_userspace_profile_has_no_qualified_TC002_boot_image"]
    reasons.extend("unmet:" + key for key, check in checks.items() if check["state"] != "passed")
    reasons.extend("unmet:component_review:" + ident for ident, component in sorted(validated.items())
                   if component["review"]["decision"] != "accepted")
    reasons.extend(f"vulnerability_triage:{ident}:{status}" for ident, status in sorted(triaged["findings"].items())
                   if status in ("affected", "under_investigation"))
    report = {"schema_version": 1, "profile": inputs["profile"],
              "application_commit": inputs["application_commit"], "inputs_sha256": result["inputs_sha256"],
              "build_result_sha256": build.digest(work / "build-result.json"),
              "artifacts": result["artifacts"], "checks": checks,
              "automated_checks": {key: "passed" for key in
                                   ("frozen_inputs", "artifact_integrity", "ARM_ELF", "legal_metadata_consistency",
                                    "component_manifest_consistency")},
              "legal_evidence_sha256": checked,
              "component_manifest_sha256": manifest_sha256,
              "application_components": {ident: {"version": component["version"],
                                                 "files_sha256": component.get("files_sha256"),
                                                 "targets": component["targets"],
                                                 "review_decision": component["review"]["decision"]}
                                         for ident, component in sorted(validated.items())},
              "vulnerability_triage": triaged,
              "public_release": {"eligible": False, "reasons": reasons},
              "limitations": [INVENTORY_SCOPE, TRIAGE_LIMITATION,
                              "Hashed review records do not authenticate reviewer identity.",
                              "No hardware test, security audit or reproducibility test is performed by this tool."]}
    # Detect changes while collecting metadata; all successful outputs describe one snapshot.
    if verify_build(work) != (inputs, result):
        raise BuildError("build inputs or result changed while qualifying")
    return report, sbom, info, evidence_template(result), triage_template(manifest_sha256, validated)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output", type=Path, help="output directory (default: work/output/qualification)")
    parser.add_argument("--evidence", type=Path, help="hash-bound review records; missing checks remain unmet")
    parser.add_argument("--triage", type=Path, help="hash-bound vulnerability triage record; without it the "
                                                    "triage status is not_performed")
    parser.add_argument("--public", action="store_true", help="return exit 2 unless a public release is eligible")
    args = parser.parse_args(argv)
    try:
        work = args.work.expanduser().resolve()
        output = (args.output or work / "output/qualification").expanduser().resolve()
        protected = ("application", "external", "buildroot", "downloads", "output/target",
                     "output/images", "output/legal-info")
        if any(output.is_relative_to((work / name).resolve()) for name in protected):
            raise BuildError("qualification output must not modify build inputs or artifacts")
        names = ("inventory.cdx.json", "show-info.json", "evidence-template.json", "triage-template.json",
                 "qualification.json")
        supplied = [path.resolve() for path in (args.evidence, args.triage) if path]
        for name in names:
            target = output / name
            partial = target.with_suffix(target.suffix + ".partial")
            if target.is_symlink() or partial.exists() or partial.is_symlink():
                raise BuildError("qualification output contains a symlink or unfinished write")
            if target.resolve() in supplied:
                raise BuildError("qualification output would overwrite the supplied evidence or triage record")
        report, sbom, info, template, triage = qualify(work, args.evidence.resolve() if args.evidence else None,
                                                       args.triage.resolve() if args.triage else None)
        output.mkdir(parents=True, exist_ok=True)
        generated = (("inventory.cdx.json", sbom), ("show-info.json", info),
                     ("evidence-template.json", template), ("triage-template.json", triage))
        for name, value in generated:
            build.write_json(output / name, value)
        report["generated_files_sha256"] = {name: build.digest(output / name) for name, _ in generated}
        build.write_json(output / "qualification.json", report)
        nested = sum(len(c.get("components", [])) for c in sbom["components"])
        print(f"Verified development build; {len(sbom['components'])} inventory components, {nested} nested "
              f"application components, vulnerability triage {report['vulnerability_triage']['status']}. "
              "Public release: blocked.")
        print(output / "qualification.json")
        return 2 if args.public else 0
    except (BuildError, OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"qualification: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
