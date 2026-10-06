#ifndef AWTRIX_TC002_LAYOUT_H
#define AWTRIX_TC002_LAYOUT_H

/* Shared paths and panel constants for TC002 programs and the installer.
 * The *_DIR names are absolute; state and runtime paths are suffixes below their group
 * directory. Device paths and TC002_LOOP_MODULE are absolute. */

/* /proc/device-tree/model of the one supported board. */
#define TC002_BOARD_MODEL "PIONEER3 SSC021A-S01A-S"

/* Persistent state: paths below TC002_DATA_DIR. */
#define TC002_DATA_DIR "/data/awtrix-ng"
#define TC002_STATE "/state"
#define TC002_BOOT_ATTEMPTS TC002_STATE "/boot-attempts"
#define TC002_UPDATE_STATE TC002_STATE "/update-state.json"
#define TC002_REBOOT_MARKER TC002_STATE "/rebooted"
#define TC002_DEVICE_ID TC002_STATE "/uid"
#define TC002_UPDATE_RESULT TC002_STATE "/update-result"

/* Daemon path below the release root. */
#define TC002_RELEASE_DAEMON "/bin/awtrix-tc002d"

/* Runtime paths below TC002_VOLATILE_DIR; cleared at power-on.
 * The loader counts starts here when /data cannot take them. */
#define TC002_VOLATILE_DIR "/tmp"
#define TC002_VOLATILE_BOOT_ATTEMPTS "/awtrix-loader.attempts"
#define TC002_LOADER_LOG "/awtrix-loader.log"
#define TC002_VENDOR_MARKER "/awtrix-loader.vendor"
#define TC002_RESCUE_MARKER "/awtrix-loader.rescue"
#define TC002_DAEMON_RUN "/awtrix-tc002d"
#define TC002_CONTROL_SOCKET TC002_DAEMON_RUN "/control.sock"
#define TC002_UPDATE_WORK "/awtrix-update"
#define TC002_INSTALL_WORK "/awtrix-install"
#define TC002_DEPLOY_TOKEN TC002_INSTALL_WORK "/lock.token"
#define TC002_RELEASE_MOUNT "/awtrix-release"  /* Mounted release slot. */

/* The res partition: the loader, the vendor app, loop.ko and, behind its squashfs, the release
 * slot (release_slot.h). */
#define TC002_RES_PARTITION "res"
#define TC002_LOOP_MODULE "/res/awtrix-ng/loop.ko"
#define TC002_MTD_DIR "/dev/mtd"
#define TC002_MTD_BLOCK_DIR "/dev/block"

/* Devices. */
#define TC002_KEYS_DEVICE_NAME "soc:gpio_keys_1"
#define TC002_KNOB_DEVICE_NAME "knob_key"
#define TC002_PANEL_WIDTH 52
#define TC002_PANEL_HEIGHT 16
#define TC002_PANEL_ROW_BYTES 192
#define TC002_PANEL_FRAME_BYTES (TC002_PANEL_ROW_BYTES * TC002_PANEL_HEIGHT)
#define TC002_PANEL_SPI_HZ 10000000u
#define TC002_PANEL_LATCH_SETTLE_MS 1
#define TC002_PANEL_FRAME_GAP_MS 15
#define TC002_AP_ADDRESS "192.168.4.1"
#define TC002_PANEL_SPI "/dev/spidev0.0"
#define TC002_PANEL_LATCH_GPIO "35"
#define TC002_PANEL_LATCH "/sys/class/gpio/gpio" TC002_PANEL_LATCH_GPIO
#define TC002_MCU_UART "/dev/ttyS1"
#define TC002_USB_ROLE "/sys/bus/platform/devices/soc:usbotg/otg_role"
#define TC002_USB_DEVICE_ROLE "usb_device"

#endif
