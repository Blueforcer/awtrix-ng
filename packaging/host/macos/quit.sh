#!/bin/sh
PROGRAMS="^/Applications/AWTRIX Script Host.app/Contents/MacOS/"

gone_within() {
  tries=$(($1 * 4))
  while pgrep -f "$PROGRAMS" >/dev/null 2>&1; do
    [ "$tries" -le 0 ] && return 1
    sleep 0.25
    tries=$((tries - 1))
  done
  return 0
}

pkill -TERM -f "${PROGRAMS}AWTRIX Script Host" >/dev/null 2>&1
gone_within 15 && exit 0
pkill -KILL -f "$PROGRAMS" >/dev/null 2>&1
gone_within 5
exit 0
