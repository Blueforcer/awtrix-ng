#!/bin/sh
set -eu
target=${1:?target directory required}
for helper in "$target"/usr/lib/libstdc++.so.*-gdb.py; do
    [ ! -f "$helper" ] || rm -f -- "$helper"
done
