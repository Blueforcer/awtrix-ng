#!/bin/bash
set -eu
. "$(dirname "$0")/common.sh"
tc002_need_glibc

AIC_REPO=https://github.com/kendryte/k230_linux_sdk.git
AIC_COMMIT=dd73e713829ab31029e371bc3fb67b367b8ae2eb
AIC_PATH=buildroot-overlay/package/aic8800_sdio/src
AIC_TREE=3ff4a299165d4bce65bb91e6f8924a7ca3dbe9fb
AIC_RELEASE=2024_1024_9a3636f0

PATCH_DIR=$(cd "$TC002_KERNEL_DIR/../../../src/platform/tc002/kmod/aic8800" && pwd)
STOCK_PATCHES="0001-register-tdls-mgmt.patch 0002-tc002-power-and-defaults.patch 0004-fdrv-fwlog-en-is-bool.patch"
FIX_PATCHES="0003-never-log-key-material.patch"

usage() {
	echo "usage: $0 [--work <dir>] [--stock-equivalent] [out-dir]" >&2
	exit 2
}

work=$TC002_CACHE/aic8800
patches="$STOCK_PATCHES $FIX_PATCHES"
out=
while [ $# -gt 0 ]; do
	case $1 in
	--work) [ $# -ge 2 ] || usage; work=$2; shift ;;
	--stock-equivalent) patches=$STOCK_PATCHES ;;
	-*) usage ;;
	*) [ -z "$out" ] || usage; out=$1 ;;
	esac
	shift
done
AIC_SRC=$work/src/k230-${AIC_COMMIT:0:8}
out=${out:-$work/out}

test -f "$KERNEL_OUT/Module.symvers"

if [ ! -d "$AIC_SRC/.git" ]; then
	mkdir -p "$AIC_SRC"
	git -C "$AIC_SRC" init -q
	git -C "$AIC_SRC" remote add origin "$AIC_REPO"
fi
if [ "$(git -C "$AIC_SRC" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$AIC_COMMIT" ]; then
	git -C "$AIC_SRC" fetch -q --depth 1 --filter=blob:none origin "$AIC_COMMIT"
	git -C "$AIC_SRC" sparse-checkout set --no-cone "/$AIC_PATH/"
	git -C "$AIC_SRC" -c advice.detachedHead=false checkout -q FETCH_HEAD
fi
test "$(git -C "$AIC_SRC" rev-parse HEAD)" = "$AIC_COMMIT"
test "$(git -C "$AIC_SRC" rev-parse "HEAD:$AIC_PATH")" = "$AIC_TREE"
grep -q "RELEASE_DATE \"$AIC_RELEASE\"" "$AIC_SRC/$AIC_PATH/aic8800_fdrv/rwnx_version_gen.h"

rm -rf "$out"
mkdir -p "$out"
out=$(cd "$out" && pwd)
git -C "$AIC_SRC" archive --format=tar "HEAD:$AIC_PATH" aic8800_bsp aic8800_fdrv | tar -x -C "$out"
for patch in $patches; do
	patch -p1 -s --fuzz=0 -d "$out" < "$PATCH_DIR/$patch"
	echo "applied $patch"
done

printf 'obj-m += aic8800_bsp/ aic8800_fdrv/\n' > "$out/Kbuild"
for symbol in sunxi_wlan_get_bus_index sunxi_wlan_set_power sigmastar_mmc_rescan_card; do
	printf '0x00000000\t%s\tvmlinux\tEXPORT_SYMBOL\n' "$symbol"
done > "$out/tc002-kernel-extra.symvers"

kmake M="$out" KBUILD_EXTRA_SYMBOLS="$out/tc002-kernel-extra.symvers" \
	KCFLAGS="-ffile-prefix-map=$out/=drivers/net/wireless/aic8800/ -ffile-prefix-map=$KERNEL_OUT/=" \
	CONFIG_PLATFORM_TC002=y CONFIG_AIC_FW_PATH=/lib/firmware/aic8800DC \
	CONFIG_USE_P2P0=y CONFIG_PREALLOC_RX_SKB=y \
	modules > "$out/build.log" 2>&1 || { tail -40 "$out/build.log"; exit 1; }

status=0
for module in aic8800_bsp aic8800_fdrv; do
	ko=$out/$module.ko
	"${TC002_GLIBC_PREFIX}objcopy" -R .comment -R .note.gnu.build-id -R .note.GNU-stack \
		-x -G __this_module --strip-unneeded "$out/$module/$module.ko" "$ko"
	bash "$TC002_KERNEL_DIR/check-ko.sh" "$ko" || status=1
done
(cd "$out" && sha256sum aic8800_bsp.ko aic8800_fdrv.ko)
exit $status
