"""Run real application/security/update contracts on a verified ARM userspace.

QEMU user emulation exercises software; it never establishes TC002 hardware
support. Native MQTT, TLS and signing fixtures must be installed and may not skip.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile

import build
import qualify

REPO = Path(__file__).resolve().parents[2]
TEST_INPUTS = ("tools/system/run_arm_contracts.py", "tools/system/build.py", "tools/system/qualify.py",
               "tests/linux/test_contract.py", "tests/linux/test_security.py", "tests/linux/test_headers.py",
               "scripts/test_dynamic_gif_api.py",
               "tests/update/test_update.py", "tools/update/package.py", "tests/update/test_faults.cpp")


def runtime_files(work: Path) -> dict[str, str]:
    """Bind the emulated loader, libraries and executables to the rootfs artifact.

    No extraction or ownership changes occur. Buildroot's target-only warning
    file is the sole permitted extra file; it is deliberately absent from tar.
    """
    expected = {}
    seen = set()
    with tarfile.open(qualify.regular_file(work, "output/images/rootfs.tar")) as archive:
        for member in archive:
            name = member.name.removeprefix("./")
            path = PurePosixPath(name)
            if name == "." and member.isdir():
                continue
            if (not name or path.is_absolute() or ".." in path.parts or "\\" in name or
                    path.as_posix() != name or name in seen):
                raise build.BuildError("invalid or duplicate rootfs entry: " + member.name)
            seen.add(name)
            if member.isdir():
                continue
            if member.issym():
                expected[name] = "symlink:" + member.linkname
            elif member.isfile():
                checksum = hashlib.sha256()
                with archive.extractfile(member) as stream:
                    while chunk := stream.read(1024 * 1024):
                        checksum.update(chunk)
                expected[name] = checksum.hexdigest()
            else:
                raise build.BuildError("unsupported entry in userspace rootfs: " + name)
    target = work / "output/target"
    if target.is_symlink() or not target.is_dir():
        raise build.BuildError("emulation sysroot must be a real directory")
    actual = build.tree_hashes(target, allow_links=True)
    marker = "THIS_IS_NOT_YOUR_ROOT_FILESYSTEM"
    if marker not in expected and marker in actual and not actual[marker].startswith("symlink:"):
        del actual[marker]
    if actual != expected:
        raise build.BuildError("emulation sysroot differs from the verified rootfs archive")
    for relative in ("usr/bin/awtrix-linux", "usr/bin/awtrix-update-verify"):
        binary = qualify.regular_file(target, relative)
        with binary.open("rb") as stream:
            header = stream.read(20)
        if not build.is_arm_elf(header):
            raise build.BuildError("emulated executable is not ARM 32-bit ELF: " + relative)
    return actual


def test_inputs() -> dict[str, str]:
    return {name: build.digest(qualify.regular_file(REPO, name)) for name in TEST_INPUTS}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-arm")
    args = parser.parse_args(argv)
    started = False
    try:
        if sys.platform != "linux" or os.geteuid() == 0:
            raise build.BuildError("contracts require a non-root Linux user")
        qemu = shutil.which(args.qemu)
        if not qemu or any(not shutil.which(tool) for tool in ("openssl", "mosquitto")):
            raise build.BuildError("qemu-arm, openssl and mosquitto are required")
        qemu = str(Path(qemu).resolve())
        work = args.work.expanduser().resolve()
        inputs, result = qualify.verify_build(work)
        # A failed rerun must not leave a previous success looking current.
        build.write_json(work / "arm-contracts.json", {"schema_version": 1, "status": "running"})
        started = True
        runtime = runtime_files(work)
        scripts = test_inputs()
        target = work / "output/target"
        binary = target / "usr/bin/awtrix-linux"
        verifier = target / "usr/bin/awtrix-update-verify"
        verifier_hash = build.digest(verifier)
        compiler = work / "output/host/bin/arm-buildroot-linux-musleabihf-g++"
        compiler_hash = build.digest(compiler)
        emulator_hash = build.digest(Path(qemu))
        with tempfile.TemporaryDirectory(prefix="awtrix-arm-contracts-") as folder:
            temporary = Path(folder)
            wrapper = temporary / "awtrix-linux"
            wrapper.write_text("#!/usr/bin/env python3\nimport os\nos.execv(" + repr(qemu) + ", " +
                               repr([qemu, "-L", str(target), str(binary)]) + " + __import__('sys').argv[1:])\n",
                               encoding="utf-8")
            wrapper.chmod(0o700)
            fault_library = temporary / "update-test-faults.so"
            subprocess.run([str(compiler), "-shared", "-fPIC", "-O2", "-o", str(fault_library),
                            str(REPO / "tests/update/test_faults.cpp"), "-ldl"], check=True)
            fault_hash = build.digest(fault_library)
            contracts = REPO / "tests/linux/test_contract.py"
            subprocess.run([sys.executable, str(contracts), "--binary", str(wrapper),
                            "--webui", str(work / "application/webui/index.html"), "--require-mqtt",
                            "--require-tls"], check=True)
            subprocess.run([sys.executable, str(REPO / "tests/linux/test_security.py"),
                            "--binary", str(wrapper), "--webui", str(work / "application/webui/index.html"),
                            "--require-tools"], check=True)
            subprocess.run([sys.executable, str(REPO / "tests/linux/test_headers.py"),
                            "--binary", str(wrapper), "--webui", str(work / "application/webui/index.html")], check=True)
            subprocess.run([sys.executable, str(REPO / "tests/update/test_update.py"),
                            "--verifier", str(verifier), "--faults", str(fault_library),
                            "--qemu", qemu, "--sysroot", str(target)], check=True)
        if (qualify.verify_build(work) != (inputs, result) or runtime_files(work) != runtime or
                test_inputs() != scripts or build.digest(compiler) != compiler_hash or
                build.digest(Path(qemu)) != emulator_hash):
            raise build.BuildError("build result changed during emulation tests")
        build.write_json(work / "arm-contracts.json", {
            "schema_version": 1, "status": "passed", "application_commit": result["application_commit"],
            "application_sha256": build.digest(binary), "update_verifier_sha256": verifier_hash,
            "inputs_sha256": result["inputs_sha256"], "artifacts": result["artifacts"],
            "sysroot_files_sha256": hashlib.sha256(json.dumps(runtime, sort_keys=True).encode()).hexdigest(),
            "test_inputs_sha256": scripts, "fault_fixture_sha256": fault_hash,
            "compiler_sha256": compiler_hash, "emulator_sha256": emulator_hash,
            "runner_sha256": scripts["tools/system/run_arm_contracts.py"],
            "emulator": subprocess.check_output([qemu, "--version"], text=True).splitlines()[0],
            "tc002_hardware_verified": False,
        })
        print("ARM application, security and update contracts passed under userspace emulation")
        return 0
    except (build.BuildError, qualify.BuildError, OSError, ValueError, KeyError,
            tarfile.TarError, subprocess.CalledProcessError) as error:
        if started:
            build.write_json(work / "arm-contracts.json", {"schema_version": 1, "status": "failed"})
        print(f"ARM contracts: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
