---
only: [tc002]
---

# iPhone

This page shows how to connect an iPhone to the clock over Bluetooth. The clock then shows
notifications from the apps you choose, and the song your iPhone is playing. The clock and the
iPhone talk directly. The [AWTRIX NG app](app.md) chooses what the clock shows.

The iPhone link needs an iPhone with iOS 13 or later.

## How it behaves

The iPhone link is off until you switch it on. While it is off, the clock is not visible to
iPhones and shows nothing from them. Once paired, the clock remembers the iPhone and connects to it
by itself whenever it comes back in range, about 10 m. The clock connects to one iPhone at a time,
and only to the one it remembers.

## Connect your iPhone

1. Switch the iPhone link on: in the web UI, open **System → iPhone** and turn on **iPhone link**,
   or switch it on in the AWTRIX NG app.
2. On the iPhone, open **Settings → Bluetooth**. The clock appears under *Other Devices*. Tap it.
3. Tap **Pair**.
4. Allow **Share System Notifications**.

**Connection** in the web UI shows *Connected* and the iPhone's name once everything is set up.

## Choose the apps

Only the apps you choose appear on the clock. All other notifications stay on the iPhone.

You choose the apps in the AWTRIX NG app, under **Notifications from**. Tap **Add app** and search
for the app, or add one under **Recently seen**, the apps that sent a notification lately. Each
chosen app gets its icon.

Each notification shows the app's icon, the app's name and the message: the title, a colon and the
text, up to 240 characters. It stays 8 seconds, in orange text.

## Music

With **Show music** on in the AWTRIX NG app, the clock shows what the iPhone plays: title, artist
and a progress bar. It appears when a song starts and disappears a few seconds after you pause or
stop, or when the iPhone leaves.

With **Load covers** on, the clock looks up the album cover and shows it as the icon. For that it
sends the artist and title of each song to Apple's public iTunes search. Turn **Load covers** off to
keep this to yourself. Without a cover, the clock shows the icon of the chosen app whose name matches
the player, such as *Spotify*, or else a note.

## Forget the iPhone

To connect a different iPhone, or to stop for good, forget the iPhone on both sides:

1. In the web UI, open **System → iPhone** and press **Forget iPhone**, then press again to confirm.
2. On the iPhone, open **Settings → Bluetooth**, tap **ⓘ** next to the clock and choose **Forget This
   Device**.

The next iPhone to pair is then remembered instead. To only pause the link, turn **iPhone link**
off. The clock keeps the iPhone and your chosen apps.

## When it goes wrong

| Symptom | What to do |
|---|---|
| The clock does not appear in iOS Settings | Check that **iPhone link** is on and that the iPhone is close to the clock. If the clock remembers another iPhone, press **Forget iPhone** first. |
| Pairing fails, or the iPhone keeps trying | On the iPhone, choose **Forget This Device** for the clock, press **Forget iPhone** in the web UI, then pair again. |
| **Connection** stays at *Connecting…* | Wait a few seconds. After pairing, the iPhone reconnects by itself. |
| No notifications appear | Check that the app is chosen in the AWTRIX NG app and that the iPhone allowed **Share System Notifications** (iPhone **Settings → Bluetooth → ⓘ** next to the clock). |
| The cover is missing | The song was not found in Apple's search, or **Load covers** is off. |

## Good to know

- **iOS Settings may keep showing a spinner next to the clock.** Nothing to do: the link works
  anyway.
- **Notifications that were already on the iPhone when it connected are not shown, and neither are
  silent ones.**
- **iOS does not share alarms and timers, so the clock cannot show them.**

## Related

- [Notifications](notifications.md): what a notification looks like and how it queues
- [Gamepad](gamepad.md): the other Bluetooth device the clock pairs
