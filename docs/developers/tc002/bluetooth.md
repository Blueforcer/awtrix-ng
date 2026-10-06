---
only: [tc002]
---

# TC002 Bluetooth configuration

The TC002's Bluetooth controller is part of the AIC8800 chip on a UART. When `awtrix-tc002d`
brings Bluetooth up, it sends a 104-byte AIC always-on (AON) parameter block with the vendor
command `0xfc4d`. This page documents the format of that block, where its values come from and
the licence that applies. The block lives as `kConfig` in
[`src/platform/tc002/daemon/BtService.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/tc002/daemon/BtService.cpp);
the attribution file is
[`tools/tc002/bluetooth/NOTICE`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/bluetooth/NOTICE).

## How it works

`BtService` sends six HCI commands to set up the controller:

| Opcode | Command | Parameter |
|---|---|---|
| `0x0c03` | Reset | — |
| `0xfc70` | Write device address | the 6-byte address, read at runtime |
| `0xfc4d` | Write AON parameters | the 104-byte block below |
| `0xfc50` | Low-power level | `2` |
| `0xfc51` | Slave power control | `1` |
| `0xfc52` | CPU power-off enable | `1` |

The AIC SDK names all five vendor opcodes. The block holds addresses, limits, timings and
power/debug settings. It contains no controller instructions or firmware image, and no AIC
controller firmware is part of AWTRIX NG.

## Phone and gamepad coexistence on Linux 4.9

AWTRIX acts as a BLE peripheral for a phone and as a BLE central for a gamepad.
Linux 4.9 blocks these roles from connecting in either order: `check_pending_le_conn`
returns while `le_num_slave > 0` in [`hci_event.c`][linux49-event], and
`__hci_req_enable_advertising` returns while any LE link exists in
[`hci_request.c`][linux49-request]. Increasing an application connection limit does
not remove either kernel guard.

[`LinuxBleRadio.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/linux/ble/LinuxBleRadio.cpp)
uses a workaround only on kernels whose release starts with `4.9.`:

- With a phone already connected, send raw **LE Create Connection** before creating
  a pending L2CAP connection. The ATT listener receives the resulting regular kernel
  socket; pairing, encryption and bonds go through the kernel's SMP path. Match by
  connection handle as well as address because privacy can replace a resolvable
  private address with the peer's identity address. Cancel an attempt after 10 seconds
  with **LE Create Connection Cancel**, or disconnect the link if the controller has
  already reported it as established. No other connection starts until the controller
  confirms. Without a confirmation, a cancelled attempt counts as done after 2 seconds;
  an established link gets a second disconnect and counts as gone 2 seconds later. A
  command that cannot be written counts as confirmed at once.
- With a gamepad already connected, set the advertising parameters with the kernel's
  default interval of 1.28 s and enable advertising through HCI so that a phone can
  connect. The kernel's management path can otherwise report success without
  programming advertising.

HCI command failures include the opcode and status in the BLE log. This addresses
the kernel guards without changing controller connection limits or the AON block.

## Two-controller gamepad registry

`GamepadManager` owns one HID session per device slot on the `BleService` worker. Both sessions'
events go through one queue in the manager, so reports from the two gamepads are handled in
arrival order. A session reports through callbacks: a status change, a change of its remembered
device, and each input report. On a status change the manager assigns players and publishes the
slot's state, name and address under its lock; an input report only overwrites the controls of its
slot. Scripts read a player's state and controls as plain data, with no allocation; only the name
and the HTTP device list copy strings. HTTP pairing and removal run on the worker.

A single Ready slot is Player 1; a second one becomes Player 2, so two keep the order in which
their first valid input reports arrived. When one leaves Ready, the other becomes Player 1. A
player without a Ready slot reads the state of a slot that is not Ready, a paired one before an
empty one, so a script sees `waiting` while its gamepad is away. Players are never stored.

The shared radio queues outgoing connection attempts in arrival order and starts the oldest
eligible attempt, including requests from Berry and the gamepad sessions. A failed attempt rejoins
the queue with its retry delay; ready requests can proceed while it waits. Time in the queue does
not consume retry attempts, and removing a session also removes its queued attempt. The Linux 4.9
phone coexistence workaround above uses the same queue and cancellation handling.

`gamepad.json` holds `{"version":2,"devices":[...]}` with the slot id (1 or 2), Bluetooth
address, address type and name of each paired device. A file without `version` holds one device
in the form `{"addr":...,"random":...,"name":...}` and loads into slot 1; the next change writes
the versioned form. The manager writes the whole file atomically whenever a session pairs,
replaces or forgets its device. A failed write is logged; the next such change writes again.
Pairing skips the address stored in the other slot; removing a device forgets only its bond and
its owner in the shared hub. [Gamepad](../../guides/gamepad.md) describes the player-facing API.

## Block format

The AIC SDK file [`aic_hardware.c`][aic] defines `bt_drv_wr_aon_param` at lines 423-504 and
initialises it at line 517. Its sender copies 104 bytes of that structure into command `0xfc4d`
at lines 2422-2424. On the little-endian ARM layout, the 39 fixed-width fields take 102 bytes,
followed by two zero padding bytes.

| Offset | Type | Field | TC002 value |
| --- | --- | --- | --- |
| 0 | `uint32_t` | `em_save_start_addr` | `0x18d700` |
| 4 | `uint32_t` | `em_save_end_addr` | `0x18f700` |
| 8 | `int32_t` | `aon_min_power_off_duration` | `64` |
| 12 | `uint16_t` | `aon_max_nb_params` | `40` |
| 14 | `int16_t` | `aon_rf_config_time_cpus` | `400` |
| 16 | `int16_t` | `aon_rf_config_time_aon` | `400` |
| 18 | `uint16_t` | `aon_max_nb_active_acl` | `3` |
| 20 | `uint16_t` | `aon_ble_activity_max` | `2` |
| 22 | `uint16_t` | `aon_max_bt_rxdesc_field` | `3` |
| 24 | `uint16_t` | `aon_max_ble_rxdesc_field` | `2` |
| 26 | `uint16_t` | `aon_max_nb_regs` | `40` |
| 28 | `uint16_t` | `aon_max_ke_env_len` | `512` |
| 30 | `uint16_t` | `aon_max_nb_sc_arb_elt` | `20` |
| 32 | `uint16_t` | `aon_max_nb_sch_plan_elt` | `21` |
| 34 | `uint16_t` | `aon_max_nb_sch_alarm_elt` | `20` |
| 36 | `uint32_t` | `aon_min_ble_adv_intv` | `32` |
| 40 | `uint32_t` | `aon_min_ble_con_intv` | `8` |
| 44 | `int32_t` | `aon_extra_sleep_duration_cpus` | `-1` |
| 48 | `int32_t` | `aon_extra_sleep_duration_aon` | `0` |
| 52 | `int32_t` | `aon_min_power_off_duration_cpup` | `20000` |
| 56 | `uint32_t` | `aon_debug_level` | `0x113` |
| 60 | `uint32_t` | `aon_debug_level_aon` | `0x20067302` |
| 64 | `uint16_t` | `aon_bt_pwr_on_dly1` | `7` |
| 66 | `uint16_t` | `aon_bt_pwr_on_dly2` | `64` |
| 68 | `uint16_t` | `aon_bt_pwr_on_dly3` | `72` |
| 70 | `uint16_t` | `aon_bt_pwr_on_dly_aon` | `71` |
| 72 | `uint16_t` | `aon_sch_arb_cancel_in_advance_time` | `32` |
| 74 | `uint16_t` | `aon_sleep_algo_dur_cpus` | `512` |
| 76 | `uint16_t` | `aon_sleep_algo_dur_aon` | `420` |
| 78 | `uint16_t` | `aon_restore_time_ceil_cpus` | `100` |
| 80 | `uint16_t` | `aon_restore_time_ceil_aon` | `100` |
| 82 | `uint16_t` | `aon_min_sleep_duration_cpus` | `8` |
| 84 | `uint16_t` | `aon_min_sleep_duration_aon` | `24` |
| 86 | `int16_t` | `aon_restore_save_time_diff_cpus` | `40` |
| 88 | `int16_t` | `aon_restore_save_time_diff_aon` | `140` |
| 90 | `int16_t` | `aon_restore_save_time_diff_cpus_aon` | `0` |
| 92 | `int32_t` | `aon_min_clock_gate_duration` | `64` |
| 96 | `int32_t` | `aon_min_clock_gate_duration_cpup` | `20000` |
| 100 | `uint16_t` | `aon_max_nb_rf_mdm_regs` | `50` |
| 102 | 2 bytes | Tail padding | `00 00` |

The SHA-256 of the whole block is:

```text
86abdec6b049b001418f1a40ae8b46320a182bda6d4276c4b11e292f5219c0ef
```

## Where the values come from

Two public sources pin the TC002 values:

- **[AIC SDK `aic_hardware.c`][aic]** in `radxa-pkg/aic8800`, commit
  `516e3b087763d80c44f5e3b6d2dd63e0d925c91d`, path
  `src/SDIO/patch/for_Amlogic/905x4/Android11/mod/vendor/amlogic/common/wifi_bt/bluetooth/aic/libbt/src/aic_hardware.c`,
  imported with SDK `aic8800d_linux_sdk_V3.0_2023_1107_581567ff` in commit
  `7c8ed35d5e634186e0d1a25bc1436531ab57088f`. Its own header grants Apache-2.0 and names
  `Copyright (C) 2019-2027 AIC Corporation`. The repository's root GPL licence is not the licence
  evidence for this file.
- **[Tina/BlueZ AW869B patch][tina]** in `paulwratt/mangopi-sbc-tina-package`, commit
  `7f4072cfded97aef8d6ace123056f9f8a7dcebfc`, path
  `utils/bluez/patches/012-add-aw869b-hciattach-support.patch`. It adds `tools/hciattach_aic.c`,
  which has no licence header of its own; its [package Makefile][tina-package] declares
  `GPL-2.0-or-later`. It shows that the exact values are public; it is not evidence of an Apache
  grant, and none of its implementation is copied into AWTRIX NG.

Serialising the Tina initializer with the layout above reproduces all 104 bytes, padding
included. The Apache-licensed Amlogic SDK initializer reproduces 101 of 104 bytes; the only
differences are:

| Offset | Field | Amlogic SDK default | TC002 / Tina value | Changed byte offsets |
| --- | --- | --- | --- | --- |
| 44 | `aon_extra_sleep_duration_cpus` | `-2` | `-1` | 44 |
| 56 | `aon_debug_level` | `0x00000000` | `0x00000113` | 56, 57 |

The Rockchip 3326 and 3566 copies in the same SDK have the same two differences. The Allwinner
A133 copy differs in three fields: extra sleep duration (`0` / `-1`), debug level (`0x103` /
`0x113`) and CPU sleep algorithm duration (`360` / `512`). The SDK defaults therefore must not
replace the TC002 values.

## Attribution

Keep the exact copyright line:

```text
Copyright (C) 2019-2027 AIC Corporation
```

Identify the material as **AIC Bluetooth AON parameter format and defaults**, licensed under
**Apache License 2.0**, and ship the complete [Apache-2.0 licence][apache-license] with every
distribution that includes it. The adaptation is the byte representation above with the two
TC002 values in place of the Amlogic defaults. Apache section 4 also requires keeping applicable
source notices and identifying modifications to covered files. The pinned Bluetooth `libbt`
subtree and its parent directories have no `NOTICE` file (the SDK's `wpa_supplicant_8_lib/NOTICE`
files concern separate Wi-Fi material), so the AIC attribution entry in
[`tools/tc002/bluetooth/NOTICE`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/bluetooth/NOTICE)
does not invent an upstream NOTICE text or imply AIC endorsement. This covers the parameter data
only, not unrelated SDK contents, stock firmware or trademarks.

## Tests

The host test `tc002d-bt`
([`tests/tc002/daemon/test_bt.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/tests/tc002/daemon/test_bt.cpp))
runs `BtService` against a pseudo terminal and a fake controller and checks the command sequence
and the complete 104-byte transmission.

## Related

- [Wi-Fi driver](wifi-driver.md)
- [TC002 overview](index.md)
- [Third-party notices](https://github.com/Blueforcer/awtrix-ng/blob/main/THIRD-PARTY-NOTICES.md)

[aic]: https://github.com/radxa-pkg/aic8800/blob/516e3b087763d80c44f5e3b6d2dd63e0d925c91d/src/SDIO/patch/for_Amlogic/905x4/Android11/mod/vendor/amlogic/common/wifi_bt/bluetooth/aic/libbt/src/aic_hardware.c
[tina]: https://github.com/paulwratt/mangopi-sbc-tina-package/blob/7f4072cfded97aef8d6ace123056f9f8a7dcebfc/utils/bluez/patches/012-add-aw869b-hciattach-support.patch
[tina-package]: https://github.com/paulwratt/mangopi-sbc-tina-package/blob/7f4072cfded97aef8d6ace123056f9f8a7dcebfc/utils/bluez/Makefile
[apache-license]: https://www.apache.org/licenses/LICENSE-2.0
[linux49-event]: https://raw.githubusercontent.com/torvalds/linux/v4.9/net/bluetooth/hci_event.c
[linux49-request]: https://raw.githubusercontent.com/torvalds/linux/v4.9/net/bluetooth/hci_request.c
