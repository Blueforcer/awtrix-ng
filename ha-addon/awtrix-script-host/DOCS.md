# AWTRIX Script Host

This app runs the scripts of your AWTRIX NG clocks on Home Assistant. The clock shows them in its
rotation and keeps more memory free for everything else. It is for ESP32 clocks, for example the
Ulanzi TC001. The TC002 runs its own scripts and does not need it.

## Pair a clock

1. On the page of this app, press **Start**, then **Open Web UI**. The page shows a 6-digit
   pairing code.
2. Open the web UI of the clock and go to **System → Script host**.
3. Check **Address**. When the clock has found Home Assistant, its address is there already. With
   several script hosts, press **Home Assistant** under **Found**. Otherwise type the address of
   Home Assistant, for example `homeassistant.local` or `192.168.1.5`.
4. Type the code into **Code** and press **Pair**.

The clock copies its scripts to Home Assistant and restarts. Then **System → Script host** shows
**Connected**.

A code works for one clock and for ten minutes. **Open Web UI** always shows the current one. Pair
each further clock the same way. **Show in sidebar** keeps the page one click away.

## Good to know

- You keep installing and changing scripts in the web UI of the clock and on the AWTRIX Hub.
- While Home Assistant restarts, the clocks show their other apps. The scripts come back on their
  own.
- The app uses the network of the Home Assistant computer, so that the clocks can find it.
- Updates of the app keep your scripts and pairings. They are part of your Home Assistant backups.
- Before you uninstall the app, unpair each clock with **Bring scripts back**. Uninstalling deletes
  the scripts on Home Assistant.

## If something does not work

- **Home Assistant is not under Found:** reload the page of the clock after half a minute, or type
  the address of Home Assistant into **Address**.
- **wrong code:** type the code that **Open Web UI** shows now.
- **The Log tab says `cannot listen on port 8300`:** another app uses this port. Stop it, then start
  this app again.

The [script host guide](https://blueforcer.github.io/awtrix-ng/guides/script-host/) explains how to
move your scripts to another computer and how to unpair a clock.

## Help and license

Report problems at <https://github.com/Blueforcer/awtrix-ng/issues>. AWTRIX NG is licensed under
the PolyForm Noncommercial License 1.0.0.
