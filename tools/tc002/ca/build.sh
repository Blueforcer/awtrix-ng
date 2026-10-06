#!/usr/bin/env bash
# Builds share/ca-certificates.crt, the Mozilla CA bundle awtrix-linux verifies HTTPS against,
# the way Buildroot's ca-certificates package builds /etc/ssl/certs/ca-certificates.crt.
#
#   tools/tc002/ca/build.sh WORKDIR [OUTDIR]
#
# OUTDIR (default WORKDIR/out) receives ca-certificates.crt and NOTICE (MPL-2.0). The Debian
# source tarball comes through TC002_DL_DIR (tools/tc002/toolchain/env.sh) and must match the
# pinned SHA-256; the bundle must match its pinned SHA-256 too. Needs python3 with the
# cryptography module (Debian's certdata2pem.py).
set -euo pipefail
. "$(dirname "$0")/../toolchain/env.sh"

VERSION=20260223
SHA256=2fa2b00d4360f0d14ec51640ae8aea9e563956b95ea786e3c3c01c4eead42b56
URL="https://snapshot.debian.org/archive/debian/20260223T202245Z/pool/main/c/ca-certificates/ca-certificates_${VERSION}.tar.xz"
BUNDLE_SHA256=3d664e3fa39e5d3e95f653127b067ef02e5d450112b2f4d65f1ad89b0a62a506

WORK=${1:?usage: build.sh WORKDIR [OUTDIR]}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
OUT=${2:-$WORK/out}
TARBALL=$(tc002_fetch "ca-certificates_$VERSION.tar.xz" "$URL" "$SHA256")

SRC=$WORK/ca-certificates
rm -rf "$SRC"
tar -xJf "$TARBALL" -C "$WORK"
export LC_ALL=C
make -C "$SRC" clean all >"$WORK/build.log" 2>&1 || { tail -20 "$WORK/build.log" >&2; exit 1; }

mkdir -p "$OUT"
python3 - "$SRC/mozilla" "$OUT/ca-certificates.crt.part" <<'PY'
import sys
from pathlib import Path
source, output = Path(sys.argv[1]), Path(sys.argv[2])
names = sorted((p.name for p in source.glob("*.crt")), key=lambda n: n.encode())
output.write_bytes(b"".join((source / name).read_bytes() for name in names))
PY
echo "$BUNDLE_SHA256  $OUT/ca-certificates.crt.part" | sha256sum -c --quiet - || {
  echo "the CA bundle differs from the pinned one" >&2
  exit 1
}
mv "$OUT/ca-certificates.crt.part" "$OUT/ca-certificates.crt"
chmod 0644 "$OUT/ca-certificates.crt"
{
  echo "share/ca-certificates.crt is the Mozilla CA certificate store (NSS certdata.txt),"
  echo "converted to PEM by Debian ca-certificates $VERSION:"
  echo "$URL"
  echo "SHA-256 $SHA256"
  echo "Copyright: Mozilla Contributors"
  echo
  sed -n '2,4s/^# //p' "$SRC/mozilla/certdata.txt"
} > "$OUT/NOTICE"
grep -q 'Mozilla Public' "$OUT/NOTICE"
rm -rf "$SRC"

echo "ca-certificates.crt: $(grep -c 'BEGIN CERTIFICATE' "$OUT/ca-certificates.crt") certificates," \
  "$(stat -c %s "$OUT/ca-certificates.crt") bytes"
