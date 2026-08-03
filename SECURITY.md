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

Only the latest release is supported. AWTRIX NG is pre-1.0; fixes land on `main`
and ship in the next tag rather than as backports.

## Threat model — read this before deploying

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
