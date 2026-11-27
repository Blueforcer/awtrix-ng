#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
REPO=${AWTRIX_IMAGE:-ghcr.io/blueforcer/awtrix-script-host}

if [ $# -ne 1 ]; then
  echo "usage: build.sh WORKDIR  (the images of the script host from WORKDIR/out/docker/rootfs-ARCH.tar of" >&2
  echo "  packaging/host/linux/build.sh: $REPO and $REPO-ha, VERSION-amd64 and VERSION-arm64, as archives in" >&2
  echo "  WORKDIR/out/docker and, when docker is there, loaded with VERSION on this machine's architecture;" >&2
  echo "  SOURCE_DATE_EPOCH as for build.sh, AWTRIX_LOAD=0 only writes the archives)" >&2
  exit 2
fi
OUT=$(cd "$1" && pwd)/out/docker
VERSION=$(tr -d '[:space:]' < "$ROOT/version")
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}

archives=()
for arch in amd64 arm64; do
  rootfs=$OUT/rootfs-$arch.tar
  [ -f "$rootfs" ] || { echo "build.sh: missing $rootfs (packaging/host/linux/build.sh makes it)" >&2; exit 1; }
  for variant in docker ha; do
    case "$variant" in docker) name=$REPO ;; ha) name=$REPO-ha ;; esac
    archive=$OUT/${name##*/}-$VERSION-$arch.tar
    python3 -I "$HERE/image.py" --rootfs "$rootfs" --arch "$arch" --variant "$variant" --version "$VERSION" \
      --tag "$name:$VERSION-$arch" --out "$archive"
    archives+=("$archive")
  done
done

if [ "${AWTRIX_LOAD:-1}" = 1 ] && command -v docker >/dev/null; then
  case "$(uname -m)" in x86_64) native=amd64 ;; aarch64|arm64) native=arm64 ;; *) native= ;; esac
  for archive in "${archives[@]}"; do docker load -q -i "$archive"; done
  if [ -n "$native" ]; then
    for name in "$REPO" "$REPO-ha"; do docker tag "$name:$VERSION-$native" "$name:$VERSION"; done
  fi
  docker image ls --format '{{.Repository}}:{{.Tag}} {{.ID}} {{.Size}}' | grep -F "$REPO" | sort
fi
