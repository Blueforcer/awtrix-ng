#!/usr/bin/env bash
set -euo pipefail

OPENSSL_VERSION=3.5.8
OPENSSL_SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
JPEG_VERSION=3.2.0
JPEG_SHA256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e

OPTIONS=(
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

ARCH=${1:-}
case "$ARCH" in
  x64) OPENSSL_TARGET=linux-x86_64; PROCESSOR=x86_64 ;;
  arm64) OPENSSL_TARGET=linux-aarch64; PROCESSOR=aarch64 ;;
  *) OPENSSL_TARGET= ;;
esac
if [ $# -ne 3 ] || [ -z "$OPENSSL_TARGET" ] || [ -z "${AWTRIX_DL_DIR:-}" ]; then
  echo "usage: AWTRIX_DL_DIR=DIR deps.sh x64|arm64 CROSS_PREFIX OUTDIR  (static OpenSSL $OPENSSL_VERSION and" >&2
  echo "  libjpeg-turbo $JPEG_VERSION for that toolchain; DIR holds their pinned tarballs)" >&2
  exit 2
fi
CROSS=$2
[ -x "${CROSS}gcc" ] || { echo "deps.sh: no compiler at ${CROSS}gcc" >&2; exit 1; }
mkdir -p "$3"
OUT=$(cd "$3" && pwd)

check() {
  local file=$AWTRIX_DL_DIR/$1
  [ -f "$file" ] || { echo "deps.sh: missing $file" >&2; exit 1; }
  [ "$(sha256sum < "$file" | cut -d' ' -f1)" = "$2" ] || { echo "deps.sh: $file does not match its pin" >&2; exit 1; }
  echo "$file"
}
OPENSSL_TAR=$(check "openssl-$OPENSSL_VERSION.tar.gz" "$OPENSSL_SHA256")
JPEG_TAR=$(check "libjpeg-turbo-$JPEG_VERSION.tar.gz" "$JPEG_SHA256")

KEY=$({ cat "${BASH_SOURCE[0]}"; "${CROSS}gcc" -v 2>&1; } | sha256sum | cut -d' ' -f1)
if [ -f "$OUT/.recipe" ] && [ "$(cat "$OUT/.recipe")" = "$KEY" ]; then
  echo "dependencies up to date in $OUT"
  exit 0
fi

WORK=$OUT.work
rm -rf "$WORK" "$OUT"
mkdir -p "$WORK" "$OUT/licenses"
WORK=$(cd "$WORK" && pwd)
export SOURCE_DATE_EPOCH=0 LC_ALL=C TZ=UTC
FLAGS=(-g0 -Os -ffunction-sections -fdata-sections)

tar -xzf "$OPENSSL_TAR" -C "$WORK"
(
  cd "$WORK/openssl-$OPENSSL_VERSION"
  perl ./Configure "$OPENSSL_TARGET" --cross-compile-prefix="$CROSS" --prefix=/usr/local --openssldir=/etc/ssl \
    --libdir=lib "${OPTIONS[@]}" "${FLAGS[@]}"
  make -j"$(nproc)" build_libs
  make DESTDIR="$WORK/stage" install_dev
) >"$WORK/openssl.log" 2>&1 || { tail -40 "$WORK/openssl.log" >&2; exit 1; }
cp -R "$WORK/stage/usr/local/include" "$OUT/include"
mkdir -p "$OUT/lib"
cp "$WORK/stage/usr/local/lib/libssl.a" "$WORK/stage/usr/local/lib/libcrypto.a" "$OUT/lib/"
cp "$WORK/openssl-$OPENSSL_VERSION/LICENSE.txt" "$OUT/licenses/OpenSSL.txt"

tar -xzf "$JPEG_TAR" -C "$WORK"
{
  cmake -S "$WORK/libjpeg-turbo-$JPEG_VERSION" -B "$WORK/jpeg" -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR="$PROCESSOR" -DCMAKE_C_COMPILER="${CROSS}gcc" \
    -DCMAKE_INSTALL_PREFIX="$WORK/jpeg-stage" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_FLAGS="${FLAGS[*]} -ffile-prefix-map=$WORK=." \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF \
    -DWITH_SIMD=OFF -DWITH_ARITH_ENC=OFF
  cmake --build "$WORK/jpeg" --parallel "$(nproc)"
  cmake --install "$WORK/jpeg"
} >"$WORK/jpeg.log" 2>&1 || { tail -40 "$WORK/jpeg.log" >&2; exit 1; }
cp -R "$WORK/jpeg-stage/include/." "$OUT/include/"
cp "$WORK/jpeg-stage/lib/libjpeg.a" "$OUT/lib/"
cat "$WORK/libjpeg-turbo-$JPEG_VERSION/LICENSE.md" "$WORK/libjpeg-turbo-$JPEG_VERSION/README.ijg" > "$OUT/licenses/libjpeg-turbo.txt"

for lib in "$OUT"/lib/*.a; do
  if grep -qF "$WORK" "$lib"; then
    echo "$lib embeds the build directory $WORK" >&2
    exit 1
  fi
done
echo "$KEY" > "$OUT/.recipe"
rm -rf "$WORK"
ls -l "$OUT/lib"
