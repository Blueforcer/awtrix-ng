#!/usr/bin/env bash
# Static libjpeg for awtrix-linux: decodes pictures from the internet, baseline and progressive.
# libjpeg-turbo 3.2.0 is under the IJG license, BSD-3-Clause and zlib (LICENSE.md, README.ijg).
set -euo pipefail
. "$(dirname "$0")/../toolchain/env.sh"
tc002_need_musl
VERSION=3.2.0
SHA256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e
URL="https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$VERSION/libjpeg-turbo-$VERSION.tar.gz"
WORK=${1:?usage: build.sh WORKDIR [OUTDIR]}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
OUT=${2:-$WORK/out}
ARCHIVE=$(tc002_fetch "libjpeg-turbo-$VERSION.tar.gz" "$URL" "$SHA256")
if [ ! -d "$WORK/libjpeg-turbo-$VERSION" ]; then tar -xzf "$ARCHIVE" -C "$WORK"; fi
cmake -S "$WORK/libjpeg-turbo-$VERSION" -B "$WORK/build" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
  -DCMAKE_C_COMPILER="${TC002_MUSL_PREFIX}gcc" -DCMAKE_INSTALL_PREFIX="$OUT" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_C_FLAGS="-Os -ffunction-sections -fdata-sections -ffile-prefix-map=$WORK=jpeg" \
  -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF \
  -DWITH_SIMD=OFF -DWITH_ARITH_ENC=OFF \
  > "$WORK/build.log" 2>&1
cmake --build "$WORK/build" --parallel 6 >> "$WORK/build.log" 2>&1
cmake --install "$WORK/build" >> "$WORK/build.log" 2>&1
cat "$WORK/libjpeg-turbo-$VERSION/LICENSE.md" "$WORK/libjpeg-turbo-$VERSION/README.ijg" > "$OUT/LICENSE.txt"
chmod 0644 "$OUT/LICENSE.txt"
