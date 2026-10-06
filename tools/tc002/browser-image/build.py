#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

SOURCES = {
    "squashfs-tools-ng-1.3.2.tar.xz": ("https://infraroot.at/pub/squashfs/squashfs-tools-ng-1.3.2.tar.xz", "0d907ac3e735c351e47c867fb51d94bffa3b05fb95bec01f31e848b7c44215a9"),
    "xz-5.8.4.tar.xz": ("https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.xz", "4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b"),
    "emsdk-4.0.21.tar.gz": ("https://codeload.github.com/emscripten-core/emsdk/tar.gz/b2436aafa7351ee1b581f15841f1b45ed716a279", "0279973682a9830976256cd5b28692c4ea7931f2a47baf2d0b3e22e31b6d3741"),
}


def run(args, cwd, env=None):
    subprocess.run([str(x) for x in args], cwd=cwd, env=env, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sdk", type=Path)
    args = parser.parse_args()
    work, output = args.work.resolve(), args.output.resolve()
    work.mkdir(parents=True, exist_ok=True)
    downloads = work / "downloads"
    downloads.mkdir(exist_ok=True)
    for name, (url, digest) in SOURCES.items():
        archive = downloads / name
        if not archive.exists():
            with urllib.request.urlopen(url) as source, archive.open("wb") as target:
                shutil.copyfileobj(source, target)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise SystemExit(f"source digest mismatch: {name}")
        with tarfile.open(archive) as source:
            top = source.getmembers()[0].name.split("/")[0]
            if not (work / top).exists():
                source.extractall(work, filter="data")
    compat = work / "squashfs-tools-ng-1.3.2/include/compat.h"
    before = "#if defined(__GNUC__) && __GNUC__ >= 5"
    after = "#if (defined(__GNUC__) && __GNUC__ >= 5) || defined(__clang__)"
    content = compat.read_text()
    if before in content:
        compat.write_text(content.replace(before, after, 1))
    elif after not in content:
        raise SystemExit("unexpected compat.h source")
    content = compat.read_text()
    content = content.replace("#if defined(__linux__) || defined(__GLIBC__)\n", "#if defined(__linux__) || defined(__GLIBC__) || defined(__EMSCRIPTEN__)\n")
    compat.write_text(content)
    sdk = args.sdk.resolve() if args.sdk else work / "emsdk-b2436aafa7351ee1b581f15841f1b45ed716a279"
    run([sdk / "emsdk", "install", "4.0.21"], sdk)
    run([sdk / "emsdk", "activate", "4.0.21"], sdk)
    em = sdk / "upstream/emscripten"
    env = dict(os.environ, SOURCE_DATE_EPOCH="1727359200", TZ="UTC", LC_ALL="C")
    env["PATH"] = f"{em}:{sdk / 'node/22.16.0_64bit/bin'}:{env['PATH']}"
    env["EMSDK"] = str(sdk)
    env["EM_CONFIG"] = str(sdk / ".emscripten")
    prefix = work / "prefix"
    xzbuild = work / "xz-build"
    flags = f"-O2 -ffile-prefix-map={work}=/build"
    run([em / "emcmake", "cmake", "-S", work / "xz-5.8.4", "-B", xzbuild,
         f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_C_FLAGS={flags}", "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_TESTING=OFF",
         "-DXZ_THREADS=no", "-DXZ_NLS=OFF", "-DXZ_DOC=OFF", "-DXZ_TOOL_XZ=OFF",
         "-DXZ_TOOL_XZDEC=OFF", "-DXZ_TOOL_LZMADEC=OFF", "-DXZ_TOOL_LZMAINFO=OFF"], work, env)
    run(["cmake", "--build", xzbuild, "--parallel", "6"], work, env)
    run(["cmake", "--install", xzbuild], work, env)
    sqbuild = work / "sqfs-build"
    sqbuild.mkdir(exist_ok=True)
    env.update(CFLAGS=flags, XZ_CFLAGS=f"-I{prefix}/include", XZ_LIBS=f"-L{prefix}/lib -llzma")
    env["LDFLAGS"] = "-O2 -sMODULARIZE=1 -sEXPORT_ES6=1 -sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=FS,callMain -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=536870912 -sINITIAL_MEMORY=33554432 -sSTACK_SIZE=1048576 -sENVIRONMENT=web,worker,node -sEXIT_RUNTIME=0"
    run([em / "emconfigure", work / "squashfs-tools-ng-1.3.2/configure",
         "--host=wasm32-unknown-emscripten", "--disable-shared", "--enable-static",
         "--without-pthread", "--without-gzip", "--without-bzip2", "--without-lzo",
         "--without-lz4", "--without-zstd", "--without-selinux", "--with-xz"], sqbuild, env)
    run(["make", "-j6", "sqfs2tar", "tar2sqfs"], sqbuild, env)
    output.mkdir(parents=True, exist_ok=True)
    for name in ("sqfs2tar", "tar2sqfs"):
        banner = "/* SPDX-License-Identifier: GPL-3.0-or-later; see NOTICE.md and sources/build.py. */\n"
        (output / f"{name}.js").write_text(banner + (sqbuild / name).read_text(), encoding="utf-8")
        shutil.copyfile(sqbuild / f"{name}.wasm", output / f"{name}.wasm")
    sourceout = output / "sources"
    sourceout.mkdir(exist_ok=True)
    for name in SOURCES:
        shutil.copyfile(downloads / name, sourceout / name)
    shutil.copyfile(__file__, sourceout / "build.py")
    shutil.copyfile(Path(__file__).with_name("clang-size-t.patch"), sourceout / "clang-size-t.patch")
    licenses = output / "licenses"
    licenses.mkdir(exist_ok=True)
    for name in ("GPLv3.txt", "LGPLv3.txt", "xxhash.txt", "hash_table.txt", "musl.txt"):
        shutil.copyfile(work / "squashfs-tools-ng-1.3.2/licenses" / name, licenses / name)
    shutil.copyfile(work / "squashfs-tools-ng-1.3.2/COPYING.md", licenses / "squashfs-tools-ng.md")
    shutil.copyfile(work / "xz-5.8.4/COPYING", licenses / "xz.txt")
    shutil.copyfile(work / "xz-5.8.4/COPYING.0BSD", licenses / "0BSD.txt")
    shutil.copyfile(em / "LICENSE", licenses / "emscripten.txt")
    shutil.copyfile(em / "system/lib/libc/musl/COPYRIGHT", licenses / "emscripten-musl.txt")
    shutil.copyfile(em / "system/lib/compiler-rt/LICENSE.TXT", licenses / "compiler-rt.txt")
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(output.iterdir()) if p.stem in ("sqfs2tar", "tar2sqfs") and p.suffix in (".wasm", ".js")}
    (output / "build.json").write_text(json.dumps({"sources": SOURCES, "artifacts": hashes}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
