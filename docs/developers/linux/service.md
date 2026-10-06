---
only: [tc002]
---

# Running as a systemd service

An optional systemd unit runs `awtrix-linux` in [hardened mode](security.md) as an isolated
system service with a dynamic, non-root user. It is for a Linux host with systemd 250 or newer.
The TC002 does not use it; there a supervisor starts the program (see
[AWTRIX NG on the TC002](../tc002/index.md)).

The unit template is
[`packaging/linux/awtrix-ng.service.in`](https://github.com/Blueforcer/awtrix-ng/blob/main/packaging/linux/awtrix-ng.service.in).
CMake fills in the install paths and writes `awtrix-ng.service` into the build directory.
`cmake --install` places it under `share/awtrix-ng/systemd/` of the install prefix, next to the
web UI in `share/awtrix-ng/`. Installing does not enable anything.

## How it works

- **Credentials.** The unit loads `admin.token`, `tls.crt` and `tls.key` from `/etc/awtrix-ng`
  through systemd's credential mechanism (`LoadCredential=`). systemd hands the service private
  copies owned by the service user with no group or other access, which meets the application's
  file rules. The unit ships no default credentials. Missing files, wrong permissions or invalid
  TLS material stop the start.
- **Settings.** `/etc/awtrix-ng/service.env` supplies `AWTRIX_ORIGIN`, `AWTRIX_LISTEN` and
  `AWTRIX_PORT`. The unit defaults are `127.0.0.1` and `8443`; the origin has no default.
- **Identity and privileges.** `DynamicUser=yes`, no Linux capabilities, `NoNewPrivileges=yes`.
  The port must be above 1023, since no privileged-bind capability is granted.
- **Isolation.** The filesystem is read-only except for the state directory; home directories
  and physical devices are hidden. Namespace creation, kernel tunables and modules, real-time
  scheduling, writable-and-executable memory and system calls outside `@system-service` are
  blocked. Network access stays, for HTTP and MQTT. See the
  [systemd execution model](https://www.freedesktop.org/software/systemd/man/latest/systemd.exec.html).
- **State.** `StateDirectory=awtrix-ng` keeps data in `/var/lib/awtrix-ng` across restarts.
  With a dynamic user, systemd manages its owner and may expose it through `/var/lib/private`;
  do not assign a fixed user ID to it. Core dumps are off, `/tmp` is private and the process
  count is limited to 128.
- **Restarts.** `Restart=on-failure` after 3 s, at most three starts per minute. Stop uses
  SIGTERM, which lets the application save its state (15 s timeout).
- **MQTT.** The shipped unit does not pass `--mqtt-ca-file`, so MQTT stays off even when the
  settings enable it. See [Enable MQTT](#enable-mqtt).

Reboot and sleep requests from the application stop the process; they never reboot or power off
the host. A factory reset clears the application state and keeps the external credentials.
Restart the service yourself after these requests.

## Set up the service

Build and install the CMake target with your chosen prefix (see
[AWTRIX on Linux](index.md#build)).

Create the credentials with the helper from [HTTPS administration](security.md). Both commands
run as the owner of the directory, root here:

```sh
sudo install -d -m 0700 -o root -g root /etc/awtrix-ng
sudo python3 tools/linux/provision.py generate --dir /etc/awtrix-ng \
  --origin https://device.example:8443
sudo python3 tools/linux/provision.py check --dir /etc/awtrix-ng \
  --origin https://device.example:8443
```

This leaves root-owned files in `/etc/awtrix-ng`: the directory with mode `0700`, the token and
key with `0600`, the certificate with `0644`.

To use a certificate from your own certificate authority, keep the `admin.token` from the
`generate` run, then replace the self-signed certificate and key with the issued material,
root-owned with the same modes, and validate the set against the CA. `chain.pem` holds the leaf
first, followed by its issuers:

```sh
sudo install -m 0644 -o root -g root chain.pem /etc/awtrix-ng/tls.crt
sudo install -m 0600 -o root -g root leaf.key /etc/awtrix-ng/tls.key
sudo python3 tools/linux/provision.py check --dir /etc/awtrix-ng \
  --origin https://device.example:8443 --ca-file ca.pem
```

Never put these files into the repository or into application backups.

Create `/etc/awtrix-ng/service.env`, owned by root with mode `0600`:

```ini
AWTRIX_ORIGIN=https://device.example:8443
AWTRIX_LISTEN=0.0.0.0
AWTRIX_PORT=8443
```

Use the same origin you gave `generate` or `check`; its host is in the certificate's Subject
Alternative Name. For access from the local host only, use `https://localhost:8443` for both and
the listen address `127.0.0.1`.

Copy the unit into the system unit directory, reload systemd and enable it. For the default CMake
prefix `/usr/local`:

```sh
sudo install -m 0644 /usr/local/share/awtrix-ng/systemd/awtrix-ng.service \
  /etc/systemd/system/awtrix-ng.service
sudo systemctl daemon-reload
sudo systemctl enable --now awtrix-ng.service
sudo systemctl status awtrix-ng.service
```

### Enable MQTT

Add a unit override (`systemctl edit awtrix-ng.service`) that loads the broker CA as another
credential and appends `--mqtt-ca-file` with its credential path (`%d/<name>`) to the existing
start arguments. Set up broker credentials and ACLs as described in
[MQTT over verified TLS](security.md#mqtt-over-verified-tls).

### Rotate credentials

- New token and certificate: generate a new set into an empty directory, or remove the three
  root-owned files and run `generate` again, then restart the service.
- CA-issued certificate: replace `tls.crt` and `tls.key` as shown above, run `check --ca-file`,
  and restart. `admin.token` stays.

The application reads one consistent credential set at start. Stop the service before taking an
offline backup of its state.

## Test the real service

The integration test creates a uniquely named temporary unit and state directory. It keeps the
shipped security settings, exercises HTTPS and state persistence, checks the actual process
privileges and removes its fixture afterwards. It needs root to start the sandbox; the
application itself runs as a dynamic non-root user. It installs no persistent service and does
not touch `/etc`:

```sh
sudo python3 tests/linux/test_service.py \
  --unit .pio/linux/awtrix-ng.service \
  --binary .pio/linux/awtrix-linux --webui webui/index.html
```

The test fails when the host cannot provide the requested sandbox.

## Related

- [HTTPS administration](security.md)
- [AWTRIX on Linux](index.md)
