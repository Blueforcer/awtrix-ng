#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
if [ $# -ne 1 ] || [ -z "${AWTRIX_DEPS:-}" ]; then
  echo "usage: AWTRIX_DEPS=DIR build.sh WORKDIR  (DIR holds base64/ and Unity/ as pio pkg install -e native puts them;" >&2
  echo "  AWTRIX_TESTS=0 skips the tests; the packages land in WORKDIR/out)" >&2
  exit 2
fi
mkdir -p "$1"
WORK=$(cd "$1" && pwd)
VERSION=$(tr -d '[:space:]' < "$ROOT/version")
ID=de.blueforcer.awtrix.script-host
JOBS=$(sysctl -n hw.ncpu)
TESTS=${AWTRIX_TESTS:-1}
export MACOSX_DEPLOYMENT_TARGET=11.0

"$HERE/deps.sh" "$WORK/deps"
DEPS=$WORK/deps/out
testing=OFF
[ "$TESTS" = 1 ] && testing=ON
cmake -S "$ROOT" -B "$WORK/build" -DAWTRIX_HOST_ONLY=ON -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$testing" \
  "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
  -DAWTRIX_DEPS="$AWTRIX_DEPS" -DOPENSSL_ROOT_DIR="$DEPS" -DOPENSSL_USE_STATIC_LIBS=TRUE \
  -DJPEG_INCLUDE_DIR="$DEPS/include" -DJPEG_LIBRARY="$DEPS/lib/libjpeg.a" >"$WORK/configure.log"
cmake --build "$WORK/build" --parallel "$JOBS" >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; exit 1; }
if [ "$TESTS" = 1 ]; then
  (cd "$WORK/build" && ctest -j"$JOBS" --output-on-failure) >"$WORK/test.log" 2>&1 || { tail -60 "$WORK/test.log" >&2; exit 1; }
  grep "tests passed" "$WORK/test.log"
fi

STAGE=$WORK/stage
APP="$STAGE/root/Library/Application Support/AWTRIX Script Host"
rm -rf "$STAGE"
mkdir -p "$APP/bin" "$APP/share" "$APP/licenses" "$STAGE/root/Library/LaunchDaemons" "$STAGE/scripts" \
  "$STAGE/uninstall-scripts" "$WORK/out"
for program in awtrix-host awtrix-script-runner; do
  cp "$WORK/build/$program" "$APP/bin/$program"
  strip "$APP/bin/$program"
  codesign --force --sign - --identifier "de.blueforcer.awtrix.$program" "$APP/bin/$program" 2>/dev/null
  for arch in arm64 x86_64; do
    if otool -arch "$arch" -L "$APP/bin/$program" | tail -n +2 | grep -vE '^[[:space:]]+/usr/lib/'; then
      echo "$program ($arch) links a library outside /usr/lib" >&2
      exit 1
    fi
  done
done
cp "$ROOT/webui/index.html" "$APP/share/index.html"
cp "$HERE/uninstall.sh" "$APP/uninstall.sh"
cp "$ROOT/LICENSE.md" "$APP/licenses/AWTRIX-NG.md"
for license in MIT-Berry MIT-cpp-httplib MIT-densaugeo-base64 MIT-PubSubClient TJpg_Decoder; do
  cp "$ROOT/LICENSES/$license.txt" "$APP/licenses/"
done
cp "$DEPS/licenses/"*.txt "$APP/licenses/"
cp "$HERE/$ID.plist" "$STAGE/root/Library/LaunchDaemons/"
cp "$HERE/scripts/preinstall" "$HERE/scripts/postinstall" "$STAGE/scripts/"
cp "$HERE/uninstall-scripts/postinstall" "$HERE/uninstall.sh" "$STAGE/uninstall-scripts/"
chmod 755 "$APP/bin/"* "$APP/uninstall.sh" "$STAGE/scripts/"* "$STAGE/uninstall-scripts/"*
chmod 644 "$APP/share/index.html" "$APP/licenses/"* "$STAGE/root/Library/LaunchDaemons/$ID.plist"
plutil -lint "$STAGE/root/Library/LaunchDaemons/$ID.plist" >/dev/null

product() {
  local title=$1 id=$2 package=$3 output=$4
  sed -e "s|@TITLE@|$title|" -e "s|@ID@|$id|g" -e "s|@VERSION@|$VERSION|" -e "s|@PACKAGE@|$package|" \
    "$HERE/distribution.xml" > "$STAGE/$id.xml"
  productbuild --distribution "$STAGE/$id.xml" --package-path "$STAGE" "$output" >/dev/null
}

pkgbuild --root "$STAGE/root" --identifier "$ID" --version "$VERSION" --install-location / \
  --scripts "$STAGE/scripts" --ownership recommended "$STAGE/host.pkg" >/dev/null
pkgbuild --nopayload --identifier "$ID.uninstall" --version "$VERSION" --scripts "$STAGE/uninstall-scripts" \
  "$STAGE/uninstall.pkg" >/dev/null
product "AWTRIX Script Host" "$ID" host.pkg "$WORK/out/AWTRIX-Script-Host-macos.pkg"
product "Remove AWTRIX Script Host" "$ID.uninstall" uninstall.pkg "$WORK/out/AWTRIX-Script-Host-Uninstall-macos.pkg"
for file in "$WORK/out/"*.pkg "$APP/bin/"*; do echo "$(basename "$file"): $(stat -f %z "$file") bytes"; done
