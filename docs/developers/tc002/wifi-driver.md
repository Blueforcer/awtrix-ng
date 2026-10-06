---
only: [tc002]
---

# TC002 Wi-Fi driver

The TC002's Wi-Fi chip is an AICSemi AIC8800 on SDIO. A release ships its own build of the two
driver modules, `aic8800_bsp.ko` and `aic8800_fdrv.ko`, rebuilt from the AICSemi GPL source with
four patches, one of which keeps Wi-Fi keys out of the kernel log. The recipe is
[`tools/tc002/kernel/build_aic8800.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/kernel/build_aic8800.sh);
the patches live in
[`src/platform/tc002/kmod/aic8800/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/kmod/aic8800).

## How it works

The daemon loads the pair from its release's `lib/modules` with `aicwf_dbg_level=0` when it finds
no `wlan0`. When it starts, it also clears the kernel log and sets `dmesg_restrict=1`. On a cold
boot, Wi-Fi reaches an address and the time in 3.3 to 3.7 s.

## Source

| Item | Value |
|---|---|
| Release | AICSemi `aic8800d_linux_sdk_V4.0_2024_1024_9a3636f0`, SDIO driver |
| Repository | `kendryte/k230_linux_sdk`, commit `dd73e713829ab31029e371bc3fb67b367b8ae2eb` |
| Path | `buildroot-overlay/package/aic8800_sdio/src`, git tree `3ff4a299165d4bce65bb91e6f8924a7ca3dbe9fb` (checked by the script) |

## Build

```sh
tools/tc002/kernel/prepare-tree.sh        # once, see kernel.md
tools/tc002/kernel/build_aic8800.sh [--work <dir>] [--stock-equivalent] [out-dir]
```

The script builds against the tree that `prepare-tree.sh` prepares (see
[Kernel modules](kernel.md)). The work directory is `--work` (default `$TC002_CACHE/aic8800`),
and the modules are written to `<out-dir>` (default `<work>/out`). The build is reproducible: two
output directories give byte-identical modules. `--stock-equivalent` leaves out the key-log fix.

### Build settings

The script passes these make variables; the others keep the defaults of the driver Makefiles.

| Variable | Why |
|---|---|
| `CONFIG_PLATFORM_TC002=y` | Enables the power hooks from patch 0002. |
| `CONFIG_AIC_FW_PATH=/lib/firmware/aic8800DC` | The chip firmware directory on the clock. |
| `CONFIG_USE_P2P0=y` | The driver creates `p2p0` next to `wlan0`. |
| `CONFIG_PREALLOC_RX_SKB=y` | Preallocated receive buffers (`aicwf_prealloc_*` / `rxbuff_*`). |
| `KCFLAGS=-ffile-prefix-map=...` | `__FILE__` becomes `drivers/net/wireless/aic8800/...`, and no host path ends up in the modules. |

Three exports of the clock's kernel that the GPL kernel tree lacks come from a generated
`KBUILD_EXTRA_SYMBOLS` file: `sunxi_wlan_get_bus_index`, `sunxi_wlan_set_power` and
`sigmastar_mmc_rescan_card`. The modules are stripped with
`objcopy -x -G __this_module --strip-unneeded`. Unstripped copies stay in `<out-dir>/aic8800_*/`
for `addr2line`.

### Patches

| Patch | Change |
|---|---|
| `0001-register-tdls-mgmt.patch` | Registers `.tdls_mgmt` again, which the k230 import comments out. |
| `0002-tc002-power-and-defaults.patch` | Adds the TC002 power sequence. Power-on: `sunxi_wlan_set_power(1)`, `msleep(100)`, `sigmastar_mmc_rescan_card(slot, 1)`. Power-off: `set_power(0)`, `mdelay(100)`, `rescan(slot, 0)`. Also sets `aicwf_dbg_level` and `aicwf_dbg_level_bsp` to `LOGERROR` and uses `platform_device_unregister` on unload. |
| `0003-never-log-key-material.patch` | Removes `print_hex_dump_bytes("key: ", ...)` from `rwnx_send_key_add`. The AICSemi source dumps every pairwise and group key there, whatever `aicwf_dbg_level` is set to. |
| `0004-fdrv-fwlog-en-is-bool.patch` | Declares `fwlog_en` in the fdrv copy of `aic_bsp_export.h` as `bool`, as the bsp copy does. With `int`, fdrv copied three uninitialised stack bytes into `rwnx_hw->fwlog_en`. |

Patches 0001, 0002 and 0004 fit the source to the TC002; 0003 is the key-log fix.

## Trying a new build on a clock

These steps swap the pair on a running clock without building a release. They use USB ADB only
(`adb -s SERIAL`), because Wi-Fi goes down during the swap. The root ADB shell can read the kernel
log despite `dmesg_restrict`.

1. Push the modules: `adb push aic8800_bsp.ko aic8800_fdrv.ko /tmp/`.
2. Make an oops reboot the clock instead of hanging it (does not survive a reboot):
   `echo 5 > /proc/sys/kernel/panic; echo 1 > /proc/sys/kernel/panic_on_oops`.
3. Stop the daemon with `setprop ctl.stop zkswe`, the init service it runs under. After
   `awtrix-tc002d ctl stop` alone, init starts it again. Check that `getprop init.svc.zkswe`
   reports `stopped` and that `ps` shows no `awtrix-tc002d`, `wpa_supplicant` or `udhcpc`.
4. Load the new modules: `rmmod aic8800_fdrv; rmmod aic8800_bsp`, then
   `insmod /tmp/aic8800_bsp.ko; echo rc=$?` and `insmod /tmp/aic8800_fdrv.ko; echo rc=$?`. Both
   must print `rc=0`, and `wlan0` and `p2p0` must appear in `/sys/class/net`.
5. Check the modules:
   - `/sys/module/aic8800_*/srcversion` matches `modinfo -F srcversion` of the pushed files.
   - `/proc/modules` lists both with `(O)`.
   - `dmesg` shows the bring-up lines including `RELEASE_DATE:2024_1024_9a3636f0`, and no `Unknown symbol`, `version magic` or oops.
6. Start the daemon with `setprop ctl.start zkswe`. It finds `wlan0` and starts its supplicant.
7. Check the connection: `/tmp/awtrix-release/bin/awtrix-tc002d ctl wifi-status` must report
   `"link":"connected"`, and `ctl status` must show an IPv4 address.
8. Check that no key was dumped: save `adb shell dmesg` on the host and print only the count of
   `re.findall(r"key: [0-9a-f]{8}: ", text)`. The count must be 0. Never print the matching lines.

**Rollback:** `setprop ctl.stop zkswe; rmmod aic8800_fdrv; rmmod aic8800_bsp; setprop ctl.start zkswe`.
The daemon finds no `wlan0` and loads the pair from its release; after a reboot it does the same,
because nothing on flash changes. Afterwards restore `panic`/`panic_on_oops` to 0 and delete
`/tmp/aic8800_*.ko`.

Stop and roll back if any `insmod` or `rmmod` returns non-zero, if an oops appears, or if `wlan0`
is missing after 10 s. Powering the chip off during `rmmod` also cuts Bluetooth.

## Related

- [Kernel modules](kernel.md)
- [Bluetooth configuration](bluetooth.md)
- [wpa_supplicant](wpa-supplicant.md)
- [TC002 tools](tools.md)
