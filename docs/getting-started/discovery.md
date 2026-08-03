# Finding AWTRIX

AWTRIX joined your Wi-Fi and you need its address. You have three ways to get it, in
increasing order of effort:

1. [Read it off the panel](#read-it-off-the-panel) - it scrolled past at boot.
2. [Use the hostname](#use-the-hostname) - `awtrixng-<last 6 hex of MAC>.local`, no tools required.
3. [Ask the whole LAN](#broadcast-udp-discovery) - broadcast a discovery packet and collect the
   replies.

## Read it off the panel

On a successful Wi-Fi connection - and only then - the panel scrolls `AWTRIX   <ip>` in rainbow
text. mDNS, the HTTP API, MQTT and UDP discovery are already active while it scrolls, so you can
reach AWTRIX before it finishes. If you changed the web port away from 80, the scroll reads
`AWTRIX   <ip>:<port>`.

This happens once per boot and takes a few seconds. If you missed it, power-cycle AWTRIX and
watch again, or use one of the methods below.

If the panel shows a rainbow **`AP MODE`** instead, AWTRIX could not join your network and
fell back to its own open access point. Nothing on this page will find it - join that access point
and follow [First boot](first-boot.md) instead.

## Use the hostname

AWTRIX registers itself with mDNS (Bonjour / Avahi / Zeroconf), so it is reachable by name
without knowing its IP at all:

```bash
curl http://awtrixng-a1b2c3.local/api/v1/device   # use your own hostname
```

The default hostname is `awtrixng-` followed by the last 6 hex characters of the Wi-Fi
MAC address - `awtrixng-a1b2c3` for a MAC ending in `a1:b2:c3`. Set `hostname` to
`kitchen-clock` and AWTRIX answers to `kitchen-clock.local` instead. The same name is used for
the mDNS record, the discovery reply and the provisioning access point's SSID. See
[Identity, web server and authentication](../reference/system.md#identity-web-server-and-authentication).

To check name resolution on its own:

=== "Linux"

    ```bash
    ping -c 3 awtrixng-a1b2c3.local
    ```

    Requires `avahi-daemon` and `nss-mdns`. Without them, `.local` names do not resolve.

=== "macOS"

    ```bash
    ping -c 3 awtrixng-a1b2c3.local
    ```

    Works out of the box.

=== "Windows"

    ```powershell
    ping awtrixng-a1b2c3.local
    ```

    Works out of the box on Windows 10 and later.

For a script that runs unattended, prefer the IP - a name lookup is one more thing that can stall
or be blocked by your router. Resolve the name once and keep the address, ideally backed by a DHCP
reservation or a [static IP](../reference/system.md#wi-fi) configured on AWTRIX.

## Browse for AWTRIX devices

AWTRIX announces itself as `_awtrixng._tcp`, so you can list every unit on the LAN -
name, address and port in one shot:

=== "Linux"

    ```bash
    avahi-browse -rt _awtrixng._tcp
    ```

    `-r` resolves each hit to an address; `-t` exits when the initial scan is done.

=== "macOS"

    ```bash
    dns-sd -B _awtrixng._tcp
    ```

    That lists instance names. To get the address and port for one of them:

    ```bash
    dns-sd -L "awtrixng-a1b2c3" _awtrixng._tcp
    ```

    `dns-sd` runs until you press ++ctrl+c++.

=== "Windows"

    ```powershell
    dns-sd -B _awtrixng._tcp
    ```

    `dns-sd` ships with Bonjour (installed by iTunes, or standalone as part of the Bonjour Print
    Services package). Without Bonjour, use the UDP broadcast below - it needs no extra software.

Each one also appears as a plain web server, which is why it shows up in network browsers and
Bonjour device lists.

## Broadcast UDP discovery

This is the method with no dependencies: no mDNS responder on your machine, no Bonjour, no router
cooperation beyond passing a broadcast.

You send `FIND_AWTRIXNG` to UDP port **4210** as a broadcast. Every device that hears it answers
with its hostname - or `HOSTNAME:PORT` when the web port is not 80 - and the IP address you want
is the address the reply came from. The reply always goes to UDP port **4211**, so your socket has
to be bound to 4211 to hear it. Both snippets below do that for you.

=== "Python (any OS)"

    ```python
    import socket

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("", 4211))
    s.settimeout(2.0)
    s.sendto(b"FIND_AWTRIXNG", ("255.255.255.255", 4210))

    while True:
        try:
            data, addr = s.recvfrom(64)
        except socket.timeout:
            break
        reply = data.decode(errors="replace")
        host, _, port = reply.partition(":")
        print(f"{host:20} http://{addr[0]}:{port or 80}")
    ```

    ```
    awtrixng-a1b2c3      http://192.168.1.42:80
    ```

=== "PowerShell"

    ```powershell
    $udp = New-Object System.Net.Sockets.UdpClient(4211)
    $udp.EnableBroadcast = $true
    $udp.Client.ReceiveTimeout = 2000
    $msg = [Text.Encoding]::ASCII.GetBytes("FIND_AWTRIXNG")
    $udp.Send($msg, $msg.Length, "255.255.255.255", 4210) | Out-Null

    $ep = New-Object System.Net.IPEndPoint([Net.IPAddress]::Any, 0)
    try {
        while ($true) {
            $data = $udp.Receive([ref]$ep)
            "{0}`t{1}" -f $ep.Address, [Text.Encoding]::ASCII.GetString($data)
        }
    } catch { }
    $udp.Close()
    ```

If `255.255.255.255` is filtered on your network, send to your subnet's directed broadcast
instead - e.g. `192.168.1.255` for a `192.168.1.0/24` network.

Send one request and wait. A single broadcast to many devices gets many answers, but a rapid burst
of requests aimed at one device can lose some of them - if nothing comes back, retry rather than
flood.

## Confirm you found the right one

Once you have an address, ask it who it is:

```bash
curl http://192.168.1.42/api/v1/device
```

```json
{
  "version": "1.0.12",
  "uid": "a4cf12ab34cd",
  "boardType": "awtrixng",
  "ipAddress": "192.168.1.42",
  "currentApp": "Time"
}
```

The `uid` is the device's MAC address without the colons, and it is stable across reboots and
reflashes - that is how you tell two devices apart when both answer a broadcast. The full response
is documented in [Device state](../reference/device.md#endpoint).

If authentication is enabled, this call needs credentials:

```bash
curl -u admin:secret http://192.168.1.42/api/v1/device
```

## When nothing answers

UDP discovery, the mDNS record and the boot IP scroll all need a successful Wi-Fi station
connection. Fall back to the provisioning access point and none of them start - but AWTRIX keeps
retrying your configured network every 30 seconds while no one is connected to that access
point, and restarts itself the moment a retry succeeds. A transient Wi-Fi outage can resolve on
its own; no reboot needed.

Otherwise, work through these in order:

| Symptom | Likely cause |
|---|---|
| Panel shows a rainbow `AP MODE` | AWTRIX never joined your Wi-Fi. Join its open access point (SSID = the hostname, `awtrixng-<last 6 hex of the MAC>` by default) and configure Wi-Fi - see [First boot](first-boot.md). |
| No broadcast replies, AWTRIX is definitely online | Your client is not bound to UDP 4211, or a firewall is dropping the inbound reply. Broadcast is also commonly blocked between Wi-Fi and wired segments, and on "guest" or client-isolated SSIDs. |
| Broadcast works, `.local` does not | mDNS. Either your OS has no responder, or registration failed on AWTRIX. Use the IP. |
| `.local` works, the API does not respond | You are probably on a non-default web port. Check the discovery reply - `HOSTNAME:PORT` tells you the port AWTRIX thinks it is on. |
| Found it, but every request returns `401` | Authentication is configured. See [Authentication](../reference/http.md#authentication). |

## Related

* [First boot](first-boot.md) - joining AWTRIX to your Wi-Fi in the first place
* [Web UI tour](web-ui.md) - what to do once you have the address
* [Wi-Fi configuration](../reference/system.md#wi-fi) - static IP, hostname, credentials
* [Wi-Fi scan](../reference/system.md#wi-fi-scan) - asking AWTRIX what networks it can see
* [Device state](../reference/device.md#endpoint) - the full identity and status snapshot
* [HTTP API base URL](../reference/http.md#base-url) - how addresses and ports work across the API
