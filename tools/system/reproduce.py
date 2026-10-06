"""Compare two clean offline builds in one immutable local Docker image.

This checks userspace reproducibility for one snapshot, never TC002 hardware.
Sources must already be cached by a successful build and legal-info collection.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import build
import qualify


ARTIFACTS = set(qualify.ARTIFACTS)
SHA256 = re.compile(r"[0-9a-f]{64}")
IMAGE_ID = re.compile(r"sha256:[0-9a-f]{64}")
LOCK = Path(__file__).with_name("host-packages.lock")
LINE = re.compile(r"(\S+)\t(\S+)")


def parse_packages(text: str, name: str) -> dict[str, str]:
    """Parse raw dpkg-query -W output: one name<TAB>version entry per line."""
    packages = {}
    for number, line in enumerate(text.split("\n"), 1):
        if not line:
            continue
        match = LINE.fullmatch(line)
        if match is None or match[1] in packages:
            raise build.BuildError(f"invalid host package entry in {name} at line {number}")
        packages[match[1]] = match[2]
    if not packages:
        raise build.BuildError(f"empty host package inventory: {name}")
    return packages


def check_host_packages(inventory: str, lock: str) -> None:
    actual = parse_packages(inventory, "image inventory")
    expected = parse_packages(lock, "host-packages.lock")
    missing = sorted(set(expected) - set(actual))
    unexpected = sorted(set(actual) - set(expected))
    changed = sorted(f"{name} {expected[name]}->{actual[name]}" for name in set(expected) & set(actual)
                     if expected[name] != actual[name])
    if missing or unexpected or changed:
        def bounded(items):
            return ", ".join(items[:10] + (["..."] if len(items) > 10 else []))
        raise build.BuildError("host package inventory differs from host-packages.lock: "
                               f"missing [{bounded(missing)}], unexpected [{bounded(unexpected)}], "
                               f"changed [{bounded(changed)}]")


def resolve_image(image: str) -> str:
    if not image or image.startswith("-"):
        raise build.BuildError("invalid image")
    image_id = subprocess.check_output(
        ["docker", "image", "inspect", "--format", "{{.Id}}", image], text=True).strip()
    if not IMAGE_ID.fullmatch(image_id):
        raise build.BuildError("expected an immutable local Docker image ID")
    return image_id


def capture_inventory(image_id: str) -> str:
    return subprocess.check_output([
        "docker", "run", "--rm", "--network", "none", "--read-only",
        "--entrypoint", "/usr/bin/dpkg-query", image_id, "-W"], text=True)


def compare_with_lock(inventory: bytes) -> dict:
    """Report, without refusing, whether a recorded inventory equals the committed lock."""
    lock = LOCK.read_bytes()
    try:
        check_host_packages(inventory.decode("utf-8"), lock.decode("utf-8"))
        matches = True
    except (build.BuildError, UnicodeDecodeError):
        matches = False
    return {"lock_sha256": hashlib.sha256(lock).hexdigest(), "inventory_matches": matches}


def check_host_lock(image: str) -> dict:
    image_id = resolve_image(image)
    inventory = capture_inventory(image_id)
    check_host_packages(inventory, LOCK.read_text(encoding="utf-8"))
    return {"image_id": image_id, "host_packages_lock_sha256": build.digest(LOCK),
            "packages": len(parse_packages(inventory, "image inventory"))}


def validate_result(result: dict) -> None:
    if (not isinstance(result, dict) or result.get("schema_version") != 1 or result.get("status") != "crossbuild_passed" or
            result.get("profile") != "awtrix_armv7_defconfig" or
            not isinstance(result.get("application_commit"), str) or
            not re.fullmatch(r"[0-9a-f]{40}", result["application_commit"])):
        raise build.BuildError("both independent builds must identify a successful ARM development build")
    for key in ("inputs_sha256", "config_sha256"):
        if not isinstance(result.get(key), str) or not SHA256.fullmatch(result[key]):
            raise build.BuildError(f"invalid independent build identity: {key}")
    artifacts = result.get("artifacts")
    if not isinstance(artifacts, dict) or set(artifacts) != ARTIFACTS:
        raise build.BuildError("independent builds must contain exactly the two expected artifacts")
    for artifact in artifacts.values():
        if (not isinstance(artifact, dict) or type(artifact.get("bytes")) is not int or artifact["bytes"] <= 0 or
                not isinstance(artifact.get("sha256"), str) or not SHA256.fullmatch(artifact["sha256"])):
            raise build.BuildError("invalid independent artifact hash or size")


def compare(first: dict, second: dict) -> dict:
    for result in (first, second):
        validate_result(result)
    for key in ("application_commit", "profile", "inputs_sha256", "config_sha256"):
        if first.get(key) != second.get(key) or not first.get(key):
            raise build.BuildError(f"independent build inputs differ: {key}")
    if first["artifacts"] != second["artifacts"]:
        raise build.BuildError("independent build artifact hashes or sizes differ")
    return first["artifacts"]


def verify_source(source: Path, expected_inputs_sha256: str) -> dict:
    if not isinstance(expected_inputs_sha256, str) or not SHA256.fullmatch(expected_inputs_sha256):
        raise build.BuildError("a trusted original inputs.json SHA-256 is required")
    qualify.verified_file(source, "inputs.json", expected_inputs_sha256)
    inputs = build.verify(source)
    qualify.verified_file(source, "inputs.json", expected_inputs_sha256)
    return inputs


def verify_output(work: Path, expected_inputs: dict, expected_inputs_sha256: str) -> dict:
    if work.is_symlink() or not work.is_dir():
        raise build.BuildError("independent build directory must be a real directory")
    qualify.verified_file(work, "inputs.json", expected_inputs_sha256)
    inputs, result = qualify.verify_build(work)
    if inputs != expected_inputs or result["inputs_sha256"] != expected_inputs_sha256:
        raise build.BuildError("independent build changed the original frozen inputs")
    validate_result(result)
    return result


def verify_run_files(destination: Path, evidence: dict, image_id: str, runner_sha256: str) -> None:
    if not isinstance(image_id, str) or not IMAGE_ID.fullmatch(image_id) or evidence.get("image_id") != image_id:
        raise build.BuildError("reproduction does not match the trusted immutable image ID")
    if evidence.get("runner_sha256") != runner_sha256:
        raise build.BuildError("reproduction does not match the trusted runner hash")
    if (evidence.get("schema_version") != 1 or evidence.get("internal_work_path") != "/work" or
            evidence.get("network_during_build") is not False):
        raise build.BuildError("unsupported reproduction process record")
    qualify.verified_file(destination, "runner/build.py", runner_sha256)
    qualify.verified_file(destination, "host-packages.txt", evidence.get("host_packages_sha256"))


def verify_existing(source: Path, destination: Path, image_id: str, inputs_sha256: str,
                    runner_sha256: str) -> dict:
    """Recheck old completed runs without repeating compilation or trusting output paths.

    Image and runner pins must come from the operator's original trusted record.
    Logs/JSON are local provenance, not authenticated evidence of Docker execution.
    """
    source, destination = source.resolve(), destination.resolve()
    expected_inputs = verify_source(source, inputs_sha256)
    original_path = qualify.regular_file(destination, "reproduction.json")
    original_sha256 = build.digest(original_path)
    original = qualify.json_file(original_path)
    if original.get("status") != "reproduced" or original.get("clean_builds") != 2:
        raise build.BuildError("original process record must describe two completed clean builds")
    if original.get("application_commit") != expected_inputs["application_commit"]:
        raise build.BuildError("original process record identifies another application")
    verify_run_files(destination, original, image_id, runner_sha256)
    first = verify_output(destination / "first", expected_inputs, inputs_sha256)
    second = verify_output(destination / "second", expected_inputs, inputs_sha256)
    artifacts = compare(first, second)
    if original.get("artifacts") != artifacts or original.get("inputs_sha256") != inputs_sha256 or \
            original.get("config_sha256") != first["config_sha256"]:
        raise build.BuildError("original reproduction summary differs from verified outputs")
    logs = {}
    for name in ("first.log", "second.log"):
        logs[name] = build.digest(qualify.regular_file(destination, name))
    lock_check = compare_with_lock((destination / "host-packages.txt").read_bytes())
    verify_source(source, inputs_sha256)
    verify_run_files(destination, original, image_id, runner_sha256)
    if build.digest(original_path) != original_sha256:
        raise build.BuildError("original process record changed during verification")
    report = dict(original, status="reproduced", bit_reproducibility_tested=True,
                  verification_schema_version=1, verification_mode="post-run-integrity-check",
                  original_record_sha256=original_sha256, source_inputs_sha256=inputs_sha256,
                  verifier_files_sha256={"reproduce.py": build.digest(Path(__file__)),
                                         "qualify.py": build.digest(Path(qualify.__file__)),
                                         "build.py": build.digest(Path(build.__file__))}, log_sha256=logs,
                  process_provenance="Trusted original local orchestrator record; no authenticated Docker execution attestation",
                  verified_outputs={"first": build.digest(destination / "first/build-result.json"),
                                    "second": build.digest(destination / "second/build-result.json")},
                  host_packages_lock_check=lock_check, tc002_hardware_verified=False, bootable_tc002_image=False)
    build.write_json(destination / "reproduction-verified.json", report)
    print(f"Existing independent builds verified: {destination / 'reproduction-verified.json'}")
    return report


def reproduce(source: Path, destination: Path, image: str, jobs: int) -> None:
    if sys.platform != "linux" or os.geteuid() == 0:
        raise build.BuildError("run as an unprivileged Linux user with local Docker access")
    if not 1 <= jobs <= 64 or not image or image.startswith("-"):
        raise build.BuildError("invalid jobs or image")
    source, destination = source.resolve(), destination.resolve()
    if destination.exists() or destination == source or source in destination.parents:
        raise build.BuildError("select a new output directory outside the prepared inputs")
    if any(c.isspace() or c == "," for c in str(destination)):
        raise build.BuildError("output path must not contain whitespace or commas")
    inputs = build.verify(source)
    inputs_sha256 = build.digest(source / "inputs.json")
    verify_source(source, inputs_sha256)
    image_id = resolve_image(image)
    # A host whose packages drifted from the lock must not bind evidence to this run.
    inventory = capture_inventory(image_id)
    check_host_packages(inventory, LOCK.read_text(encoding="utf-8"))
    destination.mkdir(parents=True)
    runner = destination / "runner"
    runner.mkdir()
    shutil.copyfile(Path(build.__file__), runner / "build.py")
    (destination / "host-packages.txt").write_bytes(inventory.encode("utf-8"))
    evidence = {
        "schema_version": 1, "status": "building", "image_id": image_id,
        "application_commit": inputs["application_commit"],
        "source_inputs_sha256": inputs_sha256,
        "runner_sha256": build.digest(runner / "build.py"),
        "orchestrator_sha256": build.digest(Path(__file__)),
        "validator_sha256": build.digest(Path(qualify.__file__)),
        "host_packages_sha256": build.digest(destination / "host-packages.txt"),
        "host_packages_lock_sha256": build.digest(LOCK),
        "internal_work_path": "/work", "network_during_build": False,
        "clean_builds": 0, "bit_reproducibility_tested": False,
        "tc002_hardware_verified": False, "bootable_tc002_image": False,
    }
    result_path = destination / "reproduction.json"
    build.write_json(result_path, evidence)
    try:
        results = []
        for name in ("first", "second"):
            verify_source(source, inputs_sha256)
            work = destination / name
            work.mkdir()
            for item in ("application", "external", "buildroot", "downloads"):
                shutil.copytree(source / item, work / item, symlinks=True)
            for item in ("inputs.json", "application.tar", build.MARKER):
                shutil.copyfile(source / item, work / item)
            # No output/toolchain/cache from the successful source build is copied.
            command = [
                "docker", "run", "--rm", "--network", "none", "--read-only",
                "--cap-drop", "ALL", "--security-opt", "no-new-privileges:true",
                "--user", f"{os.getuid()}:{os.getgid()}", "--tmpfs", "/tmp:exec,mode=1777",
                "--mount", f"type=bind,source={work},target=/work",
                "--mount", f"type=bind,source={runner},target=/runner/tools/system,readonly",
                "--entrypoint", "/usr/bin/python3", image_id,
                "/runner/tools/system/build.py", "build", "--work", "/work", "--jobs", str(jobs),
            ]
            evidence.setdefault("commands", {})[name] = command
            build.write_json(result_path, evidence)
            with (destination / f"{name}.log").open("w", encoding="utf-8") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            result = verify_output(work, inputs, inputs_sha256)
            results.append(result)
            evidence["clean_builds"] += 1
            build.write_json(result_path, evidence)
        # The first output can change while the second compiler is running.
        results = [verify_output(destination / name, inputs, inputs_sha256) for name in ("first", "second")]
        verify_source(source, inputs_sha256)
        verify_run_files(destination, evidence, image_id, evidence["runner_sha256"])
        evidence["artifacts"] = compare(*results)
        evidence.update(status="reproduced", bit_reproducibility_tested=True,
                        inputs_sha256=results[0]["inputs_sha256"],
                        config_sha256=results[0]["config_sha256"],
                        process_provenance="Trusted local orchestrator and immutable local image; no hostile-container attestation")
    except BaseException:
        evidence["status"] = "failed"
        build.write_json(result_path, evidence)
        raise
    build.write_json(result_path, evidence)
    print(f"Two clean offline builds match: {result_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, help="prepared build directory; required unless --check-host-lock")
    parser.add_argument("--output", type=Path, help="new reproduction directory; required unless --check-host-lock")
    parser.add_argument("--image", required=True)
    parser.add_argument("--jobs", type=int, help="compiler jobs per build (default: 4)")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--verify-existing", action="store_true", help="verify two completed runs without compiling")
    modes.add_argument("--check-host-lock", action="store_true",
                       help="compare the image's dpkg inventory with host-packages.lock and exit; takes only --image")
    parser.add_argument("--expected-inputs-sha256", help="trusted original inputs.json hash; required with --verify-existing")
    parser.add_argument("--expected-runner-sha256", help="trusted original runner/build.py hash; required with --verify-existing")
    args = parser.parse_args()
    try:
        if args.check_host_lock:
            if any(value is not None for value in (args.work, args.output, args.jobs, args.expected_inputs_sha256,
                                                   args.expected_runner_sha256)):
                raise build.BuildError("--check-host-lock takes only --image")
            result = check_host_lock(args.image)
            print(f"host packages match host-packages.lock: {result['host_packages_lock_sha256']} "
                  f"({result['packages']} packages)")
            return 0
        if args.work is None or args.output is None:
            raise build.BuildError("--work and --output are required")
        if args.verify_existing:
            verify_existing(args.work, args.output, args.image, args.expected_inputs_sha256, args.expected_runner_sha256)
        else:
            reproduce(args.work, args.output, args.image, 4 if args.jobs is None else args.jobs)
        return 0
    except (build.BuildError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"reproduction: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
