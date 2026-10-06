#!/usr/bin/env bash
# Static liblzma for the local MCU patch helper. XZ 5.8.4 liblzma is 0BSD.
set -euo pipefail
. "$(dirname "$0")/../toolchain/env.sh"
tc002_need_musl
VERSION=5.8.4
SHA256=4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b
URL="https://github.com/tukaani-project/xz/releases/download/v$VERSION/xz-$VERSION.tar.xz"
WORK=${1:?usage: build.sh WORKDIR [OUTDIR]}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
OUT=${2:-$WORK/out}
ARCHIVE=$(tc002_fetch "xz-$VERSION.tar.xz" "$URL" "$SHA256")
if [ ! -d "$WORK/xz-$VERSION" ]; then tar -xJf "$ARCHIVE" -C "$WORK"; fi
cmake -S "$WORK/xz-$VERSION" -B "$WORK/build" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
  -DCMAKE_C_COMPILER="${TC002_MUSL_PREFIX}gcc" -DCMAKE_INSTALL_PREFIX="$OUT" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_C_FLAGS="-Os -ffunction-sections -fdata-sections -ffile-prefix-map=$WORK=lzma" \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DXZ_THREADS=no -DXZ_NLS=OFF -DXZ_DOC=OFF \
  -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF \
  > "$WORK/build.log" 2>&1
cmake --build "$WORK/build" --parallel 6 >> "$WORK/build.log" 2>&1
cmake --install "$WORK/build" >> "$WORK/build.log" 2>&1
install -m 0644 "$WORK/xz-$VERSION/COPYING.0BSD" "$OUT/LICENSE.txt"
