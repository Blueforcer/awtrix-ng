---
only: [tc002]
---

# AWTRIX on Linux

AWTRIX on Linux is the same AWTRIX NG core that runs on the ESP32, built as an ordinary Linux
program called `awtrix-linux`. It uses the same engine, renderer, HTTP API, MQTT client, Berry
script engine and web UI. The program has two jobs:

- **Headless development target.** On a PC, or in WSL (Windows Subsystem for Linux), it runs
  without a clock attached. You see its pixels in the web UI. Use it to develop and test the API,
  scripts, rendering, MQTT and persistence.
- **The runtime of the Ulanzi TC002.** On the TC002, `awtrix-linux --board tc002` drives the
  52 × 16 panel, the keys, the knob and the speaker. A supervisor starts it and handles the
  hardware around it. That side is described in [AWTRIX NG on the TC002](../tc002/index.md).

The code lives in
[`src/platform/linux/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/linux);
the entry point is
[`main_linux.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/linux/main_linux.cpp),
the CMake target is defined in
[`cmake/Linux.cmake`](https://github.com/Blueforcer/awtrix-ng/blob/main/cmake/Linux.cmake).

## Pages in this section

Read them in this order:

1. **This page** – build, run and test `awtrix-linux` on a development machine.
2. [HTTPS administration](security.md) – the `--hardened` mode: authenticated HTTPS and MQTT over
   verified TLS for a Linux host that is reachable from the network.
3. [Running as a systemd service](service.md) – the shipped systemd unit that runs the hardened
   mode as an isolated system service.
4. [Update packages](updates.md) – the `.awup` update container, the verifier and the update
   state policy that the TC002 web update is built on.
5. [ARM system build](system-build.md) – the runner that builds a pinned ARM cross toolchain and
   userspace, and the evidence and reproduction tools around it.
6. [Buildroot configuration](buildroot.md) – what the Buildroot external tree contains, its pinned
   versions and its license evidence.

## Build

Use Linux, or a Linux distribution inside WSL on Windows. The build needs a C++17 compiler,
CMake 3.20 or newer, Python, PlatformIO and the OpenSSL development libraries. On Debian or
Ubuntu, from the repository root:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev liblzma-dev libjpeg-turbo8-dev python3-venv openssl mosquitto
python3 -m venv .pio/venv-linux
. .pio/venv-linux/bin/activate
python -m pip install platformio
pio pkg install -e native
cmake -S . -B .pio/linux -DCMAKE_BUILD_TYPE=Release
cmake --build .pio/linux --parallel 2
```

PlatformIO fetches the shared libraries. CMake builds `awtrix-linux` against the same core the
ESP32 firmware uses. The `host` CMake preset does the same into `build/host`:
`cmake --preset host && cmake --build --preset host`. The rest of this section uses `.pio/linux`,
as CI does.

## Run locally

Choose a dedicated directory for application data, then start the program from the repository
root:

```bash
.pio/linux/awtrix-linux \
  --data "$HOME/.local/share/awtrix-ng-dev" \
  --port 8080 \
  --width 52 --height 16 \
  --webui "$PWD/webui/index.html"
```

Open **http://127.0.0.1:8080**. By default the HTTP server listens on IPv4 loopback only and has
no login. Other local processes can call the API, so run it as an ordinary user and keep it
local. Browser requests must come from the service's own origin; foreign origins and unknown
`Host` headers are rejected. This is not authentication.

There are two other network modes:

- `--lan` serves the device on the network like the ESP32 does, with the login configured in the
  web UI. It listens on all IPv4 addresses unless `--listen` names one, and allows ports 1 to
  65535. It accepts no other security option. The TC002 uses this mode.
- `--hardened` serves authenticated HTTPS. See [HTTPS administration](security.md).

### Command-line options

| Flag | Default | Meaning |
|---|---|---|
| `--data DIRECTORY` | required | Persistent application data, locked exclusively while running |
| `--port NUMBER` | `8080` | HTTP port, `1024` to `65535` (with `--lan`: `1` to `65535`) |
| `--width NUMBER` | `52` | Display width, `8` to `128` pixels |
| `--height NUMBER` | `16` | Display height, `8` to `32` pixels |
| `--board NAME` | `headless` | `headless` serves pixels to the web UI only; `tc002` drives the TC002 panel and requires exactly 52 × 16 |
| `--webui FILE` | `webui/index.html` | The web UI file; an absolute path avoids depending on the working directory. A name ending in `.gz` is served gzip-encoded |
| `--lan` | off | Network service with the web UI login, as on the ESP32 |
| `--listen IPv4_ADDRESS` | `0.0.0.0` with `--lan`, `127.0.0.1` with `--hardened` | Listen address; only allowed with `--lan` or `--hardened` |
| `--ca-file FILE` | – | PEM root certificates that every HTTPS client of the program verifies against, read once at start |
| `--boot-intro` | off | Show the power-on intro before the first app |
| `--performance-report FILE` | – | Write frame-timing statistics as private JSON to a new file at exit |
| `--help` | – | Print usage and exit |

The hardened-mode flags are listed in [HTTPS administration](security.md). The TC002-only flags
(input, supervisor and speaker descriptors, start reason, device ID, web update) are passed by
the TC002 supervisor and documented in [AWTRIX NG on the TC002](../tc002/index.md).

The display size is fixed for the life of the process. In headless mode the device reports the
platform `linux`, that size, and no GPIO or panel wiring settings, so the web UI hides them. A
Berry or JSON drawing command at `(51, 15)` sets the bottom-right pixel of a 52 × 16 display.

## State and shutdown

The data directory holds settings, device configuration, a persistent device ID, app order,
radio stations, scripts and uploaded assets. Each instance needs its own directory and HTTP port.
Pushed apps live in memory only and disappear when the process stops.

Stop the program with **Ctrl+C** or **SIGTERM**. It stops accepting work, joins its HTTP workers
and writes pending script state and changed settings before it exits. A forced kill cannot do
this final write.

Files are replaced atomically, with a shared data quota of 8 MiB. When a configuration write
fails, the latest value is kept for a retry. A system-configuration write can answer
`507 insufficientStorage` while the new configuration is already active in memory; the response
says that saving is pending. Unsaved writes at shutdown give exit code 1.

Linux owns the network, DNS and the clock. The program uses the host clock with its configured
display timezone. On a PC it does not configure Wi-Fi or run an NTP client; set those up in the
host OS. MQTT connects to the configured broker; restart after changing the broker settings.

## Headless behaviour

| Area | Headless behaviour |
|---|---|
| Engine, scripts, notifications and rendering | Shared implementation |
| HTTP API, MQTT, assets and script stores | Shared host services with real files on disk |
| Script HTTP/HTTPS requests | Worker thread; HTTPS verifies the server certificate |
| LEDs | Shown in the web UI only |
| Buttons and sensors | None; no simulated sensor readings |
| Audio and internet radio | No audio output; the device reports no audio sinks |
| Wi-Fi setup, Art-Net and firmware updates | Not available |
| Reboot and sleep commands | Stop the process; Linux itself is not rebooted or suspended |
| Factory reset | Empties the data directory except its lock file, then exits |

## Test

After building, run the integration contracts:

```bash
ctest --test-dir .pio/linux --output-on-failure
python -m unittest discover -s tools/tc002 -p "test_*.py" -v
```

The contracts start temporary instances of the program and local test services. Mosquitto is the
MQTT broker; OpenSSL makes the local HTTPS test certificate. No clock, external broker or real
data directory is needed. The TC002 tool tests use fixtures and local processes and never connect
to a device.

The contract suite skips the MQTT and HTTPS fixtures when their tools are missing. Configure with
`-DAWTRIX_REQUIRE_INTEGRATION_SERVICES=ON` to make them mandatory; CI does this. The hardened
security suite always needs a non-root Linux account, `openssl`, `mosquitto` and
`mosquitto_passwd`.

CI builds `awtrix-linux` and runs these checks next to the core, web UI and ESP32 checks.

## Related

- [Building the firmware](../building.md)
- [Host tests](../host-tests.md)
- [AWTRIX NG on the TC002](../tc002/index.md)
