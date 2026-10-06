#!/bin/bash
set -eu
. "$(dirname "$0")/common.sh"
tc002_need_glibc
tc002_need_musl

repo=$(cd "$TC002_KERNEL_DIR/../../.." && pwd)
src=$repo/src/platform/tc002/kmod/awtrix_pcm
out=${1:-$TC002_CACHE/awtrix_pcm}
stock_modules=${TC002_STOCK_MODULES:-}

"$TC002_KERNEL_DIR/build-module.sh" "$src" "$out"
ko=$out/awtrix_pcm.ko

"${TC002_GLIBC_PREFIX}nm" -u "$ko" | awk '{ print $2 }' | sort -u > "$out/imports.txt"
awk '{ print $2 }' "$KERNEL_OUT/Module.symvers" | sort -u > "$out/kernel-exports.txt"
awk '{ print $2 }' "$src/mhal.symvers" | sort -u > "$out/mhal-exports.txt"
comm -23 "$out/imports.txt" "$out/kernel-exports.txt" > "$out/imports-not-kernel.txt"
unresolved=$(comm -23 "$out/imports-not-kernel.txt" "$out/mhal-exports.txt")
if [ -n "$unresolved" ]; then
	echo "unresolved imports: $unresolved" >&2
	exit 1
fi
if ! cmp -s "$out/imports-not-kernel.txt" "$out/mhal-exports.txt"; then
	echo "MHAL imports differ from mhal.symvers:" >&2
	diff "$out/mhal-exports.txt" "$out/imports-not-kernel.txt" >&2 || true
	exit 1
fi
depends=$("${TC002_GLIBC_PREFIX}objcopy" -O binary -j .modinfo "$ko" /dev/stdout | tr '\0' '\n' | sed -n 's/^depends=//p')
[ "$depends" = mhal ] || { echo "depends=$depends, expected mhal" >&2; exit 1; }
echo "imports: $(wc -l < "$out/imports.txt") symbols, MHAL: $(tr '\n' ' ' < "$out/mhal-exports.txt")"

if [ -n "$stock_modules" ]; then
	"${TC002_GLIBC_PREFIX}objcopy" -O binary -j __ksymtab_strings "$stock_modules/mhal.ko" /dev/stdout |
		tr '\0' '\n' | sed '/^$/d' | sort -u > "$out/stock-mhal-exports.txt"
	missing=$(comm -23 "$out/mhal-exports.txt" "$out/stock-mhal-exports.txt")
	[ -z "$missing" ] || { echo "not exported by the stock mhal.ko: $missing" >&2; exit 1; }
	echo "all MHAL imports are exported by the stock mhal.ko"
	for m in "$stock_modules"/*.ko; do
		"${TC002_GLIBC_PREFIX}nm" -u "$m" | awk '{ print $2 }'
	done | sort -u | comm -23 - "$out/stock-mhal-exports.txt" > "$out/stock-kernel-imports.txt"
	comm -23 "$out/imports.txt" "$out/mhal-exports.txt" > "$out/kernel-imports.txt"
	if [ -n "${TC002_STOCK_IMAGE:-}" ]; then
		python3 "$TC002_KERNEL_DIR/check-stock-exports.py" "$TC002_STOCK_IMAGE" \
			"$out/stock-kernel-imports.txt" "$out/kernel-imports.txt"
	fi
fi

"${TC002_MUSL_PREFIX}gcc" -static -Os -Wall -Wextra -Werror -o "$out/awtrix-pcm-ctl" "$src/ctl/awtrix_pcm_ctl.c"
"${TC002_MUSL_PREFIX}strip" "$out/awtrix-pcm-ctl"
python3 "$TC002_KERNEL_DIR/awtrix-pcm-tone.py" "$out/tone-440hz-300ms.raw"
python3 "$TC002_KERNEL_DIR/awtrix-pcm-tone.py" --ms 3000 "$out/tone-440hz-3s.raw"
(cd "$out" && sha256sum awtrix_pcm.ko awtrix-pcm-ctl tone-440hz-300ms.raw tone-440hz-3s.raw)
