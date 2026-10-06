#!/bin/bash
# Builds loop.ko, the kernel's loop block driver, from the pinned kernel source for the stock
# TC002 kernel, which has no loop support: the loader mounts the release slot in res through it.
#   build-loop.sh [OUT_DIR]   (default $TC002_CACHE/loop)
set -eu
. "$(dirname "$0")/common.sh"
tc002_need_glibc

out=${1:-$TC002_CACHE/loop}
src=$out/src
rm -rf "$out"
mkdir -p "$src"
cp "$KERNEL_SRC/drivers/block/loop.c" "$KERNEL_SRC/drivers/block/loop.h" "$src/"
printf 'obj-m := loop.o\nccflags-y := -DCONFIG_BLK_DEV_LOOP_MIN_COUNT=1\n' > "$src/Makefile"
"$TC002_KERNEL_DIR/build-module.sh" "$src" "$out/build"
ko=$out/build/loop.ko

"${TC002_GLIBC_PREFIX}nm" -u "$ko" | awk '{ print $2 }' | sort -u > "$out/imports.txt"
awk '{ print $2 }' "$KERNEL_OUT/Module.symvers" | sort -u > "$out/kernel-exports.txt"
unresolved=$(comm -23 "$out/imports.txt" "$out/kernel-exports.txt")
if [ -n "$unresolved" ]; then
	echo "unresolved imports: $unresolved" >&2
	exit 1
fi
if [ -n "${TC002_STOCK_MODULES:-}" ] && [ -n "${TC002_STOCK_IMAGE:-}" ]; then
	for m in "$TC002_STOCK_MODULES"/*.ko; do
		"${TC002_GLIBC_PREFIX}nm" -u "$m" | awk '{ print $2 }'
	done | sort -u > "$out/stock-kernel-imports.txt"
	python3 "$TC002_KERNEL_DIR/check-stock-exports.py" "$TC002_STOCK_IMAGE" \
		"$out/stock-kernel-imports.txt" "$out/imports.txt"
fi
cp "$ko" "$out/loop.ko"
echo "loop.ko: $(wc -l < "$out/imports.txt") kernel imports, $(stat -c %s "$out/loop.ko") bytes"
