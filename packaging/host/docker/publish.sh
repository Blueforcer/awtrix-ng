#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
REPO=${AWTRIX_IMAGE:-ghcr.io/blueforcer/awtrix-script-host}
VERSION=$(tr -d '[:space:]' < "$ROOT/version")
REGISTRY=${REPO%%/*}

if [ $# -ne 0 ]; then
  echo "usage: publish.sh  (pushes $REPO and $REPO-ha $VERSION-amd64 and $VERSION-arm64 of build.sh, then" >&2
  echo "  $VERSION of both as multi-arch lists and latest of $REPO; AWTRIX_REPUBLISH=1 replaces an existing $VERSION)" >&2
  exit 2
fi

for name in "$REPO" "$REPO-ha"; do
  for arch in amd64 arm64; do
    found=$(docker image inspect --format '{{.Os}}/{{.Architecture}}' "$name:$VERSION-$arch" 2>/dev/null) ||
      { echo "publish.sh: no image $name:$VERSION-$arch here (run build.sh first)" >&2; exit 1; }
    [ "$found" = "linux/$arch" ] || { echo "publish.sh: $name:$VERSION-$arch is $found" >&2; exit 1; }
  done
done

docker login "$REGISTRY"

for name in "$REPO" "$REPO-ha"; do
  if [ "${AWTRIX_REPUBLISH:-0}" != 1 ] && docker manifest inspect "$name:$VERSION" >/dev/null 2>&1; then
    echo "publish.sh: $name:$VERSION is published already (AWTRIX_REPUBLISH=1 replaces it)" >&2
    exit 1
  fi
done

publish_list() {
  local list=$1 base=$2
  docker manifest rm "$list" >/dev/null 2>&1 || true
  docker manifest create "$list" "$base-amd64" "$base-arm64"
  docker manifest annotate "$list" "$base-arm64" --os linux --arch arm64 --variant v8
  docker manifest push --purge "$list"
}

for name in "$REPO" "$REPO-ha"; do
  for arch in amd64 arm64; do docker push "$name:$VERSION-$arch"; done
  publish_list "$name:$VERSION" "$name:$VERSION"
done
publish_list "$REPO:latest" "$REPO:$VERSION"

for name in "$REPO" "$REPO-ha"; do docker manifest inspect "$name:$VERSION"; done
echo "Published. New packages on ghcr.io start private: set ${REPO##*/} and ${REPO##*/}-ha to public in the"
echo "package settings on GitHub (Packages > package > Package settings > Change visibility)."
