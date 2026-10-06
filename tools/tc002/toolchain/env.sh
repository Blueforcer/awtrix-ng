# Sourced by every TC002 recipe: the two cross compilers, the download cache and the work root.
# Nothing defaults to a path on one machine; a recipe that needs a toolchain that is not set stops
# and says how to get it.
#
#   TC002_MUSL_PREFIX   the static musl armv7 hard-float toolchain that builds every program of the
#                       release, e.g. BUILDROOT_WORK/output/host/bin/arm-linux-
#   TC002_GLIBC_PREFIX  the Arm GNU Toolchain 9.2-2019.12 arm-none-linux-gnueabihf that builds the
#                       loader, which the stock glibc zkgui loads, and the kernel modules
#   TC002_CACHE         work root, default ${XDG_CACHE_HOME:-$HOME/.cache}/awtrix-tc002
#   TC002_DL_DIR        source tarballs, default $TC002_CACHE/downloads; a tarball placed there by
#                       hand is used when it matches the recipe's SHA-256

TC002_CACHE=${TC002_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/awtrix-tc002}
TC002_DL_DIR=${TC002_DL_DIR:-$TC002_CACHE/downloads}
. "$(dirname "${BASH_SOURCE[0]}")/../lib/fetch.sh"

tc002_need_musl() {
  if [ -n "${TC002_MUSL_PREFIX:-}" ] && [ -x "${TC002_MUSL_PREFIX}gcc" ]; then
    return 0
  fi
  cat >&2 <<EOF
${0##*/} needs TC002_MUSL_PREFIX, the static musl armv7 hard-float toolchain the TC002 programs
are built with (Buildroot 2025.02.18, GCC 14.4, musl 1.2.6)${TC002_MUSL_PREFIX:+; ${TC002_MUSL_PREFIX}gcc is missing}.
tools/system/build.py builds it (prepare, configure, build; see docs/developers/linux/system-build.md), then:
  export TC002_MUSL_PREFIX=WORK/output/host/bin/arm-linux-
EOF
  exit 1
}

tc002_need_glibc() {
  if [ -n "${TC002_GLIBC_PREFIX:-}" ] && [ -x "${TC002_GLIBC_PREFIX}gcc" ]; then
    return 0
  fi
  cat >&2 <<EOF
${0##*/} needs TC002_GLIBC_PREFIX, the Arm GNU Toolchain 9.2-2019.12 for arm-none-linux-gnueabihf
that builds the loader and the kernel modules${TC002_GLIBC_PREFIX:+; ${TC002_GLIBC_PREFIX}gcc is missing}.
tools/tc002/toolchain/fetch-arm-gnu-9.2.sh downloads and checks it and prints the line to use:
  export TC002_GLIBC_PREFIX=DIR/gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
EOF
  exit 1
}
