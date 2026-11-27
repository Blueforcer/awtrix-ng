# AWTRIX Script Host

This app runs the scripts of your AWTRIX NG clocks. The clock shows them in its rotation, and its
memory stays free. It works with ESP32 clocks, for example the Ulanzi TC001. The TC002 runs its own
scripts.

## Install

1. Go to **Settings → Apps → Install app**.
2. Open the menu at the top right, select **Repositories** and add
   `https://github.com/Blueforcer/awtrix-ng`.
3. Select **AWTRIX Script Host** and select **Install**.
4. Select **Start**.

## Pair a clock

1. Select **Open Web UI**. The page shows a six-digit pairing code.
2. Open the web UI of the clock and go to **System → Script host**.
3. **Home Assistant** shows up under **Found**. If it does not, type the address of Home Assistant
   into **Address**, for example `192.168.1.5` or `homeassistant.local`.
4. Type the code into **Code** and press **Pair**.

The clock moves its scripts to Home Assistant and restarts. A code works once and for ten minutes.
The next code shows on the same page. **Show in sidebar** keeps the page one click away.

The [script host guide](https://blueforcer.github.io/awtrix-ng/guides/script-host/) explains how
to use, move and unpair your scripts.

## Network

The app uses the network of the Home Assistant computer, so the clocks find it and reach it
directly. It uses TCP port 8300, UDP port 4211 and, for each clock, TCP and UDP ports from 8301
upwards.

## Help and license

Report problems at <https://github.com/Blueforcer/awtrix-ng/issues>. AWTRIX NG is licensed under
the PolyForm Noncommercial License 1.0.0.
