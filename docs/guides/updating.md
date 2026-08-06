# Updating firmware

You upload a new firmware `.bin` yourself - from the web UI or with `curl` - and AWTRIX writes it
into its spare firmware slot, reboots, and comes back on the new version. Your settings and your
uploaded files stay as they are.

## Upload a new image

=== "Web UI"

    **System → Maintenance → Upload firmware (.bin)**. Pick your `.bin` and the browser uploads it
    with a progress bar. On success the UI toasts *"Rebooting - the page reloads automatically…"*
    and reloads itself after 12 seconds.

=== "curl"

    ```bash
    curl -X POST http://<awtrix-ip>/update -F "firmware=@firmware-awtrix-ng.bin"
    ```

    ```json
    {"ok":true}
    ```

    That reply means the image was written and verified, and it is the last thing you hear from the
    running version - AWTRIX reboots into the new one straight after. Give it a few seconds and
    reload.

    `curl -F` sends the multipart upload this route expects; the name of the file field does not
    matter.

=== "With authentication"

    If you turned on login under [Identity, web server and
    authentication](../reference/system.md#identity-web-server-and-authentication), the upload
    needs HTTP Basic auth like every other route:

    ```bash
    curl -X POST http://<awtrix-ip>/update \
      -u myuser:mypass \
      -F "firmware=@firmware-awtrix-ng.bin"
    ```

While AWTRIX is in provisioning mode - running its own access point, before it has joined your
Wi-Fi - firmware upload is disabled and answers `403 forbidden`. Join it to your network and
upload over HTTP there, or flash it over USB, which needs no network at all.

Status codes for the route: [Firmware upload](../reference/http.md#firmware-upload).

## Which file to download

Every release on the [releases page](https://github.com/Blueforcer/awtrix-ng/releases) carries one
file per chip:

| File | For |
|---|---|
| `firmware-awtrix-ng.bin` | Ulanzi TC001 and every other classic ESP32 - 32×8 clocks, AWTRIX 2 conversions, DIY builds |
| `firmware-awtrix-ng-s3.bin` | ESP32-S3 boards |

One file covers every board of that chip, whatever its flash size - boards differ in their pin
map, which you set on AWTRIX. See [Board presets](../reference/gpio.md#board-presets).

!!! warning "Not the `factory-*.bin` files"
    A TC001 has a 4 MB ESP32, but `factory-awtrix-ng-4mb.bin` is **not** the file you want here.
    The `factory-*.bin` images are for a first flash over USB - see
    [Flashing](../getting-started/flashing.md). To update a device that already runs AWTRIX NG,
    take `firmware-awtrix-ng.bin`.

An image built for the other chip, and a factory image, are both refused with `400 wrongChip`
before a single byte reaches flash, so uploading the wrong one costs you nothing but the upload. To upload a build of your own, `pio run -e awtrix`
writes it to `.pio/build/awtrix/firmware.bin` - see
[Building from source](../advanced/building.md).

## An update that fails changes nothing

The image is written into the spare slot as it arrives, never over the firmware that is running,
and AWTRIX switches to it only once the whole image has arrived and been verified.

So an upload that goes wrong - a cable pulled out, a truncated file, a corrupt image - is simply
refused, and AWTRIX carries on running the firmware it already had. Upload it again.

Your data survives an update too:

* **Settings** and the **device configuration** are stored separately from the firmware.
* **Icons, melodies and palettes** (`/ICONS`, `/MELODIES`, `/PALETTES`) are never part of a
  firmware image. They are uploaded at runtime via
  [`POST /api/v1/files`](../reference/http.md#post-apiv1files) and stay put across updates.

There is no filesystem image to flash, and no step where you re-upload your icons.

### Size limit

A firmware image may not exceed **1,769,472 bytes (1.69 MB)** - the size of one firmware slot,
which is the same on every board whatever its chip or flash size. An image larger than that is
refused with `500` `internalError` before any of it is stored.

## Confirm which version is running

The web UI shows the running version beside the live preview. Over HTTP:

```bash
curl http://<awtrix-ip>/api/v1/version
```

```json
{"version":"1.0.12"}
```

`GET /version` returns the same string as `text/plain` if you want it without the JSON wrapper,
and `version` is also a field of [`GET /api/v1/device`](../reference/device.md#endpoint).

## Recovering a device that will not boot

If AWTRIX no longer answers on the network, reflash it over USB - see
[Flashing](../getting-started/flashing.md). That path does not depend on the firmware currently on
the chip, but flashing a `factory-*.bin` does erase your settings and Wi-Fi credentials, so you
set AWTRIX up again from [First boot](../getting-started/first-boot.md).

To start clean while AWTRIX still works, use **System → Maintenance → Erase everything** in the
web UI, or [`POST /api/v1/device/factory-reset`](../reference/http.md#device). This wipes your
settings, your Wi-Fi credentials and every file you uploaded - icons, melodies, palettes and
scripts. Download anything you want to keep first.

## Related

* [Firmware upload](../reference/http.md#firmware-upload) - status codes for `POST /update`
* [Device](../reference/http.md#device) - the device routes
* [Device state](../reference/device.md#endpoint) - `version`
* [Flashing](../getting-started/flashing.md) - the first flash, and reflashing over USB
* [Board presets](../reference/gpio.md#board-presets) - one image, every board of a chip
