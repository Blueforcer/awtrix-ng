---
only: [tc002]
---

# HTTPS administration

`awtrix-linux --hardened` serves the normal application and web UI behind authenticated HTTPS.
It is meant for a Linux host that other machines can reach. The mode lives in
[`src/platform/linux/LinuxAdminSecurity.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/linux/LinuxAdminSecurity.cpp)
and [`LinuxMqttTls.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/linux/LinuxMqttTls.cpp);
the provisioning helper is
[`tools/linux/provision.py`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/linux/provision.py).

Without `--hardened`, `awtrix-linux` serves plain HTTP on `127.0.0.1` (see
[AWTRIX on Linux](index.md)). Passing any security or listen option without `--hardened` is an
error (`--lan` accepts `--listen` only), so a typo cannot open an unauthenticated listener.
ESP32 behaviour is not affected by this mode.

## How it works

- **One administrator.** The username is `admin`; the password is a token of exactly 64
  hexadecimal characters from a file. There is no default password and no enrollment endpoint.
  Authenticated scripts and the administrative API can control the whole application; there is
  no restricted web role. The application runs no privileged shell actions through HTTP, MQTT or
  scripts.
- **TLS 1.2 or newer, always.** The listener never falls back to plain HTTP.
- **Every route is guarded.** A central check authenticates each request before any handler runs
  or any body is read: web UI, static assets, secrets, logs, backup, restore, scripts, unknown
  paths and upgrade attempts.
- **Credentials.** Basic and Bearer credentials are compared as fixed-size SHA-256 digests in
  constant time. Duplicate `Authorization`, `Host` or `Origin` headers are rejected. Tokens in
  query strings or cookies are not accepted. Authorization headers and credentials are never
  logged.
- **One origin.** `Host` must equal the host and port of `--origin`. When an `Origin` header is
  present it must equal the full HTTPS origin. Cross-site requests are rejected; there is no
  wildcard CORS. Alternate host names, forwarded-host headers and reverse-proxy rewriting do not
  relax this. IPv6 origins are not supported.
- **Response headers.** `no-store`, HSTS, anti-framing, `nosniff` and no-referrer. TLS workers,
  queued connections, payload size and idle, read and write waits are bounded. These limits do
  not replace network-level denial-of-service protection.
- **Lifecycle.** Credentials and keys are read once at start. To rotate them, replace the files
  and restart. Browser Basic authentication has no logout; close the browser session when done.
  Backup, restore and factory reset never contain, replace or erase the credential files, and a
  missing credential file stops the start.
- **No streaming channels.** The HTTP API is polling only. WebSocket or SSE attempts pass through
  the same guard.

## Provision a local administrator

Run the service as a dedicated unprivileged account; hardened mode refuses to run as root. Keep
the security files outside the application `--data` directory, in a directory owned by the
service account or root that no other account can write.

`provision.py` creates and checks the three files on the host that runs the service. It needs
Linux, Python 3.12, the `openssl` command and a filesystem that keeps POSIX owners and modes and
supports hard links. Run it as the account that owns the directory:

```sh
install -d -m 0700 "$HOME/.config/awtrix-ng/security" "$HOME/.local/state/awtrix-ng"
python3 tools/linux/provision.py generate \
  --dir "$HOME/.config/awtrix-ng/security" --origin https://localhost:8443
python3 tools/linux/provision.py check \
  --dir "$HOME/.config/awtrix-ng/security" --origin https://localhost:8443
```

`generate` writes:

| File | Mode | Content |
|---|---|---|
| `admin.token` | `0600` | One line of 64 lowercase hexadecimal characters from the OS random generator |
| `tls.key` | `0600` | EC P-256 private key, PKCS#8 PEM |
| `tls.crt` | `0644` | Self-signed ECDSA SHA-256 certificate. The Subject Alternative Name (SAN) holds the origin host as `DNS:` or `IP:`; valid for `--days` (default 365) from now |

The origin is `https://HOST:PORT` with a lowercase DNS name or IPv4 address. Each file is
written to a temporary name, synced, and published with a hard link, which never replaces an
existing name. A failed run removes everything it created, so the directory holds either an
accepted set or nothing. A process killed mid-run can leave `.<name>.<hex>.partial` files and
some final names; remove them before running `generate` again. `generate` never overwrites: to
rotate, use a new directory or remove the three files, then restart the service.

`generate` refuses existing names, a symlinked or non-directory `--dir`, a directory writable by
group or others or owned by another account, a directory inside `--data`, whitespace or control
characters in paths, a malformed origin or validity, and a missing `openssl`. On success it
prints one JSON line. Compare its `fingerprint_sha256` with the SHA-256 fingerprint in the
browser's certificate details before you trust the certificate.

`check` answers one question: will `awtrix-linux --hardened` accept this directory for this
origin? It applies the application's file rules and then runs the application's TLS handshake
in-process: the application's cipher list at security level 2, TLS 1.2 or newer, and a client
that verifies host name, validity, key strength and signature strength. With `--ca-file FILE`
the chain in `tls.crt` must verify against that CA. Run `check` on the service host as the
account that owns the files; for the root-owned layout in
[Running as a systemd service](service.md) that is root.

Exit status: 0 generated or valid, 1 environment or set refused, 2 usage error. Messages name
files, never their contents.

### File rules

- The token file holds exactly 64 hexadecimal characters, with one optional line ending.
- Secret files (token, key) have no group or other permissions, for example mode `0600`.
- Certificate files may be readable by others but not writable by group or others.
- Symlinks as the last path component, non-regular files, foreign owners, writable parent
  directories and files inside `--data` are rejected.
- Encrypted private keys are not supported. Protect the key with file permissions or service
  manager credentials.

### Certificates from your own CA

The SAN of a CA-issued certificate must cover the exact DNS name or IPv4 address of `--origin`.
Put the leaf first in `tls.crt`, followed by its issuers. Run `generate` first to get the token,
then replace `tls.crt` and `tls.key` with the issued material, keeping their modes, and validate
with `check --ca-file`. Expired, not-yet-valid, mismatched, malformed, weak and wrong-origin
certificates or keys stop the start.

### Known differences between `check` and the application

`check` is stricter than the application in a few edge cases. It refuses these, although the
application starts with them:

- partial-wildcard names and a symlinked `--dir`;
- a directory or files owned by another account when run as root;
- an issuer certificate with a weak key (the application starts but then fails every handshake);
- a `tls.crt` with a non-certificate block after the leaf, or leaf data that continues after the
  certificate;
- a NUL byte in `tls.crt` or `tls.key`;
- an origin host with an empty label or a label longer than 63 characters.

`TRUSTED CERTIFICATE` blocks are skipped by both. `check` uses the OpenSSL library of the Python
interpreter; its JSON `openssl` field names it. Certificates start at the second of generation,
so a client with a lagging clock refuses them until it catches up. The helper produces no IPv6
origins, encrypted keys or extra names, and does not provision MQTT broker trust.

## Start the application

```sh
awtrix-linux \
  --data "$HOME/.local/state/awtrix-ng" \
  --webui /usr/share/awtrix-ng/index.html \
  --port 8443 --hardened \
  --credentials-file "$HOME/.config/awtrix-ng/security/admin.token" \
  --tls-cert "$HOME/.config/awtrix-ng/security/tls.crt" \
  --tls-key "$HOME/.config/awtrix-ng/security/tls.key" \
  --origin https://localhost:8443
```

Open `https://localhost:8443` in a browser that trusts the certificate. The browser asks for
`admin` and the token; this also authenticates the web UI's API and asset requests. A
self-signed certificate must be trusted explicitly on the client. Keep verification on.

To call the API, let curl prompt for the password so it stays out of command arguments and
shell history:

```sh
curl --cacert "$HOME/.config/awtrix-ng/security/tls.crt" \
  --user admin https://localhost:8443/api/v1/version
```

Automation can send `Authorization: Bearer <token>` over HTTPS instead. Keep the token in
protected client configuration or header files.

The listener stays on loopback by default. To accept LAN traffic, set `--listen 0.0.0.0` and an
`--origin https://device.example:8443` that the certificate covers. The origin must include the
port.

### Hardened-mode options

| Flag | Meaning |
|---|---|
| `--hardened` | Enable authenticated HTTPS; refuses to run as root |
| `--credentials-file FILE` | The administrator token |
| `--tls-cert FILE` | Leaf certificate followed by its issuers, PEM |
| `--tls-key FILE` | Unencrypted private key, PEM |
| `--origin https://HOST:PORT` | The one accepted origin; its port must equal `--port` |
| `--listen IPv4_ADDRESS` | Listen address, default `127.0.0.1` |
| `--mqtt-ca-file FILE` | Broker CA; without it MQTT stays off |

## MQTT over verified TLS

In hardened mode MQTT stays closed unless `--mqtt-ca-file FILE` provides the broker's CA
certificates. That file follows the same protected-file and outside-`--data` rules as the HTTPS
certificate. There is no plain-text fallback, no system trust store fallback, and no API option
to turn verification off.

To enable it:

1. Set up a broker with TLS, `allow_anonymous false`, individual device credentials and topic
   ACLs (access control lists).
2. In the authenticated web UI or configuration API, set `mqttEnabled`, `mqttHost`, `mqttPort`,
   `mqttUser`, `mqttPass` and `mqttPrefix`.
3. Put the broker's CA PEM outside `--data` and restart with, for example,
   `--mqtt-ca-file /etc/awtrix-ng/security/mqtt-ca.pem` in addition to the HTTPS options.

The broker certificate must match `mqttHost`; DNS resolution does not replace the name with its
IP address during SNI or verification. For an IP host, the certificate needs that IP in its SAN.
An unreadable or invalid CA file, or MQTT enabled with an empty username or password, stops the
start. A broker certificate that fails trust or name checks, or credentials the broker rejects,
keep MQTT offline while HTTPS keeps working. Broker changes take effect after a restart.

Example Mosquitto configuration (adapt certificates, paths and the prefix):

```text
listener 8883
certfile /etc/mosquitto/certs/broker-chain.pem
keyfile /etc/mosquitto/certs/broker.key
allow_anonymous false
password_file /etc/mosquitto/passwords
acl_file /etc/mosquitto/awtrix.acl
```

```text
user awtrix-device
topic readwrite clock-living-room/#

user trusted-automation
topic readwrite clock-living-room/#
```

Access to a device's `cmd/#` topics is administrative control, including scripts and reset.
Grant it only to trusted administrators and automation. Home Assistant discovery and scripts that
use other topics need matching ACL entries. HTTPS and MQTT are authenticated separately; the
HTTPS token is not an MQTT password.

The shared `MqttLink`, `PubSubClient`, routing and responses are used unchanged. Linux adds a
verified TLS transport and a connection worker:

| Limit | Value |
|---|---|
| TCP and TLS setup | 3 s deadline |
| MQTT CONNACK | 2 s, off the render thread |
| Partial incoming packets and queued writes | expire after 2 s |
| Incoming packet size | 8192 bytes (the shared buffer) |
| Queued outgoing bytes | 256 KiB |

After connecting, reads and writes are non-blocking, and PubSubClient only sees complete MQTT
packets. The development mode and the ESP32 keep their own transports.

## Test

Build `awtrix-linux` (see [AWTRIX on Linux](index.md#build)), then run:

```sh
python3 tests/linux/test_security.py \
  --binary .pio/linux/awtrix-linux --webui webui/index.html
python3 tests/linux/test_provision.py \
  --binary .pio/linux/awtrix-linux --webui webui/index.html
ctest --test-dir .pio/linux -R 'linux-security|linux-provision'
```

Both runners need a non-root Linux account and `openssl`. `test_security.py` also needs
`mosquitto` and `mosquitto_passwd`; without them it exits 77, which CTest reports as skipped.
`--require-tools`, which CI passes, turns that into a failure.

`test_security.py` generates temporary certificates and credentials and drives the real process
over HTTPS. It covers browser and API access, unauthenticated routes and changes, `Host` and
`Origin` rules, duplicate headers, TLS trust and protocol refusal, plain-text failure, unsafe
provisioning, invalid keys, reset preserving credentials, MQTT closed without a CA, and real
TLS broker commands, results, screen responses and ACLs. Negative broker tests cover wrong trust,
host name, credentials, slow handshakes and partial packets without blocking HTTP responses.

`test_provision.py` covers generation, every refusal, fault injection at each filesystem step,
`check` against CA-issued, expired, not-yet-valid, weak and mismatched material, and agreement
with the real binary: every set `check` refuses is refused at start, every set it accepts starts,
and the edge cases listed above are asserted on both sides. It ends with a real HTTPS start for a
DNS and an IPv4 origin: 401 without credentials, 200 with the generated token. It runs
unprivileged, so the root invocation of the service layout is simulated by substituting the
effective user ID.

## Related

- [AWTRIX on Linux](index.md)
- [Running as a systemd service](service.md)
- [MQTT guide](../../guides/mqtt.md)
