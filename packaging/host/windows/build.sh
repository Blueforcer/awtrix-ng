#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: AWTRIX_DL_DIR=DIR AWTRIX_CA_DIR=DIR build.sh OUTDIR}
CA_DIR=${AWTRIX_CA_DIR:?set AWTRIX_CA_DIR to the output folder of tools/tc002/ca/build.sh (ca-certificates.crt, NOTICE)}
CA_SHA256=3d664e3fa39e5d3e95f653127b067ef02e5d450112b2f4d65f1ad89b0a62a506
MAKENSIS=${MAKENSIS:-$LOCALAPPDATA/tauri/NSIS/makensis.exe}
DEPS=${AWTRIX_WIN_DEPS:-$OUT/deps}
VERSION=$(tr -d ' \r\n' < "$ROOT/version")

for tool in gcc g++ cmake ninja cygpath sha256sum; do
  command -v "$tool" >/dev/null || { echo "build.sh: $tool must be on PATH (Git Bash, WinLibs MinGW-w64 UCRT)" >&2; exit 1; }
done
[ -x "$MAKENSIS" ] || { echo "build.sh: NSIS 3 not found at $MAKENSIS (set MAKENSIS)" >&2; exit 1; }
[ -f "$ROOT/.pio/libdeps/native/base64/src/base64.hpp" ] || { echo "build.sh: run pio pkg install -e native first" >&2; exit 1; }
[ "$(sha256sum "$CA_DIR/ca-certificates.crt" | cut -d' ' -f1)" = "$CA_SHA256" ] ||
  { echo "build.sh: $CA_DIR/ca-certificates.crt is not the pinned Mozilla bundle" >&2; exit 1; }
[ -f "$CA_DIR/NOTICE" ] || { echo "build.sh: missing $CA_DIR/NOTICE" >&2; exit 1; }

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
bash "$HERE/deps.sh" "$DEPS"
DEPS=$(cd "$DEPS" && pwd)
PREFIX=$(cygpath -m "$DEPS")

cmake -S "$ROOT" -B "$OUT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAWTRIX_HOST_ONLY=ON -DBUILD_TESTING=OFF \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DOPENSSL_ROOT_DIR="$PREFIX" -DOPENSSL_USE_STATIC_LIBS=TRUE \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DAWTRIX_DEPS="$(cygpath -m "$ROOT/.pio/libdeps/native")" > "$OUT/configure.log"
cmake --build "$OUT/build" --target awtrix-host awtrix-script-runner

STAGE=$OUT/stage
rm -rf "$STAGE"
mkdir -p "$STAGE/webui" "$STAGE/licenses"
cp "$OUT/build/awtrix-host.exe" "$OUT/build/awtrix-script-runner.exe" "$CA_DIR/ca-certificates.crt" "$STAGE/"
cp "$ROOT/webui/index.html" "$STAGE/webui/"
cp "$ROOT/LICENSE.md" "$STAGE/licenses/LICENSE.md"
{ printf '\357\273\277'; cat "$ROOT/LICENSE.md"; } > "$STAGE/license-page.txt"
for text in MIT-cpp-httplib MIT-Berry MIT-PubSubClient MIT-densaugeo-base64 MIT-Matrix-Fonts TJpg_Decoder \
            MIT-mingw-w64-winpthreads; do
  cp "$ROOT/LICENSES/$text.txt" "$STAGE/licenses/"
done
cp "$DEPS/licenses/OpenSSL.txt" "$DEPS/licenses/libjpeg-turbo.txt" "$STAGE/licenses/"
cp "$CA_DIR/NOTICE" "$STAGE/licenses/Mozilla-CA-NOTICE.txt"

SETUP=$OUT/AWTRIX-Script-Host-windows-x64.exe
MSYS2_ARG_CONV_EXCL='*' "$MAKENSIS" -V2 "-DVERSION=$VERSION" "-DSTAGE=$(cygpath -w "$STAGE")" \
  "-DOUTFILE=$(cygpath -w "$SETUP")" "$(cygpath -w "$HERE/installer.nsi")"
( cd "$OUT" && ls -l "$(basename "$SETUP")" stage/*.exe && sha256sum "$(basename "$SETUP")" )
