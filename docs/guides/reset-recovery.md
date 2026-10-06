# Reset & recovery

<!-- only esp32 esp32-s3 -->
This page shows how to reset AWTRIX NG and bring back a clock that does not start.
<!-- /only -->
<!-- only tc002 -->
This page shows how to reset AWTRIX NG, start the Ulanzi app, remove AWTRIX NG again, and bring
back a clock that does not start.
<!-- /only -->

## Reset the settings

1. Open the web UI and go to **System → Maintenance**.
2. Press **Reset settings**, then press it again to confirm.

The clock restarts with all [settings](../reference/settings.md) back at their defaults: the
Display tab, volumes, which apps rotate and the rest. Wi-Fi, MQTT<!-- only esp32 esp32-s3 -->, the
pin setup<!-- /only --> and your uploaded files stay as they are.

## Factory reset in the web UI

A factory reset erases everything: settings, saved Wi-Fi and every uploaded file (icons, melodies,
palettes<!-- only esp32-s3 tc002 -->, MP3s<!-- /only --> and scripts). AWTRIX NG stays installed.

1. If you want to keep anything, create a [backup](../getting-started/web-ui.md#backup-and-restore)
   first.
2. Under **System → Maintenance**, type the clock's hostname in the **Factory reset** row and press
   **Erase everything**.
3. The clock restarts and opens its setup hotspot. Set up Wi-Fi again as in
   [Connect to Wi-Fi](../getting-started/first-boot.md).

<!-- only tc002 -->
## Start the Ulanzi app {#starting-the-ulanzi-app}

1. Switch the clock off.
2. Hold the **knob pressed in** and switch the clock on.
3. Keep holding until the Ulanzi app appears.

The Ulanzi app runs offline for this one start. The next start without the knob runs AWTRIX NG
again.

## Remove AWTRIX NG {#uninstall}

<span id="factory-reset-with-the-reset-button"></span>

Hold the physical **reset button for 5 seconds**. The clock erases its data and reinstalls the
Ulanzi firmware stored on it.

- All AWTRIX NG settings, saved Wi-Fi and uploaded files are erased.
- The restored Ulanzi firmware can be older than the version your clock came with.
- To go back to AWTRIX NG, run the [USB installer](../getting-started/tc002.md) again.

## When the clock does not start {#when-awtrix-ng-cannot-start}

The clock shows **USB RECOVERY** when an update was interrupted, the installed version is damaged,
or a version failed to start three times in a row. Your settings, uploaded files and Wi-Fi are
still on the clock at this point.

1. Switch the clock off and on once. After a version that failed to start, this tries it one more
   time.
2. If it still shows **USB RECOVERY**, hold the **reset button for 5 seconds**. The clock goes back
   to the Ulanzi firmware. If you want to keep your files, ask on
   [Discord](https://discord.gg/5pbmeCrs3a) before you press the button.
3. Install AWTRIX NG again with the [USB installer](../getting-started/tc002.md). This is a fresh
   installation: settings, Wi-Fi and uploaded files are gone.
<!-- /only -->

<!-- only esp32 esp32-s3 -->
## When the clock does not start {#recovering-a-device-that-will-not-boot}

1. Install AWTRIX NG again over USB as described in
   [Install AWTRIX NG](../getting-started/flashing.md).
2. The USB install image resets the settings and the saved Wi-Fi. Set up Wi-Fi again as in
   [Connect to Wi-Fi](../getting-started/first-boot.md).
<!-- /only -->

## Good to know

- **A factory reset cannot be undone.** Only a backup made before it brings your settings and
  files back.
<!-- only tc002 -->
- **The setup hotspot only repairs missing Wi-Fi details.** It cannot repair damaged software:
  follow [When the clock does not start](#when-awtrix-ng-cannot-start).
<!-- /only -->

## Details

- [System configuration → Persistence and resets](../reference/system.md#persistence-and-resets):
  what each reset clears and keeps, and the same resets over HTTP

## Related

- [Updating firmware](updating.md): install a newer version
<!-- only tc002 -->
- [Install AWTRIX NG](../getting-started/tc002.md): the USB installer
<!-- /only -->
<!-- only esp32 esp32-s3 -->
- [Install AWTRIX NG](../getting-started/flashing.md): install over USB
<!-- /only -->
- [The web UI](../getting-started/web-ui.md#maintenance): the Maintenance section
