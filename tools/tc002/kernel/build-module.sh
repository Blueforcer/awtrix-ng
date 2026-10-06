#!/bin/bash
set -eu
. "$(dirname "$0")/common.sh"
tc002_need_glibc

if [ $# -lt 1 ]; then
	echo "usage: $0 <module-source-dir> [build-dir]" >&2
	exit 2
fi
src=$(cd "$1" && pwd)
out=${2:-$TC002_KMOD_WORK/modules/$(basename "$src")}

test -f "$KERNEL_OUT/Module.symvers"
rm -rf "$out"
mkdir -p "$out"
cp -r "$src"/. "$out"/
kmake M="$out" LDFLAGS_MODULE=--build-id=none modules
for ko in "$out"/*.ko; do
	"${TC002_GLIBC_PREFIX}strip" --strip-debug "$ko"
	"$TC002_KERNEL_DIR/check-ko.sh" "$ko"
done
