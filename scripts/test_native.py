"""Build and run the native test suite without recompiling the core per suite."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / ".pio" / "native-tests"


def executable(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found
    raise SystemExit(f"test_native: required executable not found: {name}")


def compilers() -> tuple[str, str]:
    cc = shutil.which("gcc")
    cxx = shutil.which("g++")
    if cc and cxx:
        return cc, cxx

    if sys.platform == "win32":
        kit = Path(os.environ.get("W64DEVKIT", r"D:\tools\w64devkit")) / "bin"
        cc = str(kit / "gcc.exe")
        cxx = str(kit / "g++.exe")
        if Path(cc).is_file() and Path(cxx).is_file():
            return cc, cxx

    raise SystemExit("test_native: gcc and g++ are required")


def ensure_platformio_dependencies() -> None:
    unity = ROOT / ".pio" / "libdeps" / "native" / "Unity" / "src" / "unity.c"
    base64 = ROOT / ".pio" / "libdeps" / "native" / "base64" / "src" / "base64.hpp"
    if unity.is_file() and base64.is_file():
        return
    subprocess.run(
        [executable("pio"), "pkg", "install", "-e", "native"],
        cwd=ROOT,
        check=True,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="parallel build and test jobs (default: all logical CPUs)",
    )
    parser.add_argument(
        "-R",
        "--filter",
        metavar="REGEX",
        help="run only CTest suite names matching this regular expression",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.jobs < 1:
        raise SystemExit("test_native: --jobs must be positive")

    ensure_platformio_dependencies()
    cc, cxx = compilers()
    cmake = executable("cmake")
    ninja = executable("ninja")

    subprocess.run(
        [
            cmake,
            "-S",
            str(ROOT / "test"),
            "-B",
            str(BUILD_DIR),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            f"-DCMAKE_C_COMPILER={cc}",
            f"-DCMAKE_CXX_COMPILER={cxx}",
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [cmake, "--build", str(BUILD_DIR), "--parallel", str(args.jobs)],
        cwd=ROOT,
        check=True,
    )

    test_command = [
        executable("ctest"),
        "--test-dir",
        str(BUILD_DIR),
        "--parallel",
        str(args.jobs),
        "--output-on-failure",
    ]
    if args.filter:
        test_command.extend(["-R", args.filter])
    return subprocess.run(test_command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
