---
only: [tc002]
---

# TC002 tools

The host tooling for AWTRIX NG on the Ulanzi TC002 lives in
[`tools/tc002/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002). It builds the
release bundle and its third-party programs, installs and updates the clock over USB, and builds
the desktop and browser installers. How the port itself works is in the
[TC002 overview](index.md). End users install with the
[TC002 guide](../../getting-started/tc002.md).

## Tool pages

| Page | What it covers |
|---|---|
| [Install tools](install-tools.md) | `tc002_install.py`: build the bundle, install, deploy, back up, restore and recover a clock over USB ADB; web update packages. |
| [Desktop installer](desktop-installer.md) | The Tauri desktop app and terminal installer for end users: downloads, offline use, build and verification. |
| [Browser image tools](browser-image.md) | The WebAssembly SquashFS tools that build the `res` image in the browser and desktop installers. |
| [Kernel modules](kernel.md) | The kernel tree and config that build modules for the stock 4.9.84 kernel, `loop.ko`, module checks. |
| [Wi-Fi driver](wifi-driver.md) | The `aic8800` Wi-Fi driver rebuilt from source without key logging. |
| [Speaker driver](speaker-driver.md) | `awtrix_pcm.ko`, the GPL speaker driver on top of `mhal.ko`, and its userspace helper. |
| [Bluetooth configuration](bluetooth.md) | Format, values and licence of the 104-byte AIC Bluetooth parameter block the daemon sends. |
| [CA certificates](ca-certificates.md) | The pinned Mozilla CA bundle that `awtrix-linux` verifies HTTPS against. |
| [OpenSSL](openssl.md) | The static, trimmed OpenSSL 3.5.8 that `awtrix-linux` links. |
| [wpa_supplicant](wpa-supplicant.md) | The static wpa_supplicant 2.12 the daemon runs for Wi-Fi. |

## Directory map

| Path | What it is |
|---|---|
| `install/` | `tc002_install.py`, the release bundle (`build_bundle.sh`, `bundle.py`), the flash backup (`backup.py`), the `res` image (`build_res_image.py`) and the browser and source packages. See [Install tools](install-tools.md). |
| `desktop/` | The desktop and terminal installer. See [Desktop installer](desktop-installer.md). |
| `browser-image/` | Build recipe of the WebAssembly image tools. See [Browser image tools](browser-image.md). |
| `kernel/` | Kernel tree, module builds and checks; `smoke/hello` is the module that proves a tree. See [Kernel modules](kernel.md). |
| `toolchain/` | `env.sh`, which every recipe sources: the musl and glibc cross toolchains, the download cache and the work root. `fetch-arm-gnu-9.2.sh` fetches the glibc toolchain. |
| `lib/` | `tc002_device.py`, the ADB layer that `tc002_install.py` and `tc002_wifi.py` share (adb from `--adb`, `$ADB` or `PATH`; connecting and keeping USB ADB up), and `fetch.sh`, which takes a source tarball from the download cache or its URL and checks its pinned SHA-256. |
| `openssl/`, `busybox/`, `wpa_supplicant/`, `ca/`, `lzma/`, `jpeg/` | Pinned recipes for what a release ships: OpenSSL, the BusyBox DHCP client and server, `wpa_supplicant`, the CA bundle, the static liblzma for the MCU patch helper and the static libjpeg-turbo for pictures from the internet. |
| `mcu/` | Prepares the MCU extension a bundle ships. See [MCU update](mcu-update.md). |
| `licenses/` | Composes the licence texts a release ships. |
| `bluetooth/` | The `NOTICE` for the Bluetooth parameter block. See [Bluetooth configuration](bluetooth.md). |
| `tc002_wifi.py` | Wi-Fi setup over USB: `scan`, `set`, `status`. |
| `test_tc002_wifi.py` | Tests of `tc002_wifi.py`. The C and C++ contracts of the TC002 programs live in [`tests/tc002/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/tc002). |

## Tests

The CMake build runs the tool tests as `tc002-install-tools`, `tc002-device-tool`,
`tc002-wifi-host-tool`, `tc002-fetch` and `tc002-licenses`:

```text
ctest --test-dir BUILD_DIR -R "tc002-.*-tool|tc002-fetch|tc002-licenses" --output-on-failure
```

## Related

- [TC002 overview](index.md)
- [Install tools](install-tools.md)
- [Host tests](../host-tests.md)
