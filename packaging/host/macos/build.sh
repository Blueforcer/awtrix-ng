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
APP="$STAGE/root/AWTRIX Script Host.app"
PROGRAMS="$APP/Contents/MacOS"
LICENSES="$APP/Contents/Resources/licenses"
rm -rf "$STAGE"
mkdir -p "$STAGE/root" "$STAGE/scripts" "$STAGE/uninstall-scripts" "$WORK/out"
cp -R "$WORK/build/AWTRIX Script Host.app" "$APP"
mkdir -p "$LICENSES"
cp "$ROOT/LICENSE.md" "$LICENSES/AWTRIX-NG.md"
for license in MIT-Berry MIT-cpp-httplib MIT-densaugeo-base64 MIT-PubSubClient TJpg_Decoder; do
  cp "$ROOT/LICENSES/$license.txt" "$LICENSES/"
done
cp "$DEPS/licenses/"*.txt "$LICENSES/"
xattr -cr "$APP"
for program in awtrix-host awtrix-script-runner "AWTRIX Script Host"; do
  system='/usr/lib/'
  [ "$program" = "AWTRIX Script Host" ] && system='(/usr/lib/|/System/Library/Frameworks/)'
  strip "$PROGRAMS/$program"
  for arch in arm64 x86_64; do
    if otool -arch "$arch" -L "$PROGRAMS/$program" | tail -n +2 | grep -vE "^[[:space:]]+$system"; then
      echo "$program ($arch) links a library outside the system" >&2
      exit 1
    fi
  done
done
for program in awtrix-host awtrix-script-runner; do
  codesign --force --sign - --identifier "de.blueforcer.awtrix.$program" "$PROGRAMS/$program" 2>/dev/null
done
codesign --force --sign - "$APP" 2>/dev/null
codesign --verify --strict --deep "$APP"
plutil -lint "$APP/Contents/Info.plist" >/dev/null
chmod -R u=rwX,go=rX "$APP"
cp "$HERE/scripts/preinstall" "$HERE/scripts/postinstall" "$HERE/quit.sh" "$STAGE/scripts/"
cp "$HERE/uninstall-scripts/postinstall" "$HERE/uninstall.sh" "$HERE/quit.sh" "$STAGE/uninstall-scripts/"
chmod 755 "$STAGE/scripts/"* "$STAGE/uninstall-scripts/"*

product() {
  local title=$1 id=$2 package=$3 resources=$4 output=$5 pages="" page
  for page in welcome conclusion; do
    if [ -f "$resources/$page.html" ]; then pages="$pages<$page file=\"$page.html\" mime-type=\"text/html\"/>"; fi
  done
  sed -e "s|@TITLE@|$title|" -e "s|@ID@|$id|g" -e "s|@VERSION@|$VERSION|" -e "s|@PACKAGE@|$package|" \
    -e "s|@PAGES@|$pages|" "$HERE/distribution.xml" > "$STAGE/$id.xml"
  productbuild --distribution "$STAGE/$id.xml" --resources "$resources" --package-path "$STAGE" "$output" >/dev/null
}

pkgbuild --analyze --root "$STAGE/root" "$STAGE/component.plist" >/dev/null
plutil -replace 0.BundleIsRelocatable -bool NO "$STAGE/component.plist"
plutil -replace 0.BundleIsVersionChecked -bool NO "$STAGE/component.plist"
pkgbuild --root "$STAGE/root" --component-plist "$STAGE/component.plist" --identifier "$ID" --version "$VERSION" \
  --install-location /Applications --scripts "$STAGE/scripts" --ownership recommended "$STAGE/host.pkg" >/dev/null
if pkgutil --payload-files "$STAGE/host.pkg" | grep -F '/._'; then
  echo "the package carries extended attributes" >&2
  exit 1
fi
pkgbuild --nopayload --identifier "$ID.uninstall" --version "$VERSION" --scripts "$STAGE/uninstall-scripts" \
  "$STAGE/uninstall.pkg" >/dev/null
product "AWTRIX Script Host" "$ID" host.pkg "$HERE/resources" "$WORK/out/AWTRIX-Script-Host-macos.pkg"
product "Remove AWTRIX Script Host" "$ID.uninstall" uninstall.pkg "$HERE/uninstall-resources" \
  "$WORK/out/AWTRIX-Script-Host-Uninstall-macos.pkg"
for file in "$WORK/out/"*.pkg "$PROGRAMS/"*; do echo "$(basename "$file"): $(stat -f %z "$file") bytes"; done
