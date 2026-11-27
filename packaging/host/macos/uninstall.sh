#!/bin/sh
set -u
ID=de.blueforcer.awtrix.script-host
APP="/Applications/AWTRIX Script Host.app"
FIREWALL=/usr/libexec/ApplicationFirewall/socketfilterfw

if [ "$(id -u)" != 0 ]; then
  echo "run it with sudo" >&2
  exit 1
fi

/bin/sh "$(dirname "$0")/quit.sh"
user=$(stat -f %Su /dev/console 2>/dev/null)
if [ -n "$user" ] && [ "$user" != root ] && [ "$user" != loginwindow ]; then
  uid=$(id -u "$user")
  home=$(dscl . -read "/Users/$user" NFSHomeDirectory 2>/dev/null | sed -n 's/^NFSHomeDirectory: //p')
  if [ -x "$APP/Contents/MacOS/AWTRIX Script Host" ]; then
    launchctl asuser "$uid" sudo -u "$user" "$APP/Contents/MacOS/AWTRIX Script Host" --remove-login-item >/dev/null 2>&1
  fi
  [ -n "$home" ] && rm -f "$home/Library/LaunchAgents/$ID.plist"
  launchctl asuser "$uid" sudo -u "$user" defaults delete "$ID" StartAtLogin >/dev/null 2>&1
fi
if [ -x "$FIREWALL" ]; then
  for program in "AWTRIX Script Host" awtrix-host awtrix-script-runner; do
    "$FIREWALL" --remove "$APP/Contents/MacOS/$program" >/dev/null 2>&1
  done
fi
rm -rf "$APP"
pkgutil --forget "$ID" >/dev/null 2>&1
pkgutil --forget "$ID.uninstall" >/dev/null 2>&1
echo "AWTRIX Script Host removed. Scripts and pairing stay in ~/Library/Application Support/AWTRIX Script Host."
exit 0
