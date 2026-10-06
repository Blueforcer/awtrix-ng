# Updating firmware

This page shows how to install a newer AWTRIX NG version on a clock that already runs AWTRIX NG.
Settings and uploaded files are kept. If you want a separate copy of them, create a
[backup](../getting-started/web-ui.md#backup-and-restore) first.

<!-- only esp32 esp32-s3 -->
To install AWTRIX NG for the first time, see [Install AWTRIX NG](../getting-started/flashing.md).
<!-- /only -->
<!-- only tc002 -->
To install AWTRIX NG for the first time, see [Install AWTRIX NG](../getting-started/tc002.md).
<!-- /only -->

## How it behaves

The web UI asks GitHub for the newest release and downloads it in your browser. The clock itself
never contacts GitHub.
<!-- only esp32 esp32-s3 -->
The clock writes the new firmware next to the running one and switches only after the whole image
is checked, so a failed update leaves the clock as it was. Settings and uploaded files are stored
apart from the firmware and stay.
<!-- /only -->
<!-- only tc002 -->
The clock installs only a complete package for this clock that is newer than the installed
version, and a refused package changes nothing. After the restart, the new version must keep
running for one minute. Until then the clock refuses further uploads.
<!-- /only -->

## Update from the web UI

The easiest way:

1. Open the web UI and go to **System → Maintenance**.
2. Press **Check for updates**.
3. If a newer release has a file for your clock, the button turns into **Download & install**.
   Press it, then press it again to confirm.
4. Keep the page open and the clock powered until it restarts.

A newer version number alone does not mean the release has a file for this clock. Without one, the
button stays at **Check for updates**.

## Upload a file yourself

### Which file to download

Download the file for your clock from the
[releases page](https://github.com/Blueforcer/awtrix-ng/releases):

<!-- only esp32 -->
| Update file | Maximum upload |
|---|---|
| `firmware-awtrix-ng.bin` | 1,769,472 bytes |
<!-- /only -->
<!-- only esp32-s3 -->
| Board | Update file | Maximum upload |
|---|---|---|
| Octal PSRAM | `firmware-awtrix-ng-s3-octal.bin` | 1,769,472 bytes |
| Quad PSRAM | `firmware-awtrix-ng-s3-quad.bin` | 1,769,472 bytes |

Choose the image by how the PSRAM is wired on your board. The memory size alone does not tell you.
See [Which of the two images](../getting-started/flashing.md#which-of-the-two-s3-images).
<!-- /only -->
<!-- only tc002 -->
| Update file | Maximum upload |
|---|---|
| `awtrix-ng-tc002.awup` | 8 MiB |

If a release has no `awtrix-ng-tc002.awup`, it has no update for this clock.
<!-- /only -->

### Upload a new image

=== "Web UI"

    Open **System → Maintenance**, press **Choose file…** next to **Upload firmware** and select
    the file. Wait for the progress display. After the clock restarts, reload the page if it does
    not reconnect by itself.

<!-- only esp32 esp32-s3 -->
=== "With curl"

<!-- only esp32 -->
    ```bash
    curl -X POST http://<awtrix-ip>/update -F "firmware=@firmware-awtrix-ng.bin"
    ```
<!-- /only -->
<!-- only esp32-s3 -->
    ```bash
    curl -X POST http://<awtrix-ip>/update -F "firmware=@firmware-awtrix-ng-s3-octal.bin"
    ```
<!-- /only -->

    Success is `200 {"ok":true}`. The image is written and checked, then the clock restarts into it.

<!-- /only -->
<!-- only tc002 -->
=== "With curl"

    ```bash
    curl -X POST http://<awtrix-ip>/update -F "firmware=@awtrix-ng-tc002.awup"
    ```

    Success is `200 {"ok":true,"applying":true}`. The package is accepted for installation. Wait
    for the clock to restart, then check the running version.

<!-- /only -->
=== "With authentication"

    Add `-u myuser:mypass` to the command when HTTP login is on. Use the same user name and password
    as for the web UI.

`curl -F` sends the file as a form upload. Name the form field `firmware`.<!-- only tc002 --> The
clock refuses any other field name.<!-- /only -->

## Confirm which version is running

The web UI shows the running version. You can also ask for it:

```bash
curl http://<awtrix-ip>/api/v1/version
```

The answer is `{"version":"…"}`. `GET /version` returns the version as plain text, and
[Device state](../reference/device.md#endpoint) includes it as `version`.

<!-- only tc002 -->
## What to know about updates {#tc002-updates}

- If the new version fails to start three times, the clock shows **USB RECOVERY**. See
  [Reset & recovery](reset-recovery.md#when-awtrix-ng-cannot-start).
- The clock keeps 1 MiB of storage free for settings and updates. A release can still need more
  free space.

### Microphone controller updates {#mcu-updates}

Some releases also update the clock's microphone controller. This happens by itself:

1. The clock checks the installed controller version.
2. For the first update, it downloads the original controller firmware from Ulanzi once, checks it
   and keeps it on the clock. Later updates use that copy.
3. It installs the update. The clock may restart afterwards.

Keep the clock on USB power while this runs. If the download is not available, the clock works
normally and tries again later.

### Install an older version {#going-back-to-an-earlier-version}

The web UI installs only newer versions. If you need an older one, ask on
[Discord](https://discord.gg/5pbmeCrs3a).

<!-- /only -->
## When an update fails {#an-update-that-fails-changes-nothing}

<!-- only esp32 -->
An image for another chip is refused with `400 wrongChip`.
<!-- /only -->
<!-- only esp32-s3 -->
An image for another chip or for the other PSRAM type is refused with `400 wrongChip`.
<!-- /only -->
<!-- only tc002 -->
The answer names the reason. Settings, uploaded files and Wi-Fi are kept.

| Error | What to do |
|---|---|
| `invalidPackage` | The file is damaged. Download an intact package again. |
| `wrongTarget` | Select the `awtrix-ng-tc002.awup` package. |
| `notNewer` | This clock already has that version or a newer one. |
| `insufficientStorage` | The package is larger than the clock can hold. The answer names both sizes. Use the package from the releases page. |
| `updateBusy` | Wait until the current upload, installation or confirmation has finished. |
| `payloadTooLarge` / `insufficientMemory` | Check the package size and the free memory. The answer gives the details. |
<!-- /only -->

The size limits are in [Which file to download](#which-file-to-download). Exact status codes and
answers are in [Firmware upload](../reference/http.md#firmware-upload).

## Recovering a device that will not boot {#recovering-a-device-that-will-not-boot}

<!-- only esp32 esp32-s3 -->
See [Reset & recovery](reset-recovery.md#recovering-a-device-that-will-not-boot).
<!-- /only -->
<!-- only tc002 -->
See [Reset & recovery](reset-recovery.md#when-awtrix-ng-cannot-start).
<!-- /only -->

## Good to know

<!-- only esp32 -->
- **The `usb-*.bin` files do not work for this upload.** They are for the first installation over
  USB. Upload `firmware-awtrix-ng.bin`.
<!-- /only -->
<!-- only esp32-s3 -->
- **The `usb-*.bin` files do not work for this upload.** They are for the first installation over
  USB. Upload the `firmware-awtrix-ng-s3-…` file that matches your board.
<!-- /only -->
<!-- only tc002 -->
- **The USB installer does not update AWTRIX NG.** It is only for clocks that still run the Ulanzi
  app. Use the web UI or the upload above.
- **Packages are not signed.** Download them only from the releases page or another source you
  trust.
<!-- /only -->
- **The clock refuses firmware uploads while its setup hotspot is active.** Put the clock on your
  Wi-Fi first, as in [Connect to Wi-Fi](../getting-started/first-boot.md).

## Details

- [HTTP API → Firmware upload](../reference/http.md#firmware-upload): every answer of `/update`
- [Device state](../reference/device.md): the running version and the other values

## Related

- [Reset & recovery](reset-recovery.md): when the clock does not start
