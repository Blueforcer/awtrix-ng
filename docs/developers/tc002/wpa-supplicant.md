---
only: [tc002]
---

# TC002 wpa_supplicant

A TC002 release ships a static wpa_supplicant for the clock's armv7 hard-float userspace, built
with the musl toolchain `TC002_MUSL_PREFIX` (see
[`tools/tc002/toolchain/env.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/toolchain/env.sh)).
`awtrix-tc002d` runs it with `-Dnl80211 -iwlan0 -c <config>` in place of the stock
wpa_supplicant 2.6, which is linked against OpenSSL 1.1.0i. The recipe lives in
[`tools/tc002/wpa_supplicant/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/wpa_supplicant)
and also builds a matching `wpa_cli` for tests and for repair over USB ADB.

## How it works

The daemon sends the supplicant's output to `/dev/null` and reads everything it needs from
control-interface events. The shipped binary therefore has no debug output, which keeps it smaller.

Supported networks are open and WPA/WPA2-Personal (PSK). Networks in WPA2/WPA3 transition mode
connect with WPA2-PSK. There is no SAE, so a WPA3-only network does not connect: the built-in
crypto has no elliptic curves, and SAE would need the static OpenSSL. AP mode is built for the
clock's open setup access point. EAP, WPS, P2P, mesh, D-Bus and WEP are not built. A WEP network block is loaded but disabled. Config writing
stays built in, because the daemon's configuration contains `update_config=0`, which a build
without it rejects.

## Build

```sh
tools/tc002/wpa_supplicant/build.sh WORKDIR [OUTDIR]   # OUTDIR defaults to WORKDIR/out
```

| Variable | Effect |
|---|---|
| `WPA_VARIANT=host` | Builds the same configuration as a native binary, for example for a `mac80211_hwsim` test on a kernel that has cfg80211. |
| `WPA_STDOUT_DEBUG=1` | Keeps the `-d` debug output. Pass the same variable to `verify.sh`. |

Only `libnl-3` and `libnl-genl-3` are built. The build is reproducible: two builds in different
directories produce the same bytes.

To see why nl80211 setup or association fails on a clock, run the `WPA_STDOUT_DEBUG=1` build with
`-d` over USB ADB.

## Configuration

`wpa_supplicant.config` is the complete build configuration.

| Option | Effect |
|---|---|
| `CONFIG_DRIVER_NL80211`, `CONFIG_LIBNL32` | nl80211 is the only driver, linked statically against libnl |
| `CONFIG_AP` | access point mode, for the setup access point (open, no encryption) |
| `CONFIG_CTRL_IFACE=unix` | control interface on UNIX sockets only; nothing listens on the network |
| `CONFIG_TLS=internal`, `CONFIG_CRYPTO=internal`, `CONFIG_INTERNAL_LIBTOMMATH` | built-in crypto, no OpenSSL |
| `CONFIG_GETRANDOM` | seeds the random pool from `getrandom()` |
| `CONFIG_NO_CONFIG_BLOBS`, `CONFIG_NO_RRM`, `CONFIG_NO_WMM_AC`, `CONFIG_NO_ROBUST_AV` | drops certificate blobs, 802.11k, WMM admission control and SCS/MSCS |
| `CONFIG_NO_STDOUT_DEBUG` | drops the `-d` debug output; control-interface events are unaffected |

## Reference

| Component | Version | SHA-256 | Also checked against |
|---|---|---|---|
| [wpa_supplicant](https://w1.fi/releases/) | 2.12 | `08e23937e16d0155e55cab2b51f51fbe10d80a1aa91c4e15442645059b737ef6` | Buildroot 2025.02.18 `package/wpa_supplicant`; upstream signature by key `EC4A A0A9 91A5 F246 4582 D52D 2B6E F432 EFC8 95FA` |
| [libnl](https://github.com/thom311/libnl/releases) | 3.11.0 | `2a56e1edefa3e68a7c00879496736fdbf62fc94ed3232c0baba127ecfa76874d` | Buildroot 2025.02.18 `package/libnl`; upstream `.sha256sum` |

| Output | Size | SHA-256 |
|---|---|---|
| `wpa_supplicant` | 723224 bytes | `e62a42aa6d7f71d1d02295dfef90af3c02aa1bba3cf464e45c1338d5526c5d56` |
| `wpa_cli` | 112688 bytes | `f1ddda88c50c0111ab2477fa111ef590e16420f29bb9cf13778ffb2c986b829e` |

## Licences

wpa_supplicant is distributed under the BSD licence (3-clause). libnl is LGPL-2.1 and is linked
statically. `build.sh` copies the licence texts (`wpa_supplicant.COPYING`, the `README` that holds
the BSD terms, and `libnl.COPYING`) to `OUTDIR/licenses/`; the bundle ships them in
`share/licenses.txt.gz`. Anyone who receives the binaries must also be able to get the pinned
sources and this recipe, so that they can relink against a modified libnl (LGPL-2.1 section 6).

## Related

- [Wi-Fi driver](wifi-driver.md)
- [Install tools](install-tools.md)
- [TC002 tools](tools.md)
