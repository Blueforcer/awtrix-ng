#!/usr/bin/env bash
# Builds the TC002 release bundle in WSL/Linux:
#
#   tools/tc002/install/build_bundle.sh OUTPUT_DIR [--commit REV | --working-tree] [--work DIR]
#                                      [--jobs N] [--mcu-release DIR | --without-mcu]
#
# OUTPUT_DIR/bundle/{bin,share,lib} + manifest.json + release.img, ready for tc002_install.py deploy:
#   bin/awtrix-linux  bin/awtrix-tc002d  bin/udhcpc  bin/dhcp-callback  bin/wpa_supplicant
#   bin/awtrix-tc002-audio-pcm (the speaker helper for awtrix_pcm)
#   share/index.html.gz (the minified web UI with its Linux-only parts, WEBUI_FULL_GZ in WebUiAsset.h)
#   share/boot.mp3  share/ca-certificates.crt
#   share/speech/voice.atts (the voice, when the commit holds assets/speech/voice.atts)
#   share/licenses.txt.gz (every licence text the release ships)
#   lib/modules/{aic8800_bsp,aic8800_fdrv}.ko, the Wi-Fi driver rebuilt without key logging
#   (tools/tc002/kernel/build_aic8800.sh; needs the tree tools/tc002/kernel/prepare-tree.sh prepares)
#   lib/modules/awtrix_pcm.ko, the speaker driver (tools/tc002/kernel/build-awtrix-pcm.sh)
#   bin/awtrix-tc002-flash, the flash helper
# plus the host-only lib/libawtrix-loader.so and lib/modules/loop.ko
# (tools/tc002/kernel/build-loop.sh), which res gets through build_res_image.py: the manifest lists
# them apart, and no release image or update package contains them. release.img is the release
# image, the squashfs of the installed release (bundle.py image, mksquashfs 4.6 or later), which
# deploy writes into the release slot and update packages carry.
# awtrix-linux links the static trimmed OpenSSL of tools/tc002/openssl; bin/udhcpc is the
# DHCP client and AP server BusyBox of tools/tc002/busybox; bin/wpa_supplicant comes from
# tools/tc002/wpa_supplicant; the CA bundle from tools/tc002/ca. Every source tarball, the base64
# header awtrix-linux compiles and the musl licence text come through TC002_DL_DIR and are checked
# against their pinned SHA-256 (tools/tc002/toolchain/env.sh).
# The sources, recipes and licence texts all come from one commit (--commit, default HEAD),
# exported with git archive into WORK/src (default $TC002_CACHE/bundle), so nothing
# outside the commit can enter the release; the version is version-g<commit> and the manifest
# counter the commit time. --working-tree builds the checkout as it is instead, for trying a
# change on a clock: the version then ends in -dirty whenever anything differs from HEAD,
# untracked files included, or git cannot read the checkout (then SOURCE_DATE_EPOCH gives the
# counter), and tc002_install.py package refuses such a bundle without --allow-dirty.
# The AWTRIX NG files come from the CMake presets in CMakePresets.json through
# cmake --install --component tc002: tc002 builds the programs with the static musl ARM toolchain
# TC002_MUSL_PREFIX; tc002-loader builds the loader, which zkgui loads, against the stock glibc with
# the kernel modules' TC002_GLIBC_PREFIX. Every component above is built; the build stops when one
# of them fails.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
. "$HERE/../toolchain/env.sh"
MUSL_VERSION=1.2.6
MUSL_URL=https://musl.libc.org/releases/musl-$MUSL_VERSION.tar.gz
MUSL_SHA256=d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a
# densaugeo/base64 1.4.0, the commit firmware/buildroot/package/awtrix-base64 pins.
BASE64_COMMIT=ac168f5fa2865de855384f6a2d61444ce7d92c27
BASE64_URL=https://github.com/Densaugeo/base64_arduino/archive/$BASE64_COMMIT/base64_arduino-$BASE64_COMMIT.tar.gz
BASE64_SHA256=acb182019e908a0d21c4a110d87a732da184daa793489a60871ed957c129b616
OUTPUT=""
WORK=$TC002_CACHE/bundle
JOBS=$(nproc)
REV=HEAD
FROM_TREE=0
MCU_RELEASE=""
WITH_MCU=1
while [ $# -gt 0 ]; do
  case "$1" in
    --commit) REV=${2:?--commit needs a revision}; shift ;;
    --working-tree) FROM_TREE=1 ;;
    --work) WORK=${2:?--work needs a directory}; shift ;;
    --jobs) JOBS=${2:?--jobs needs a number}; shift ;;
    --mcu-release) MCU_RELEASE=${2:?--mcu-release needs a directory}; shift ;;
    --without-mcu) WITH_MCU=0 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) [ -z "$OUTPUT" ] || { echo "only one OUTPUT_DIR" >&2; exit 2; }; OUTPUT=$1 ;;
  esac
  shift
done
[ -n "$OUTPUT" ] || { sed -n '2,/^[^#]/{/^#/p}' "$0" >&2; exit 2; }
if [ "$FROM_TREE" = 1 ] && [ "$REV" != HEAD ]; then
  echo "--commit and --working-tree exclude each other" >&2
  exit 2
fi
tc002_need_musl
tc002_need_glibc

recipe_value() { sed -n "s/^$2=//p" "$1" | head -1; }

mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
SRC=$WORK/src
rm -rf "$SRC"
mkdir -p "$SRC"
DIRTY=""
if [ "$FROM_TREE" = 1 ]; then
  rsync -a --delete --exclude .git --exclude .pio --exclude site --exclude node_modules \
    --exclude '/build*' "$REPO/" "$SRC/"
  if git -C "$REPO" rev-parse --verify -q HEAD >/dev/null 2>&1; then
    COMMIT=$(git -C "$REPO" rev-parse --short=12 HEAD)
    COUNTER=$(git -C "$REPO" log -1 --format=%ct HEAD)
    [ -z "$(git -C "$REPO" status --porcelain)" ] || DIRTY=--dirty
  else
    COMMIT=unknown
    COUNTER=${SOURCE_DATE_EPOCH:?git cannot read this checkout; set SOURCE_DATE_EPOCH to the commit time}
    DIRTY=--dirty
  fi
else
  if ! git -C "$REPO" rev-parse --verify -q "$REV^{commit}" >/dev/null 2>&1; then
    echo "git cannot read $REV in $REPO; build from a checkout git can read, or pass --working-tree" >&2
    exit 2
  fi
  COMMIT=$(git -C "$REPO" rev-parse --short=12 "$REV^{commit}")
  COUNTER=$(git -C "$REPO" log -1 --format=%ct "$REV")
  git -C "$REPO" -c core.autocrlf=false archive --format=tar "$REV" | tar -x -C "$SRC"
fi
VERSION="$(tr -d '[:space:]' < "$SRC/version")-g$COMMIT${DIRTY:+-dirty}"
echo "building $VERSION (counter $COUNTER)"

# Check the MCU extension against this source snapshot's pin before the lengthy Linux build. It
# comes from the snapshot itself unless --mcu-release names another directory to try.
if [ "$WITH_MCU" = 1 ]; then
  MCU_RELEASE=${MCU_RELEASE:-$SRC/tools/tc002/mcu/release}
  python3 "$SRC/tools/tc002/mcu/bundle.py" "$MCU_RELEASE" "$WORK/mcu-patch"
fi

OPENSSL_RECIPE=$SRC/tools/tc002/openssl/build.sh
OPENSSL_OUT=$WORK/openssl/out
bash "$OPENSSL_RECIPE" "$WORK/openssl" "$OPENSSL_OUT" >/dev/null
LZMA_OUT=$WORK/lzma/out
bash "$SRC/tools/tc002/lzma/build.sh" "$WORK/lzma" "$LZMA_OUT"
JPEG_RECIPE=$SRC/tools/tc002/jpeg/build.sh
JPEG_OUT=$WORK/jpeg/out
bash "$JPEG_RECIPE" "$WORK/jpeg" "$JPEG_OUT"

DEPS=$WORK/deps
BASE64_TARBALL=$(tc002_fetch "base64_arduino-$BASE64_COMMIT.tar.gz" "$BASE64_URL" "$BASE64_SHA256")
rm -rf "$DEPS"
mkdir -p "$DEPS/base64/src"
tar -xzOf "$BASE64_TARBALL" "base64_arduino-$BASE64_COMMIT/src/base64.hpp" > "$DEPS/base64/src/base64.hpp"

BUILD=$WORK/build-arm
rm -rf "$BUILD"
(cd "$SRC" && cmake --preset tc002 -B "$BUILD" -DBUILD_TESTING=OFF -DAWTRIX_DEPS="$DEPS" \
  -DAWTRIX_RELEASE_VERSION="$VERSION" -DOPENSSL_INCLUDE_DIR="$OPENSSL_OUT/include" \
  -DOPENSSL_CRYPTO_LIBRARY="$OPENSSL_OUT/lib/libcrypto.a" -DOPENSSL_SSL_LIBRARY="$OPENSSL_OUT/lib/libssl.a" \
  -DLIBLZMA_INCLUDE_DIR="$LZMA_OUT/include" -DLIBLZMA_LIBRARY="$LZMA_OUT/lib/liblzma.a" \
  -DJPEG_INCLUDE_DIR="$JPEG_OUT/include" -DJPEG_LIBRARY="$JPEG_OUT/lib/libjpeg.a") >/dev/null
cmake --build "$BUILD" --parallel "$JOBS"
if ! ninja -C "$BUILD" -t commands awtrix-linux | tail -1 | grep -qF "$OPENSSL_OUT/lib/libcrypto.a"; then
  echo "awtrix-linux did not link $OPENSSL_OUT/lib/libcrypto.a" >&2
  exit 1
fi
LOADER_BUILD=$WORK/build-loader
rm -rf "$LOADER_BUILD"
(cd "$SRC" && cmake --preset tc002-loader -B "$LOADER_BUILD") >/dev/null
cmake --build "$LOADER_BUILD" --parallel "$JOBS"

BUSYBOX_RECIPE=$SRC/tools/tc002/busybox/build.sh
BUSYBOX_VARIANT=tc002 bash "$BUSYBOX_RECIPE" "$WORK/busybox" "$WORK/busybox/busybox" >/dev/null
WPA_RECIPE=$SRC/tools/tc002/wpa_supplicant/build.sh
WPA_OUT=$WORK/wpa_supplicant/out
WPA_FILES=("$WPA_OUT/wpa_supplicant" "$WPA_OUT/licenses/wpa_supplicant.COPYING"
           "$WPA_OUT/licenses/wpa_supplicant.README" "$WPA_OUT/licenses/libnl.COPYING")
rm -rf "$WPA_OUT"
if ! WPA_VARIANT=tc002 WPA_STDOUT_DEBUG=0 bash "$WPA_RECIPE" "$WORK/wpa_supplicant" "$WPA_OUT" >/dev/null ||
   ! ls "${WPA_FILES[@]}" >/dev/null; then
  echo "$WPA_RECIPE did not produce ${WPA_FILES[*]}" >&2
  exit 1
fi
KMOD_OUT=$WORK/aic8800/out
if ! bash "$SRC/tools/tc002/kernel/build_aic8800.sh" --work "$WORK/aic8800" "$KMOD_OUT" \
       > "$WORK/aic8800.log" 2>&1; then
  tail -20 "$WORK/aic8800.log" >&2
  echo "the aic8800 modules did not build; run tools/tc002/kernel/prepare-tree.sh first" >&2
  exit 1
fi
if ! bash "$SRC/tools/tc002/kernel/build-awtrix-pcm.sh" "$WORK/awtrix_pcm" > "$WORK/awtrix_pcm.log" 2>&1; then
  tail -20 "$WORK/awtrix_pcm.log" >&2
  echo "awtrix_pcm did not build" >&2
  exit 1
fi
if ! bash "$SRC/tools/tc002/kernel/build-loop.sh" "$WORK/loop" > "$WORK/loop.log" 2>&1; then
  tail -20 "$WORK/loop.log" >&2
  echo "loop.ko did not build" >&2
  exit 1
fi
CA_OUT=$WORK/ca/out
bash "$SRC/tools/tc002/ca/build.sh" "$WORK/ca" "$CA_OUT" >/dev/null

LICENSES=$WORK/licenses
rm -rf "$LICENSES"
mkdir -p "$LICENSES"
MUSL_TARBALL=$(tc002_fetch "musl-$MUSL_VERSION.tar.gz" "$MUSL_URL" "$MUSL_SHA256")
tar -xzOf "$MUSL_TARBALL" "musl-$MUSL_VERSION/COPYRIGHT" > "$LICENSES/musl.COPYRIGHT"
OWN_FILES="the AWTRIX NG programs in bin/, share/index.html.gz, share/boot.mp3"
if [ -f "$SRC/assets/speech/voice.atts" ]; then OWN_FILES+=", share/speech/voice.atts"; fi
TEXTS=(
  --text "AWTRIX NG - PolyForm Noncommercial License 1.0.0 - $OWN_FILES" "$SRC/LICENSE.md"
  --text "cpp-httplib - MIT - in bin/awtrix-linux" "$SRC/LICENSES/MIT-cpp-httplib.txt"
  --text "CMUdict and SCOWL - English pronunciation data in bin/awtrix-linux" "$SRC/LICENSES/CMUdict-SCOWL.txt"
  --text "Berry - MIT - in bin/awtrix-linux" "$SRC/LICENSES/MIT-Berry.txt"
  --text "PubSubClient - MIT - in bin/awtrix-linux" "$SRC/LICENSES/MIT-PubSubClient.txt"
  --text "base64 (Densaugeo) - MIT - in bin/awtrix-linux" "$SRC/LICENSES/MIT-densaugeo-base64.txt"
  --text "Matrix-Fonts - upstream font licenses and provenance - in bin/awtrix-linux" "$SRC/LICENSES/MIT-Matrix-Fonts.txt"
  --text "TJpgDec (ChaN) - in bin/awtrix-linux" "$SRC/LICENSES/TJpg_Decoder.txt"
  --text "OpenSSL $(recipe_value "$OPENSSL_RECIPE" VERSION) - Apache-2.0 - in bin/awtrix-linux" "$OPENSSL_OUT/LICENSE.txt"
  --text "liblzma 5.8.4 - 0BSD - in bin/awtrix-linux" "$LZMA_OUT/LICENSE.txt"
  --text "libjpeg-turbo $(recipe_value "$JPEG_RECIPE" VERSION) - IJG, BSD-3-Clause and zlib - in bin/awtrix-linux" "$JPEG_OUT/LICENSE.txt"
  --text "bluetrum-tools - MIT - format reference for the MCU patcher" "$SRC/LICENSES/MIT-bluetrum-tools.txt"
  --text "musl $MUSL_VERSION - MIT - in every program in bin/" "$LICENSES/musl.COPYRIGHT"
  --text "AIC Bluetooth AON parameter format and defaults - Apache-2.0 - in bin/awtrix-tc002d"
  "$SRC/tools/tc002/bluetooth/NOTICE" "$SRC/LICENSES/Apache-2.0.txt"
)
if [ "$WITH_MCU" = 1 ]; then
  TEXTS+=(--text "AWTRIX TC002 MCU extension - MIT - share/mcu/extension.bin" "$WORK/mcu-patch/LICENSE.txt")
fi
BUSYBOX_VERSION=$(recipe_value "$BUSYBOX_RECIPE" VERSION)
KERNEL_COPYING=$(bash -c '. "$1" && echo "$KERNEL_SRC/COPYING"' _ "$SRC/tools/tc002/kernel/common.sh")
TEXTS+=(--text "BusyBox $BUSYBOX_VERSION - GPL-2.0 - bin/udhcpc" "$WORK/busybox/busybox-$BUSYBOX_VERSION/LICENSE"
        --text "wpa_supplicant $(recipe_value "$WPA_RECIPE" WPA_VERSION) - BSD-3-Clause - bin/wpa_supplicant"
        "$WPA_OUT/licenses/wpa_supplicant.COPYING" "$WPA_OUT/licenses/wpa_supplicant.README"
        --text "libnl $(recipe_value "$WPA_RECIPE" LIBNL_VERSION) - LGPL-2.1 - in bin/wpa_supplicant"
        "$WPA_OUT/licenses/libnl.COPYING"
        --text "aic8800 Wi-Fi driver, awtrix_pcm speaker driver and loop block driver (Linux kernel modules) - GPL-2.0 - lib/modules/"
        "$KERNEL_COPYING"
        --text "Mozilla CA certificates - MPL-2.0 - share/ca-certificates.crt" "$CA_OUT/NOTICE" "$SRC/tools/tc002/ca/MPL-2.0.txt")
python3 "$SRC/tools/tc002/licenses/compose.py" "$LICENSES/licenses.txt.gz" \
  --preamble "$SRC/tools/tc002/licenses/preamble.txt" "${TEXTS[@]}" >/dev/null

BUNDLE=$OUTPUT/bundle
rm -rf "$BUNDLE"
cmake --install "$BUILD" --component tc002 --prefix "$BUNDLE" --strip >/dev/null
cmake --install "$LOADER_BUILD" --component tc002 --prefix "$BUNDLE" --strip >/dev/null
install -m 0755 "$WORK/busybox/busybox" "$BUNDLE/bin/udhcpc"
install -m 0755 "$WPA_OUT/wpa_supplicant" "$BUNDLE/bin/wpa_supplicant"
install -m 0644 "$CA_OUT/ca-certificates.crt" "$BUNDLE/share/ca-certificates.crt"
install -m 0644 "$LICENSES/licenses.txt.gz" "$BUNDLE/share/licenses.txt.gz"
if [ "$WITH_MCU" = 1 ]; then
  python3 "$SRC/tools/tc002/mcu/bundle.py" "$WORK/mcu-patch" "$BUNDLE/share/mcu"
fi
mkdir -p "$BUNDLE/lib/modules"
install -m 0644 "$KMOD_OUT/aic8800_bsp.ko" "$BUNDLE/lib/modules/aic8800_bsp.ko"
install -m 0644 "$KMOD_OUT/aic8800_fdrv.ko" "$BUNDLE/lib/modules/aic8800_fdrv.ko"
install -m 0644 "$WORK/awtrix_pcm/awtrix_pcm.ko" "$BUNDLE/lib/modules/awtrix_pcm.ko"
install -m 0644 "$WORK/loop/loop.ko" "$BUNDLE/lib/modules/loop.ko"

for binary in "$BUNDLE"/bin/*; do
  "${TC002_MUSL_PREFIX}strip" "$binary"
  header=$("${TC002_MUSL_PREFIX}readelf" -h -l -A "$binary")
  if ! grep -q 'Machine: *ARM' <<<"$header" || ! grep -q 'Tag_ABI_VFP_args: VFP registers' <<<"$header" ||
     grep -q 'INTERP' <<<"$header"; then
    echo "$binary is not a static ARM hard-float executable" >&2
    exit 1
  fi
done

python3 "$SRC/tools/tc002/install/bundle.py" manifest "$BUNDLE" --version "$VERSION" --commit "$COMMIT" \
  --counter "$COUNTER" $DIRTY --compiler "musl=${TC002_MUSL_PREFIX}gcc" --compiler "glibc=${TC002_GLIBC_PREFIX}gcc"
python3 "$SRC/tools/tc002/install/bundle.py" image "$BUNDLE"
( cd "$BUNDLE" && find . -type f -printf '%10s  %p\n' | sort -k2 )
