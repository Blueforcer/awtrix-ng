#!/bin/sh
set -u
LABEL=de.blueforcer.awtrix.script-host
ACCOUNT=_awtrixhost
APP="/Library/Application Support/AWTRIX Script Host"
LOGS="/Library/Logs/AWTRIX Script Host"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
FIREWALL=/usr/libexec/ApplicationFirewall/socketfilterfw

everything=0
case "${1:-}" in
  "") ;;
  --all) everything=1 ;;
  *) echo "usage: sudo \"$APP/uninstall.sh\" [--all]   (--all also removes the scripts, the pairing and the account)" >&2; exit 2 ;;
esac
if [ "$(id -u)" != 0 ]; then
  echo "run it with sudo" >&2
  exit 1
fi

launchctl bootout "system/$LABEL" >/dev/null 2>&1
rm -f "$PLIST"
if [ -x "$FIREWALL" ]; then
  for program in awtrix-host awtrix-script-runner; do
    "$FIREWALL" --remove "$APP/bin/$program" >/dev/null 2>&1
  done
fi
rm -rf "$APP/bin" "$APP/share" "$APP/licenses" "$APP/uninstall.sh" "$LOGS"
pkgutil --forget de.blueforcer.awtrix.script-host >/dev/null 2>&1
pkgutil --forget de.blueforcer.awtrix.script-host.uninstall >/dev/null 2>&1

if [ "$everything" = 1 ]; then
  rm -rf "$APP"
  if dscl . -delete "/Users/$ACCOUNT" >/dev/null 2>&1; then
    dscl . -delete "/Groups/$ACCOUNT" >/dev/null 2>&1
    echo "AWTRIX Script Host removed, with its scripts and pairing."
  else
    echo "AWTRIX Script Host removed, with its scripts and pairing. macOS kept the hidden account $ACCOUNT;"
    echo "it owns no files and cannot sign in."
  fi
else
  echo "AWTRIX Script Host removed. Scripts and pairing stay in $APP/data for a new installation."
fi
exit 0
