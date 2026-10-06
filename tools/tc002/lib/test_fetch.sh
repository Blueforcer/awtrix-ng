#!/usr/bin/env bash
# Tests tc002_fetch with file:// URLs: a download, a cached file, a mismatching cached file and a
# mismatching or failed download.
set -euo pipefail
command -v curl >/dev/null || { echo "curl is missing" >&2; exit 77; }
. "$(dirname "$0")/fetch.sh"

root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
mkdir "$root/remote"
printf 'tarball\n' > "$root/remote/good.tar"
good=$(sha256sum < "$root/remote/good.tar" | cut -d' ' -f1)
TC002_DL_DIR=$root/dl
fail() { echo "FAIL: $*" >&2; exit 1; }

path=$(tc002_fetch good.tar "file://$root/remote/good.tar" "$good") || fail "download"
[ "$path" = "$root/dl/good.tar" ] && cmp -s "$path" "$root/remote/good.tar" || fail "downloaded file"
rm "$root/remote/good.tar"
[ "$(tc002_fetch good.tar "file://$root/remote/good.tar" "$good")" = "$path" ] || fail "cached file"

printf 'changed\n' > "$path"
if message=$(tc002_fetch good.tar "file://$root/remote/good.tar" "$good" 2>&1); then fail "changed cache accepted"; fi
[[ $message == *"remove it to download it again"* ]] && [ "$(cat "$path")" = changed ] || fail "changed cache: $message"

printf 'other\n' > "$root/remote/other.tar"
if message=$(tc002_fetch other.tar "file://$root/remote/other.tar" "$good" 2>&1); then fail "bad download accepted"; fi
[[ $message == *"does not have the pinned SHA-256"* ]] || fail "bad download: $message"
[ ! -e "$root/dl/other.tar" ] && [ ! -e "$root/dl/other.tar.part" ] || fail "bad download kept"

if message=$(tc002_fetch gone.tar "file://$root/remote/gone.tar" "$good" 2>&1); then fail "missing source accepted"; fi
[[ $message == *"cannot download file://$root/remote/gone.tar" ]] && [ ! -e "$root/dl/gone.tar.part" ] ||
  fail "missing source: $message"
echo "tc002_fetch: 5 cases pass"
