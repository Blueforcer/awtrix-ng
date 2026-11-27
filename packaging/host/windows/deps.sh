#!/usr/bin/env bash
set -euo pipefail

OPENSSL=3.5.8
OPENSSL_SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
JPEG=3.2.0
JPEG_SHA256=6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e

OPTIONS=(
  no-shared no-module no-dso no-engine no-legacy no-apps no-docs no-tests no-quic no-dtls no-sctp
  no-comp no-zlib no-srp no-srtp no-idea no-camellia no-seed no-aria no-sm2 no-sm3 no-sm4
  no-whirlpool no-md4 no-mdc2 no-rc2 no-rc4 no-rc5 no-md2 no-bf no-cast no-gost no-cms no-ts
  no-ct no-cmp no-http no-ocsp no-weak-ssl-ciphers no-ssl3 no-tls1 no-tls1_1 no-ec2m no-siphash
  no-argon2 no-scrypt no-ssl-trace no-nextprotoneg no-rfc3779 no-async no-ktls no-afalgeng
  no-devcryptoeng no-padlockeng no-capieng no-fips no-acvp-tests no-sslkeylog
  no-zstd no-brotli no-unstable-qlog no-h3demo no-hqinterop no-demos no-sm2-precomp
  no-deprecated no-dh no-dsa no-des no-ml-dsa no-slh-dsa no-psk no-ocb no-siv no-cmac no-blake2
  no-rmd160 no-autoload-config no-multiblock no-dgram no-secure-memory no-integrity-only-ciphers
  no-thread-pool no-default-thread-pool no-ui-console no-filenames no-tls-deprecated-ec
)

OUT=${1:?usage: AWTRIX_DL_DIR=DIR deps.sh OUTDIR}
DL=${AWTRIX_DL_DIR:?set AWTRIX_DL_DIR to the folder with openssl-$OPENSSL.tar.gz and libjpeg-turbo-$JPEG.tar.gz}
for tool in gcc mingw32-make perl cmake ninja cygpath; do
  command -v "$tool" >/dev/null || { echo "deps.sh: $tool must be on PATH (Git Bash, WinLibs MinGW-w64 UCRT)" >&2; exit 1; }
done

check() {
  local file=$DL/$1
  [ -f "$file" ] || { echo "deps.sh: missing $file" >&2; exit 1; }
  [ "$(sha256sum "$file" | cut -d' ' -f1)" = "$2" ] || { echo "deps.sh: $file does not match its pin" >&2; exit 1; }
  echo "$file"
}
OPENSSL_TAR=$(check "openssl-$OPENSSL.tar.gz" "$OPENSSL_SHA256")
JPEG_TAR=$(check "libjpeg-turbo-$JPEG.tar.gz" "$JPEG_SHA256")

KEY=$({ cat "${BASH_SOURCE[0]}"; gcc -v 2>&1; } | sha256sum | cut -d' ' -f1)
if [ -f "$OUT/.recipe" ] && [ "$(cat "$OUT/.recipe")" = "$KEY" ]; then
  echo "dependencies up to date in $OUT"
  exit 0
fi

WORK=$OUT.work
rm -rf "$WORK" "$OUT"
mkdir -p "$WORK/perl/IPC" "$WORK/perl/Pod" "$OUT/licenses"
WORK=$(cd "$WORK" && pwd)
PREFIX=$(cygpath -m "$(cd "$OUT" && pwd)")
export SOURCE_DATE_EPOCH=0 LC_ALL=C

cat > "$WORK/perl/IPC/Cmd.pm" <<'PERL'
package IPC::Cmd;
use strict;
use File::Spec;
sub can_run {
  my $command = shift;
  return (-x $command ? $command : undef) if File::Spec->file_name_is_absolute($command);
  for my $dir (File::Spec->path) {
    for my $name ($command, "$command.exe") {
      my $path = File::Spec->catfile($dir, $name);
      return $path if -f $path && -x _;
    }
  }
  return undef;
}
1;
PERL

cat > "$WORK/perl/Pod/Usage.pm" <<'PERL'
package Pod::Usage;
use strict;
use Exporter 'import';
our @EXPORT = qw(pod2usage);
sub pod2usage { exit 2 }
1;
PERL

tar -xzf "$OPENSSL_TAR" -C "$WORK"
(
  cd "$WORK/openssl-$OPENSSL"
  export PERL5OPT="-I$(cygpath -m "$WORK/perl")"
  CC=gcc AR=ar RANLIB=ranlib CFLAGS="-O2 -g0 -ffunction-sections -fdata-sections" \
    perl ./Configure mingw64 --prefix="$PREFIX" --libdir=lib "--openssldir=$PREFIX/ssl" "${OPTIONS[@]}"
  mingw32-make -j"$(nproc)" build_libs
  mingw32-make install_dev
) >"$WORK/openssl.log" 2>&1 || { tail -40 "$WORK/openssl.log" >&2; exit 1; }
cp "$WORK/openssl-$OPENSSL/LICENSE.txt" "$OUT/licenses/OpenSSL.txt"

tar -xzf "$JPEG_TAR" -C "$WORK"
{
  cmake -S "$WORK/libjpeg-turbo-$JPEG" -B "$WORK/jpeg" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF \
    -DWITH_SIMD=OFF -DWITH_ARITH_ENC=OFF
  cmake --build "$WORK/jpeg" --parallel "$(nproc)"
  cmake --install "$WORK/jpeg"
} >"$WORK/jpeg.log" 2>&1 || { tail -40 "$WORK/jpeg.log" >&2; exit 1; }
cat "$WORK/libjpeg-turbo-$JPEG/LICENSE.md" "$WORK/libjpeg-turbo-$JPEG/README.ijg" > "$OUT/licenses/libjpeg-turbo.txt"

rm -rf "$OUT/bin" "$OUT/share" "$OUT/ssl" "$OUT/lib/pkgconfig" "$OUT/lib/cmake"
echo "$KEY" > "$OUT/.recipe"
rm -rf "$WORK"
ls -l "$OUT/lib"
