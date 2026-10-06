# Security policy

## Reporting a vulnerability

**Please do not open a public issue for a security problem.**

Use GitHub's private reporting:
**[Report a vulnerability](https://github.com/Blueforcer/awtrix-ng/security/advisories/new)**.
Only the maintainers can see it, and we can credit you in the advisory when it
is published.

Useful things to include: the firmware version (`GET /api/v1/version`), the
board, and the smallest request or payload that reproduces the problem.

Expect an acknowledgement within a week. This is a hobby project maintained in
spare time, not a vendor with an on-call rotation — please allow a reasonable
window before disclosing publicly.

## Supported versions

Only the latest release is supported, on ESP32 and TC002 alike; fixes land on
`main` and ship in the next tag rather than as backports. There is currently no
qualified public TC002 system image.

## ESP32 threat model — read this before deploying

AWTRIX NG is designed for a **trusted home LAN**. Understanding what that means
is more useful than any list of patched CVEs.

**The HTTP API is unauthenticated by default.** Anyone who can reach the
device's IP can read its configuration, change its settings, push apps, upload
files and flash new firmware. You can turn on HTTP Basic authentication under
**System → Webserver** in the web UI, or by setting `authEnabled`, `authUser`
and `authPass` through `PUT /api/v1/system`.

**There is no TLS.** The device serves plain HTTP. Basic authentication
therefore sends its credentials in a trivially reversible encoding over the
wire, and firmware uploads are neither encrypted nor signed. Enabling
authentication raises the bar against casual access on your own network; it is
not protection against someone who can observe your traffic.

**The provisioning access point is open.** On first boot, and whenever it cannot
reach a known network, the device opens an unencrypted Wi-Fi AP with a captive
portal that accepts your Wi-Fi credentials. Anyone in radio range during that
window can connect to it.

**Berry scripts are not a security sandbox.** The resource limits — an
instruction budget per call, a heap cap, script-count and size limits — exist to
keep one misbehaving script from taking down the firmware. They are not a
boundary against a hostile script, and a script can use the device's HTTP and
MQTT clients to reach anything the device can reach. Only run scripts you trust.

**Art-Net and MQTT are unauthenticated at the protocol level.** Art-Net has no
authentication by design. MQTT security is whatever your broker enforces.

### What follows from that

- **Do not expose the device to the internet.** No port forwarding, no DMZ. If
  you need remote access, put it behind a VPN.
- Prefer a **guest or IoT VLAN** if your router supports one.
- Turn on HTTP authentication if untrusted devices or people share the network.
- Treat any script or pushed app you did not write the way you would treat any
  other untrusted code.

## What counts as a vulnerability

**In scope:** anything that lets an attacker exceed the model above — a remote
crash or memory-corruption bug reachable from a request, an authentication
bypass when authentication is enabled, a path traversal in the file API, a
script escaping its instruction or heap limits, or credentials leaking through
an endpoint or the log.

**Out of scope**, because they are documented properties rather than defects:
the API being open by default, the absence of TLS, the open provisioning AP, and
unsigned OTA images. Arguments for changing those are very welcome — as a
regular issue or pull request.

## Ulanzi TC002

The TC002 follows the ESP32 model above: plain HTTP on port 80 of your LAN, the API open until
you turn on the Basic login, Berry scripts trusted. What differs:

- **Bluetooth LE is script-controlled.** Trusted Berry scripts can scan, connect,
  advertise and serve GATT data when BLE is available. A server characteristic
  with the `e` property requires an encrypted link for reads, writes, subscriptions
  and notification delivery. Pairing has no passkey-confirmation UI and does not
  guarantee the identity of the peer. Bond keys are stored in private files and
  replaced atomically. Only run scripts whose Bluetooth behavior you trust.
- **Updates trust the chosen source.** `POST /update` accepts unsigned `.awup` packages.
  SHA-256 checks detect damaged metadata and payloads; they do not authenticate the publisher.
  The clock checks the TC002 target and remembers the newest accepted release, so an older
  package is refused. Only install firmware from a source you trust.
  A release that does not keep running and get back onto the Wi-Fi within minutes is replaced by
  the factory release kept in the application partition.
- **Wi-Fi is set over USB.** There is no provisioning access point, and the passphrase is not
  stored: the supervisor keeps only the key derived from it.
- **USB access is root access.** The manufacturer's system gives `adb` root; ADB over Wi-Fi is
  off unless a developer flag file is placed over USB. AWTRIX NG itself runs as root.
- **The manufacturer's cloud is gone.** Its login is removed from the clock, and the Ulanzi app
  runs only when the knob is held at power-on.
- **The kernel and boot chain are the manufacturer's.** Security fixes in them are out of reach
  until AWTRIX NG has its own kernel and root filesystem.

**In scope** on the TC002, besides the ESP32 list: bypassing package integrity, device checks
or the accepted release counter; getting past the
factory fallback; reaching the Wi-Fi key over the network. **Out of scope:** anything that needs
physical USB access, and the ESP32 exclusions above.

## Headless Linux

`awtrix-linux` without `--board tc002` is a development target. By default it listens on loopback
only and does not authenticate local processes. Its `--hardened` mode requires HTTPS and a locally
provisioned administrator credential, rejects root execution and protects every route; its MQTT
path requires an explicit CA and broker credentials. Setup and limits are described in
[Linux administration](docs/developers/linux/security.md) and
[service isolation](docs/developers/linux/service.md). An authentication bypass, a
certificate-verification bypass, a leaked administration secret or an escape from the documented
service isolation is in scope for that mode.
