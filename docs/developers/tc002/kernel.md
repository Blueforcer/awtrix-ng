---
only: [tc002]
---

# TC002 kernel modules

AWTRIX NG loads its own kernel modules into the stock TC002 kernel: the rebuilt
[Wi-Fi driver](wifi-driver.md), the [speaker driver](speaker-driver.md) and `loop.ko`. The stock
kernel is Linux 4.9.84. Its source and configuration are not published, so
[`tools/tc002/kernel/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/kernel)
builds modules against the SigmaStar Pioneer3 SDK kernel with a configuration that matches the
clock's module ABI.

## Source and toolchain

| Item | Value |
|---|---|
| Kernel tree | `linux-chenxing/linux-chenxing-vendor-slop`, branch `ikayaki_dlm00v014`, commit `e53dccbcd926a883a2859695a6b8839e12daf321` (SigmaStar Pioneer3 SDK kernel 4.9.84) |
| Base config | `arch/arm/configs/pioneer3_ssc021a_s01a_defconfig`, the board in the TC002 device tree (`PIONEER3 SSC021A-S01A-S`) |
| Derivation | `tc002_stock.fragment` is applied on top of the base config, and `make olddefconfig` resolves it to `tc002_stock.config` |
| Compiler | ARM GNU Toolchain 9.2-2019.12 `arm-none-linux-gnueabihf`, fetched and checked by `tools/tc002/toolchain/fetch-arm-gnu-9.2.sh`. The compiler version is not part of the 4.9 vermagic |
| Host | WSL Ubuntu. `HOSTCFLAGS=-fcommon` is needed for the 4.9 host tools with GCC 10 or newer |

## Usage

```sh
tools/tc002/kernel/fetch-source.sh             # shallow fetch of the pinned commit
tools/tc002/kernel/prepare-tree.sh             # export, configure, vmlinux + modules_prepare
tools/tc002/kernel/build-module.sh tools/tc002/kernel/smoke/hello
tools/tc002/kernel/check-ko.sh <module.ko>...  # vermagic + struct module layout check
tools/tc002/kernel/build_aic8800.sh            # aic8800 Wi-Fi driver, see wifi-driver.md
tools/tc002/kernel/build-awtrix-pcm.sh         # speaker driver, see speaker-driver.md
tools/tc002/kernel/build-loop.sh [OUT]         # loop.ko from the tree's drivers/block/loop.c
```

- The work directory is `$TC002_KMOD_WORK` (default `$TC002_CACHE/kernel`), and the compiler is
  `$TC002_GLIBC_PREFIX` (see
  [`tools/tc002/toolchain/env.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/toolchain/env.sh)).
- `prepare-tree.sh` stops if the resolved config drifts from `tc002_stock.config`. After editing
  the fragment, run it with `--update-config` to accept the new config.
- `vmlinux` is built so that `Module.symvers` lists the kernel exports for modpost.
- `smoke/hello` is a minimal module that proves a prepared tree produces loadable modules.

### loop.ko

The stock kernel has no loop support. `build-loop.sh` builds the tree's own `loop.c` as a module
with one loop device created at load, and checks its imports against `Module.symvers` (and, with
`TC002_STOCK_MODULES` and `TC002_STOCK_IMAGE`, against the stock kernel's export table). The
loader loads it from `res` to mount the release slot (see
[Install tools](install-tools.md#the-release-slot)).

## Module ABI

A module loads into the clock's kernel when its vermagic is
`4.9.84 SMP preempt mod_unload ARMv7 thumb2 p2v8 ` and its `.gnu.linkonce.this_module` uses the
`0x1c0`-byte layout with `init` at `0xe8` and `exit` at `0x180`. `tc002_stock.config` produces
exactly that; the unmodified SDK defconfig produces a `0x200`-byte layout, whose modules the kernel
lists as `[permanent]` because it finds no `exit`. `check-ko.sh` checks both properties. The build
recipes also take `TC002_STOCK_MODULES` and `TC002_STOCK_IMAGE` (private files from a clock's own
backup, never committed) to check a module's imports against the clock's export tables.

## Loading a test module on a clock

The steps below print `dmesg`. Run them only after a boot in which only the release's Wi-Fi
driver ran: a driver without patch `0003-never-log-key-material.patch` can write Wi-Fi keys to the
kernel log (see [Wi-Fi driver](wifi-driver.md)).

1. Build `hello.ko` with `build-module.sh` and check that `check-ko.sh` prints `ok`.
2. `adb -s SERIAL push hello.ko /tmp/hello.ko`
3. Make an oops reboot the clock instead of hanging it (volatile until reboot):
   `adb -s SERIAL shell 'echo 5 > /proc/sys/kernel/panic; echo 1 > /proc/sys/kernel/panic_on_oops'`
4. `adb -s SERIAL shell 'insmod /tmp/hello.ko; echo rc=$?'` prints `rc=0`.
5. `adb -s SERIAL shell 'cat /proc/modules; dmesg'` shows `hello … Live` **without**
   `[permanent]`, and `hello: loaded` in dmesg (filter on the host; the clock's shell has no grep).
6. `adb -s SERIAL shell 'rmmod hello; echo rc=$?; cat /proc/modules; dmesg'` shows `rc=0`, no
   `hello` line, and `hello: unloaded`.
7. Restore the defaults and clean up:
   `adb -s SERIAL shell 'echo 0 > /proc/sys/kernel/panic; echo 0 > /proc/sys/kernel/panic_on_oops; rm /tmp/hello.ko'`

Stop if `insmod` returns non-zero, if dmesg shows `version magic`, `Unknown symbol` or an oops, or
if `hello` appears as `[permanent]`. A `[permanent]` entry means the exit offset is wrong: do not
attempt `rmmod`. After step 3, a kernel oops reboots the clock after 5 s.

## Related

- [Wi-Fi driver](wifi-driver.md)
- [Speaker driver](speaker-driver.md)
- [Install tools](install-tools.md)
- [TC002 tools](tools.md)
