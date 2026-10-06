#!/bin/bash
set -eu
. "$(dirname "$0")/common.sh"

if [ ! -d "$KERNEL_SRC/.git" ]; then
	mkdir -p "$KERNEL_SRC"
	git -C "$KERNEL_SRC" init -q
	git -C "$KERNEL_SRC" remote add origin "$KERNEL_REPO"
fi
if [ "$(git -C "$KERNEL_SRC" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$KERNEL_COMMIT" ]; then
	git -C "$KERNEL_SRC" fetch --depth 1 origin "$KERNEL_COMMIT"
	git -C "$KERNEL_SRC" -c advice.detachedHead=false checkout -q FETCH_HEAD
fi
test "$(git -C "$KERNEL_SRC" rev-parse HEAD)" = "$KERNEL_COMMIT"
echo "kernel source $KERNEL_SRC at $KERNEL_COMMIT"
