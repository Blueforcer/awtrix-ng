# Find your clock

Your clock is on your Wi-Fi and you need its address to open the web UI. There are three
ways to find it, from easiest to most technical:

1. [Read it from the display](#read-it-off-the-panel).
2. [Use its name](#use-the-hostname), like `http://awtrixng-a1b2c3.local`.
3. [Search the network](#broadcast-udp-discovery) with a small script.

You can also look in your router's list of connected devices.

## Read it from the display {#read-it-off-the-panel}

Each time the clock starts, it shows its address for a few seconds after the start animation.

<!-- only tc002 -->
* The firmware version stands above it.
<!-- /only -->
* An address too long for the display scrolls past once.
<!-- only esp32 esp32-s3 -->
* If you changed the web port from 80, the address reads `<ip>:<port>`.
<!-- /only -->

The address only appears when the clock is connected to your Wi-Fi. Without Wi-Fi it shows only
the version. You can already reach the clock while the address is shown.

Missed it? Unplug the clock and plug it back in, or use one of the ways below.

<!-- only tc002 -->
The [Status app](../guides/device-controls.md#the-status-app) shows the address at any time. The
web UI always uses port 80.
<!-- /only -->

If the display shows **`AP MODE`** instead, the clock could not join your Wi-Fi and opened its setup
hotspot. Nothing on this page finds it then. Follow [Connect to Wi-Fi](first-boot.md).

## Use the hostname {#use-the-hostname}

The clock announces its name on your network with **mDNS** (also called Bonjour, Avahi or
Zeroconf). This lets you open it by name instead of by IP address:

```text
http://awtrixng-a1b2c3.local
```

The default name is `awtrixng-` plus the last 6 characters of the clock's Wi-Fi MAC address, for
example `awtrixng-a1b2c3` for a MAC ending in `a1:b2:c3`. If you set the `hostname` to `kitchen-clock`,
the clock answers to `kitchen-clock.local`. The same name is used for the setup hotspot and for
the discovery reply below. See
[Identity, web server and authentication](../reference/system.md#identity-web-server-and-authentication).

To test whether the name works on your computer:

=== "Windows"

    ```powershell
    ping awtrixng-a1b2c3.local
    ```

    Works on Windows 10 and later.

=== "macOS"

    ```bash
    ping -c 3 awtrixng-a1b2c3.local
    ```

    Works out of the box.

=== "Linux"

    ```bash
    ping -c 3 awtrixng-a1b2c3.local
    ```

    Needs `avahi-daemon` and `nss-mdns`. Without them, `.local` names do not work.

For scripts that run on their own, use the IP address instead of the name. It is one less thing
that can fail. Give the clock a fixed address, either as a DHCP reservation in your router or as a
[static IP](../reference/system.md#wi-fi) on the clock.

## List all clocks with mDNS

Each clock announces itself as the service `_awtrixng._tcp`. With an mDNS browser you can list
every clock on your network, with name, address and port:

=== "Windows"

    ```powershell
    dns-sd -B _awtrixng._tcp
    ```

    `dns-sd` comes with Bonjour (installed by iTunes, or as Bonjour Print Services). Without
    Bonjour, use the [UDP search](#broadcast-udp-discovery) below. It needs no extra software.

=== "macOS"

    ```bash
    dns-sd -B _awtrixng._tcp
    ```

    This lists the names. To get the address and port of one clock:

    ```bash
    dns-sd -L "awtrixng-a1b2c3" _awtrixng._tcp
    ```

    `dns-sd` runs until you press ++ctrl+c++.

=== "Linux"

    ```bash
    avahi-browse -rt _awtrixng._tcp
    ```

    `-r` shows the address of each clock. `-t` stops when the first scan is done.

Each clock also appears as a normal web server, so it shows up in network browsers and Bonjour
device lists.

## Search with a UDP broadcast {#broadcast-udp-discovery}

This way needs no mDNS and no extra software, only a network that passes broadcasts.

How it works:

1. Your computer sends the text `FIND_AWTRIXNG` as a broadcast to UDP port **4210**.
2. Every clock that receives it answers with its hostname<!-- only esp32 esp32-s3 -->, or `HOSTNAME:PORT` when the web port is not 80<!-- /only -->.
3. The answer always goes to UDP port **4211**. Your computer must listen on 4211 to receive it.
4. The IP address of the clock is the address the answer came from.

Both scripts below do all of this for you:

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

If your network blocks `255.255.255.255`, send to your subnet's broadcast address instead, for
example `192.168.1.255` for a `192.168.1.0/24` network.

Send one request and wait for the answers. If nothing comes back, try again. Do not send many
requests quickly, because the clock may miss some of them.

## Check that you found the right clock

Once you have an address, ask the clock who it is:

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

`uid` is the clock's MAC address without colons. It never changes, not even after a reinstall, so
you can use it to tell two clocks apart. All fields are in
[Device state](../reference/device.md#endpoint).

If you turned on login, add your username and password:

```bash
curl -u admin:secret http://192.168.1.42/api/v1/device
```

## When nothing answers {#when-nothing-answers}

The address on the display, the `.local` name and the UDP search all need a working Wi-Fi
connection. If the clock falls back to its setup hotspot, none of them work. While nobody is
connected to the hotspot, the clock tries your saved network again every <!-- only esp32 esp32-s3 -->30<!-- /only --><!-- only tc002 -->60<!-- /only --> seconds.
It connects as soon as it succeeds. A short Wi-Fi outage fixes itself. You do not need to restart
the clock.

Otherwise, check these in order:

| Symptom | Likely cause |
|---|---|
| The display shows `AP MODE` | The clock is not on your Wi-Fi. Join its setup hotspot (named after the hostname, `awtrixng-<last 6 characters of the MAC>` by default) and follow [Connect to Wi-Fi](first-boot.md). |
| No answer to the UDP search, but the clock is online | Your computer does not listen on UDP 4211, or a firewall blocks the answer. Many routers also block broadcasts between Wi-Fi and cable, and on guest networks. |
| The UDP search works, `.local` does not | Your computer has no mDNS support, or mDNS failed on the clock. Use the IP address. |
<!-- only esp32 esp32-s3 -->
| `.local` works, but the web UI does not open | The web port is probably not 80. The UDP answer `HOSTNAME:PORT` shows the port. |
<!-- /only -->
| Every request returns `401` | Login is turned on. See [Authentication](../reference/http.md#authentication). |

More help: [Troubleshooting](../troubleshooting/troubleshooting.md#finding-awtrix-on-the-network).

## Related

* [Connect to Wi-Fi](first-boot.md) - put the clock on your Wi-Fi
* [The web UI](web-ui.md) - what to do once you have the address
* [Wi-Fi configuration](../reference/system.md#wi-fi) - static IP, hostname, Wi-Fi details
* [Wi-Fi scan](../reference/system.md#wi-fi-scan) - list the networks the clock can see
* [Device state](../reference/device.md#endpoint) - the full status of the clock
* [HTTP API base URL](../reference/http.md#base-url) - addresses and ports in the API
