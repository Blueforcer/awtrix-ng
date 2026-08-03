# First boot

**Goal: connect AWTRIX to your Wi-Fi and learn its address.**

With no Wi-Fi credentials yet, AWTRIX cannot reach your network, so it opens its own **open
access point** and waits for you. The whole setup is three steps:

1. Join that access point with your phone or laptop.
2. Tell it your own network's name and password.
3. Restart it.

AWTRIX then scrolls its new IP address across the matrix, and that address is where the web UI
lives.

## The panel tells you where you are

| The matrix shows | Meaning |
|---|---|
| Sparks rising into a glowing **`AWTRIX`** | It is starting up and looking for your Wi-Fi. |
| Rainbow **`AP MODE`** | No Wi-Fi. AWTRIX is waiting for you to configure it. |
| Glowing **`AWTRIX   192.168.…`** scrolling past | It joined your network. That is its address. Setup is done. |

The address scroll happens once per boot and takes a few seconds. Miss it and you can power-cycle
AWTRIX to see it again, or find its address another way - see
[Finding AWTRIX](discovery.md).

## Step 1 - join the access point

Look for a new Wi-Fi network in your phone's or laptop's list and connect to it. **There is no
password.**

The network takes its name from AWTRIX. On a brand-new unit that is a name derived from its
hardware address, like `awtrixng-a1b2c3`. If you have already set a name, it uses that instead -
a unit named `kitchen-clock` comes up as a network called `kitchen-clock`.

!!! warning "The access point is open - anyone in range can use it"
    While it is up, anyone within radio range can join and read or write the stored Wi-Fi
    configuration. Set AWTRIX up somewhere you trust and get it out of access-point mode
    promptly.

Once you are connected, the setup page usually **opens by itself**, the way hotel and airport
Wi-Fi login pages do.

If nothing opens, type **`192.168.4.1`** into a browser. That is the address AWTRIX uses while it
is its own access point. It is also the **router** or **gateway** address your phone shows in the
connection details for this network, if you would rather read it off there.

## Step 2 - enter your Wi-Fi

The page that opens is the AWTRIX web UI, in setup mode: a single **System** tab with a blue
banner telling you to get it onto your Wi-Fi. The other tabs stay hidden until AWTRIX is on a
network.

1. Press **Scan** next to the network name field. AWTRIX surveys the air and fills a dropdown,
   strongest signal first, with a 🔒 on password-protected networks. Pick yours.
2. Type your Wi-Fi password.
3. While you are here, give it a **hostname** - a name like `kitchen-clock`. It becomes the
   address you use instead of an IP (`kitchen-clock.local`), which is far easier to remember than
   a number that can change.
4. Press **Save**.

Everything the web UI does, it does through the AWTRIX [HTTP API](../reference/http.md), so the
save and the restart below both work from a terminal:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"wifiSsid":"MyNetwork","wifiPass":"secret123","hostname":"kitchen-clock"}'

curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

Always send `Content-Type: application/json` as in the example above. AWTRIX only refuses a
`PUT` when the header is present with the *wrong* value - `curl -d` without an explicit `-H`
sends `application/x-www-form-urlencoded` by default, and that is what gets refused with
`415 unsupportedMediaType` ([Conventions](../reference/conventions.md#content-type-is-mandatory)).
Full field list: [Wi-Fi configuration](../reference/system.md#wi-fi).

## Step 3 - reboot

Saving only stores your credentials. The restart is what makes AWTRIX leave its own access point
and join your network. The **Reboot now** button in the banner that appears after saving does it,
and pulling the power and plugging it back in works exactly as well.

The access point disappears as AWTRIX restarts, so your phone drops back to its usual network by
itself.

You can also just walk away. While its access point is up, AWTRIX retries your configured
network every **30 seconds** and restarts itself the moment it gets in. Retries pause while someone
is connected to the access point, so correct credentials get you onto your network within
half a minute of your phone leaving. The same applies to a router that was slower to boot, or one
that came back after a power cut - no reboot needed on your side.

## What you can and cannot do in setup mode

The access point exists to configure Wi-Fi, and that is nearly all it allows. Saving Wi-Fi settings
and restarting work. Uploading icons, melodies or firmware, resetting AWTRIX, and putting it to
sleep are all refused until it is on a network, along with the features that need a real network:

| Not available until AWTRIX is on your Wi-Fi |
|---|
| Finding AWTRIX by name (`.local`) or by network discovery |
| The IP address scroll on the panel |
| Art-Net |
| The LaMetric icon downloader, and exporting a settings backup |

If you had already turned on a **username and password**, they are enforced on the access
point too. Authentication is off until you enable it - see
[Securing the API](../reference/http.md#authentication).

## It came back as an access point

Rainbow `AP MODE` after a restart means AWTRIX could not join your network within about 15
seconds. In order of likelihood:

- **Typo in the network name or password.** Nothing checks them when you save - a mistake only
  shows up at the next boot. Rejoin the access point and correct it. Use **Scan** to pick the
  network rather than typing its name.
- **5 GHz-only network.** The AWTRIX radio is 2.4 GHz. If your router publishes both bands under
  one name, that is fine; if the 2.4 GHz band is disabled, it cannot see the network at all
  and it will not appear in the scan.
- **A fixed IP address that does not fit your network.** If you configured a static address, switch
  AWTRIX back to automatic (DHCP) and let it get an address from your router.
- **Your router is down.** AWTRIX joins by itself once it is back, without a reboot on your side.

### Starting over

Two resets, in the web UI under **System → Maintenance**:

| | |
|---|---|
| **Reset settings** | Clears display settings only. Your Wi-Fi, MQTT and hardware configuration and all your files survive. |
| **Factory reset** | Clears **everything** - Wi-Fi credentials, all settings, and every file you uploaded (icons, melodies, palettes, scripts). AWTRIX comes back exactly as it was on its first boot, in access-point mode. |

!!! danger "A factory reset cannot be undone"
    You confirm it by typing the hostname. Once it runs, your uploaded files are gone and
    you start this page over from step 1.

More on what each reset keeps: [Persistence and resets](../reference/system.md#persistence-and-resets).

## Related

- [Finding AWTRIX](discovery.md) - by name, by discovery, or off the panel
- [Web UI tour](web-ui.md) - what the built-in interface can do now that it is all unlocked
- [Securing the API](../reference/http.md#authentication) - turning on a username and password,
  which are **off by default**
