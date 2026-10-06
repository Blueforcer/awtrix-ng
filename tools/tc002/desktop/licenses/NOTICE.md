# AWTRIX NG TC002 Installer

Required Notice: Copyright © Stephan Mühl (Blueforcer) https://github.com/Blueforcer/awtrix-ng

The installer application and its shared AWTRIX code use the project's PolyForm
Noncommercial License 1.0.0. The complete terms accompany the program as
`licenses/LICENSE.md`.

The native application uses Tauri and nusb with their respective MIT or Apache
2.0 licenses. The build collects resolved Rust dependency license texts and
notices under `licenses/native/`, together with an inventory of versions and
license expressions. The graphical application uses the operating system's
WebView. It does not distribute Chromium or Electron.

The terminal installer reuses an installed Node.js 24 or newer, or downloads a
verified official Node.js distribution from nodejs.org. Its archive is cached
locally and its included license accompanies the extracted runtime. Node.js is
not embedded in the installer download. Runtime checksums are pinned in
`native/src/runtime.rs` against the official distribution's SHASUMS256.txt.

The separate SquashFS conversion programs and their components keep their own
licenses. Their notices, complete license texts, exact source archives, patch
and build recipe accompany the program in `shared/image/NOTICE.md`,
`shared/image/licenses/` and `shared/image/sources/`. The installer source archive
also accompanies each platform's release artifacts.

Firmware is downloaded separately from the latest published AWTRIX NG GitHub
release. Its license notices and corresponding source location are part of the
verified firmware package. The installer does not embed firmware, manufacturer
partition images, backups, saved Wi-Fi passwords or signing keys.
