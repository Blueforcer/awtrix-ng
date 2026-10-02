#!/usr/bin/env bash
# Loads the device the way a browser loads the web UI: eight GETs at once, every round.
# Prints one line per request and exits non-zero if any request is not a 200 or takes > 45 s.
#
#   tools/pico/burst.sh http://awtrixng-xxxxxx.local [rounds]    (default 10 rounds)
set -u

base="${1:?usage: $0 <base-url> [rounds]}"
base="${base%/}"
rounds="${2:-10}"
paths=(/ /api/v1/device /api/v1/settings /api/v1/apps /api/v1/display /api/v1/capabilities
       /api/v1/logs /api/v1/system)
tmp="$(mktemp -d)"
trap 'rm -r "$tmp"' EXIT

failed=0
for ((round = 1; round <= rounds; round++)); do
  for i in "${!paths[@]}"; do
    # -w writes code and time even when curl fails; stderr carries the curl error text.
    curl -sS -m 45 -o /dev/null -w '%{http_code} %{time_total}' "$base${paths[$i]}" \
      >"$tmp/$i.out" 2>"$tmp/$i.err" &
  done
  wait
  for i in "${!paths[@]}"; do
    read -r code secs <"$tmp/$i.out"
    err="$(tr '\n' ' ' <"$tmp/$i.err")"
    verdict=ok
    if [[ "$code" != 200 ]] || awk -v t="$secs" 'BEGIN { exit !(t > 45) }'; then
      verdict=FAIL
      failed=$((failed + 1))
    fi
    printf 'round %2d  %-22s %s  %7.3fs  %-4s %s\n' "$round" "${paths[$i]}" "$code" "$secs" \
      "$verdict" "$err"
  done
done

total=$((rounds * ${#paths[@]}))
echo "burst: $((total - failed))/$total requests ok"
[[ "$failed" -eq 0 ]]
