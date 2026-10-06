#!/bin/bash
set -eu
. "$(dirname "$0")/common.sh"
tc002_need_glibc

status=0
for ko in "$@"; do
	vermagic=$("${TC002_GLIBC_PREFIX}objcopy" -O binary -j .modinfo "$ko" /dev/stdout | tr '\0' '\n' | sed -n 's/^vermagic=//p')
	size=$("${TC002_GLIBC_PREFIX}readelf" -S -W "$ko" | awk '$2 == ".gnu.linkonce.this_module" { print "0x" $6 }')
	relocs=$("${TC002_GLIBC_PREFIX}readelf" -r -W "$ko" | awk '/\.rel\.gnu\.linkonce\.this_module/ { f = 1; next } /^$/ { f = 0 } f && /R_ARM_ABS32/ { print $5 "=0x" $1 }')
	init=$(echo "$relocs" | sed -n 's/^init_module=//p')
	exit_=$(echo "$relocs" | sed -n 's/^cleanup_module=//p')
	verdict=ok
	[ "$vermagic" = "$STOCK_VERMAGIC" ] || verdict="vermagic mismatch"
	[ $((size)) -eq $((STOCK_THIS_MODULE_SIZE)) ] || verdict="this_module size mismatch"
	[ -z "$init" ] || [ $((init)) -eq $((STOCK_INIT_OFFSET)) ] || verdict="init offset mismatch"
	[ -z "$exit_" ] || [ $((exit_)) -eq $((STOCK_EXIT_OFFSET)) ] || verdict="exit offset mismatch"
	printf '%s: vermagic="%s" this_module=%s init=%s exit=%s -> %s\n' \
		"$(basename "$ko")" "$vermagic" "$size" "${init:-none}" "${exit_:-none}" "$verdict"
	[ "$verdict" = ok ] || status=1
done
exit $status
