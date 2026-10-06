# Mirroring

This page lets you show the display of one clock on other clocks. Run a script or a pushed app on
one clock, and the others show exactly the same picture. The data is fetched only once, by the
clock that shares.

It takes two settings:

- On the clock with the content: **Share display**.
- On every clock that should show it: **Mirror clock**, with the address of the first clock.

Both clocks need the **same display size**.
<!-- only esp32 esp32-s3 -->
A 32×8 clock cannot mirror a 64×8 clock, or the other way round.
<!-- /only -->
<!-- only tc002 -->
Your clock can mirror and be mirrored by other clocks with a 52×16 display.
<!-- /only -->

---

## Set it up in the web UI

On the clock that shares, under *System → Mirroring*:

1. Switch **Share display** on.
2. Save.

On each clock that mirrors, in the same section:

1. Enter the IP address or host name of the sharing clock in **Mirror clock**, for example
   `192.168.1.42` or `kitchen.local`.
2. Save.

A second later the mirroring clock shows the other clock's display. The **State** line in the same
section tells you what is going on. See [State](#state).

With the HTTP API it is the same two settings on `PUT /api/v1/system`:

```bash
# on the clock that shares
curl -X PUT http://<sharing-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"mirrorShare":true}'

# on each clock that mirrors
curl -X PUT http://<mirroring-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"mirrorFrom":"192.168.1.42"}'
```

Both apply at once, without a restart.

---

## How it behaves {#what-the-mirroring-clock-shows}

While the other clock shows something to mirror, the mirroring clock shows that picture instead of
its own apps, pixel for pixel: the same apps, the same transitions and, if they are shared, the
same notifications. As soon as the other clock shows nothing to mirror, the mirroring clock goes
back to its own apps. That happens when the other clock shows an app you did not select, when its
display is off, when its mood light is on, and when it is out of reach for more than 3 seconds. A
mirrored picture is never passed on: if clock B mirrors clock A and clock C mirrors clock B, clock
C shows only what B shows of its own.

### What stays with the mirroring clock

- **Its own notifications** still appear. They cover the mirrored picture while they run, just
  as they cover an app.
- **Its own indicators and connection dots** are drawn on top.
- **Brightness, color settings, the display power and the mood light** are its own. A mirroring
  clock that is switched off stays dark.

---

## Choose what to mirror

Both sides can limit mirroring to some apps and to notifications:

| Setting | On | Meaning |
|---|---|---|
| **Shared apps** | the sharing clock | Which of its apps other clocks may show. |
| **Share notifications** | the sharing clock | Whether its notifications are shared too. |
| **Mirrored apps** | the mirroring clock | Which of the other clock's apps to show. |
| **Mirror notifications** | the mirroring clock | Whether to show the other clock's notifications. |

The app lists take app names separated by commas, for example `Weather, Time`. Upper and lower case
do not matter. `*` stands for every app, and an empty list for none. Both lists start at `*`, and
both notification switches start on.

A picture is shown only when both sides let it through. Some examples:

- **Only the weather script:** on the mirroring clock set **Mirrored apps** to `Weather` and switch
  **Mirror notifications** off.
- **Only notifications:** set **Mirrored apps** to an empty list and leave **Mirror
  notifications** on. The mirroring clock runs its own apps and shows the other clock's
  notifications.
- **Keep one app private:** on the sharing clock list every other app in **Shared apps**.

While one app slides in and another slides out, the transition counts for both. If either of the
two is selected, the whole transition is mirrored.

---

## State

The **State** line in the web UI, and the `mirror` object in
[`GET /api/v1/device`](../reference/device.md#mirroring-status), show what mirroring is doing:

| State | What it means | What to do |
|---|---|---|
| Off | No clock is entered in **Mirror clock**. | - |
| Viewers: *n* | This clock shares, and *n* clocks watch it. | - |
| Mirroring | The picture of the other clock is on the display. | - |
| Nothing shared | The other clock is reachable, but shows nothing that it shares. | Check **Shared apps** on the other clock. |
| App not selected | The other clock shows something this clock does not select. | Check **Mirrored apps**. |
| Other display size | The two displays differ in size. The line shows the other clock's size. | Mirroring needs displays of the same size. |
| No answer | Nothing came back from that address. | Check that **Share display** is on over there and that the address is right. |
| Searching… / Clock not found | The host name is being looked up, or could not be found. | Check the spelling, or enter the IP address instead. |
| No network | This clock is not on the network. | Fix Wi-Fi first. |
| Out of memory | This clock could not set aside room for the mirrored picture. | Restart the clock, or run fewer scripts. |

---

## Network and security

Mirroring uses **UDP port 4212** on your local network. The port is open only while **Share
display** is on or a clock is entered in **Mirror clock**.

Mirroring has no password. While **Share display** is on, **every device on your network** can
watch the display, up to eight clocks at a time. Only switch it on in a network you trust, and
choose with **Shared apps** what may be seen.

A mirroring clock takes pictures only from the address it looks up for **Mirror clock**. If the
sharing clock gets a new address from your router, a host name is looked up again after half a
minute without an answer. With a fixed IP address, reserve that address for the clock in your
router.

Unchanged pictures are sent once a second, so a still clock face causes almost no traffic.

---

## Related

- [Find your clock](../getting-started/discovery.md): get the IP or host name
- [System settings: Mirroring](../reference/system.md#mirroring): the settings in detail
- [Device status: Mirroring status](../reference/device.md#mirroring-status)
- [Pushed apps](pushed-apps.md) and [Scripting guide](scripting/index.md): content worth sharing
