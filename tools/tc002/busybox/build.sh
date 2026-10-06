#!/usr/bin/env bash
# Builds the static ARM BusyBox that awtrix-tc002d runs as its DHCP client and AP server.
#
#   tools/tc002/busybox/build.sh WORKDIR [OUTPUT]
#
# The source tarball comes through TC002_DL_DIR and must match the pinned SHA-256. busybox.config
# is complete: `make oldconfig` must not change it. The compiler is TC002_MUSL_PREFIX
# (tools/tc002/toolchain/env.sh). BUSYBOX_VARIANT=host-dhcp-test builds a native binary
# for the privileged IP-layer system test (tc002d-ip-system); it is never installed on the
# device.
set -euo pipefail
. "$(dirname "$0")/../toolchain/env.sh"

VERSION=1.37.0
SHA256=3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
URL="https://busybox.net/downloads/busybox-${VERSION}.tar.bz2"
VARIANT=${BUSYBOX_VARIANT:-tc002}

HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${1:?usage: build.sh WORKDIR [OUTPUT]}
OUT=${2:-$WORK/busybox}
case "$VARIANT" in
  tc002)
    tc002_need_musl
    CROSS="$TC002_MUSL_PREFIX"
    ;;
  host-dhcp-test) CROSS="" ;;
  *)
    echo "unknown BUSYBOX_VARIANT $VARIANT" >&2
    exit 2
    ;;
esac
TARBALL=$(tc002_fetch "busybox-$VERSION.tar.bz2" "$URL" "$SHA256")

mkdir -p "$WORK"
SRC="$WORK/busybox-${VERSION}"
rm -rf "$SRC"
tar -xjf "$TARBALL" -C "$WORK"

export KCONFIG_NOTIMESTAMP=1 SOURCE_DATE_EPOCH=0 LC_ALL=C TZ=UTC
cp "$HERE/busybox.config" "$SRC/.config"
make -C "$SRC" -s oldconfig </dev/null >/dev/null
symbols() { grep -E '^(# )?CONFIG_' "$1"; }
if ! diff <(symbols "$HERE/busybox.config") <(symbols "$SRC/.config") >&2; then
  echo "busybox.config is incomplete for BusyBox $VERSION" >&2
  exit 1
fi

make -C "$SRC" -s -j"${JOBS:-$(nproc)}" CROSS_COMPILE="$CROSS" \
  EXTRA_CFLAGS="-ffile-prefix-map=$SRC=busybox-$VERSION" busybox

install -m 0755 "$SRC/busybox" "$OUT"
file "$OUT"
stat -c '%s bytes' "$OUT"
sha256sum "$OUT"
