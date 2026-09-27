#!/usr/bin/env bash
# Proves the HTTP API behaves like an Ulanzi's: one call per line, PASS or FAIL, then a summary.
# Exits non-zero if any call fails. Every write is undone, so it can run against a live device
# again and again. Needs curl and jq.
#
#   tools/pico/smoke.sh http://awtrixng-xxxxxx.local   (a Galactic Unicorn)
#   tools/pico/smoke.sh http://127.0.0.1:8080          (native_sim)
#
# Where the Pico and the simulator legitimately differ, the target is read from
# /api/v1/device (boardType) and /api/v1/capabilities, and the documented answer for it is
# expected: scripting, MP3 and radio routes answer 503 "unavailable" when the build leaves them
# out, /update is 503 on the Pico and 501 in the simulator, and the Pico has no sensors.
# Responses may contain configuration; this script prints only status codes and verdicts.
set -u

base="${1:?usage: $0 <base-url>}"
base="${base%/}"
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
name=smoketest   # every name this script creates; cleanup() removes all of them
json='Content-Type: application/json'
passed=0
failed=0

verdict() { # verdict OK LABEL DETAIL
  if [[ "$1" == 1 ]]; then
    passed=$((passed + 1))
    printf 'PASS  %-46s %s\n' "$2" "$3"
  else
    failed=$((failed + 1))
    printf 'FAIL  %-46s %s\n' "$2" "$3"
  fi
}

# call LABEL EXPECT METHOD PATH [curl args...]
# EXPECT is a status regex such as 200 or '200|201'. The body lands in $tmp/body; every non-empty
# body must be valid JSON unless RAW=1 is set for the call.
call() {
  local label=$1 expect=$2 method=$3 path=$4 out ok=1 note=""
  shift 4
  out="$(curl -sS -m 45 -o "$tmp/body" -w '%{http_code} %{time_total}' -X "$method" "$@" \
    "$base$path" 2>"$tmp/err")"
  read -r code secs <<<"$out"
  [[ "$code" =~ ^($expect)$ ]] || { ok=0; note="want $expect $(tr '\n' ' ' <"$tmp/err")"; }
  if [[ "${RAW:-0}" != 1 && -s "$tmp/body" ]] && ! jq -e . "$tmp/body" >/dev/null 2>&1; then
    ok=0
    note="$note invalid JSON: $(head -c 120 "$tmp/body")"
  fi
  verdict "$ok" "$method $path ($label)" "$code ${secs}s $note"
  [[ "$ok" == 1 ]]
}

# check LABEL JQ-EXPR [jq args...]: evaluates JQ-EXPR against the last body.
check() {
  local label=$1 expr=$2
  shift 2
  if jq -e "$@" "$expr" "$tmp/body" >/dev/null 2>&1; then
    verdict 1 "  $label" ""
  else
    verdict 0 "  $label" "($expr)"
  fi
}

# get PATH: a silent read for undo bookkeeping, not counted.
get() { curl -sS -m 45 "$base$1"; }

cleanup() {
  # Best effort, so an interrupted run leaves nothing behind for the next one.
  for p in "/api/v1/notifications/$name" "/api/v1/apps/$name" "/api/v1/audio/melodies/$name" \
           "/api/v1/files?path=/ICONS/$name.gif"; do
    curl -s -m 45 -o /dev/null -X DELETE "$base$p"
  done
  rm -r "$tmp"
}
trap cleanup EXIT

echo "smoke: $base"

# --- what are we talking to -----------------------------------------------------------------
call "capabilities" 200 GET /api/v1/capabilities
check "gpio.soc is named" '.gpio.soc | type == "string"'
cp "$tmp/body" "$tmp/caps"
scripting=$(jq -r '.scripting' "$tmp/caps")
mp3=$(jq -r '.audio.mp3' "$tmp/caps")
radio=$(jq -r '.audio.radio' "$tmp/caps")
soc=$(jq -r '.gpio.soc' "$tmp/caps")

call "device" 200 GET /api/v1/device
cp "$tmp/body" "$tmp/device"
board=$(jq -r '.boardType' "$tmp/device")
if [[ "$soc" == rp2040 ]]; then target=pico
elif [[ "$board" == simulator ]]; then target=sim
else target=esp32
fi
echo "smoke: target $target (soc $soc, boardType $board, scripting $scripting, mp3 $mp3, radio $radio)"
check "fps > 0" '.fps > 0'
if [[ "$target" == pico ]]; then
  # The free block at the top of the heap, never more than the total free.
  check "largestFreeBlockBytes in (0, freeHeapBytes]" \
    '.largestFreeBlockBytes > 0 and .largestFreeBlockBytes <= .freeHeapBytes'
  check "no sensors on the Pico" \
    'has("temperature") or has("humidity") or has("batteryPercent") | not'
else
  check "largestFreeBlockBytes in [0, freeHeapBytes]" \
    '.largestFreeBlockBytes >= 0 and .largestFreeBlockBytes <= .freeHeapBytes'
fi
check "resetReason is a string" '.resetReason | type == "string"'
call "version" 200 GET /api/v1/version
call "system" 200 GET /api/v1/system
check "panel geometry is numeric" '(.panelWidth | type == "number") and (.panelHeight | type == "number")'
check "secrets are omitted" 'has("wifiPass") or has("mqttPass") or has("authPass") | not'
call "screen" 200 GET /api/v1/display/screen
check "pixels = width x height" '(.pixels | length) == .width * .height and .width > 0'

# --- settings: change one, restore it, and the three refusals --------------------------------
call "settings" 200 GET /api/v1/settings
orig=$(jq -r '.uppercase' "$tmp/body")
flip=$([[ "$orig" == true ]] && echo false || echo true)
call "change uppercase" 200 PATCH /api/v1/settings -H "$json" -d "{\"uppercase\":$flip}"
check "uppercase is now $flip" ".uppercase == $flip"
call "restore uppercase" 200 PATCH /api/v1/settings -H "$json" -d "{\"uppercase\":$orig}"
check "uppercase is back to $orig" ".uppercase == $orig"
call "invalid setting" 422 PATCH /api/v1/settings -H "$json" -d '{"brightness":999}'
check "validationFailed on brightness" '.error.code == "validationFailed" and .error.field == "brightness"'
call "unknown setting" 422 PATCH /api/v1/settings -H "$json" -d '{"noSuchSetting":1}'
check "validationFailed on noSuchSetting" '.error.field == "noSuchSetting"'
call "wrong content type" 415 PATCH /api/v1/settings -H 'Content-Type: text/plain' -d '{"uppercase":true}'
check "unsupportedMediaType" '.error.code == "unsupportedMediaType"'
printf '{"textColor":"%s"}' "$(head -c 9000 /dev/zero | tr '\0' a)" >"$tmp/big.json"
call "oversized body" 413 PATCH /api/v1/settings -H "$json" --data-binary "@$tmp/big.json"
check "payloadTooLarge" '.error.code == "payloadTooLarge"'
call "settings unchanged" 200 GET /api/v1/settings
check "uppercase still $orig" ".uppercase == $orig"

# --- an icon, uploaded and then used -------------------------------------------------------
# A 1x1 GIF; the upload checks the magic bytes.
printf 'GIF89a\x01\x00\x01\x00\x80\x00\x00\xff\x00\x00\x00\x00\x00!\xf9\x04\x00\x00\x00\x00\x00,\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02D\x01\x00;' \
  >"$tmp/$name.gif"
call "upload icon" 200 POST "/api/v1/files?dir=/ICONS" -F "file=@$tmp/$name.gif"
call "list icons" 200 GET "/api/v1/files?dir=/ICONS"
check "$name.gif is listed" '[.files[].name | ltrimstr("/ICONS/")] | index($n) != null' --arg n "$name.gif"
RAW=1 call "serve icon" 200 GET "/ICONS/$name.gif"
printf 'hello' >"$tmp/not-an-icon.gif"
call "icon that is not an image" 415 POST "/api/v1/files?dir=/ICONS" -F "file=@$tmp/not-an-icon.gif;filename=$name.gif"
RAW=1 call "icon kept after a refused replacement" 200 GET "/ICONS/$name.gif"
cmp -s "$tmp/body" "$tmp/$name.gif" && verdict 1 "  same bytes as uploaded" "" \
  || verdict 0 "  same bytes as uploaded" "(the refused upload replaced or removed it)"
# The same GIF in blue, uploaded over the first.
printf 'GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\xff\x00\x00\x00!\xf9\x04\x00\x00\x00\x00\x00,\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02D\x01\x00;' \
  >"$tmp/replacement.gif"
call "replace icon" 200 POST "/api/v1/files?dir=/ICONS" -F "file=@$tmp/replacement.gif;filename=$name.gif"
RAW=1 call "serve replaced icon" 200 GET "/ICONS/$name.gif"
cmp -s "$tmp/body" "$tmp/replacement.gif" && verdict 1 "  new bytes served" "" \
  || verdict 0 "  new bytes served" "(still the old icon)"
call "list icons" 200 GET "/api/v1/files?dir=/ICONS"
check "listed once, nothing left beside it" \
  '[.files[].name | ltrimstr("/ICONS/") | select(startswith($n))] == [$n]' --arg n "$name.gif"

# --- notification: post, then dismiss --------------------------------------------------------
call "notify" 200 POST /api/v1/notifications -H "$json" \
  -d "{\"name\":\"$name\",\"text\":\"smoke\",\"textColor\":\"#00FF00\",\"icon\":\"$name\",\"hold\":true}"
# 200 only when a notification of that name was queued (a second dismiss is a 404).
call "dismiss by name" 200 DELETE "/api/v1/notifications/$name"
call "notification with color" 422 POST /api/v1/notifications -H "$json" -d '{"text":"x","color":"#FF0000"}'
check "color is not a payload key" '.error.field == "color"'

# --- a pushed app: put, then delete -----------------------------------------------------------
call "push app" 200 PUT "/api/v1/apps/pushed/$name" -H "$json" \
  -d "{\"text\":\"smoke\",\"icon\":\"$name\",\"textColor\":\"#FF00FF\"}"
call "apps" 200 GET /api/v1/apps
check "$name is pushed, with its icon" \
  '.[] | select(.name == $n) | .origin == "pushed" and .present and .icon == $n' --arg n "$name"
call "empty push" 422 PUT "/api/v1/apps/pushed/$name" -H "$json" -d '{}'
call "delete app" 200 DELETE "/api/v1/apps/$name"
call "apps" 200 GET /api/v1/apps
check "$name is gone" '[.[] | select(.name == $n and .present)] | length == 0' --arg n "$name"

# --- app switch, next, previous, order -------------------------------------------------------
current=$(jq -r '.currentApp' "$tmp/device")
# Any other app in the loop: which built-ins are enabled is the user's choice (a device may have
# Date switched off), so the target is read, not assumed.
other=$(get /api/v1/apps | jq -r --arg c "$current" \
  '[.[] | select(.inLoop and .present != false and .name != $c) | .name][0] // empty')
if [[ -n "$other" ]]; then
  call "switch to $other" 200 PUT /api/v1/apps/active -H "$json" -d "{\"name\":\"$other\",\"fast\":true}"
  call "device" 200 GET /api/v1/device
  check "currentApp is $other" '.currentApp == $o' --arg o "$other"
else
  echo "skip  PUT /api/v1/apps/active (no second app in the loop)"
fi
# An explicit empty body, as a browser sends: the simulator's HTTP library waits 5 s for the body
# of a POST that has no Content-Length at all, then answers 400 (a known simulator gap).
call "next" 200 POST /api/v1/apps/next --data ''
call "previous" 200 POST /api/v1/apps/previous --data ''
call "unknown app" 404 PUT /api/v1/apps/active -H "$json" -d "{\"name\":\"$name-none\"}"
call "switch back to $current" 200 PUT /api/v1/apps/active -H "$json" -d "{\"name\":\"$current\",\"fast\":true}"

get /api/v1/apps >"$tmp/apps"
order=$(jq -c '[.[] | select(.slot != null)] | sort_by(.slot) | map(.name)' "$tmp/apps")
disabled=$(jq -c '[.[] | select(.enabled == false) | .name]' "$tmp/apps")
if [[ $(jq 'length' <<<"$order") -ge 2 ]]; then
  swapped=$(jq -c '.[1:2] + .[0:1] + .[2:]' <<<"$order")
  call "reorder" 200 PUT /api/v1/apps/order -H "$json" -d "{\"order\":$swapped,\"disabled\":$disabled}"
  call "apps" 200 GET /api/v1/apps
  check "order is swapped" '[.[] | select(.slot != null)] | sort_by(.slot) | map(.name) == $o' \
    --argjson o "$swapped"
  call "restore order" 200 PUT /api/v1/apps/order -H "$json" -d "{\"order\":$order,\"disabled\":$disabled}"
  call "apps" 200 GET /api/v1/apps
  check "order is restored" '[.[] | select(.slot != null)] | sort_by(.slot) | map(.name) == $o' \
    --argjson o "$order"
else
  # Nothing arranged yet: an order would create one that could not be taken back.
  call "order, disabled only" 200 PUT /api/v1/apps/order -H "$json" -d "{\"disabled\":$disabled}"
fi
call "order without disabled" 400 PUT /api/v1/apps/order -H "$json" -d "$order"

# --- indicators ------------------------------------------------------------------------------
ind=$(jq -c '.indicators[0]' "$tmp/device")
call "indicator 1 on" 200 PUT /api/v1/indicators/1 -H "$json" -d '{"color":"#FF0000","blinkMs":500}'
call "device" 200 GET /api/v1/device
check "indicator 1 is on" '.indicators[0].on and .indicators[0].blinkMs == 500'
if [[ $(jq -r '.on' <<<"$ind") == true ]]; then
  call "restore indicator 1" 200 PUT /api/v1/indicators/1 -H "$json" -d "$ind"
else
  call "indicator 1 off" 200 DELETE /api/v1/indicators/1
fi
call "device" 200 GET /api/v1/device
check "indicator 1 as it was" '.indicators[0] == $i' --argjson i "$ind"
call "indicator 4" 404 PUT /api/v1/indicators/4 -H "$json" -d '{"color":"#FF0000"}'

# --- moodlight -------------------------------------------------------------------------------
mood=$(get /api/v1/display | jq -c '.moodlight')
call "moodlight on" 200 PUT /api/v1/display/moodlight -H "$json" -d '{"kelvin":2700,"brightness":90}'
call "display" 200 GET /api/v1/display
check "moodlight is on" '.moodlight != null'
if [[ "$mood" == null ]]; then
  call "moodlight off" 200 DELETE /api/v1/display/moodlight
else
  call "restore moodlight" 200 PUT /api/v1/display/moodlight -H "$json" -d "$mood"
fi
call "display" 200 GET /api/v1/display
check "moodlight as it was" '.moodlight == $m' --argjson m "$mood"

# --- a melody file ---------------------------------------------------------------------------
call "save melody" 201 PUT "/api/v1/audio/melodies/$name" -H "$json" -d '{"rtttl":"d=4,o=5,b=100:e,c"}'
call "melodies" 200 GET /api/v1/audio/melodies
check "$name parses, titled by its name" \
  '.melodies[] | select(.name == $n) | .valid and (.rtttl | startswith($n + ":"))' --arg n "$name"
RAW=1 call "serve melody" 200 GET "/MELODIES/$name.txt"
call "delete melody" 200 DELETE "/api/v1/audio/melodies/$name"
call "delete it again" 404 DELETE "/api/v1/audio/melodies/$name"

# --- features this build leaves out: 503 "unavailable", as on a classic ESP32 ------------------
unavailable() { check "unavailable" '.error.code == "unavailable"'; }
if [[ "$scripting" == false ]]; then
  call "no scripting" 503 GET "/api/v1/apps/script/$name" && unavailable
  call "no scripting" 503 PUT "/api/v1/apps/script/$name" -H 'Content-Type: text/plain' -d 'print(1)' && unavailable
  call "no scripting" 503 GET /api/v1/scripts/shared && unavailable
  call "no scripting" 503 GET "/api/v1/apps/$name/config" && unavailable
else
  call "scripting" 200 GET /api/v1/scripts/shared
  # Script source is exempt from the 8 KB body cap, updates included: a big update of a missing
  # script is a 404, not a 413.
  printf '{"expected_source":"%s","source":"x"}' "$(head -c 9000 /dev/zero | tr '\0' a)" \
    >"$tmp/big-update.json"
  call "big update of a missing script" 404 PUT "/api/v1/apps/script-update/$name" -H "$json" \
    --data-binary "@$tmp/big-update.json"
fi
if [[ "$mp3" == false ]]; then
  call "no MP3" 503 GET /api/v1/audio/mp3 && unavailable
  call "no MP3" 503 POST /api/v1/audio/play -H "$json" -d '{"mp3":"song"}' && unavailable
  call "no MP3" 503 DELETE /api/v1/audio/mp3/song && unavailable
elif [[ "$target" == sim ]]; then
  echo "skip  GET /api/v1/audio/mp3 (known gap: the simulator has no such route)"
else
  call "MP3 list" 200 GET /api/v1/audio/mp3
fi
if [[ "$radio" == false ]]; then
  call "no radio" 503 PUT /api/v1/audio/stations -H "$json" -d '[]' && unavailable
  call "no radio" 503 POST /api/v1/audio/play -H "$json" -d '{"station":0}' && unavailable
fi
call "audio" 200 GET /api/v1/audio
# /update is only exercised where it cannot replace the firmware.
printf 'not firmware' >"$tmp/firmware.bin"
case "$target" in
  pico) call "no browser update" 503 POST /update -F "firmware=@$tmp/firmware.bin" && unavailable ;;
  sim)  call "simulator has no firmware" 501 POST /update -F "firmware=@$tmp/firmware.bin" ;;
  *)    echo "skip  POST /update (a real ESP32 would take it)" ;;
esac

# --- the web UI's parallel load --------------------------------------------------------------
if "$here/burst.sh" "$base" >"$tmp/burst" 2>&1; then
  verdict 1 "burst.sh (8 parallel GETs x 10 rounds)" "$(tail -1 "$tmp/burst")"
else
  grep -v ' ok ' "$tmp/burst" | sed 's/^/      /'
  verdict 0 "burst.sh (8 parallel GETs x 10 rounds)" "$(tail -1 "$tmp/burst")"
fi

# --- leave nothing behind --------------------------------------------------------------------
call "delete icon" 200 DELETE "/api/v1/files?path=/ICONS/$name.gif"
call "list icons" 200 GET "/api/v1/files?dir=/ICONS"
check "$name.gif is gone" '[.files[].name | ltrimstr("/ICONS/")] | index($n) == null' --arg n "$name.gif"
call "apps" 200 GET /api/v1/apps
check "no $name app left" '[.[] | select(.name | startswith($n))] | length == 0' --arg n "$name"

echo "smoke: $passed passed, $failed failed"
[[ "$failed" -eq 0 ]]
