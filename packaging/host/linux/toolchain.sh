#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
ARCH=${1:-}
case "$ARCH" in
  x64) TARGET=("BR2_x86_64=y" "BR2_x86_x86_64=y"); TUPLE=x86_64-buildroot-linux-musl ;;
  arm64) TARGET=("BR2_aarch64=y" "BR2_cortex_a53=y"); TUPLE=aarch64-buildroot-linux-musl ;;
  *) TUPLE= ;;
esac
if [ $# -ne 2 ] || [ -z "$TUPLE" ] || [ -z "${BR2_DL_DIR:-}" ]; then
  echo "usage: BR2_DL_DIR=DIR toolchain.sh x64|arm64 WORKDIR  (static musl GCC 14 toolchain from the pinned Buildroot" >&2
  echo "  into WORKDIR/toolchain-ARCH; DIR holds buildroot-VERSION.tar.xz and Buildroot's package downloads)" >&2
  exit 2
fi
mkdir -p "$2"
WORK=$(cd "$2" && pwd)
OUT=$WORK/toolchain-$ARCH
read -r BR_VERSION BR_SHA256 HEADERS < <(python3 -I -c '
import json, sys
lock = json.load(open(sys.argv[1]))
print(lock["buildroot"]["version"], lock["buildroot"]["sha256"], lock["linux_headers"]["version"])
' "$ROOT/tools/system/sources.lock.json")
TARBALL=$BR2_DL_DIR/buildroot-$BR_VERSION.tar.xz
[ -f "$TARBALL" ] || { echo "toolchain.sh: missing $TARBALL" >&2; exit 1; }
[ "$(sha256sum < "$TARBALL" | cut -d' ' -f1)" = "$BR_SHA256" ] ||
  { echo "toolchain.sh: $TARBALL does not match its pin" >&2; exit 1; }

DEFCONFIG=$(printf '%s\n' "${TARGET[@]}" \
  BR2_TOOLCHAIN_BUILDROOT_MUSL=y BR2_GCC_VERSION_14_X=y BR2_TOOLCHAIN_BUILDROOT_CXX=y \
  BR2_KERNEL_HEADERS_VERSION=y "BR2_DEFAULT_KERNEL_VERSION=\"$HEADERS\"" BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_6_12=y \
  BR2_DOWNLOAD_FORCE_CHECK_HASHES=y "BR2_GLOBAL_PATCH_DIR=\"$ROOT/firmware/buildroot/patches\"" \
  BR2_INIT_NONE=y BR2_SYSTEM_BIN_SH_NONE=y "# BR2_PACKAGE_BUSYBOX is not set" "# BR2_TARGET_ROOTFS_TAR is not set")
KEY=$({ cat "${BASH_SOURCE[0]}"; echo "$BR_SHA256"; echo "$DEFCONFIG"; cat "$ROOT/firmware/buildroot/patches/linux-headers/$HEADERS/linux-headers.hash"; } |
  sha256sum | cut -d' ' -f1)
if [ -x "$OUT/host/bin/$TUPLE-g++" ] && [ "$(cat "$OUT/.recipe" 2>/dev/null)" = "$KEY" ]; then
  echo "$OUT/host/bin/$TUPLE-"
  exit 0
fi

rm -rf "$OUT"
mkdir -p "$OUT/buildroot"
tar -xJf "$TARBALL" -C "$OUT/buildroot" --strip-components 1
printf '%s\n' "$DEFCONFIG" > "$OUT/defconfig"
export LC_ALL=C TZ=UTC
if ! { make -C "$OUT/buildroot" O="$OUT" BR2_DEFCONFIG="$OUT/defconfig" defconfig &&
       make -C "$OUT/buildroot" O="$OUT" toolchain; } >"$OUT/build.log" 2>&1; then
  tail -40 "$OUT/build.log" >&2
  exit 1
fi
[ -x "$OUT/host/bin/$TUPLE-g++" ] || { echo "toolchain.sh: no $TUPLE-g++ in $OUT/host/bin" >&2; exit 1; }
rm -rf "$OUT/buildroot" "$OUT/build"
echo "$KEY" > "$OUT/.recipe"
echo "$OUT/host/bin/$TUPLE-"
