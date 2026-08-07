# GPIO & boards

AWTRIX NG ships **one firmware per chip**, not per board. A commercial 32×8 clock, an AWTRIX 2
mainboard conversion and a panel you wired yourself all run the same binary; what differs is the pin map,
which is runtime configuration stored on AWTRIX and editable through the API or the web UI.

There are three images: `awtrix` for the ESP32, `awtrix_s3_octal` and `awtrix_s3_quad` for
the ESP32-S3 - see
[Rules come from the chip](#rules-come-from-the-chip).

The pin map lives in [system configuration](system.md) alongside Wi-Fi, MQTT and calibration -
it is read and written with `GET`/`PUT /api/v1/system`. This page is the complete GPIO
reference.

!!! warning "Changes apply after a reboot"
    Writing a new pin map stores it and returns `200`, but AWTRIX keeps using the old map until
    you restart it.

## Rules come from the chip

An ESP32 and an ESP32-S3 do not agree on a single one of the rules below:

| | ESP32 | ESP32-S3 |
|---|---|---|
| GPIO numbers | 0–39 | 0–48, but 22–25 are not bonded out |
| Input-only | 34–39 | **none** - every pin can drive an output |
| ADC1 | 32–39 | 1–10 |
| Reserved | 6–11 (SPI flash) | 19–20 (USB-JTAG), 26–37 (flash + octal PSRAM), 43–44 (UART0) |
| Matrix drivers | 2, 4, 5, 13, 14, 15, 16, 18, 21, 25, 26, 27, 32, 33 | 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47 |
| Strapping | 0, 2, 5, 12, 15 | 0, 3, 45, 46 |
| Wake from deep sleep | 0, 2, 4, 12–15, 25–27, 32–39 | 0–21 |

So GPIO 34 is a perfectly good battery tap on an ESP32 and a flash pin on an S3; GPIO 38 is
input-only on an ESP32 and a usable output on an S3.

The **strapping** row is the one entry in that table AWTRIX never enforces. Those pins are
sampled by the chip during reset to decide how it boots, so wiring something to them that holds
them high or low can stop the board from starting. Assigning one is accepted.

The **wake** row is the other one AWTRIX never enforces, and it applies to a single field.
`pinBtnSelect` is wired to the deep-sleep wakeup, so a select button on one of those pins ends a
[`POST /api/v1/device/sleep`](http.md#post-apiv1devicesleep) on a press. Any other pin is accepted
and the button keeps working normally while AWTRIX is awake - it just cannot bring it back, and
the sleep then runs its full `durationMs`. The other two buttons never wake AWTRIX.

**Do not hardcode this table.** AWTRIX reports its own rules under `gpio` in
[`GET /api/v1/capabilities`](http.md#gpio-what-the-chip-can-do), and which chip it is as `soc` in
[device state](device.md).

## The pin map

Fourteen fields describe the whole board. All are plain integers naming a **GPIO number** (not a
silkscreen label, not a Dx pin name), and `-1` disables the peripheral.

Defaults below are per chip: the ESP32 column is the Ulanzi TC001 wiring, the ESP32-S3 column is
a generic DIY layout with analog inputs on ADC1 and the Arduino-S3 default I²C bus.

| Key | Type | ESP32 | ESP32-S3 | `-1` allowed | Meaning |
|---|---|---|---|---|---|
| `pinMatrix` | int | `32` | `21` | **no** | LED matrix data line. Must come from the [matrix driver list](#1-matrix-pin-whitelist). |
| `pinBtnLeft` | int | `26` | `11` | yes | Left button, `INPUT_PULLUP`, active LOW. |
| `pinBtnSelect` | int | `27` | `12` | yes | Middle/select button, `INPUT_PULLUP`, active LOW. Also the deep-sleep wake button - see the wake row [above](#rules-come-from-the-chip). |
| `pinBtnRight` | int | `14` | `13` | yes | Right button, `INPUT_PULLUP`, active LOW. |
| `pinBattery` | int | `34` | `1` | yes | Battery voltage divider tap. Must be ADC1. |
| `pinLdr` | int | `35` | `2` | yes | Light sensor (LDR) tap. Must be ADC1. |
| `pinBuzzer` | int | `15` | `7` | yes | Passive buzzer. |
| `pinI2cSda` | int | `21` | `8` | yes | I²C data for the temperature/humidity sensor bus. |
| `pinI2cScl` | int | `22` | `9` | yes | I²C clock. |
| `pinDfRx` | int | `23` | `17` | yes | DFPlayer Mini serial RX. Used only while `dfplayer` is `true`, but validated whenever it is assigned. |
| `pinDfTx` | int | `18` | `18` | yes | DFPlayer Mini serial TX. Used only while `dfplayer` is `true`, but validated whenever it is assigned. |
| `pinI2sBclk` | int | `-1` | `5` | yes | I²S bit clock to an external DAC such as the MAX98357A. |
| `pinI2sLrclk` | int | `-1` | `6` | yes | I²S word-select (left/right) clock. |
| `pinI2sDout` | int | `-1` | `4` | yes | I²S serial data out. |

The three `pinI2s*` lines are a single bus and are checked as a set: all three assigned, or all
three `-1`. A partial set is rejected with `422 validationFailed`, naming the one you left out:

```json
{"error":{"code":"validationFailed","message":"the I2S pins work as a set: give all three, or -1 for all three","field":"pinI2sDout"}}
```

The ESP32 has no I²S audio output, so on an ESP32 the three fields are `-1` and stay there, and
the web UI's GPIO form shows eleven pins and no I²S row at all.

The related non-pin field:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `dfplayer` | bool | `false` | Selects the sound backend. `true` **and** `pinDfRx >= 0` **and** `pinDfTx >= 0` picks the DFPlayer Mini; otherwise the passive buzzer on `pinBuzzer`. It does **not** affect pin validation - the DF pins follow the same rules as every other pin. |

### What `-1` actually does

Each disabled peripheral has a defined consequence.

| Field | Effect of `-1` |
|---|---|
| `pinMatrix` | **Rejected.** The matrix cannot be disabled; `-1` is not in the driver list and fails validation. |
| `pinBtnLeft` / `pinBtnSelect` / `pinBtnRight` | The button always reads as not-pressed. No pull-up is configured. |
| `pinBattery` | Battery support switches off entirely: the `batteryPercent`, `batteryVoltage`, `batteryPinMillivolts` and `lowBattery` keys are omitted from device state altogether, the battery entities disappear from the Home Assistant discovery, and the built-in Battery app is removed from the rotation. |
| `pinLdr` | Light sensing switches off: the `lightLevel` and `ldrRaw` keys are omitted from device state altogether and the light-level entity disappears from the Home Assistant discovery. Auto-brightness keeps running and reads as pitch dark - see the warning below. |
| `pinBuzzer` | No buzzer. Combined with `dfplayer: false`, AWTRIX is silent. |
| `pinI2cSda` / `pinI2cScl` | No sensor bus; temperature and humidity are never populated. |
| `pinDfRx` / `pinDfTx` | The DFPlayer backend is not selected; AWTRIX falls back to the buzzer. |
| `pinI2sBclk` / `pinI2sLrclk` / `pinI2sDout` | No I²S output. Internet radio is unavailable and its API answers `503 unavailable`. |

!!! warning "Switch off `autoBrightness` when `pinLdr` is `-1`"
    With no light sensor the raw reading is `0`, which is indistinguishable from pitch darkness.
    With `autoBrightness` on, the panel then pins to one end of its range and stays there:
    `minBrightness` with `ldrOnGround: false` (the default), `maxBrightness` with
    `ldrOnGround: true`.

## Board presets

The web UI's **System → GPIO** section carries two preset buttons. They fill the form fields and
leave them unsaved, so you review the values before anything reaches AWTRIX.

Both presets are ESP32 boards, so the row is hidden on an ESP32-S3 build, and neither touches the
I²S fields.

=== "Ulanzi TC001"

    The stock hardware, and the ESP32 build's default for every field. A factory-fresh AWTRIX, or
    one whose stored map failed validation, boots on exactly this map.

    | Field | Value |
    |---|---|
    | `pinMatrix` | `32` |
    | `pinBtnLeft` | `26` |
    | `pinBtnSelect` | `27` |
    | `pinBtnRight` | `14` |
    | `pinBattery` | `34` |
    | `pinLdr` | `35` |
    | `pinBuzzer` | `15` |
    | `pinI2cSda` | `21` |
    | `pinI2cScl` | `22` |
    | `pinDfRx` | `23` |
    | `pinDfTx` | `18` |
    | `dfplayer` | `false` |

=== "AWTRIX 2 mainboard"

    An AWTRIX 2 mainboard carrying a **WeMos D1 mini32**. The board's silkscreen uses D-labels;
    AWTRIX wants GPIO numbers, so both are shown. No battery, no buzzer - a DFPlayer Mini
    provides the sound.

    | Field | Value | Board label |
    |---|---|---|
    | `pinMatrix` | `21` | D2 |
    | `pinBtnLeft` | `26` | D0 |
    | `pinBtnSelect` | `16` | D4 |
    | `pinBtnRight` | `5` | D8 |
    | `pinBattery` | `-1` | - (no battery) |
    | `pinLdr` | `36` | A0 |
    | `pinBuzzer` | `-1` | - (DFPlayer instead) |
    | `pinI2cSda` | `17` | D3 |
    | `pinI2cScl` | `22` | D1 |
    | `pinDfRx` | `23` | |
    | `pinDfTx` | `18` | |
    | `dfplayer` | `true` | |

    `pinMatrix: 21` collides with the default `pinI2cSda: 21`, so a `PUT` that sets only
    `pinMatrix` is rejected. Both have to move in the same request - send the whole map, as
    shown under [Write a complete map](#write-a-complete-map).

## Validation rules

Validation happens in three stages, all of them before anything is persisted.

Every rule below is evaluated against the running chip's profile, so the numbers in the examples
are the ESP32 ones. On an ESP32-S3 the same rules produce different limits and different
messages; the authoritative values for your own board are in `gpio` from
[`GET /api/v1/capabilities`](http.md#gpio-what-the-chip-can-do).

First, each `pin*` value in the request body must be an integer that is `-1` (disabled) or a GPIO
within the chip's range - `0`–`39` on an ESP32, `0`–`48` on an ESP32-S3. A value that is not gets
`422 validationFailed` naming the field:

```json
{"error":{"code":"validationFailed","message":"must be -1 (disabled) or a GPIO in 0..39","field":"pinLdr"}}
```

Second, the I²S trio is checked as a set on the **merged** map - your fields applied on top of
the stored ones - and answers `422 validationFailed` on a partial set, as shown above.

Then the structural rules run against that same merged map, and the **first** failure returns:

```json
{"error":{"code":"invalidPinConfig","message":"pinBtnLeft: GPIO 34-39 are input-only"}}
```

Rule 1 is checked first, for the whole map. Rules 2-4 (range, reserved, input-only) are then
checked **one pin at a time**, in field order - `pinMatrix`, `pinBtnLeft`, `pinBtnSelect`,
`pinBtnRight`, `pinBattery`, `pinLdr`, `pinBuzzer`, `pinI2cSda`, `pinI2cScl`, `pinDfRx`,
`pinDfTx`, `pinI2sBclk`, `pinI2sLrclk`, `pinI2sDout` - each pin fully checked before the next
one is looked at. An earlier field's reserved-pin error therefore comes back before a later
field's out-of-range one. Rules 5 and 6 run last, across the whole map.

### 1. Matrix pin whitelist

`pinMatrix` must come from the list the running image compiled drivers for - the **Matrix
drivers** row of the [chip table](#rules-come-from-the-chip). Anything else, including `-1`, is
rejected with the list in the message:

```
pinMatrix: unsupported pin (compiled drivers: 2,4,5,13,14,15,16,18,21,25,26,27,32,33)
```

### 2. Valid GPIO range

Each **enabled** pin must exist on the chip. The ESP32-S3 additionally has a hole: GPIO `22`–`25`
are not bonded out.

```
<field>: not a valid ESP32 GPIO (0-39, or -1 = disabled)
<field>: not a valid ESP32-S3 GPIO (0-48 except 22-25, or -1 = disabled)
```

### 3. Reserved pins

Pins the chip needs for itself. The message names what took them:

| Chip | Range | Reserved for |
|---|---|---|
| ESP32 | `6`–`11` | the SPI flash |
| ESP32-S3 | `19`–`20` | the USB-JTAG interface |
| ESP32-S3 | `26`–`37` | the SPI flash and PSRAM |
| ESP32-S3 | `43`–`44` | the UART0 console |

```
<field>: GPIO 6-11 are reserved for the SPI flash
<field>: GPIO 26-37 are reserved for the SPI flash and PSRAM
```

### 4. Input-only pins

On the ESP32, GPIO `34`–`39` have no output driver and no internal pull-ups. Fields that need to
drive a line - or need `INPUT_PULLUP` - are rejected on them.

```
<field>: GPIO 34-39 are input-only
```

**The ESP32-S3 has no input-only pins**, so this rule never fires there.

| Field | Needs output? | Why |
|---|---|---|
| `pinMatrix` | yes | Drives the LED data line. |
| `pinBtnLeft`, `pinBtnSelect`, `pinBtnRight` | yes | Need the internal pull-up. |
| `pinBuzzer` | yes | Drives the buzzer. |
| `pinI2cSda`, `pinI2cScl` | yes | I²C needs bidirectional drive. |
| `pinDfTx` | yes | AWTRIX transmits on it. |
| `pinI2sBclk` / `pinI2sLrclk` / `pinI2sDout` | yes | All three drive the DAC. |
| `pinBattery` | no | Read-only ADC. |
| `pinLdr` | no | Read-only ADC. |
| `pinDfRx` | no | AWTRIX receives on it. |

### 5. ADC1 requirement

`pinBattery` and `pinLdr` must be ADC1 channels - GPIO `32`–`39` on an ESP32, GPIO `1`–`10` on an
ESP32-S3. ADC2 is unusable while Wi-Fi is active on both.

```
pinBattery: must be an ADC1 pin (GPIO 32-39, usable while WiFi is on)
pinLdr: must be an ADC1 pin (GPIO 32-39, usable while WiFi is on)
```

An ESP32-S3 names its own range, `GPIO 1-10`, in the same message.

### 6. No duplicates

Every **enabled** pin must be unique across the whole map.

```
duplicate pin <n> (<fieldA>, <fieldB>)
```

When one of the two fields is `pinMatrix`, the message goes on to name the fix - the matrix pin
is the one that cannot move freely:

```
duplicate pin 21 (pinMatrix, pinI2cSda) - the matrix pin cannot be shared; move the
other pin in the SAME request (AWTRIX 2: set pinI2cSda to 17 together with pinMatrix 21)
```

Disabled pins (`-1`) are skipped entirely - four fields can all be `-1` without conflicting.
That is the only exemption: an assigned pin is checked whether or not the peripheral behind it
is switched on, so `pinDfTx: 34` is rejected as input-only even while `dfplayer` is `false`, and
`pinBuzzer: 23` collides with the default `pinDfRx`. Set the DF pins to `-1` if you are not
wiring a DFPlayer.

## Reading and writing the map

The pin fields are ordinary keys of the system configuration resource.

### Read the current map

```bash
curl http://<awtrix-ip>/api/v1/system
```

### Write a complete map

Send the whole map in one request so cross-field rules are evaluated against the values you
intend, not a mixture of new and stored ones.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{
        "pinMatrix": 21,
        "pinBtnLeft": 26,
        "pinBtnSelect": 16,
        "pinBtnRight": 5,
        "pinBattery": -1,
        "pinLdr": 36,
        "pinBuzzer": -1,
        "pinI2cSda": 17,
        "pinI2cScl": 22,
        "pinDfRx": 23,
        "pinDfTx": 18,
        "dfplayer": true
      }'
```

On success the response is `200` with the resulting configuration.

Then reboot to apply it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

The `Content-Type` header is
[mandatory on every `PUT`](conventions.md#content-type-is-mandatory).

### A rejected write

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"pinBattery": 25}'
```

```json
{"error":{"code":"invalidPinConfig","message":"pinBattery: must be an ADC1 pin (GPIO 32-39, usable while WiFi is on)"}}
```

Nothing was stored. The merged map is validated **before** anything reaches flash, so a rejected
request leaves AWTRIX exactly as it was, including any non-pin fields in the same body.

Every message a rejected pin map can carry, and the other statuses this route answers, are in
[Errors - GPIO validation](errors.md#gpio-validation-invalidpinconfig).

## Recovery from a bad map

You cannot brick AWTRIX with a pin map.

Validation runs a second time at boot, on whatever is stored. If the stored map does not
validate - a hand-edited file, or an ESP32 map on an ESP32-S3 - the board falls back to the
running chip's defaults. AWTRIX comes up, the web UI is reachable, and you can fix the map. The
stored map is *not* rewritten, so the same fallback repeats until you save a valid one.

A map that is *valid* but *wrong* for your hardware boots normally with a dead panel or dead
buttons. If that happens and you cannot reach the web UI, a [factory reset](system.md) restores
the chip's defaults along with everything else.

## Panel layout

A panel you wired yourself also needs describing: how wide one panel is, how many panels the
cable runs through, which corner it enters, whether the strip runs along rows or columns, and
whether every second run comes back the other way.

The panel **height is fixed at 8 pixels**; the width is `panelWidth × panels` and must come to
between 32 and 128 (so 32 × 8 = 256 LEDs by default).

The keys, their ranges and the settings for the common builds are documented once, under
[Panel and orientation](system.md#panel-and-orientation). They live on `PUT /api/v1/system` only,
and every one except the total width takes effect on the next frame.

## Wiring your own board

A checklist, in the order to work through it:

1. **Read the chip's rules first**: `curl http://<awtrix-ip>/api/v1/capabilities` and look at
   `gpio`. Every number in the steps below is the ESP32 value; an ESP32-S3 answers differently
   on all of them.
2. **Pick a matrix pin from the whitelist.** This constrains your layout more than anything else
   - decide it first.
3. **Battery and LDR need ADC1** (`32`–`39` on an ESP32, `1`–`10` on an ESP32-S3). If you only
   have one ADC1 pin free, prefer the battery: a missing LDR degrades badly (see the warning
   above), a missing battery degrades cleanly.
4. **Buttons need pull-ups**, so on an ESP32 they cannot live on `34`–`39`. Wire them to ground;
   `INPUT_PULLUP` is configured for you and LOW is pressed. An ESP32-S3 has no input-only pins,
   so any existing pin works. If you want the select button to end a deep sleep, take it from the
   wake row of the [chip table](#rules-come-from-the-chip).
5. **Avoid the reserved ranges** - `6`–`11` on an ESP32 is your flash; on an ESP32-S3 it is
   `26`–`37` for flash and PSRAM, plus `19`–`20` and `43`–`44` for USB and the console.
6. **Disable what you do not have** with `-1` rather than leaving a plausible-looking pin.
7. **Send the whole map at once** and reboot.
8. **Calibrate the analog inputs afterwards.** The pin map only says *where* to read; what the
   readings *mean* is separate configuration - `batteryDividerRatio` for the battery divider,
   `ldrFactor` / `ldrGamma` / `ldrOnGround` for the light sensor. Defaults match the stock wiring
   and will be wrong for your divider. See [Brightness & sensors](../guides/brightness.md) and
   [Power & battery](../guides/power.md).

A differently-assembled panel may also need `rotate` or `mirror`, under
[Panel and orientation](system.md#panel-and-orientation), and `swapButtons`, under
[Buttons](system.md#buttons).

## Sensor bus

I²C sensors are **auto-detected** at boot, not configured. Only `pinI2cSda` and `pinI2cScl` are
yours to set; which chip is on the bus is discovered.

Recognised, in the order they are probed: **BME280** (`0x76`, then `0x77`), **BMP280** (the same two
addresses), **HTU21DF** (its own fixed address), **SHT31** (`0x44`). The first chip that answers
wins, so a BME280 and an SHT31 on one bus leaves the SHT31 unused.

Which readings you get depends on the chip that answers: all four report temperature, the BME280,
HTU21DF and SHT31 add humidity, and the BME280 and BMP280 add air pressure.

Whatever the sensor does not measure - and everything, when there is no sensor or no bus - is
never populated in device state, and `tempOffset` / `humOffset` are then not applied to
anything.

## The web UI's pin dropdowns

Each pin field in the web UI is a dropdown, and it lists only what the running chip can actually
do with that peripheral: analog-capable pins for the battery and light-sensor taps, output-capable
pins for the buttons, buzzer, I²C and the outgoing serial line, the compiled driver list for the
matrix, and **not connected** wherever `-1` is allowed. Pins the chip needs for flash, PSRAM, USB
or its console are not offered at all. A number the list cannot offer - a map saved on a different
board, for example - is shown as its own entry marked as the stored value, so it is never
overwritten by accident.

The dropdown is a convenience, not the rule: the same limits are enforced on every request, so a
direct API call cannot bypass them.

Wi-Fi, MQTT, NTP, auth, sensor calibration and everything else `PUT /api/v1/system` accepts -
including the range checks on its other numeric fields - are documented in
[System configuration](system.md).
