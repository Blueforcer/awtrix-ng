#!/bin/bash
set -eu
. "$(dirname "$0")/common.sh"
tc002_need_glibc

update_config=0
[ "${1:-}" = "--update-config" ] && update_config=1

test "$(git -C "$KERNEL_SRC" rev-parse HEAD)" = "$KERNEL_COMMIT"
"${TC002_GLIBC_PREFIX}gcc" --version | head -1

rm -rf "$KERNEL_OUT"
mkdir -p "$KERNEL_OUT"
git -C "$KERNEL_SRC" archive --format=tar "$KERNEL_COMMIT" | tar -x -C "$KERNEL_OUT"

cat "$KERNEL_OUT/arch/arm/configs/$KERNEL_BASE_DEFCONFIG" "$TC002_KERNEL_DIR/tc002_stock.fragment" > "$KERNEL_OUT/.config"
kmake olddefconfig > "$KERNEL_OUT/olddefconfig.log" 2>&1

if [ $update_config = 1 ]; then
	cp "$KERNEL_OUT/.config" "$TC002_KERNEL_DIR/tc002_stock.config"
elif ! diff -u "$TC002_KERNEL_DIR/tc002_stock.config" "$KERNEL_OUT/.config"; then
	echo "resolved .config differs from tc002_stock.config" >&2
	exit 1
fi

kmake -j"$(nproc)" vmlinux > "$KERNEL_OUT/build.log" 2>&1 || { tail -40 "$KERNEL_OUT/build.log"; exit 1; }
kmake modules_prepare > "$KERNEL_OUT/modules_prepare.log" 2>&1 || { tail -40 "$KERNEL_OUT/modules_prepare.log"; exit 1; }

echo "prepared $KERNEL_OUT ($(grep -c . "$KERNEL_OUT/Module.symvers") exports in Module.symvers)"
