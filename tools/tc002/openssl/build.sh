#!/usr/bin/env bash
# Builds the static, trimmed OpenSSL that bin/awtrix-linux links on the TC002.
#
#   tools/tc002/openssl/build.sh WORKDIR [OUTDIR]
#
# OUTDIR (default WORKDIR/out) receives include/, lib/libcrypto.a, lib/libssl.a and LICENSE.txt.
# The source tarball comes through TC002_DL_DIR and must match the pinned SHA-256. OUTDIR is reused
# while this recipe, the tarball and the compiler stay the same. The compiler is TC002_MUSL_PREFIX
# (tools/tc002/toolchain/env.sh).
set -euo pipefail
. "$(dirname "$0")/../toolchain/env.sh"
tc002_need_musl

VERSION=3.5.8
SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
URL="https://github.com/openssl/openssl/releases/download/openssl-${VERSION}/openssl-${VERSION}.tar.gz"

OPTIONS=(
  no-shared no-module no-dso no-engine no-legacy no-apps no-docs no-tests no-quic no-dtls no-sctp
  no-comp no-zlib no-srp no-srtp no-idea no-camellia no-seed no-aria no-sm2 no-sm3 no-sm4
  no-whirlpool no-md4 no-mdc2 no-rc2 no-rc4 no-rc5 no-md2 no-bf no-cast no-gost no-cms no-ts
  no-ct no-cmp no-http no-ocsp no-weak-ssl-ciphers no-ssl3 no-tls1 no-tls1_1 no-ec2m no-siphash
  no-argon2 no-scrypt no-ssl-trace no-nextprotoneg no-rfc3779 no-async no-ktls no-afalgeng
  no-devcryptoeng no-padlockeng no-capieng no-winstore no-fips no-acvp-tests no-sslkeylog
  no-zstd no-brotli no-unstable-qlog no-h3demo no-hqinterop no-demos no-ec_nistp_64_gcc_128
  no-sm2-precomp
  no-deprecated no-dh no-dsa no-des no-ml-dsa no-slh-dsa no-psk no-ocb no-siv no-cmac no-blake2
  no-rmd160 no-autoload-config no-multiblock no-dgram no-secure-memory no-integrity-only-ciphers
  no-thread-pool no-default-thread-pool no-ui-console no-filenames no-pic no-tls-deprecated-ec
)
LFS="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"
CFLAGS_TC002="$LFS -g0 -Os -ffunction-sections -fdata-sections -DOPENSSL_SMALL_FOOTPRINT -mthumb"

WORK=${1:?usage: build.sh WORKDIR [OUTDIR]}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
OUT=${2:-$WORK/out}
TARBALL=$(tc002_fetch "openssl-$VERSION.tar.gz" "$URL" "$SHA256")

CC_PATH="${TC002_MUSL_PREFIX}gcc"
PATH="$(cd "$(dirname "$CC_PATH")" && pwd):$PATH"
CROSS=$(basename "$TC002_MUSL_PREFIX")
KEY=$({ cat "${BASH_SOURCE[0]}"; echo "$SHA256"; "${CROSS}gcc" -v 2>&1; } | sha256sum | cut -d' ' -f1)
if [ -f "$OUT/.recipe" ] && [ "$(cat "$OUT/.recipe")" = "$KEY" ] &&
   [ -f "$OUT/lib/libcrypto.a" ] && [ -f "$OUT/lib/libssl.a" ]; then
  echo "OpenSSL $VERSION up to date in $OUT"
  exit 0
fi

SRC=$WORK/openssl-$VERSION
STAGE=$WORK/stage
rm -rf "$SRC" "$STAGE" "$OUT.partial"
tar -xzf "$TARBALL" -C "$WORK"

export SOURCE_DATE_EPOCH=0 LC_ALL=C TZ=UTC
(
  cd "$SRC"
  CC="${CROSS}gcc" AR="${CROSS}ar" ARFLAGS=rD RANLIB="${CROSS}ranlib -D" CFLAGS="$CFLAGS_TC002" \
    CPPFLAGS="$LFS" perl ./Configure linux-armv4 --prefix=/usr --openssldir=/etc/ssl --libdir=lib "${OPTIONS[@]}"
  make -j"$(nproc)" build_libs
  make DESTDIR="$STAGE" install_dev
) >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; exit 1; }

mkdir -p "$OUT.partial/lib"
cp -a "$STAGE/usr/include" "$OUT.partial/include"
install -m 0644 "$STAGE/usr/lib/libcrypto.a" "$STAGE/usr/lib/libssl.a" "$OUT.partial/lib/"
install -m 0644 "$SRC/LICENSE.txt" "$OUT.partial/LICENSE.txt"
if grep -qF "$WORK" "$OUT.partial/lib/libcrypto.a" "$OUT.partial/lib/libssl.a"; then
  echo "the OpenSSL libraries embed the build directory $WORK" >&2
  exit 1
fi
echo "$KEY" > "$OUT.partial/.recipe"
rm -rf "$OUT"
mv "$OUT.partial" "$OUT"
rm -rf "$SRC" "$STAGE"

( cd "$OUT/lib" && stat -c '%n %s bytes' libcrypto.a libssl.a && sha256sum libcrypto.a libssl.a )
