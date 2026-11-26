---
only: [esp32, esp32-s3]
---

# Script host

This page lets you run your clock's scripts on a computer in your network, for example a Raspberry
Pi or a NAS, while the clock shows them.

## What you get

Your scripts run on the script host, and the clock shows them in its rotation as usual. Save this
script in the **Scripts** tab of a paired clock, and the display shows **hi** in green:

<!-- panel -->
```berry
class Hello
  def draw()
    clear()
    text(1, 6, "hi", 0x00FF00)
  end
end

return Hello()
```

- The clock has much more free memory, because no script runs on it. Scripts can fetch as much
  as they need.
- You keep using the web UI of the clock: the **Scripts** tab, the settings of each app and the
  Hub work as before.
- One script host serves several clocks. Each clock keeps its own scripts and settings.

## How it behaves

The script host runs your scripts and sends each picture to the clock. The clock decides which app
is shown, as before, and its buttons, sensors, sound and settings stay with the clock. A script
reads the clock's sensors, settings and time zone, and its sounds and notifications play on the
clock. A script's own sounds stay on the clock.

The clock holds each picture back for up to 0.15 seconds, so animations run smoothly even when the
Wi-Fi is uneven. Buttons in games therefore react a little later than with scripts on the clock.

While a clock is paired with a script host, no script runs on the clock itself. When the script
host is off or out of reach, the clock skips its scripts within three seconds and shows its other
apps. Once the script host is back, the scripts return on their own.

---

## Set up the script host

The script host runs on a computer that is always on, in the same network as the clock. It needs
Linux and Docker, for example a Raspberry Pi with Raspberry Pi OS or a NAS.

1. Get the AWTRIX NG files and start the script host:

    ```bash
    git clone https://github.com/Blueforcer/awtrix-ng.git
    cd awtrix-ng/packaging/host
    docker compose up -d
    ```

    The first start builds the script host. That takes a few minutes, longer on a Raspberry Pi.

2. Read the pairing code:

    ```bash
    docker logs awtrix-host
    ```

    The log shows a line like `pairing code 482915 (valid 10 min)`.

The script host starts again by itself after a restart of the computer. Its scripts and settings
are kept in the Docker volume `awtrix-host`.

### Without Docker

On a Linux computer, build AWTRIX NG as [Building](../developers/building.md) describes, then
install it. The installation contains a systemd service:

```bash
sudo cmake --install build/host
sudo cp /usr/local/share/awtrix-ng/systemd/awtrix-host.service /etc/systemd/system/
sudo systemctl enable --now awtrix-host
journalctl -u awtrix-host
```

The pairing code is in the journal.

---

## Pair the clock

1. Open the web UI of the clock and go to **System → Script host**.
2. The script host shows up under **Found**. If it does not, type its address into **Address**,
   for example `192.168.1.5` or `raspberrypi.local`.
3. Type the code from the log into **Code** and press **Pair**.

The clock moves its scripts to the script host, with their settings and saved data. Their sounds
stay on the clock. Then it restarts. Afterwards the **Script host** section shows **Connected**.

A code works once and for ten minutes. After five wrong codes, pairing waits one minute. Each
further five wrong codes double the wait, up to one hour, until a clock pairs. The next code is in
the log.

## Use your scripts

Everything works as before:

- **Scripts** tab: write and edit scripts. Saving sends them to the script host.
- **Apps** tab: switch apps on and off, change their order and their settings.
- **Audio** tab: upload and delete the sounds of your scripts. They are kept on the clock.
- **AWTRIX Hub**: **Send to AWTRIX** with the address of the clock. The clock passes the script on
  to the script host.

Script errors show in the **Scripts** tab and on the clock, as before.

Script changes sent to the clock's API, for example from Home Assistant, go on to the script host
while the clock can reach it.

## Pair again

When **System → Script host** says **The script host does not know this clock.**, for example after
the script host was set up anew, type a new code from the log and press **Pair again**. The clock
restarts. Your scripts stay where they are.

## Unpair

1. Go to **System → Script host** and press **Unpair**, then press it again to confirm.
2. Choose **Bring scripts back** to copy the scripts back to the clock, or **Leave them on the
   host**.

The clock restarts and runs scripts itself again. Their sounds are on the clock already. A script
that cannot be copied back, for example because it does not fit into the clock's memory, stays on
the script host. The web UI lists each one with the reason.

## Several clocks

Pair each clock the same way, each with its own code. Each clock has its own scripts, settings and
data on the script host. To show the same picture on several clocks, use
[Mirroring](mirroring.md).

---

## Good to know

- **The clock skips its scripts:** the script host is off or cannot be reached. Check that the
  computer runs and is in the same network. **System → Script host** shows the state.
- **Docker needs the host's network:** the compose file sets `network_mode: host`. Without it the
  clocks cannot reach the script host.
- **The script host is not found:** some networks block the search. Type the address of the script
  host into **Address**.
- **A firewall on the computer** must let through TCP port 8300, UDP port 4211 and, for each clock,
  TCP and UDP ports from 8301 upwards.
- **The clocks reach the computer at another address than its own,** for example through port
  forwarding: before you pair, add `--advertise` with that address to the start command, for
  example `--advertise 192.168.1.5` at the end of the `ExecStart` line of the systemd service.

## Details

- [System settings](../reference/system.md): `scriptHost` and `scriptHostKey`
- [HTTP API: script host status](../reference/http.md#get-apiv1scripthost)
- [Errors](../reference/errors.md): `scriptsOnHost`

## Related

- [Scripting guide](scripting/index.md)
- [AWTRIX Hub](hub.md)
- [Mirroring](mirroring.md)
