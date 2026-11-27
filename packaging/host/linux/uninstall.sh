#!/bin/sh
# Removes the AWTRIX script host: sudo /usr/local/lib/awtrix-script-host/uninstall.sh [--purge]
# Scripts and pairing stay in /var/lib/awtrix-script-host unless --purge is given.
set -eu

NAME=awtrix-script-host
APP=/usr/local/lib/$NAME
DATA=/var/lib/$NAME
UNIT=/etc/systemd/system/$NAME.service
ACCOUNT=awtrix-host

main() {
  purge=0
  case "${1:-}" in
    "") ;;
    --purge) purge=1 ;;
    *)
      echo "usage: sudo $APP/uninstall.sh [--purge]   (--purge also deletes the scripts, the pairing and the account)" >&2
      exit 2
      ;;
  esac
  if [ "$(id -u)" != 0 ]; then
    echo "Run it as root, for example with sudo." >&2
    exit 1
  fi
  if [ -d /run/systemd/system ]; then
    systemctl disable --now "$NAME" >/dev/null 2>&1 || true
  fi
  rm -f "$UNIT"
  if [ -d /run/systemd/system ]; then
    systemctl daemon-reload || true
    systemctl reset-failed "$NAME" >/dev/null 2>&1 || true
  fi
  rm -rf "$APP"
  if [ "$purge" = 1 ]; then
    rm -rf "$DATA"
    if id -u "$ACCOUNT" >/dev/null 2>&1; then
      userdel "$ACCOUNT" >/dev/null 2>&1 || echo "Could not remove the account $ACCOUNT." >&2
    fi
    if getent group "$ACCOUNT" >/dev/null 2>&1; then
      groupdel "$ACCOUNT" >/dev/null 2>&1 || true
    fi
    echo "AWTRIX script host removed, with its scripts and pairing."
  else
    echo "AWTRIX script host removed. Scripts and pairing stay in $DATA for a new installation."
  fi
}

main "$@"
