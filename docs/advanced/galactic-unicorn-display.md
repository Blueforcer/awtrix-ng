# Galactic Unicorn display

The Pico W/Pico 2 W board drives the fixed 53x11 panel using the display half
of Pimoroni pico **v1.23.0** (`3440ab232cdc2b019bb8d16f67b0448502efd9dc`, MIT).
Audio and pico_graphics are not included. See `THIRD-PARTY-NOTICES.md`.

## Height and wiring

`panelHeight` defaults to **11**. **8** produces a 53x8 canvas letterboxed on
physical rows **1–8** (zero-based); rows **0, 9, 10** remain dark. Width is
always 53, with one panel and fixed wiring. `PUT /api/v1/system` refuses any
other height and any change to the width, chaining or wiring keys with
`422 validationFailed`; a height stored some other way uses 11 and logs a
warning at board startup. `mirror` and `rotate` flip the picture as on any
panel, and `rotate` also swaps A and C.

Clock/data/latch/blank are GPIO 13/14/15/16, row selects GPIO 17–20. The
physical stream reverses both axes, matching Pimoroni's original driver.

## Refresh and color

One dynamically claimed PIO SM and two chained DMA channels continuously
refresh fourteen binary-weighted bit planes without CPU refresh interrupts.
PIO1 is preferred to leave PIO0 instruction memory for CYW43; allocation checks
both free SMs and program space, and can fall back to PIO0. The startup log
prints the actual display PIO/SM and DMA channels. The Pico variant's early
`init_cyw43_wifi` call is deferred with a linker wrapper until after display
startup. The radio log reports newly claimed PIO/SMs from the SDK allocation
bitmap, without depending on CYW43's private bus structure. After the Wi-Fi join
or AP startup, a bounded DMA-progress probe logs `refresh advancing` (or a
failure). This checks DMA movement, not panel wiring or visual quality; check on
the panel that the boot animation keeps moving while Wi-Fi connects.

Two aligned 9,240-byte buffers (18,480 bytes total) fit in static RAM. `show()`
packs the inactive buffer, publishes its aligned address, then waits until DMA
has begun reading it before reusing the old buffer. The control channel only
loads a new address at a full-frame boundary. The PIO FIFO preserves ordering
across that boundary. Calls must be serialized on core 0; the board is a single
device-lifetime instance. Each show may wait up to one refresh period; refresh
continues autonomously between calls, including while flash writes run.

Saturation, gamma, brightness, correction and tint follow NG ColorGrade's
ordering. A 14-bit LUT preserves panel precision instead of first quantizing
to an 8-bit intermediate canvas. Gamma is applied **once**, using the NG gamma
setting (default 1.9), replacing Pimoroni's fixed 2.2 curve. Brightness and color
balance scale linear light **after** gamma, unlike the upstream brightness
pre-scale. There is no second fixed gamma curve. Manual and automatic brightness
use the shared NG periphery service (median filtering, light curve and smoothing).

## Inputs

The map and pure debounce/step helpers live in `hal/GalacticUnicornDisplay.h`.
All buttons are active low with internal pull-ups and 35 ms debounce. A held
button acts once, not repeatedly; release and press again for another step.

| Input | GPIO | Function |
|---|---|---|
| A / B / C | 0 / 1 / 3 | Left / select / right |
| D | 6 | Unmapped |
| Sleep | 27 | Display power toggle |
| Volume up / down | 7 / 8 | `buzzerVolume` +/- 5, clamped 0–100 |
| Brightness up / down | 21 / 26 | `brightness` +/- 10, clamped 0–255; disables auto brightness |
| Light sensor | 28 (ADC2) | Automatic brightness |

A, B and C also call the [`buttonCallback`](../reference/system.md) webhook on every press and
release, with the same body as an ESP32 (`left`, `middle`, `right`). Connecting and waiting for
the answer are each capped at 300 ms, so an unreachable listener only stalls the display briefly.

ADC2 supplies native 12-bit counts (0–4095), not lux. Cover/uncover the sensor
with `autoBrightness` enabled to check the shared light curve and configured
min/max brightness and smoothing. Brightness keys select manual mode through
the same settings dispatcher as an API change. Volume keys use the onboard tone
channel setting (`buzzerVolume`); no audio sink is implemented yet, so there is
no audible output. DFPlayer/MP3/radio volume settings are not changed.

A goes to the previous app, C to the next (subject to NG rotate/swapButtons and
blockNavigation). B dismisses a notification; a double press within 300 ms
toggles power unless navigation is blocked. The shared periphery emits the same
`ButtonsChanged` state and left/middle/right callback names as Ulanzi. D is unmapped.
Extra keys dispatch `SetSettings` or `SetDisplay`, emitting normal settings/power
events; settings persist through the existing delayed LittleFS save. Power is
runtime-only, like the API, and fades out/in with NG's power animator; this is not
deep sleep. Button input continues while the panel is off.

The current Pico build has Wi-Fi, but MQTT/HA delivery and HTTP button
callbacks await the transport phase. Their source state/events are shared with
ESP32, not a private button protocol. ESP32's HTTP callback adapter is unchanged;
Pico networking must install its own `PeripheryService::setButtonPost` adapter.

ESP32 and Pico register the same `core/BuiltinCatalog.h`: five apps, nineteen
effects and six overlays, including the same palette-enabled subset. Missing
battery/environmental sensors still hide their apps. Boot logs print these
capability lists. With default settings expect Time 00:00/calendar 1, then Date
01.01.24 while the clock is unset. After NTP sync they show local time/date.
Saved settings may differ.

## Wi-Fi and setup

The shared `NetworkService` loads stored Wi-Fi credentials and uses the same
boot timeout (15 seconds by default), five-second reconnect checks and weak-signal
roam policy as ESP32. In provisioning mode the Pico retries the saved network every
**60 seconds**, not 30: its one radio can only join on the AP's channel while the AP
is up, so each retry stops the AP and captive DNS for one station-only join (up to
the join timeout) and brings them back if it fails. The setup network disappearing
for those seconds is expected. Retries pause while a phone is attached.
Static IP uses Pico's different argument order and configures both DNS servers.
Pico uses the core's worldwide country default and no-low-power mode, rather
than ESP32's country/scan/sort APIs; strongest-BSSID selection is core-dependent.

On a fresh boot without credentials, expect the boot logo followed by the
animated rainbow **AP MODE** screen. A phone sees an open SSID
**awtrixng-xxxxxx**, where `xxxxxx` is the last three MAC bytes in lowercase hex
(or the configured hostname). DHCP and wildcard captive DNS use **192.168.4.1**.
Hold **B / SELECT** for one second at boot to force this mode without erasing
credentials. The render priority matches ESP32: power animation, moodlight,
AP screen, Art-Net, normal apps.

After a successful STA boot, mDNS publishes `<hostname>.local`, `_http._tcp`
and `_awtrixng._tcp` on the configured web port (default 80), with the same
`id` (lowercase MAC without colons), `name` and `type=awtrixng` TXT records.
LEAmDNS is polled every loop. These records match ESP32. The shared HTTP
server serves the embedded, gzip-compressed web UI and `/api/v1` on port 80
in AP mode, or `webPort` (default 80) after a station-mode boot.

### Provision from a phone

1. Power on without saved credentials, or hold **B / SELECT** for one second
   during boot. Join the open `awtrixng-xxxxxx` Wi-Fi network (or your saved
   hostname). Accept the phone's **stay connected without Internet** prompt.
2. Open the captive portal, or explicitly browse to **http://192.168.4.1/**.
   The setup form offers Wi-Fi scanning or manual SSID entry. Enter your
   network name and password and save the changes.
3. Use the form's **Reboot** action to join immediately. Alternatively disconnect
   the phone from the AP: every 60 seconds the Pico pauses the AP for a
   station-only join, and once it joins the saved network it schedules a reboot
   via `setOnJoinedFromAp`. Retries deliberately pause while a phone remains
   attached, so do not wait on the open portal.
4. Rejoin your normal Wi-Fi and open **http://awtrixng-xxxxxx.local/** (or
   `http://<configured-hostname>.local:<webPort>/`). If mDNS is unavailable,
   use the address shown in the router's DHCP lease list or USB serial log.
5. Verify the API with `curl -i http://awtrixng-xxxxxx.local/api/v1/device`.
   Expect HTTP 200 and JSON containing `uid`, `soc`, `ipAddress`, `freeHeapBytes`,
   and `resetReason`. Add the configured port and Basic auth if you enabled them.

### HTTP port notes

The shared API enforces the same 8 KiB ordinary-body limit (413), JSON content
type checks (415), and settings validation (422). The Pico WebServer uses
`HTTPServer&` request hooks, including its newer `canRaw` overload; larger
bodies stream into the fixed arena instead of an unbounded String. Uploads
and file/melody listing use LittleFS's File API rather than an ESP VFS mount.
Scripting, MP3 and radio routes retain the shared 503 `unavailable` policy.
`/update` returns 503 `unavailable` with an explicit instruction to flash a
**UF2 over USB using BOOTSEL**; browser OTA is not supported. Use the UF2 built
for the actual Pico W or Pico 2 W, not an ESP32 `.bin` image.

Device heap facts use `rp2040.getFreeHeap()`. `minFreeHeapBytes` is the minimum
sampled by device-state requests, not an allocator-wide low-water mark.
`largestFreeBlockBytes` is 0 (unknown: arduino-pico provides no such query).
PSRAM facts remain zero internally and the shared serializer omits those fields
when no PSRAM exists. `scriptHeapPool` is `unavailable` with a zero budget.
The body-copy guard checks total free heap on Pico, not the largest block;
hardware soak testing is still needed to assess fragmentation and latency.

## Time, reset and sleep

The configured POSIX `tz` and `ntpServer` feed the core's asynchronous SNTP
client. It restarts only when either changes, or on a disconnected-to-connected
transition (matching ESP32 reconnect behavior). DNS/NTP retries are asynchronous.
The same `DevicePageClock` as ESP32 converts UTC to local time, including DST;
years before 2020 remain unset. After provisioning, expect AP MODE to go
away and the clock/date to change from placeholders to the configured local
time/date once the NTP server is reachable. No battery-backed clock is assumed.
Effect noise is seeded from Pico SDK `get_rand_32()` hardware entropy at boot.

Reset reporting uses the framework's best-effort cause: power-on → `poweron`,
watchdog → `watchdog`, reboot → `software`, RUN pin/debug → `external`,
brownout → `brownout` when distinguishable, otherwise `unknown`. ESP32 values
are unchanged. Pico boot logs and `/api/v1/device` report this reason.

The RP2040 hardware watchdog is armed once the radio is up, with its ~8 s maximum. A Pico that
hangs or hard-faults reboots itself after about 8 seconds, like an ESP32 after a panic, and
`/api/v1/device` then reports `resetReason: "watchdog"`. Boot's Wi-Fi join, sleep and the HTTP
server feed it while they wait. A request is parsed only once its header, and a body of up to
2 KB, has fully arrived, and a header over 2 KB is answered `431`; larger bodies (uploads, restores, script sources, refused ones
included) feed it every 1,436 bytes; and every response is written only as fast as the client
takes it, so a slow phone cannot trip it. Factory reset disarms it before formatting LittleFS
and reboots straight after. A requested reboot or a flash still reads `software`.

The shared device command dispatcher queues reboot/sleep/reset and performs it
after the response delay and display power animation, just like ESP32. The Pico
has **no ESP32-style deep sleep with GPIO wake**. Sleep instead blanks the panel
through the display-off callback, enables CYW43 aggressive power saving, and
waits with short yields. The CPU, RAM and panel refresh hardware remain powered;
this is not a low-microamp shutdown. Application/network request handling pauses.
The same requested millisecond duration ends sleep; GPIO27 also wakes it. A key
held on entry must be released before a new press wakes it. Wake reboots the
application like ESP32 deep-sleep wake, but reports `software`, not `deepSleep`.
The ordinary Sleep key display toggle remains separate and does not enter this
timed sleep. Check timer wake and a release/new GPIO27 press on physical hardware.

## UDP services

Both builds include shared discovery (query `FIND_AWTRIXNG` on UDP 4210,
reply `host[:port]` on 4211) and Art-Net on UDP 6454 when `artnet` is enabled.
The Pico binding uses WiFiUDP, preserving the shared protocol implementation.
Art-Net takes over below power/moodlight/AP screens and returns to apps five
seconds after the last frame. Universes start at zero and hold 170 RGB pixels
each, continuing across the 53x11 canvas. The frame buffer is allocated on first
use. HTTP discovery advertises the shared HTTP server.

Size comparison, measured when the UDP services were added (Pico W, same
compiler/options, 512 KiB LittleFS; later features add to both builds alike): UDP
enabled uses **100,784 bytes static RAM / 538,876 bytes flash**. The non-release
`galactic_unicorn_udp_measure` build omits both services and uses **100,644 /
536,908 bytes**: enabling them costs **140 bytes static RAM / 1,968 bytes flash**.
An active 53x11 Art-Net frame additionally needs 2,332 heap bytes for its pixels,
plus WiFiUDP/lwIP packet allocations. Both services fit comfortably; neither is
reported as unavailable. The measurement environment is not a release target.

Hardware acceptance: flash the UF2, check the SSID/AP MODE behavior above,
capture display and CYW43 PIO/SM logs plus `refresh advancing`, then check the
animated boot screen during a stored-credential join. Host tests and firmware
builds cannot establish physical Wi-Fi/display coexistence.

Host tests cannot establish physical refresh timing, orientation, or absence of
visible tearing; check those on the panel with the CI-built UF2.
