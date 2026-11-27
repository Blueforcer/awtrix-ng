#!/usr/bin/env bash
set -euo pipefail

OPENSSL_VERSION=3.5.8
OPENSSL_SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz"
JPEG_VERSION=3.2.0
JPEG_SHA256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e
JPEG_URL="https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$JPEG_VERSION/libjpeg-turbo-$JPEG_VERSION.tar.gz"
ARCHS=(arm64 x86_64)
MACOS_MIN=${MACOSX_DEPLOYMENT_TARGET:-11.0}

OPENSSL_OPTIONS=(
  no-shared no-module no-dso no-engine no-legacy no-apps no-docs no-tests no-quic no-dtls no-sctp
  no-comp no-zlib no-srp no-srtp no-idea no-camellia no-seed no-aria no-sm2 no-sm3 no-sm4
  no-whirlpool no-md4 no-mdc2 no-rc2 no-rc4 no-rc5 no-md2 no-bf no-cast no-gost no-cms no-ts
  no-ct no-cmp no-http no-ocsp no-weak-ssl-ciphers no-ssl3 no-tls1 no-tls1_1 no-ec2m no-siphash
  no-argon2 no-scrypt no-ssl-trace no-nextprotoneg no-rfc3779 no-async no-ktls no-afalgeng
  no-devcryptoeng no-padlockeng no-capieng no-winstore no-fips no-acvp-tests no-sslkeylog
  no-zstd no-brotli no-unstable-qlog no-h3demo no-hqinterop no-demos no-sm2-precomp
  no-deprecated no-dh no-dsa no-des no-ml-dsa no-slh-dsa no-psk no-ocb no-siv no-cmac no-blake2
  no-rmd160 no-autoload-config no-multiblock no-dgram no-secure-memory no-integrity-only-ciphers
  no-thread-pool no-default-thread-pool no-ui-console no-tls-deprecated-ec
)

if [ $# -ne 1 ]; then
  echo "usage: deps.sh WORKDIR  (static OpenSSL $OPENSSL_VERSION and libjpeg-turbo $JPEG_VERSION, ${ARCHS[*]}," >&2
  echo "  into WORKDIR/out; tarballs from AWTRIX_DL_DIR, default WORKDIR/dl)" >&2
  exit 2
fi
mkdir -p "$1"
WORK=$(cd "$1" && pwd)
OUT=$WORK/out
DL=${AWTRIX_DL_DIR:-$WORK/dl}
JOBS=$(sysctl -n hw.ncpu)
export SOURCE_DATE_EPOCH=0 LC_ALL=C TZ=UTC MACOSX_DEPLOYMENT_TARGET=$MACOS_MIN

fetch() {
  local path=$DL/$1
  if [ ! -e "$path" ]; then
    mkdir -p "$DL"
    curl -fsSL --retry 3 -o "$path.part" "$2" || { rm -f "$path.part"; echo "cannot download $2" >&2; return 1; }
    mv "$path.part" "$path"
  fi
  if [ "$(shasum -a 256 < "$path" | cut -d' ' -f1)" != "$3" ]; then
    echo "$path does not have the pinned SHA-256 $3" >&2
    return 1
  fi
  printf '%s\n' "$path"
}

recipe() {
  { cat "${BASH_SOURCE[0]}"; echo "$MACOS_MIN"; xcrun clang --version; } | shasum -a 256 | cut -d' ' -f1
}

openssl_arch() {
  local arch=$1 tarball=$2 src=$WORK/openssl-$arch stage=$WORK/stage-openssl-$arch
  rm -rf "$src" "$stage"
  mkdir -p "$src"
  tar -xzf "$tarball" -C "$src" --strip-components 1
  if ! (cd "$src" &&
        perl ./Configure "darwin64-$arch-cc" --prefix=/usr/local --openssldir=/etc/ssl --libdir=lib \
          "${OPENSSL_OPTIONS[@]}" -mmacosx-version-min="$MACOS_MIN" -g0 -Os &&
        make -j"$JOBS" build_libs && make DESTDIR="$stage" install_dev) >"$WORK/openssl-$arch.log" 2>&1; then
    tail -40 "$WORK/openssl-$arch.log" >&2
    return 1
  fi
  rm -rf "$src"
}

jpeg_arch() {
  local arch=$1 tarball=$2 src=$WORK/jpeg-$arch build=$WORK/build-jpeg-$arch stage=$WORK/stage-jpeg-$arch
  rm -rf "$src" "$build" "$stage"
  mkdir -p "$src"
  tar -xzf "$tarball" -C "$src" --strip-components 1
  if ! (cmake -S "$src" -B "$build" -DCMAKE_INSTALL_PREFIX="$stage" -DCMAKE_INSTALL_LIBDIR=lib \
          -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_OSX_ARCHITECTURES="$arch" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" \
          -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="-ffile-prefix-map=$src=jpeg" \
          -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF \
          -DWITH_SIMD=OFF -DWITH_ARITH_ENC=OFF &&
        cmake --build "$build" --parallel "$JOBS" && cmake --install "$build") >"$WORK/jpeg-$arch.log" 2>&1; then
    tail -40 "$WORK/jpeg-$arch.log" >&2
    return 1
  fi
  cat "$src/LICENSE.md" "$src/README.ijg" > "$stage/LICENSE.txt"
  rm -rf "$src" "$build"
}

universal() {
  local inputs=() arch
  for arch in "${ARCHS[@]}"; do inputs+=("$WORK/$1-$arch/$2"); done
  lipo -create "${inputs[@]}" -output "$3"
}

KEY=$(recipe)
if [ -f "$OUT/.recipe" ] && [ "$(cat "$OUT/.recipe")" = "$KEY" ]; then
  echo "dependencies up to date in $OUT"
  exit 0
fi
OPENSSL_TARBALL=$(fetch "openssl-$OPENSSL_VERSION.tar.gz" "$OPENSSL_URL" "$OPENSSL_SHA256")
JPEG_TARBALL=$(fetch "libjpeg-turbo-$JPEG_VERSION.tar.gz" "$JPEG_URL" "$JPEG_SHA256")
for arch in "${ARCHS[@]}"; do
  if [ ! -f "$WORK/stage-openssl-$arch/usr/local/lib/libssl.a" ] || [ "$(cat "$WORK/stage-openssl-$arch/.recipe" 2>/dev/null)" != "$KEY" ]; then
    openssl_arch "$arch" "$OPENSSL_TARBALL"
    echo "$KEY" > "$WORK/stage-openssl-$arch/.recipe"
  fi
done
command -v cmake >/dev/null || { echo "cmake is missing" >&2; exit 1; }
for arch in "${ARCHS[@]}"; do jpeg_arch "$arch" "$JPEG_TARBALL"; done

rm -rf "$OUT.partial"
mkdir -p "$OUT.partial/lib" "$OUT.partial/licenses"
cp -R "$WORK/stage-openssl-${ARCHS[0]}/usr/local/include" "$OUT.partial/include"
cp -R "$WORK/stage-jpeg-${ARCHS[0]}/include/." "$OUT.partial/include/"
universal stage-openssl usr/local/lib/libcrypto.a "$OUT.partial/lib/libcrypto.a"
universal stage-openssl usr/local/lib/libssl.a "$OUT.partial/lib/libssl.a"
universal stage-jpeg lib/libjpeg.a "$OUT.partial/lib/libjpeg.a"
tar -xzf "$OPENSSL_TARBALL" -O "openssl-$OPENSSL_VERSION/LICENSE.txt" > "$OUT.partial/licenses/OpenSSL.txt"
cp "$WORK/stage-jpeg-${ARCHS[0]}/LICENSE.txt" "$OUT.partial/licenses/libjpeg-turbo.txt"
for lib in "$OUT.partial"/lib/*.a; do
  if grep -qF "$WORK" "$lib"; then
    echo "$lib embeds the build directory $WORK" >&2
    exit 1
  fi
done
echo "$KEY" > "$OUT.partial/.recipe"
rm -rf "$OUT"
mv "$OUT.partial" "$OUT"
for arch in "${ARCHS[@]}"; do rm -rf "$WORK/stage-jpeg-$arch"; done
for lib in "$OUT"/lib/*.a; do echo "$(basename "$lib"): $(lipo -archs "$lib"), $(stat -f %z "$lib") bytes"; done
