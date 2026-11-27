#!/bin/sh
# Installs or updates the AWTRIX script host on Linux with systemd (64-bit x86 or ARM):
#   curl -fsSL https://raw.githubusercontent.com/Blueforcer/awtrix-ng/main/packaging/host/linux/install.sh | sudo sh
#   sudo sh install.sh --file AWTRIX-Script-Host-linux-x64.tar.gz
set -eu

BASE_URL=${AWTRIX_HOST_URL:-https://github.com/Blueforcer/awtrix-ng/releases/latest/download}
NAME=awtrix-script-host
APP=/usr/local/lib/$NAME
DATA=/var/lib/$NAME
UNIT=/etc/systemd/system/$NAME.service
ACCOUNT=awtrix-host
PORT=8300

fail() {
  echo "$*" >&2
  exit 1
}

have() { command -v "$1" >/dev/null 2>&1; }

fetch() {
  if have curl; then curl -fsSL --retry 3 -o "$2" "$1"
  elif have wget; then wget -q -O "$2" "$1"
  else fail "Needs curl or wget."
  fi
}

get() {
  if have curl; then curl -fsS -m 2 "$1" 2>/dev/null
  elif have wget; then wget -q -T 2 -O - "$1" 2>/dev/null
  else return 1
  fi
}

architecture() {
  case "$(uname -m)" in
    x86_64|amd64) echo x64 ;;
    aarch64|arm64|armv8*) echo arm64 ;;
    *) fail "The script host needs 64-bit Linux on x86 or ARM; this computer reports $(uname -m)." ;;
  esac
}

elf_machine() {
  od -An -tx1 -j18 -N2 "$1" 2>/dev/null | tr -d ' \n'
}

ca_file() {
  for bundle in /etc/ssl/certs/ca-certificates.crt /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/ca-bundle.pem \
                /etc/ssl/cert.pem; do
    if [ -s "$bundle" ]; then
      echo "$bundle"
      return
    fi
  done
  echo "$APP/share/ca-certificates.crt"
}

lan_address() {
  address=
  have ip && address=$(ip -4 route get 1.1.1.1 2>/dev/null | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -n 1)
  [ -n "$address" ] || address=$(hostname -I 2>/dev/null | cut -d' ' -f1)
  echo "${address:-localhost}"
}

firewall_hint() {
  if have ufw && ufw status 2>/dev/null | grep -q '^Status: active'; then
    echo
    echo "ufw is active. To let clocks on your network reach the script host:"
    echo "  sudo ufw allow 8300/tcp && sudo ufw allow 4211/udp && sudo ufw allow 8301:8399/tcp && sudo ufw allow 8301:8399/udp"
  elif have firewall-cmd && [ "$(firewall-cmd --state 2>/dev/null)" = running ]; then
    echo
    echo "firewalld is active. To let clocks on your network reach the script host:"
    echo "  sudo firewall-cmd --permanent --add-port=8300/tcp --add-port=4211/udp --add-port=8301-8399/tcp \\"
    echo "    --add-port=8301-8399/udp && sudo firewall-cmd --reload"
  fi
}

main() {
  package=
  while [ $# -gt 0 ]; do
    case "$1" in
      --file)
        [ $# -ge 2 ] || fail "usage: install.sh [--file AWTRIX-Script-Host-linux-ARCH.tar.gz]"
        package=$2
        shift 2
        ;;
      *) fail "usage: install.sh [--file AWTRIX-Script-Host-linux-ARCH.tar.gz]" ;;
    esac
  done
  [ "$(id -u)" = 0 ] || fail "Run it as root, for example with sudo."
  [ -d /run/systemd/system ] || fail "This installer needs systemd. Without it, use the Docker image."
  have useradd || fail "Needs useradd to create the account $ACCOUNT."
  arch=$(architecture)

  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  trap 'exit 1' INT TERM HUP
  if [ -z "$package" ]; then
    file=AWTRIX-Script-Host-linux-$arch.tar.gz
    echo "Downloading $file"
    fetch "$BASE_URL/$file" "$tmp/$file" || fail "Cannot download $BASE_URL/$file"
    fetch "$BASE_URL/$file.sha256" "$tmp/$file.sha256" || fail "Cannot download $BASE_URL/$file.sha256"
    expected=$(cut -d' ' -f1 < "$tmp/$file.sha256")
    actual=$(sha256sum "$tmp/$file" | cut -d' ' -f1)
    [ -n "$expected" ] && [ "$expected" = "$actual" ] || fail "$file does not match its checksum; try again."
    package=$tmp/$file
  fi
  [ -f "$package" ] || fail "No package at $package"
  mkdir "$tmp/package"
  tar -xzf "$package" -C "$tmp/package" || fail "Cannot unpack $package"
  src=$tmp/package/$NAME
  for part in bin/awtrix-host bin/awtrix-script-runner share/index.html share/ca-certificates.crt \
              share/$NAME.service uninstall.sh VERSION; do
    [ -f "$src/$part" ] || fail "$package is not a script host package for Linux."
  done
  case "$arch" in x64) machine=3e00 ;; arm64) machine=b700 ;; esac
  for program in awtrix-host awtrix-script-runner; do
    [ "$(elf_machine "$src/bin/$program")" = "$machine" ] ||
      fail "$package does not run on this computer ($(uname -m)); it needs AWTRIX-Script-Host-linux-$arch.tar.gz."
  done
  version=$(cat "$src/VERSION")

  if ! id -u "$ACCOUNT" >/dev/null 2>&1; then
    shell=$(command -v nologin || echo /bin/false)
    useradd --system --user-group --home-dir "$DATA" --no-create-home --shell "$shell" \
      --comment "AWTRIX script host" "$ACCOUNT" || fail "Cannot create the account $ACCOUNT."
  fi
  systemctl stop "$NAME" >/dev/null 2>&1 || true

  install -d -m 0755 "$APP" "$APP/bin" "$APP/share"
  rm -rf "$APP/licenses"
  install -d -m 0755 "$APP/licenses"
  install -m 0755 "$src/bin/awtrix-host" "$src/bin/awtrix-script-runner" "$APP/bin/"
  install -m 0644 "$src/share/index.html" "$src/share/ca-certificates.crt" "$APP/share/"
  install -m 0644 "$src/licenses/"* "$APP/licenses/"
  install -m 0644 "$src/VERSION" "$APP/VERSION"
  install -m 0755 "$src/uninstall.sh" "$APP/uninstall.sh"
  install -d -m 0700 -o "$ACCOUNT" -g "$ACCOUNT" "$DATA"
  chown "$ACCOUNT:$ACCOUNT" "$DATA"
  chmod 0700 "$DATA"
  sed "s|@CA_FILE@|$(ca_file)|" "$src/share/$NAME.service" > "$UNIT.new"
  chmod 0644 "$UNIT.new"
  mv "$UNIT.new" "$UNIT"
  systemctl daemon-reload
  systemctl enable "$NAME" >/dev/null 2>&1 || fail "Cannot enable the service $NAME."
  systemctl restart "$NAME" || fail "The service did not start. See: journalctl -u $NAME"

  status=
  tries=0
  while [ $tries -lt 30 ]; do
    status=$(get "http://127.0.0.1:$PORT/api/v1/status") && [ -n "$status" ] && break
    status=
    tries=$((tries + 1))
    sleep 1
  done
  [ -n "$status" ] || fail "The script host does not answer on port $PORT. See: journalctl -u $NAME"
  code=$(printf '%s' "$status" | sed -n 's/.*"code":"\([0-9]*\)".*/\1/p')

  echo
  echo "AWTRIX script host $version is running and starts with the computer."
  echo "Status page: http://$(lan_address):$PORT/"
  if [ -n "$code" ]; then
    echo "Pairing code: $code (valid 10 minutes)"
  fi
  echo "Pair a clock in its web UI under System > Script host. A new code shows on http://localhost:$PORT/"
  echo "on this computer, or with: journalctl -u $NAME | grep 'pairing code'"
  firewall_hint
  echo
  echo "Remove it with: sudo $APP/uninstall.sh   (--purge also deletes the scripts and the pairing)"
}

main "$@"
