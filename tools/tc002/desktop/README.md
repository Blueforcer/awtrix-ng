# TC002 desktop installer

The desktop installer puts AWTRIX NG on a Ulanzi TC002 over USB. It needs no stock Wi-Fi setup,
Android SDK, ADB, Python or WSL. It catches the stock firmware's short USB window, prepares the
application image, and asks before writing the clock. Optional Wi-Fi settings are sent over USB
after AWTRIX starts; if they are left empty, AWTRIX opens its setup access point, which is also the
way back when the saved network is unavailable. The source lives in
[`tools/tc002/desktop/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/desktop).

## How it works

There are two programs:

- **The graphical installer** uses [Tauri](https://v2.tauri.app/) and the operating system's
  webview. It carries no Chromium and no firmware image.
- **The terminal installer** (`awtrix-tc002-installer-cli`) walks through the same USB
  installation and optional Wi-Fi setup without a graphical session or WebKit. It runs on Node.js
  24 or newer when present, or prepares a pinned, hash-checked official Node.js runtime from a
  local archive or an automatic download.

Both use the same image-building and installation engine as the browser installer, including the
WebAssembly [browser image tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/browser-image/) that build the `res` image. The native side
(Rust, in `native/`) handles USB, downloads and runtime extraction.

Firmware selection follows this order:

1. an explicitly chosen file (**Choose firmware ZIP…** in the app, `--firmware PATH` in the
   terminal);
2. `usb-awtrix-ng-tc002.zip` beside the program;
3. a download from the latest stable GitHub release, checked against its published digest.

Every route checks the TC002 package contents. A missing, unreadable or invalid chosen or
automatically found ZIP stops installation without falling back to the next source. Wi-Fi
passwords are never passed as command-line arguments or stored in an installer configuration
file.

## Downloads

Every [release](https://github.com/Blueforcer/awtrix-ng/releases/latest) carries the installers
with version-free file names, the source archive for each platform and
`AWTRIX-NG-TC002-Installer-manifest.json` (version, commit, sizes and SHA-256).

| System | Graphical installer | Terminal version |
| --- | --- | --- |
| Windows 10/11, x64 | Standalone EXE | ZIP containing the CLI EXE |
| macOS 13.5+, Intel or Apple silicon | DMG for the matching processor | `tar.gz` for the matching processor |
| Linux, x64 or ARM64 | DEB or RPM | `tar.gz` for the matching processor |

- The Windows EXE runs from any folder and installs nothing. It uses the installed WebView2
  runtime, which Windows 11 and current Windows 10 contain; otherwise install the
  [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/).
- macOS uses the system WebKit.
- Linux packages depend on WebKitGTK 4.1 and GTK 3, which the package manager installs. The
  baseline is Ubuntu 22.04 / Debian 12 or newer; RPM-based distributions need compatible glibc,
  GTK 3 and WebKitGTK 4.1 packages.

The builds are unsigned, so Windows or macOS may ask for confirmation before opening them. There
is no automatic application updater.

Run the terminal version from its extracted folder:

```sh
./awtrix-tc002-installer-cli
```

On Windows, use `awtrix-tc002-installer-cli.exe`. `--help` shows the options instead of starting
an installation; `--version` prints the version.

## Offline use

Keep the TC002 firmware ZIP compressed. In the graphical app, press **Choose firmware ZIP…** and
select it before connecting the clock. In the terminal version, pass its path:

```sh
./awtrix-tc002-installer-cli --firmware "/path/to/usb-awtrix-ng-tc002.zip"
```

Quote paths containing spaces. The chosen file takes priority over any ZIP beside the program
and over GitHub. Cancelling the graphical file dialog keeps the current choice. **Retry firmware**
rereads the chosen file; restart the app, or omit `--firmware` on the next terminal run, to return
to automatic selection.

For automatic discovery, name the file **`usb-awtrix-ng-tc002.zip`** and put it beside the
graphical or terminal executable:

- macOS graphical app: beside the `.app`, outside the app itself.
- Linux graphical packages: beside the installed executable, not beside the DEB or RPM file. The
  file dialog can select a ZIP from any folder.

The installer shows **Local firmware** and checks it before connecting to USB.

Offline graphical use needs the system webview already installed: WebView2 on Windows, the
package dependencies on Linux.

The terminal version needs an installed Node.js 24+ or the matching original Node.js **24.21.0**
archive beside its executable, left compressed; the installer verifies and prepares it.

| System | x64 archive | ARM64 archive |
| --- | --- | --- |
| Windows | `node-v24.21.0-win-x64.zip` | — |
| macOS | `node-v24.21.0-darwin-x64.tar.gz` | `node-v24.21.0-darwin-arm64.tar.gz` |
| Linux | `node-v24.21.0-linux-x64.tar.gz` | `node-v24.21.0-linux-arm64.tar.gz` |

The archives come from the [official Node.js files](https://nodejs.org/dist/v24.21.0/). The
[TC002 guide](https://blueforcer.github.io/awtrix-ng/tc002/getting-started/tc002/) has the end-user instructions.

## Linux USB access

The DEB and RPM packages install a USB access rule restricted to `18d1:d002`, the TC002's
vendor/product pair. After installing the package, reconnect the clock. An active local desktop
session gets access through systemd-logind.

The standalone Linux CLI archive contains the same rule and a short
[`README-LINUX.md`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/desktop/native/resources/linux/README-LINUX.md).
From the extracted archive, install the rule once:

```sh
sudo install -D -m 0644 70-awtrix-tc002.rules /etc/udev/rules.d/70-awtrix-tc002.rules
sudo udevadm control --reload-rules
```

For SSH or a machine without a desktop session, also add your account to `plugdev` once:

```sh
sudo groupadd --system --force plugdev
sudo usermod -aG plugdev "$(id -un)"
sudo udevadm control --reload-rules
```

Log out completely and back in, then reconnect the clock. Run the installer as your normal user.
The rule uses mode `0660`; other users and other USB devices get no general access.

## Building

Build each graphical package on its own operating system. Install Node.js 22.15+, Rust 1.94.0
and the [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/). Windows needs the MSVC
build tools; macOS needs Xcode's command-line tools. Linux GUI builds need the WebKitGTK 4.1 and
GTK 3 development packages. A CLI-only build needs only the Rust toolchain and native linker.

Install the exact JavaScript dependencies and the test harness from the repository root:

```sh
npm ci --prefix tools/tc002/desktop
npm ci --prefix webui/test
node --test tools/tc002/desktop/test/*.test.*
```

Prepare the pinned image tools once on Linux (see [browser image tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/browser-image/)). The
generated directory can be copied to the same location in a Windows or macOS checkout:

```sh
python3 tools/tc002/browser-image/build.py --work /tmp/tc002-image-build --output docs/assets/tc002/image
```

Keep its generated JavaScript, WASM, `build.json`, licence texts and source archives together;
their hashes are checked before packaging. No firmware ZIP or stock device image is a build input.

From `tools/tc002/desktop`, build into a **new directory outside the checkout** whose parent
exists:

```sh
node build.mjs --output /path/to/new-build-directory
```

| Option | Effect |
|---|---|
| (none) | The GUI app (standalone EXE on Windows, DMG on macOS, DEB and RPM on Linux), a separate terminal archive and a source archive. |
| `--cli-only` | Only the terminal application, without Tauri or a system webview. |
| `--target TRIPLE` | The Rust target. |
| `--bundles LIST` | `dmg`, `deb`, `rpm`, `deb,rpm`, or `none` to compile without a package. Windows always uses `none`. |
| `--stage-only` | Only prepare and verify the frontend assets. |
| `--output DIR` | Omitted: a fresh directory in the system temporary directory. |

`CARGO_TARGET_DIR` reuses an external Cargo build cache between builds. The script sets
`AWTRIX_INSTALLER_ASSETS` to the exact staged assets for both binaries and passes an absolute
`frontendDist` override to Tauri. Direct Cargo or Tauri builds need the same inputs; the checked-in
default `../dist` is only a placeholder. Build and test commands never connect to a clock.

### Output

| Path | Content |
|---|---|
| `artifacts/` | The installers, terminal archive, matching source archive and manifests to publish together. |
| `frontend/` | The exact embedded UI, CLI engine, shared installation code, WASM, image-tool sources and licence notices. No firmware, stock image or device data. |
| `build-manifest.json` | Installer commit and dirty state, target, runtime version, and each embedded file's source, byte count and SHA-256. |
| `artifact-manifest.json` | The final downloadable files with sizes and SHA-256. |
| `sources/` | The application sources and the corresponding source archives of native dependencies with copyleft licences. |

The source archive includes `REBUILD.md` with the commands to rebuild its snapshot directly. Build
validation also checks that native sources did not change while the compiler ran.

Cargo dependencies are pinned by `native/Cargo.lock`. Packaging records their licence expressions
and includes their licence and copyright notices. The image-building WASM keeps its separate
GPL/LGPL notices and corresponding sources. The application is under the repository's licence.

### CI

The [installer workflow](https://github.com/Blueforcer/awtrix-ng/blob/main/.github/workflows/tc002-installer.yml)
builds the WASM once, runs the shared tests, then builds Windows x64, macOS Intel/ARM64 and Linux
x64/ARM64 packages on native runners. It uses no signing keys. On a `v*` tag the CI workflow runs
it and attaches the installers to the release.

### Publishing

The release job stages the five platform artifacts with:

```sh
node tools/tc002/desktop/publish.mjs --from ARTIFACTS --out DIRECTORY
```

It checks every file against its `artifact-manifest.json`, requires all five to come from the
same clean commit and version, and writes the version-free files and
`AWTRIX-NG-TC002-Installer-manifest.json` into `DIRECTORY`. The
[TC002 guide](https://blueforcer.github.io/awtrix-ng/tc002/getting-started/tc002/#get-the-installer)
links to these file names on the latest release.

## Testing

All tests below use fake USB sessions or no USB at all. They do not cover physical USB discovery,
drivers, flash writes or Wi-Fi setup on a real clock.

- **`npm test`** in `tools/tc002/desktop` runs the UI, native bridge, terminal flow and packaging
  tests. The terminal suite also builds a synthetic SquashFS image with the real WASM tools and
  verifies its hash, metadata, partition bounds and the release slot behind it. Build the image
  tools first.
- **`cargo test --locked --no-default-features --features cli`** in `native` checks the native USB
  lifecycle, descriptors, transfer bounds, download verification, runtime extraction and the
  terminal protocol. It never enumerates USB hardware and needs no desktop libraries.
- **`--check-release`** on the CLI executable checks the current GitHub download without opening
  USB. A missing TC002 asset is an expected failure while no matching public release exists.
  `--help` and `--version` need neither a display nor network access.

### Webview qualification

For a check of the real Tauri webview, build with the `qualification` Cargo feature and point
`AWTRIX_QUALIFICATION_FIRMWARE`, `AWTRIX_QUALIFICATION_STOCK` and `AWTRIX_QUALIFICATION_REPORT` at
a qualified TC002 test package (browser package format 2), the synthetic stock fixture and a new
report path. Run that executable on a desktop. It loads the real UI, binary IPC and WASM worker,
checks that the package's release image fits the release slot of the resulting image, compares
that image with the hash `test/tauri-smoke.js` pins from an independent native build of the same
package, writes its JSON report and exits. Qualifying another test package means pinning its
native hash there.

To qualify offline loading through the real native file reader, place `usb-awtrix-ng-tc002.zip`
beside the test executable and set `AWTRIX_QUALIFICATION_SIDECAR=1`. The report must name
**Local firmware**; without the sidecar the normal GitHub download path runs.

The `qualification` feature disables every hardware operation and is excluded from shipped
builds. Never distribute a qualification executable as an installer.

## Related

- [TC002 tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/tools/)
- [Browser image tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/browser-image/)
- [Install tools](https://blueforcer.github.io/awtrix-ng/tc002/developers/tc002/install-tools/)
- [TC002 guide](https://blueforcer.github.io/awtrix-ng/tc002/getting-started/tc002/)
