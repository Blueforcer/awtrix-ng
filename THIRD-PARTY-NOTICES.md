# Third-party notices

AWTRIX NG itself is licensed under the
[PolyForm Noncommercial License 1.0.0](LICENSE.md). It is built
from, and links against, third-party software under its own licenses. This file
lists everything and reproduces the notices those licenses require.

The verbatim license texts are in [`LICENSES/`](LICENSES/), one file per
component, linked from every row below. Several of these licenses require the
full text to travel with binary distributions, which is why the directory - not
just this list - is what ships.

Nothing here is bundled in a way that changes the terms of the components: each
retains its own license, and this document is the attribution the permissive
ones ask for.

---

## Compiled into the firmware image

These are linked into `firmware-awtrix-ng*.bin`, so their notices travel with
every distributed binary.

| Component | Version | License | Text | Copyright |
|---|---|---|---|---|
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) (via `espressif32@6.12.0`) | 2.x | **LGPL-2.1-or-later** | [LGPL-2.1.txt](LICENSES/LGPL-2.1.txt) | Espressif Systems and the Arduino community |
| [ESP-IDF](https://github.com/espressif/esp-idf) (bundled with the Arduino core) | 4.4.x | Apache-2.0 | [Apache-2.0.txt](LICENSES/Apache-2.0.txt) | Espressif Systems |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (bundled with ESP-IDF, precompiled) | 2.28.x | Apache-2.0 | [Apache-2.0.txt](LICENSES/Apache-2.0.txt) | The Mbed TLS Contributors |
| [newlib](https://sourceware.org/newlib/) C library (Espressif fork, from the toolchain) | — | BSD-style, many holders | [newlib.txt](LICENSES/newlib.txt) | Red Hat, Berkeley, Espressif and others |
| [FastLED](https://github.com/FastLED/FastLED) | ^3.9.6 | MIT | [MIT-FastLED.txt](LICENSES/MIT-FastLED.txt) | Daniel Garcia, Mark Kriegsman and contributors |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | 2.8 with local packet-validation fixes; see `lib/PubSubClient` | MIT | [MIT-PubSubClient.txt](LICENSES/MIT-PubSubClient.txt) | Nicholas O'Leary |
| [densaugeo/base64](https://github.com/Densaugeo/base64_arduino) | ^1.4.0 | MIT | [MIT-densaugeo-base64.txt](LICENSES/MIT-densaugeo-base64.txt) | Densaugeo |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | ^1.17 | MIT | [MIT-Adafruit-BusIO.txt](LICENSES/MIT-Adafruit-BusIO.txt) | Adafruit Industries |
| [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor) | ^1.1 | Apache-2.0 | [Apache-2.0.txt](LICENSES/Apache-2.0.txt) | Adafruit Industries |
| [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library) | ^2.2.2 | BSD-3-Clause | [BSD-3-Clause-Adafruit-BME280.txt](LICENSES/BSD-3-Clause-Adafruit-BME280.txt) | Limor Fried & Kevin Townsend for Adafruit Industries |
| [Adafruit BMP280 Library](https://github.com/adafruit/Adafruit_BMP280_Library) | ^2.6.8 | BSD | [BSD-Adafruit-BMP280.txt](LICENSES/BSD-Adafruit-BMP280.txt) | K. Townsend for Adafruit Industries |
| [Adafruit HTU21DF Library](https://github.com/adafruit/Adafruit_HTU21DF_Library) | ^1.0.5 | BSD | [BSD-Adafruit-HTU21DF.txt](LICENSES/BSD-Adafruit-HTU21DF.txt) | Adafruit Industries |
| [Adafruit SHT31 Library](https://github.com/adafruit/Adafruit_SHT31) | ^2.2.0 | BSD-3-Clause | [BSD-3-Clause-Adafruit-SHT31.txt](LICENSES/BSD-3-Clause-Adafruit-SHT31.txt) | Adafruit Industries |

**FastLED carries a sub-component under a different license.**
`src/third_party/cq_kernel/` inside FastLED is Apache-2.0 rather than MIT. It is
covered by [Apache-2.0.txt](LICENSES/Apache-2.0.txt).

**Adafruit BMP280 and HTU21DF ship no license file.** Their terms are a header
comment ending "BSD license, all text above must be included in any
redistribution". The two files in `LICENSES/` reproduce those headers verbatim,
which is what that sentence asks for.

**newlib is not one license.** `LICENSES/newlib.txt` is the toolchain's own
`COPYING.NEWLIB`: around forty separate BSD-style notices from different
holders, all of which require attribution in binary form. It is copied from the
`toolchain-xtensa-esp32` package this project builds with, not from an upstream
release, so it matches what is actually linked - including the nano variant that
`scripts/newlib_nano.py` selects.

### Vendored into this repository

| Component | Location | License | Text | Copyright |
|---|---|---|---|---|
| [Berry](https://github.com/berry-lang/berry) scripting language | `lib/berry/` | MIT | [MIT-Berry.txt](LICENSES/MIT-Berry.txt) | (c) 2018-2020 Guan Wenliang |
| [TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) | `lib/TJpg_Decoder/` | BSD-2-Clause (FreeBSD) | [TJpg_Decoder.txt](LICENSES/TJpg_Decoder.txt) | Bodmer |
| [TJpgDec — Tiny JPEG Decompressor](http://elm-chan.org/fsw/tjpgd/) | `lib/TJpg_Decoder/src/tjpgd.*`, shared by both platforms | permissive, notice must be retained | [TJpg_Decoder.txt](LICENSES/TJpg_Decoder.txt) | (C) 2021 ChaN |
| [ESP-IDF dynamic mbedTLS buffers](https://github.com/espressif/esp-idf) | `src/system/tlsdyn/` | Apache-2.0 | [Apache-2.0.txt](LICENSES/Apache-2.0.txt) | 2020-2022 Espressif Systems (Shanghai) CO LTD |
| [MatrixChunky6 / MatrixChunky8](https://github.com/trip5/Matrix-Fonts) panel fonts | `assets/fonts/`, compiled into `src/media/AwtrixFont.h` | OFL-1.1; retained upstream notices | [MIT-Matrix-Fonts.txt](LICENSES/MIT-Matrix-Fonts.txt) | (c) 2026 Trip5 |
| [Matrix-Fonts original catalog](https://github.com/trip5/Matrix-Fonts) ten original fonts | `assets/fonts/matrix-fonts/`, compiled into `src/media/MatrixFonts.h` and `src/media/MatrixFontsCompact.h` | OFL-1.1; retained upstream notices | [MIT-Matrix-Fonts.txt](LICENSES/MIT-Matrix-Fonts.txt) | (c) 2026 Trip5; [pinned font provenance and licensing](docs/developers/fonts.md) |

**Berry is modified.** `lib/berry/berry_conf.h` carries this project's build
configuration rather than upstream's, and the vendored tree tracks upstream
`master` rather than a release tag. MIT does not require marking changes; it is
noted here so nobody diffs it against a release and assumes a bug is upstream's.

**TJpgDec notice**, reproduced as its license requires:

> TJpgDec - Tiny JPEG Decompressor R0.03 (C)ChaN, 2021
>
> The TJpgDec is a generic JPEG decompressor module for tiny embedded systems.
> This is a free software that opened for education, research and commercial
> developments under license policy of following terms.
>
> Copyright (C) 2021, ChaN, all right reserved.
>
> * The TJpgDec module is a free software and there is NO WARRANTY.
> * No restriction on use. You can use, modify and redistribute it for
>   personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
> * Redistributions of source code must retain the above copyright notice.

That is the notice carried by both copies of the code in this repository, at
R0.03. `LICENSES/TJpg_Decoder.txt` is the wrapper library's own license file,
which quotes the same terms from the older R0.01c; the terms are identical, only
the revision line differs.

## The LGPL component — your rights

One of the components above, the Arduino core for ESP32, is licensed under the
**GNU Lesser General Public License, version 2.1 or later**, and is statically
linked into the distributed firmware image. That license grants you rights this
project does not restrict:

- **You may modify that library and relink the firmware against your modified
  version.** Everything needed to do so is public: the complete source of
  AWTRIX NG is in this repository, the core is used unmodified at the version
  pinned in `platformio.ini`, and `pio run -e awtrix` reproduces the image.
- **You may reverse engineer the firmware** for the purpose of debugging such
  modifications.
- **The full license text ships with every release**, in
  [`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt), as section 6 of that license
  requires.

The noncommercial term of AWTRIX NG's own license does not, and cannot, cut
back the rights the LGPL grants over the LGPL-licensed portions.

## Linux application and host tests

These components are used by the host builds: the `awtrix-linux` application and
the native tests. They are not present in the ESP32 firmware images.

| Component | Location | License | Text | Copyright |
|---|---|---|---|---|
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | `src/platform/linux/host/vendor/httplib.h` | MIT | [MIT-cpp-httplib.txt](LICENSES/MIT-cpp-httplib.txt) | (c) 2025 Yuji Hirose |
| [Unity](https://github.com/ThrowTheSwitch/Unity) test framework (via PlatformIO) | — | MIT | [MIT-Unity.txt](LICENSES/MIT-Unity.txt) | (c) 2007-2021 Mike Karlesky, Mark VanderVoord, Greg Williams |
| [CMUdict](https://github.com/cmusphinx/cmudict) pronunciations selected with the [SCOWL](https://github.com/en-wl/wordlist) spelling list | `tools/speech/`, compiled into `src/platform/tc002/speech/SpeechLexiconData.h` | BSD-style; SCOWL notices retained | [CMUdict-SCOWL.txt](LICENSES/CMUdict-SCOWL.txt) | (C) 1993-2015 Carnegie Mellon University; selection (C) 2000-2026 Kevin Atkinson |

The Linux CMake build dynamically links the host's **OpenSSL** libraries for
HTTPS and MQTT TLS. OpenSSL is not vendored
here. OpenSSL 3.x is licensed under
Apache-2.0; the [upstream license page](https://openssl-library.org/source/license/)
describes the terms for each release family. The license text is available in
[Apache-2.0.txt](LICENSES/Apache-2.0.txt); the installed system package carries
the notices for its exact version. A future binary or OS-image distribution
must include notices for the libraries it actually redistributes.

**cpp-httplib is modified.** The vendored header limits line lengths, aggregate
header bytes and header counts while parsing requests, responses and trailers.
It preserves literal header values, separates trailers from initial headers,
parses multipart dispositions without recursive regular expressions, rejects
signed chunk sizes and keeps exception details out of default responses. Its
client accepts response status and header lines ending in a bare LF and
Shoutcast's `ICY 200 OK`.
The local patches are recorded in `tools/system/components.json`; the original
MIT notice is retained.

Berry, PubSubClient, base64, the panel fonts and TJpgDec are also used by the
shared host application; their notices above apply there too. No TC002 vendor
kernel, bootloader, MCU firmware or radio firmware is included in this repository.

**AIC Bluetooth parameter data.** `awtrix-tc002d` uses the AIC Bluetooth AON
parameter format and defaults (Apache-2.0, Copyright (C) 2019-2027 AIC
Corporation), with two TC002-specific values. The [source comparison and
adaptation](docs/developers/tc002/bluetooth.md) distinguish this configuration
from controller firmware. The bundle includes the [attribution](tools/tc002/bluetooth/NOTICE)
and full [Apache-2.0 license](LICENSES/Apache-2.0.txt).

**The TC002 release bundle is a binary distribution.**
`tools/tc002/install/build_bundle.sh` links its programs statically:
`awtrix-linux` contains OpenSSL 3.5.8 (Apache-2.0, built by `tools/tc002/openssl`) and
libjpeg-turbo 3.2.0 (IJG license, BSD-3-Clause and zlib, built by `tools/tc002/jpeg`), and
every program contains the musl C library (MIT). The bundle also ships
BusyBox as `bin/udhcpc` (GPL-2.0), wpa_supplicant (BSD-3-Clause) with libnl
(LGPL-2.1), the rebuilt aic8800 Wi-Fi and awtrix_pcm kernel modules (GPL-2.0) and
the Mozilla CA certificates (MPL-2.0). The installer adds the kernel's loop block
driver to res as `loop.ko` (GPL-2.0), built from the same pinned kernel source by
`tools/tc002/kernel/build-loop.sh`. Every release carries `share/licenses.txt.gz` with the
licence texts of all of these, of the components above that `awtrix-linux`
compiles in, and of AWTRIX NG itself.

## Local TC002 MCU patching

| Component | Use | License | Text | Copyright |
|---|---|---|---|---|
| [bluetrum-tools](https://github.com/kagaimiq/bluetrum-tools) | Protocol format reference; upstream tool sources are not bundled | MIT | [MIT-bluetrum-tools.txt](LICENSES/MIT-bluetrum-tools.txt) | (c) 2024 Andrey Grigoryev |

The release includes only the project-owned MCU extension, its schema-2 recipe
and MIT license. Original and combined FOT/POT images are excluded from public
packages. When an update is needed, the local preparation helper obtains the
hash-pinned original directly from the manufacturer's HTTPS endpoint, retains it
on the user's device and constructs the combined image there. This separation
does not establish permission for modification or reverse engineering; those
questions remain distinct from redistribution of the original firmware.

The preparation mode in `awtrix-linux` links **liblzma 5.8.4 (0BSD)**, OpenSSL 3.5.8 (Apache-2.0),
cpp-httplib (MIT), musl (MIT) and the applicable GCC runtime libraries. Its liblzma
license ships with the bundle; the exact XZ source archive is in the source companion.
The MCU format investigation used [bluetrum-tools](https://github.com/kagaimiq/bluetrum-tools)
as a reference (MIT, Copyright (c) 2024 Andrey Grigoryev); the notice is retained in
[MIT-bluetrum-tools.txt](LICENSES/MIT-bluetrum-tools.txt).

**AIC source licensing remains a separate release review item.** The shipped
driver's available GPL declarations and per-file notices are recorded in
[the AIC source notice](tools/tc002/install/source_package_AIC_NOTICE.txt).
Local MCU patching does not resolve that review or grant rights to radio firmware.

## Published with the documentation site

Not in any firmware image, but redistributed: the built site at
<https://blueforcer.github.io/awtrix-ng/> carries these as static assets.

- **[MkDocs](https://www.mkdocs.org/)** — BSD-2-Clause.
- **[Material for MkDocs](https://squidfunk.github.io/mkdocs-material/)** — MIT.
  Its bundled icon sets carry their own terms: Material Design Icons
  (Apache-2.0), Font Awesome Free (icons CC BY 4.0, code MIT), Octicons (MIT).
- **[mkdocs-swagger-ui-tag](https://github.com/blueswen/mkdocs-swagger-ui-tag)**
  — MIT, embedding **[Swagger UI](https://github.com/swagger-api/swagger-ui)**
  (Apache-2.0) on the API reference pages.

## Reference data used to generate checked-in sources

Used at development time by the generators in `tools/`. No third-party code is
carried into the firmware by any of them.

- **ISO/IEC 11172-3:1993 Annex B** — the normative Layer III Huffman and window
  tables, from which `tools/gen_mp3_spec_tables.py` emits
  `src/core/audio/Mp3HuffmanTables.h` and `Mp3Window.h`. The MP3 decoder in
  `src/core/audio/` is an independent implementation written from the
  specification; no decoder's source was copied.
- **[minimp3](https://github.com/lieff/minimp3)** — **CC0 1.0** (public domain
  dedication, no attribution required). Was the original route to the same
  Huffman table *values* before a copy of the standard's annex was obtained;
  `tools/gen_mp3_huffman.py` remains as a cross-check. None of minimp3's
  decoding logic is used.
- **[IANA time zone database](https://www.iana.org/time-zones)** — public
  domain. `scripts/zone1970.tab` is a checked-in copy, read by
  `scripts/tz_data.py` to generate the zone list.
- **ffmpeg** — run offline by `tools/gen_mp3_vectors.py` to produce reference
  PCM for `test/test_mp3pcm/vectors.h`. Not linked, not redistributed.
- **[Pillow](https://python-pillow.org/)** (HPND) — run offline by
  `tools/gen_gif_fixtures.py` to build GIF test fixtures.

## Not bundled — loaded from elsewhere

- **[awtrix-piskel](https://github.com/Blueforcer/awtrix-piskel)** — Apache-2.0,
  a fork of [Piskel](https://github.com/piskelapp/piskel) (Apache-2.0). The Icon
  Editor tab embeds it from a separate deployment in an `<iframe>`; no Piskel
  code is in this repository or in the firmware. See `docs/developers/icon-editor.md`.
- **[LaMetric icon gallery](https://developer.lametric.com/icons)** — the web
  UI can fetch an icon from it on request, straight from your browser. Nothing
  is bundled or redistributed, and the icons remain their authors' work.

## Build tooling

[PlatformIO](https://platformio.org/) (Apache-2.0) and the Espressif
`xtensa-esp32-elf` GCC toolchain (GPL, with the GCC Runtime Library Exception —
which is why the compiler's license does not reach the compiled output, and why
the parts of `libgcc` and `libstdc++` that end up in the firmware carry no GPL
obligation).

---

Something missing or wrong here? Please open an issue — an incomplete
attribution list is a bug like any other.
