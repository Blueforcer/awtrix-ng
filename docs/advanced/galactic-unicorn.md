# Galactic Unicorn

AWTRIX NG runs on Pimoroni's fixed **53×11 RGB Galactic Unicorn**, using the
same HTTP and MQTT `/api/v1` command semantics as the ESP32 builds. Pick the
image for the microcontroller actually fitted to your board:

| Hardware | PlatformIO environment | USB image | Verification |
| --- | --- | --- | --- |
| Pico W, RP2040, 264 KB SRAM, 2 MB flash | `galactic_unicorn` | `firmware-galactic-unicorn.uf2` | Hardware-verified on a real Galactic Unicorn |
| Pico 2 W, RP2350, 520 KB SRAM, 4 MB flash | `galactic_unicorn_2w` | `firmware-galactic-unicorn-2w.uf2` | Hardware-verified on a real Galactic Unicorn (see [scope](#verification-scope)) |

Both reserve **512 KiB for LittleFS**, separate from the application. The
PlatformIO build's reported RAM denominator excludes memory reserved by the
framework. Neither board has external PSRAM. There is no battery, and no I²C
sensor is read (the Qw/ST connector is unused), so battery, temperature and
humidity values are omitted; the onboard light sensor is available. Do not select
an ESP32 image or use its runtime GPIO presets.

## Flash over USB

1. Obtain the UF2 for your Pico version. A source build uses
   `pio run -e galactic_unicorn` or `pio run -e galactic_unicorn_2w`; its output is
   `.pio/build/<environment>/firmware.uf2`. CI's `uf2-<environment>` artifact also
   contains `firmware.uf2`, so keep track of which artifact you downloaded.
2. Disconnect power. Hold the Pico's **BOOTSEL** button while connecting a USB
   **data** cable to your computer, then release it. This is BOOTSEL on the Pico,
   not A/B/C/D or the panel's Sleep button.
3. Drag and drop the matching `.uf2` onto the mounted boot drive (`RPI-RP2` for
   Pico W, `RP2350` for Pico 2 W). Let the copy finish. The drive disappears and
   the board restarts automatically.
4. Subsequent updates use the same procedure. A normal application UF2 keeps
   LittleFS settings and files when its flash layout is unchanged; back them up
   first. Do not use a full-flash erase image unless you intend to erase them.

Browser OTA is not implemented: `POST /update` returns `503 unavailable` and
instructs you to use BOOTSEL/UF2. An ESP32 `.bin` is not interchangeable with UF2.
The web UI's **Check for updates** still names a newer release and links its
notes, but offers no **Download & install** or `.bin` upload on this board.

## First boot and provisioning

The panel shows the boot animation and, without saved Wi-Fi credentials, an
animated **AP MODE** screen. Join the open **awtrixng-xxxxxx** network (`xxxxxx`
is the last three MAC bytes), or the configured hostname if it was changed.
Keep the phone connected even though this setup network has no Internet.

Open **http://192.168.4.1/** if the captive portal does not open. Select or type
your 2.4 GHz Wi-Fi SSID, enter its password and save. Use **Reboot** to join
immediately. Alternatively disconnect the phone: retries pause while it is
attached. After that, every 60 seconds the setup network goes away for a few
seconds while the Unicorn tries the saved network, and it reboots once it joins.
Hold **B / SELECT for one second during boot** to force setup mode without
erasing credentials.

Rejoin your normal network and open **http://awtrixng-xxxxxx.local/** (or the
configured hostname/port). Use the router's DHCP lease list if mDNS is
unavailable. `/api/v1/device` reports the address and connection state. Configure
MQTT and Home Assistant discovery in System as on an Ulanzi; the shared discovery
and command protocol are unchanged. NTP and the configured time zone set the
clock after joining. There is no battery-backed clock.

## Buttons

Buttons are active-low, debounced for 35 ms. The extra keys act once per press,
not repeatedly while held.

| Button | Action |
| --- | --- |
| A | Previous app / left |
| B | Select; dismiss a notification; double press within 300 ms toggles panel power |
| C | Next app / right |
| D | Unmapped |
| Brightness + / − | Step `brightness` by 10, clamped to 0–255; select manual brightness |
| Volume + / − | Step `buzzerVolume` by 5, clamped to 0–100 |
| Sleep | Toggle panel power; this is not timed device sleep |

Navigation follows the shared rotate/swap and `blockNavigation` rules. A/B/C
emit the same button events and `buttonCallback` HTTP webhook payloads as ESP32
(`left`, `middle`, `right`, press and release). See the
[pin map and electrical details](galactic-unicorn-display.md#inputs).

The onboard I²S speaker plays RTTTL: use `sound` for a saved melody or
`soundRtttl` for inline notes. It reports the **buzzer** capability, not MP3 or
DFPlayer. Volume keys and `buzzerVolume` control that same output.

## Feature availability

Always read **`GET /api/v1/capabilities`**, rather than inferring features from a
chip name.

### Scripting

Both versions run Berry scripts and report `scripting: true`, so the web UI shows
Run scripts and the script pages. They share one heap with Wi-Fi, HTTP, MQTT and
the display, so the budget depends on the chip:

| | Pico W (RP2040) | Pico 2 W (RP2350) |
| --- | --- | --- |
| Script heap budget (`scriptHeapBudgetBytes`) | 48 KB | 96 KB, as on the ESP32 |
| Interpreter cost with no script installed | about 19 KB of free heap | about 17 KB of free heap |
| Largest script that installs | about 7 KB of source | about 28 KB of source |

Both columns were measured on a real Galactic Unicorn. On the Pico 2 W a bigger
script fails with `out of memory` while plenty of heap is still free: the 96 KB
budget is the limit there, not the memory Wi-Fi needs.

On the Pico W a failed install of a large script can leave the heap fragmented,
so later installs are refused with `507` even though enough memory is free in
total. Reboot to recover. Script `http.*` requests, and Modbus reads, which go
through the same request path, return `false`, and script icons are not drawn on
either board; timers, drawing, storage, buttons, sound and MQTT
work as on the ESP32.

If a script takes the device down on every boot, hold **A and C** together for
three seconds while powering on. The panel shows `NOSCR` and the Unicorn starts
without running scripts, exactly like left and right on other boards (see
[troubleshooting](../troubleshooting/troubleshooting.md#scripts-eat-the-memory-and-awtrix-never-comes-up)).

### Compiled out

The Pico builds compile out the following:

| Feature | Reason / behavior |
| --- | --- |
| MP3 playback and Internet radio | The port supplies a bounded RTTTL tone sink, not the decoder/streaming audio pipeline; their audio flags are false. |
| Outbound TLS | No secure-client integration in the Pico transport; use a trusted local network and supported plain-HTTP endpoints. |
| Browser OTA | No dual-slot updater for the Pico flash layout; update with USB UF2 instead. |

`scriptingEnabled` turns the VM off at the next boot, as on the ESP32; stored
scripts stay listable and editable. A build made with
`-D AWTRIX_FEATURE_SCRIPTING=0` still **accepts and stores `scriptingEnabled`**,
but the setting is inert there: `scripting` is false, script routes return
`503 unavailable`, and the UI hides Run scripts and the script pages, showing
“not available on this board”. MP3/radio sections and their controls follow
their own capability flags.

HTTP Basic authentication is not encryption. Do not expose this device directly
to the Internet. Feature-disabled routes retain the shared error vocabulary,
not a second Pico-only API.

## Sleep emulation

`POST /api/v1/device/sleep` blanks the panel, enables radio power saving and waits
for `durationMs` or a new press of **Sleep / GPIO27**. A key already held must be
released before it can wake the device. The CPU, RAM and panel-refresh hardware
stay powered and application/network request handling pauses; this is **not
ESP32 deep sleep** or a low-microamp shutdown. Wake restarts the application and
reports `resetReason: "software"`. Ordinary panel-power toggling leaves the
application and networking running. The hardware watchdog still protects a
hung application and reports `watchdog` after recovery.

## Eleven rows and the tall-panel rule

`panelHeight` defaults to **11**, with fixed width **53**. Height **8** is a
compatibility mode letterboxed on physical rows 1–8. The API refuses other
heights, and any change to the width or wiring keys, with `422 validationFailed`;
a height stored some other way falls back to 11 at board startup with a warning.
A restored backup keeps the Unicorn's own pins and panel.

The shared tall-panel rule centres ordinary text, primary 8×8 icons and built-in
apps in an eight-row band starting at `floor((H - 8) / 2)` (row 1 at height 11).
Backgrounds, effects, overlays, transitions and charts use the whole canvas;
progress is on its last row. Draw commands and explicitly positioned icons use
absolute coordinates. GIFs keep their own dimensions. See
[Limits](../reference/limits.md#tall-panels-content-band-and-full-canvas).

## Verification scope

Verified on **Pico W** hardware: display/light sensor/fps, HTTP
smoke testing (`tools/pico/smoke.sh`, no failures), 80 concurrent requests, watchdog recovery,
MQTT and real Home Assistant discovery (21 entities), Matrix light control,
notifications, A/B/C events, brightness, volume, Sleep, and hearing the `alert`
melody. The button **webhook** remains host/simulator-tested only.

Verified on **Pico 2 W** hardware: display at 42 fps, `mirror` and `rotate`,
notifications, light sensor, Wi-Fi join, mDNS and NTP, HTTP smoke testing (every
API check passes; the 80-request burst depends on the Wi-Fi link), watchdog
recovery (`resetReason: "watchdog"`), settings kept across application UF2
updates, and the scripting figures above. MQTT, Home Assistant, the buttons and
hearing a melody are not yet checked on that board.

See [driver details](galactic-unicorn-display.md) and
[LittleFS persistence](pico-persistence.md) for implementation constraints.
