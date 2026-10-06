#!/usr/bin/env bash
# Fetches the Arm GNU Toolchain 9.2-2019.12 for arm-none-linux-gnueabihf, checked against its pinned
# SHA-256, and prints the TC002_GLIBC_PREFIX line to use:
#
#   tools/tc002/toolchain/fetch-arm-gnu-9.2.sh [DIR]      DIR defaults to $TC002_CACHE/toolchains
#
# Builds the loader for stock glibc 2.30 and modules for the stock 4.9.84 kernel.
set -euo pipefail
. "$(dirname "$0")/env.sh"

NAME=gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf
URL=https://developer.arm.com/-/media/Files/downloads/gnu-a/9.2-2019.12/binrel/$NAME.tar.xz
SHA256=51bbaf22a4d3e7a393264c4ef1e45566701c516274dde19c4892c911caa85617

DIR=${1:-$TC002_CACHE/toolchains}
mkdir -p "$DIR"
DIR=$(cd "$DIR" && pwd)
if [ "$(cat "$DIR/$NAME/.sha256" 2>/dev/null)" != "$SHA256" ]; then
  TARBALL=$(tc002_fetch "$NAME.tar.xz" "$URL" "$SHA256")
  rm -rf "$DIR/$NAME" "$DIR/$NAME.partial"
  mkdir "$DIR/$NAME.partial"
  tar -xJf "$TARBALL" -C "$DIR/$NAME.partial" --strip-components=1
  echo "$SHA256" > "$DIR/$NAME.partial/.sha256"
  mv "$DIR/$NAME.partial" "$DIR/$NAME"
fi
PREFIX=$DIR/$NAME/bin/arm-none-linux-gnueabihf-
if [ "$("${PREFIX}gcc" -dumpmachine)" != arm-none-linux-gnueabihf ] ||
   [ "$("${PREFIX}gcc" -dumpversion)" != 9.2.1 ]; then
  echo "${PREFIX}gcc is not the arm-none-linux-gnueabihf GCC 9.2.1 of $NAME" >&2
  exit 1
fi
echo "export TC002_GLIBC_PREFIX=$PREFIX"
