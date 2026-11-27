#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
CA_SHA256=3d664e3fa39e5d3e95f653127b067ef02e5d450112b2f4d65f1ad89b0a62a506
MUSL_VERSION=1.2.6
MUSL_SHA256=d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a
NAME=awtrix-script-host
DOCKER_UID=999

if [ $# -lt 1 ] || [ -z "${AWTRIX_DL_DIR:-}" ] || [ -z "${BR2_DL_DIR:-}" ] || [ -z "${AWTRIX_CA_DIR:-}" ]; then
  echo "usage: AWTRIX_DL_DIR=DIR BR2_DL_DIR=DIR AWTRIX_CA_DIR=DIR build.sh WORKDIR [x64|arm64 ...]" >&2
  echo "  (static musl programs and AWTRIX-Script-Host-linux-ARCH.tar.gz in WORKDIR/out; AWTRIX_DL_DIR holds the" >&2
  echo "  OpenSSL and libjpeg-turbo tarballs, BR2_DL_DIR Buildroot and its downloads, AWTRIX_CA_DIR the output of" >&2
  echo "  tools/tc002/ca/build.sh; AWTRIX_DEPS defaults to .pio/libdeps/native; AWTRIX_SMOKE=0 skips the smoke test)" >&2
  exit 2
fi
mkdir -p "$1"
WORK=$(cd "$1" && pwd)
shift
ARCHS=("$@")
[ ${#ARCHS[@]} -gt 0 ] || ARCHS=(x64 arm64)
OUT=$WORK/out
DEPS_DIR=${AWTRIX_DEPS:-$ROOT/.pio/libdeps/native}
VERSION=$(tr -d '[:space:]' < "$ROOT/version")
JOBS=$(nproc)
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0} LC_ALL=C TZ=UTC

for tool in python3 cmake make perl file sha256sum gzip; do
  command -v "$tool" >/dev/null || { echo "build.sh: $tool is missing" >&2; exit 1; }
done
[ -f "$DEPS_DIR/base64/src/base64.hpp" ] || { echo "build.sh: no base64/src/base64.hpp in $DEPS_DIR" >&2; exit 1; }
[ "$(sha256sum < "$AWTRIX_CA_DIR/ca-certificates.crt" | cut -d' ' -f1)" = "$CA_SHA256" ] ||
  { echo "build.sh: $AWTRIX_CA_DIR/ca-certificates.crt is not the pinned Mozilla bundle" >&2; exit 1; }
[ -f "$AWTRIX_CA_DIR/NOTICE" ] || { echo "build.sh: missing $AWTRIX_CA_DIR/NOTICE" >&2; exit 1; }
MUSL_TAR=$BR2_DL_DIR/musl/musl-$MUSL_VERSION.tar.gz
[ "$(sha256sum < "$MUSL_TAR" | cut -d' ' -f1)" = "$MUSL_SHA256" ] ||
  { echo "build.sh: $MUSL_TAR is missing or does not match its pin" >&2; exit 1; }

for arch in "${ARCHS[@]}"; do
  case "$arch" in x64|arm64) ;; *) echo "build.sh: unknown architecture $arch" >&2; exit 2 ;; esac
  "$HERE/toolchain.sh" "$arch" "$WORK" >"$WORK/toolchain-$arch.out" 2>&1 &
done
failed=0
for job in $(jobs -p); do wait "$job" || failed=1; done
if [ "$failed" = 1 ]; then
  for arch in "${ARCHS[@]}"; do tail -20 "$WORK/toolchain-$arch.out" >&2; done
  exit 1
fi

static_only() {
  local readelf=$1 program=$2
  if "$readelf" -lW "$program" | grep -q 'program interpreter' || "$readelf" -dW "$program" | grep -q NEEDED; then
    echo "build.sh: $program is not static" >&2
    exit 1
  fi
}

rootfs() {
  local stage=$1 tarball=$2 root=$WORK/rootfs-$3
  rm -rf "$root"
  mkdir -p "$root/etc" "$root/opt" "$root/data" "$root/tmp"
  cp -R "$stage/$NAME" "$root/opt/$NAME"
  printf 'root:x:0:0:root:/:/sbin/nologin\nawtrix:x:%s:%s:AWTRIX script host:/data:/sbin/nologin\n' \
    "$DOCKER_UID" "$DOCKER_UID" > "$root/etc/passwd"
  printf 'root:x:0:\nawtrix:x:%s:\n' "$DOCKER_UID" > "$root/etc/group"
  chmod 0644 "$root/etc/passwd" "$root/etc/group"
  chmod 0700 "$root/data"
  chmod 1777 "$root/tmp"
  local common=(--sort=name --mtime="@$SOURCE_DATE_EPOCH" --numeric-owner --format=gnu -C "$root")
  tar "${common[@]}" --owner=0 --group=0 -cf "$tarball" etc opt tmp
  tar "${common[@]}" --owner="$DOCKER_UID" --group="$DOCKER_UID" -rf "$tarball" data
  rm -rf "$root"
}

mkdir -p "$OUT/docker"
for arch in "${ARCHS[@]}"; do
  case "$arch" in x64) docker=amd64; native=x86_64 ;; arm64) docker=arm64; native=aarch64 ;; esac
  prefix=$(tail -1 "$WORK/toolchain-$arch.out")
  deps=$WORK/deps-$arch
  build=$WORK/build-$arch
  "$HERE/deps.sh" "$arch" "$prefix" "$deps" >"$WORK/deps-$arch.out" 2>&1 || { tail -20 "$WORK/deps-$arch.out" >&2; exit 1; }
  flags="-ffunction-sections -fdata-sections -ffile-prefix-map=$ROOT=. -ffile-prefix-map=$WORK=."
  cmake -S "$ROOT" -B "$build" -DAWTRIX_HOST_ONLY=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR="$native" \
    -DCMAKE_C_COMPILER="${prefix}gcc" -DCMAKE_CXX_COMPILER="${prefix}g++" \
    -DCMAKE_FIND_ROOT_PATH="$deps" -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DOPENSSL_ROOT_DIR="$deps" -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -DJPEG_INCLUDE_DIR="$deps/include" -DJPEG_LIBRARY="$deps/lib/libjpeg.a" -DAWTRIX_DEPS="$DEPS_DIR" \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
    -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,--gc-sections -Wl,-z,stack-size=524288" >"$WORK/configure-$arch.log"
  grep -q "^OPENSSL_SSL_LIBRARY:FILEPATH=$deps/lib/libssl.a$" "$build/CMakeCache.txt" ||
    { echo "build.sh: CMake did not pick $deps/lib/libssl.a" >&2; exit 1; }
  cmake --build "$build" --target awtrix-host awtrix-script-runner --parallel "$JOBS" >"$WORK/build-$arch.log" 2>&1 ||
    { tail -40 "$WORK/build-$arch.log" >&2; exit 1; }

  stage=$WORK/stage-$arch
  app=$stage/$NAME
  rm -rf "$stage"
  mkdir -p "$app/bin" "$app/share" "$app/licenses"
  for program in awtrix-host awtrix-script-runner; do
    "${prefix}strip" --strip-all -o "$app/bin/$program" "$build/$program"
    static_only "${prefix}readelf" "$app/bin/$program"
  done
  cp "$ROOT/webui/index.html" "$app/share/index.html"
  cp "$AWTRIX_CA_DIR/ca-certificates.crt" "$app/share/ca-certificates.crt"
  cp "$HERE/$NAME.service" "$app/share/$NAME.service"
  cp "$HERE/uninstall.sh" "$app/uninstall.sh"
  echo "$VERSION" > "$app/VERSION"
  cp "$ROOT/LICENSE.md" "$app/licenses/AWTRIX-NG.md"
  for text in MIT-Berry MIT-cpp-httplib MIT-densaugeo-base64 MIT-PubSubClient MIT-Matrix-Fonts TJpg_Decoder; do
    cp "$ROOT/LICENSES/$text.txt" "$app/licenses/"
  done
  cp "$deps/licenses/OpenSSL.txt" "$deps/licenses/libjpeg-turbo.txt" "$app/licenses/"
  tar -xzOf "$MUSL_TAR" "musl-$MUSL_VERSION/COPYRIGHT" > "$app/licenses/musl.txt"
  cp "$AWTRIX_CA_DIR/NOTICE" "$app/licenses/Mozilla-CA-NOTICE.txt"
  chmod 0755 "$app/bin/"* "$app/uninstall.sh"
  chmod 0644 "$app/share/"* "$app/licenses/"* "$app/VERSION"

  if [ "${AWTRIX_SMOKE:-1}" = 1 ]; then
    emulator=()
    if [ "$native" != "$(uname -m)" ]; then
      qemu=$(command -v "qemu-$native-static" || command -v "qemu-$native" || true)
      [ -n "$qemu" ] || { echo "build.sh: no qemu-$native for the smoke test (AWTRIX_SMOKE=0 skips it)" >&2; exit 1; }
      emulator=(--emulator "$qemu")
    fi
    python3 -I "$HERE/smoke.py" --bin "$app/bin" --share "$app/share" --version "$VERSION" "${emulator[@]}"
  fi

  package=AWTRIX-Script-Host-linux-$arch.tar.gz
  tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner --format=gnu -C "$stage" \
    -cf - "$NAME" | gzip -9n > "$OUT/$package"
  (cd "$OUT" && sha256sum "$package" > "$package.sha256")
  rootfs "$stage" "$OUT/docker/rootfs-$docker.tar" "$arch"
  for program in awtrix-host awtrix-script-runner; do
    echo "$arch $program: $(stat -c %s "$app/bin/$program") bytes, $(file -b "$app/bin/$program")"
  done
done
cp "$HERE/install.sh" "$HERE/uninstall.sh" "$OUT/"
chmod 0755 "$OUT/install.sh" "$OUT/uninstall.sh"
(cd "$OUT" && ls -l ./*.tar.gz docker/*.tar && cat ./*.sha256)
