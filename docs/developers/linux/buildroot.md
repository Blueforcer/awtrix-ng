---
only: [tc002]
---

# Buildroot configuration

AWTRIX NG keeps a Buildroot **external tree** in
[`firmware/buildroot/`](https://github.com/Blueforcer/awtrix-ng/tree/main/firmware/buildroot).
[Buildroot](https://buildroot.org/) builds a cross toolchain and a Linux userspace from source;
an external tree adds project-specific packages and configuration without changing Buildroot
itself. This tree cross-compiles `awtrix-linux` and its userspace dependencies for 32-bit ARM.
Its cross toolchain is the one every program of a TC002 release is built with.

Do not run it by hand; the [ARM system build](system-build.md) runner prepares locked inputs and
drives Buildroot.

## What the output is

The output is a root filesystem **tar archive**, the cross toolchain in `output/host/`, and on
request a relocatable SDK. There is no kernel image, device tree, bootloader, partition layout,
installer or flash command, and the archive has no init system or startup service. It cannot
boot a TC002 and is not TC002 firmware. The TC002 release is assembled separately (see
[AWTRIX NG on the TC002](../tc002/index.md)).

## Contents

| Path | Role |
|---|---|
| `external.desc`, `external.mk`, `Config.in` | Registers the tree as `AWTRIX` and includes its packages |
| `configs/awtrix_armv7_defconfig` | The ARM profile |
| `package/awtrix-ng/` | Builds the shared Linux application from the runner's Git snapshot with CMake (host tests off) and installs the application, web UI and license notices |
| `package/awtrix-base64/` | densaugeo/base64 1.4.0, pinned by commit, installed to staging for the application build only |
| `patches/linux-headers/6.18.52/` | Archive and license hashes for the custom kernel header pin |
| `scripts/post-build.sh` | Removes libstdc++ debugger helper scripts from the target |

## The configuration

The target is little-endian ARMv7 Cortex-A7 with NEON/VFPv4, hard-float ABI, musl and C++, the
CPU of the TC002. Binaries built against this musl userspace cannot simply be mixed with glibc
libraries or libraries built for another floating-point ABI.

OpenSSL and CA certificates support the application's outgoing TLS connections. Login, getty,
SSH servers, BusyBox, an interactive shell and debugger packages are disabled. Kernel, U-Boot,
Barebox and ARM Trusted Firmware are not built.

The application snapshot includes the project's own PubSubClient 2.8 source under
`lib/PubSubClient`, with the shared inbound-packet bounds fixes. ESP32 and Linux use the same
source; Buildroot does not select a second MQTT library. Its MIT notice ships with the
application notices.

Buildroot 2025.02's kernel header menu stops at **6.12.x or later**. Selecting that entry with the
custom version **6.18.52** is intentional and follows its Kconfig help. These are userspace build
headers only; `BR2_LINUX_KERNEL` stays disabled. `BR2_DOWNLOAD_FORCE_CHECK_HASHES` requires hashes
for custom downloads too. Linux 6.18 changed the Free Software Foundation address in its GPL
text, so its license hash differs from Buildroot's default header release; `external.mk` selects
the complete version-specific hash file for this pin instead of mixing two conflicting hashes.

## Pinned versions

The pins are recorded in
[`tools/system/sources.lock.json`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/system/sources.lock.json):

| Component | Pin | Notes |
|---|---|---|
| Buildroot | 2025.02.18 | Long-term support series, supported until March 2028 |
| Linux userspace headers | 6.18.52 | Long-term kernel series |
| C/C++ compiler | GCC 14.4.0 | Selected by `BR2_GCC_VERSION_14_X` |
| C library | musl 1.2.6 | Version and patches from the pinned Buildroot |
| TLS library | OpenSSL 3.5.8 | Version and patches from the pinned Buildroot |

| Archive | SHA-256 |
| --- | --- |
| `buildroot-2025.02.18.tar.xz` | `e38ad1df6ea0479fff6419a87a64535d02131674d463be154a763ef725b55321` |
| `linux-6.18.52.tar.xz` | `2b69564f7d4fea0c859b1959ba33709ee6e9139bd100e30a853b57159a8221b8` |

The Buildroot checksum was computed from the official HTTPS archive; its
[detached signature](https://buildroot.org/downloads/buildroot-2025.02.18.tar.xz.sign) is a
separate provenance check and is not implied by the checksum. The Linux checksum comes from the
official [signed checksum manifest](https://cdn.kernel.org/pub/linux/kernel/v6.x/sha256sums.asc).
The build verifies the pinned hashes; it does not set up a trusted PGP keyring for either
project.

Pins are reviewed inputs, not a promise that a release has no known vulnerabilities. Updating a
pin means refreshing the source lock, the hashes and the validation.

## Build

From the repository root on Linux or WSL, with a new, dedicated directory on the Linux
filesystem:

```sh
python3 tools/system/build.py doctor
python3 tools/system/build.py prepare --work "$HOME/awtrix-armv7-build" --revision HEAD
python3 tools/system/build.py configure --work "$HOME/awtrix-armv7-build"
python3 tools/system/build.py build --work "$HOME/awtrix-armv7-build" --jobs 4
python3 tools/system/build.py legal-info --work "$HOME/awtrix-armv7-build"
```

The runner owns the build inputs. Do not substitute an unrecorded working directory and claim
the same source identity. The first build downloads the selected package sources.

At the Buildroot level, `make source` fills the download cache, `make` builds the userspace
archive, `make sdk` packages the toolchain and `make legal-info` gathers license metadata and
redistributable sources. After extracting the SDK to a new location, run its `relocate-sdk.sh`.
The [Buildroot manual](https://buildroot.org/downloads/manual/manual.html) describes external
trees, SDK relocation, download caching and legal-info output.

### Reproducibility

`BR2_REPRODUCIBLE` is on, but Buildroot calls it experimental and only promises identical output
for the same absolute build and output paths. A real check needs two clean environments with the
same pinned host tools, identical paths, source snapshot, configuration and `SOURCE_DATE_EPOCH`,
with network access off during the builds. The [reproduction runner](system-build.md#reproducible-builds)
does exactly this.

### What a build does not show

An ARM ELF check or an emulated test validates the architecture and application behaviour. It
does not show anything about TC002 boot, LEDs, audio, sensors, network, updates, recovery or
power loss.

## License evidence

Keep Buildroot's own source, this external tree, the exact application snapshot, the generated
configuration, patches, source hashes and the `legal-info` output with each build record. Read
`legal-info/README`: Buildroot reports missing material there, and its manifest is an aid, not a
complete license audit.

The build includes GPL-licensed build tools, Linux userspace headers with the Linux syscall
exception, MIT-licensed musl, GCC runtime libraries with their runtime exception, and
Apache-2.0 OpenSSL. Package-specific license files and notices are authoritative. No proprietary
TC002 firmware or vendor binary is included.

For components bundled inside the application itself,
[`tools/system/components.json`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/system/components.json)
is the per-component decision record, checked by `tools/system/check_components.py`. The
qualification tool builds a CycloneDX inventory and a hash-bound release report from Buildroot's
`show-info` and `legal-info`. Both are described in [ARM system build](system-build.md).

## Related

- [ARM system build](system-build.md)
- [AWTRIX on Linux](index.md)
- [AWTRIX NG on the TC002](../tc002/index.md)
