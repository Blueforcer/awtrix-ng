# Architecture overview

This section is for developers and curious tinkerers who want to know how AWTRIX NG is built. It
explains the parts of the repository, how they fit together and where to start reading. Everything
a user or an integration needs from the outside (HTTP, MQTT, payloads, settings, scripting) is in
the [reference](#public-interfaces) and the guides; these pages describe the inside.

## The parts

AWTRIX NG is one application engine that runs on two kinds of hardware.

| Part | What it is | Where it lives |
|---|---|---|
| **Shared core** | The engine: apps and rotation, notifications, rendering, layouts, fonts, effects, MP3 decoding, the synthesizer, the HTTP and MQTT command router, settings. Portable C++17 without Arduino, so it also runs and is tested on a PC. | `src/core/`, `src/media/` |
| **ESP32 firmware** | The engine on an ESP32 or ESP32-S3 with Arduino: LED matrix output, Wi-Fi, the HTTP server, MQTT, NVS and LittleFS storage, sensors, buzzer and DFPlayer. Built with PlatformIO. | `src/main.cpp`, `src/hal/`, `src/system/`, `src/persistence/`, `src/transport/`, `src/sound/` |
| **Web UI** | Tab sources generate one HTML file with all CSS and JavaScript. The ESP32 embeds it gzipped; Linux and the TC002 serve it from a file. It asks the device what it can do and adapts. | `webui/src/`, `webui/index.html` |
| **Berry scripting** | User apps written in [Berry](https://berry-lang.github.io/), a small scripting language. The interpreter is vendored; the bindings, the prelude and the script host are part of the core. | `lib/berry/`, `src/core/script/` |
| **`awtrix-linux`** | The same engine as a Linux program. Without a board it is a headless development target with the display shown in the web UI; with `--board tc002` it drives the TC002 panel. Built with CMake. | `src/platform/linux/`, `src/platform/posix/` |
| **TC002 product** | AWTRIX NG on the Ulanzi TC002, next to the stock system: a loader plugin that the stock UI loads, a release image in a slot of the `res` flash partition, a supervisor daemon, drivers and installers. | `src/platform/tc002/`, `tools/tc002/`, `cmake/Tc002*.cmake` |
| **Tools** | Code and font generators, API and doc consistency checks, update packaging, the ARM system build, TC002 installers. | `scripts/`, `tools/` |
| **Tests** | Unit tests of the core, host and Linux contracts, TC002 program contracts and web UI tests. | `test/`, `tests/`, `webui/test/` |
| **Docs** | This MkDocs site, the OpenAPI description and the browser tools it hosts (USB flasher, TC002 browser installer, flow converter). | `docs/`, `mkdocs.yml` |

Two related projects live in their own repositories: the AWTRIX Hub at awtrix.de (icon and script
catalogue) and the [Piskel fork](icon-editor.md) the icon editor embeds.

## How they fit together

```text
                      clients: web UI, Home Assistant, MQTT, scripts, curl
                                 |                     |
                               HTTP                  MQTT
                                 |                     |
  +------------------------------v---------------------v-----------------------------+
  |  Shared core (src/core, src/media)                                                |
  |    API router -> command bus -> CoreEngine                                        |
  |    apps + rotation | notifications | layouts | Berry script host (lib/berry)      |
  |    render pipeline -> canvas -> frame buffer                                      |
  +------------------------------+----------------------------+----------------------+
          platform services      |                            |    platform services
  +------------------------------v------------+  +------------v-----------------------+
  |  ESP32 firmware (PlatformIO)              |  |  awtrix-linux (CMake)              |
  |  Arduino, FastLED, WebServer, PubSubClient|  |  host services: HTTP, MQTT, TLS,   |
  |  NVS + LittleFS, sensors, buzzer          |  |  files; --board headless | tc002   |
  |  web UI embedded as WebUiAsset.h          |  |  web UI served from a file         |
  +------------------------------+------------+  +------------+-----------------------+
                                 |                            |
                        WS2812 LED matrix           headless: pixels in the web UI
                                                    tc002: 52x16 panel over SPI
                                                              |
                                        on the TC002, started and watched by:
                                        stock GUI host -> libawtrix-loader.so
                                          -> mounts the release from the res slot
                                          -> awtrix-tc002d (supervisor: Wi-Fi, DHCP,
                                             speaker helper, updates) -> awtrix-linux
```

The core never talks to hardware directly. It calls service interfaces (`src/core/Services.h`,
`src/hal/IBoard.h`) that each platform implements: the ESP32 in `src/hal/`, `src/system/` and
`src/persistence/`, Linux in `src/platform/linux/`. Of `src/platform/` the ESP32 build takes only
`src/platform/esp32/` (`build_src_filter = +<*> -<platform/> +<platform/esp32/>`).

A request takes the same path on every platform. The transport (HTTP server or MQTT client)
hands the request to the API router in `src/core/api/`. The router turns it into a command, and
`CoreEngine` executes it on the main loop. The render pipeline composes the next frame from the
current app, notifications, transitions and overlays, and the platform pushes the frame to the
panel. See [Display foundation](display-foundation.md) for the rendering side.

### On the ESP32

One firmware image per SoC family: `awtrix` for the classic ESP32, `awtrix_s3_octal` and
`awtrix_s3_quad` for the S3. The GPIO map is a runtime setting, so one image fits every board of
that family. The partition table is generated at build time, and updates over the network replace
only the app slot. See [Building from source](building.md).

### On Linux

`awtrix-linux` links the same core with host implementations of the services. For development it
runs headless on loopback: the display appears in the web UI, and MQTT, scripts and persistence
behave as on a device. It can also serve on the LAN with the web UI login, or run as a hardened
HTTPS service under systemd. See [AWTRIX on Linux](site:tc002/developers/linux/).

### On the TC002

The Ulanzi TC002 keeps its stock kernel and boot chain. AWTRIX NG changes only the `res` flash
partition:

- **Loader.** `libawtrix-loader.so` is a plugin the stock GUI program loads at every start. It
  decides whether to start AWTRIX NG, the vendor app or a rescue mode, and mounts the release.
- **Release slot.** The release is a read-only squashfs image stored in the `res` partition behind
  the stock squashfs. It holds every program, kernel module, the web UI and a manifest. A web
  update or a USB install replaces the slot as a whole.
- **Supervisor.** `awtrix-tc002d` owns the hardware, Wi-Fi, DHCP and the speaker helper, starts
  `awtrix-linux --board tc002` and hands over updates.
- **Installers.** A desktop installer (Windows, macOS, Linux), a browser installer on this site
  and a Python tool write the loader and the first release over USB.

Settings and user data live in `/data`. See the [TC002 developer guide](site:tc002/developers/tc002/).

## Repository layout

| Directory | Purpose |
|---|---|
| `src/core/` | The portable engine: API router, engine, apps, script host, render, layout, audio, synth, MQTT discovery, settings |
| `src/media/` | Fonts, GIF and JPEG decoding, icon rendering (shared) |
| `src/hal/`, `src/system/`, `src/persistence/`, `src/transport/`, `src/sound/` | ESP32 platform: boards and sensors, system services, NVS and file storage, HTTP and MQTT transport, buzzer and DFPlayer |
| `src/main.cpp` | ESP32 entry point |
| `src/platform/linux/host/` | Host implementations of the services, shared by `awtrix-linux` and the host tests |
| `src/platform/linux/` | `awtrix-linux`: `main_linux.cpp`, security, TLS trust, device facts, BLE |
| `src/platform/tc002/update/`, `src/platform/posix/` | Update state file; small POSIX helpers for the static programs |
| `src/platform/tc002/` | TC002: `loader/`, `daemon/`, `runtime/`, `flasher/`, `update/`, `audio/`, `voice/`, `mcu/`, `kmod/`, `contract/` |
| `lib/` | Vendored libraries: `berry`, `PubSubClient`, `TJpg_Decoder` |
| `webui/` | `index.html` (the web UI) and its jsdom tests in `test/` |
| `test/` | Unit test suites of the core, run by `scripts/test_native.py` |
| `tests/` | CMake test suites: `host/`, `linux/`, `tc002/`, `update/`, `mqtt_parser/`, `bootintro/`, `tools/` |
| `scripts/` | Build steps and generators: version, web UI embedding, partitions, fonts, solidified prelude, USB images, native test runner |
| `tools/` | Consistency checks (`check_*.py`), Berry tooling (`solidify/`, `coc/`), the flow converter tests, update packaging (`update/`), Linux provisioning (`linux/`), ARM system build (`system/`), TC002 tooling (`tc002/`) |
| `cmake/` | CMake modules per product (`Core`, `Host`, `Linux`, `Tc002*`) and cross toolchains |
| `firmware/` | The Buildroot external tree (`buildroot/`) |
| `boards/` | PlatformIO board files for the ESP32-S3 modules |
| `assets/` | Font sources (`fonts/`) and the TC002 boot sound (`tc002/`) |
| `packaging/linux/` | The systemd unit template |
| `piskel-fork/` | The test stub for the icon editor bridge |
| `skills/` | The downloadable AI agent skill for writing Berry apps |
| `docs/` | This site, `api/openapi.yaml`, the browser tools under `assets/` |
| `LICENSES/`, `THIRD-PARTY-NOTICES.md` | License texts and third-party notices |

Build products go to `.pio/` (PlatformIO) and `build/` (CMake presets); both are ignored by Git.

## Tests

| Suite | Runs | Command |
|---|---|---|
| Core unit tests (`test/`) | on the PC, Unity + CTest | `python scripts/test_native.py` |
| [Host tests](host-tests.md) (`tests/host/`) | on the PC | `ctest --preset host` |
| Linux, update, TC002 and tool contracts (`tests/`) | on Linux or WSL; ARM contracts under `qemu-arm` | `ctest --preset host`, `ctest --preset tc002` |
| Web UI (`webui/test/`) | Node.js with jsdom | `npm test` in `webui/test` |
| Consistency checks (`tools/check_*.py`) | on the PC | see [Continuous integration](building.md#continuous-integration) |

CI runs the core, host, Linux, web UI and consistency checks on every push. The ARM contracts
need the TC002 toolchains and run by hand.

## Public interfaces

The external API is documented once, in the reference. Link to it rather than repeating it:

- [HTTP API](../reference/http.md) and the [OpenAPI description](../api/openapi.yaml)
- [MQTT topics](../reference/mqtt.md)
- [App and notification payload](../reference/payload.md)
- [Settings](../reference/settings.md), [system configuration](../reference/system.md) and
  [device state](../reference/device.md)
- [Errors](../reference/errors.md) and [limits](../reference/limits.md)
- [Scripting guide](../guides/scripting/index.md) for the Berry API

Keep per-route error-message tables in the [error reference](../reference/errors.md).
Other pages link to them. `python tools/check_error_messages.py` checks quoted messages
against string literals in `src/`, including platform code and `.inc` files. It reads
message columns, status tables, inline error phrases and JSON error examples in current
user documentation and OpenAPI. Use `<name>` or an ellipsis for dynamic parts. Fixed words
must match the source. Release history, developer internals and Berry exception text from
the interpreter dependency are outside this API-message check. Its parser regressions run
with `python tools/test_check_error_messages.py`.

## Where to start

- [Building from source](building.md): PlatformIO environments, CMake presets, the embedded web
  UI, partition tables, USB images, CI.
- [Display foundation](display-foundation.md): geometry, frame buffers, layouts, frame rates.
- [Fonts](fonts.md): font sources, supplements, generators, licensing.
- [Hub script updates](hub-script-updates.md): script identity and the update flow.
- [Icon editor (Piskel)](icon-editor.md): the `postMessage` bridge to the editor.
- [Host tests](host-tests.md).

AWTRIX on Linux:

- [Overview](site:tc002/developers/linux/): building and running `awtrix-linux`.
- [Running as a service](site:tc002/developers/linux/service/) under systemd.
- [Update packages](site:tc002/developers/linux/updates/).
- [Security hardening](site:tc002/developers/linux/security/): the HTTPS administration mode.
- [System build tooling](site:tc002/developers/linux/system-build/) and the [Buildroot userspace](site:tc002/developers/linux/buildroot/)
  for ARM.

TC002:

- [Developer guide](site:tc002/developers/tc002/): boot chain, loader, release slot, supervisor, updates,
  security.
- [MCU updates](site:tc002/developers/tc002/mcu-update/), [microphone](site:tc002/developers/tc002/microphone/),
  [audio gate](site:tc002/developers/tc002/audio-gate/), [Home Assistant Voice](site:tc002/developers/tc002/ha-voice/).
- [Tools overview](site:tc002/developers/tc002/tools/), [install tools](site:tc002/developers/tc002/install-tools/),
  [desktop installer](site:tc002/developers/tc002/desktop-installer/), [browser image tools](site:tc002/developers/tc002/browser-image/).
- [Kernel modules](site:tc002/developers/tc002/kernel/), [Wi-Fi driver](site:tc002/developers/tc002/wifi-driver/),
  [speaker driver](site:tc002/developers/tc002/speaker-driver/), [Bluetooth](site:tc002/developers/tc002/bluetooth/).
- Shipped third-party components: [CA certificates](site:tc002/developers/tc002/ca-certificates/),
  [OpenSSL](site:tc002/developers/tc002/openssl/), [wpa_supplicant](site:tc002/developers/tc002/wpa-supplicant/).

Contribution rules are in
[CONTRIBUTING.md](https://github.com/Blueforcer/awtrix-ng/blob/main/CONTRIBUTING.md).
