#!/usr/bin/env bash
# Builds the static ARM wpa_supplicant that awtrix-tc002d runs instead of the stock 2.6, plus
# wpa_cli for tests and repair over USB ADB.
#
#   tools/tc002/wpa_supplicant/build.sh WORKDIR [OUTDIR]
#
# Both source tarballs come through TC002_DL_DIR and must match the pinned SHA-256.
# wpa_supplicant.config is the complete build configuration. The compiler is TC002_MUSL_PREFIX
# (tools/tc002/toolchain/env.sh); WPA_VARIANT=host builds the same configuration natively for tests.
# WPA_STDOUT_DEBUG=1 keeps the -d debug output, for repair over USB ADB; the bundle ships without.
set -euo pipefail
. "$(dirname "$0")/../toolchain/env.sh"

WPA_VERSION=2.12
WPA_SHA256=08e23937e16d0155e55cab2b51f51fbe10d80a1aa91c4e15442645059b737ef6
WPA_URL="https://w1.fi/releases/wpa_supplicant-${WPA_VERSION}.tar.gz"
LIBNL_VERSION=3.11.0
LIBNL_SHA256=2a56e1edefa3e68a7c00879496736fdbf62fc94ed3232c0baba127ecfa76874d
LIBNL_URL="https://github.com/thom311/libnl/releases/download/libnl${LIBNL_VERSION//./_}/libnl-${LIBNL_VERSION}.tar.gz"
VARIANT=${WPA_VARIANT:-tc002}

HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${1:?usage: build.sh WORKDIR [OUTDIR]}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
OUT=${2:-$WORK/out}
case "$VARIANT" in
  tc002)
    tc002_need_musl
    CROSS="$TC002_MUSL_PREFIX"
    ;;
  host) CROSS="" ;;
  *)
    echo "unknown WPA_VARIANT $VARIANT" >&2
    exit 2
    ;;
esac
WPA_TARBALL=$(tc002_fetch "wpa_supplicant-$WPA_VERSION.tar.gz" "$WPA_URL" "$WPA_SHA256")
LIBNL_TARBALL=$(tc002_fetch "libnl-$LIBNL_VERSION.tar.gz" "$LIBNL_URL" "$LIBNL_SHA256")
CC="${CROSS}gcc"
TARGET=$("$CC" -dumpmachine)

WPA_SRC="$WORK/wpa_supplicant-${WPA_VERSION}"
LIBNL_SRC="$WORK/libnl-${LIBNL_VERSION}"
rm -rf "$WPA_SRC" "$LIBNL_SRC"
tar -xzf "$WPA_TARBALL" -C "$WORK"
tar -xzf "$LIBNL_TARBALL" -C "$WORK"

export SOURCE_DATE_EPOCH=0 LC_ALL=C TZ=UTC
SIZE_FLAGS="-Os -ffunction-sections -fdata-sections"

if ! (
  cd "$LIBNL_SRC" &&
    ./configure --host="$TARGET" CC="$CC" AR="${CROSS}ar" RANLIB="${CROSS}ranlib" STRIP="${CROSS}strip" \
      CFLAGS="$SIZE_FLAGS -ffile-prefix-map=$LIBNL_SRC=libnl-$LIBNL_VERSION" \
      --enable-static --disable-shared --disable-cli --disable-debug &&
    make -j"$(nproc)" lib/libnl-3.la lib/libnl-genl-3.la
) >"$WORK/libnl-build.log" 2>&1; then
  cat "$WORK/libnl-build.log" >&2
  exit 1
fi

cp "$HERE/wpa_supplicant.config" "$WPA_SRC/wpa_supplicant/.config"
if [ "${WPA_STDOUT_DEBUG:-0}" = 1 ]; then
  sed -i '/^CONFIG_NO_STDOUT_DEBUG=/d' "$WPA_SRC/wpa_supplicant/.config"
fi
CC="$CC" \
  CFLAGS="$SIZE_FLAGS -Wall -ffile-prefix-map=$WPA_SRC=wpa_supplicant-$WPA_VERSION" \
  LDFLAGS="-static -Wl,--gc-sections -L$LIBNL_SRC/lib/.libs" \
  LIBNL_INC="$LIBNL_SRC/include" EXTRALIBS="-lnl-3" \
  make -C "$WPA_SRC/wpa_supplicant" -s -j"$(nproc)" QUIET=1 wpa_supplicant wpa_cli

mkdir -p "$OUT/licenses"
for program in wpa_supplicant wpa_cli; do
  "${CROSS}strip" --strip-all -o "$OUT/$program" "$WPA_SRC/wpa_supplicant/$program"
  chmod 0755 "$OUT/$program"
done
install -m 0644 "$WPA_SRC/COPYING" "$OUT/licenses/wpa_supplicant.COPYING"
install -m 0644 "$WPA_SRC/README" "$OUT/licenses/wpa_supplicant.README"
install -m 0644 "$LIBNL_SRC/COPYING" "$OUT/licenses/libnl.COPYING"

for program in wpa_supplicant wpa_cli; do
  file "$OUT/$program"
  stat -c '%s bytes' "$OUT/$program"
  sha256sum "$OUT/$program"
done
