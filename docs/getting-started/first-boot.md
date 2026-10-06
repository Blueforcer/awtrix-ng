# Connect to Wi-Fi

This page connects your clock to your Wi-Fi. At the end you know its address and can open the web
UI.

<!-- only tc002 -->
!!! tip "Set up with the installer"
    Did you enter your Wi-Fi details in the [installer](tc002.md) and it confirmed the
    connection? Then you are done. Press **Open your clock** in the graphical installer, or open
    the address the terminal version printed.

<!-- /only -->
## Set up Wi-Fi

A clock without Wi-Fi details opens its own **setup hotspot**, a Wi-Fi network with no password.
You join it with your phone or laptop and enter your home Wi-Fi there.

1. Join the clock's hotspot ([step 1](#step-1-join-the-hotspot)).
2. Enter your Wi-Fi name and password ([step 2](#step-2-enter-your-wi-fi)).
<!-- only esp32 esp32-s3 -->
3. Restart the clock ([step 3](#step-3-reboot)).
<!-- /only -->
<!-- only tc002 -->
3. Wait. The clock connects by itself ([step 3](#step-3-reboot)).
<!-- /only -->

Once it is connected, the clock shows its address on the display. Open that address in your browser to
reach the web UI.

## What the display shows {#what-the-panel-shows}

<!-- only esp32 esp32-s3 -->
| The display shows | Meaning |
|---|---|
| The AWTRIX start animation | The clock is starting and joining your Wi-Fi. |
| **`AP MODE`** | No Wi-Fi. The setup hotspot is open and waits for you. |
| An IP address such as `192.168.1.42` | The clock joined your network. This is its address. Setup is done. |
<!-- /only -->
<!-- only tc002 -->
| The display shows | Meaning |
|---|---|
| The AWTRIX start animation | The clock is starting and joining your Wi-Fi. |
| **`AP MODE`**, with the hotspot name and address scrolling below | No Wi-Fi. The setup hotspot is open and waits for you. |
| The firmware version above an IP address such as `192.168.1.42` | The clock joined your network. This is its address. Setup is done. |

The Status app shows the address at any time.
<!-- /only -->

The address appears once, for a few seconds, each time the clock starts. If you missed it,
unplug the clock and plug it back in, or see [Find your clock](discovery.md).

## Step 1 - join the hotspot {#step-1-join-the-hotspot}

1. Open the Wi-Fi list on your phone or laptop.
2. Choose the clock's network. **It has no password.**

The network has the clock's name. A new clock uses a name made from its hardware address, like
`awtrixng-a1b2c3`. If you already gave the clock a name, the network uses that name. A clock
named `kitchen-clock` opens a network called `kitchen-clock`.

!!! warning "Anyone nearby can join the hotspot"
    While the hotspot is open, anyone in range can join it and change the Wi-Fi settings. Set up
    the clock somewhere you trust, and finish the setup soon.

After you join, the setup page usually **opens by itself**, like the login page of hotel Wi-Fi.
If nothing opens, type **`192.168.4.1`** into your browser.

## Step 2 - enter your Wi-Fi {#step-2-enter-your-wi-fi}

The setup page is the web UI in setup mode. It shows only the **System** tab, with a blue
banner that asks you to connect to Wi-Fi.

<!-- only esp32 esp32-s3 -->
1. Enter your Wi-Fi name. You can also press **Scan** and choose from the networks nearby.
<!-- /only -->
<!-- only tc002 -->
1. Type your Wi-Fi name.
<!-- /only -->
2. Enter your Wi-Fi password.
3. Optional: enter a **hostname**, a name like `kitchen-clock`. You can then open the clock at
   `http://kitchen-clock.local` instead of a number that may change.
4. Press **Save**.

??? note "Do the same from a terminal"
    The web UI uses the [HTTP API](../reference/http.md), so you can also save the settings
    and restart with `curl`:

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/system \
      -H "Content-Type: application/json" \
      -d '{"wifiSsid":"MyNetwork","wifiPass":"secret123","hostname":"kitchen-clock"}'

    curl -X POST http://<awtrix-ip>/api/v1/device/reboot
    ```

    Always send `Content-Type: application/json`. Without it, `curl -d` sends a form type, and the
    clock refuses that with `415 unsupportedMediaType`
    ([Conventions](../reference/conventions.md#content-type-is-mandatory)).
    All fields: [Wi-Fi configuration](../reference/system.md#wi-fi).

## Step 3 - connect to your network {#step-3-reboot}

<!-- only esp32 esp32-s3 -->
Saving only stores your Wi-Fi details. Restart the clock to connect: press **Reboot now** in the
banner that appears after saving, or unplug the clock and plug it back in.
<!-- /only -->
<!-- only tc002 -->
Saving starts the connection by itself. If it fails, the setup hotspot opens again.
<!-- /only -->

The hotspot closes when the clock connects. Your phone goes back to your usual Wi-Fi by itself.

While the hotspot is open and nobody is connected to it, the clock tries your saved network again
every <!-- only esp32 esp32-s3 -->**30 seconds**<!-- /only --><!-- only tc002 -->**60 seconds**<!-- /only -->.<!-- only tc002 --> The hotspot may drop for a moment during a retry.<!-- /only -->
While someone is connected to the hotspot, the retries pause.

## What works in setup mode

The setup hotspot is only for Wi-Fi setup. You can save your Wi-Fi name, Wi-Fi password and
clock name, restore a backup, read setup information and restart the clock.
Logs, scripts, file lists, other uploads, resetting and sleep are refused until the clock
is on your network. The complete list is in [Setup mode lockdown](../reference/errors.md#provisioning-lockdown-403). These also need your network:

| Not available until the clock is on your Wi-Fi |
|---|
| Finding the clock by name (`.local`) or by network discovery |
| The IP address on the display |
<!-- only esp32 esp32-s3 -->
| Art-Net |
<!-- /only -->
| The Hub icon gallery, and exporting a settings backup |

If you turned on a **username and password** before, the setup page asks for them too.
Login is off until you turn it on. See [Authentication](../reference/http.md#authentication).

## When it goes wrong

### It shows AP MODE again

`AP MODE` after a restart means the clock could not join your network within about 15 seconds.
The most common causes:

- **Wrong Wi-Fi name or password.** Join the hotspot again and correct them.<!-- only esp32 esp32-s3 --> **Scan** helps you pick the right network.<!-- /only -->
- **Your Wi-Fi is 5 GHz only.** The clock uses 2.4 GHz. A router that sends both bands under one
  name is fine. If 2.4 GHz is switched off, the clock cannot see your network.<!-- only esp32 esp32-s3 --> It does not appear in the scan either.<!-- /only -->
- **A fixed IP address that does not fit your network.** If you set a static address, switch back
  to automatic (DHCP).
- **Your router is off.** The clock joins by itself once the router is back. You do not need to
  restart it.

### Starting over

The web UI has two resets under **System → Maintenance**:

| | |
|---|---|
| **Reset settings** | Sets all [settings](../reference/settings.md) back to their defaults: the Display tab, volumes, which apps rotate and the rest. Wi-Fi, MQTT, hardware settings and all your files stay. |
| **Factory reset** | Clears **everything**: Wi-Fi details, all settings and every uploaded file (icons, melodies, palettes, <!-- only esp32-s3 tc002 -->MP3s, <!-- /only -->scripts). The clock starts like new, with its setup hotspot. |

!!! danger "A factory reset cannot be undone"
    You confirm it by typing the clock's hostname. After the reset your files are gone, and you
    start this page again from step 1.

Step by step<!-- only tc002 -->, and for a clock that does not start<!-- /only -->:
[Reset & recovery](../guides/reset-recovery.md).

## Related

- [Find your clock](discovery.md) - by name, by discovery, or from the display
- [The web UI](web-ui.md) - everything the web UI can do
- [Authentication](../reference/http.md#authentication) - turn on a username and password
