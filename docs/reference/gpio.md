---
only: [esp32, esp32-s3]
---

# GPIO & boards

<!-- only esp32 -->
This page tells you which ESP32 pins AWTRIX NG can use, how to enter your own pin map, and what
happens when a pin is not allowed.

There is one firmware per chip, not per board. A Ulanzi TC001, an AWTRIX 2 mainboard and a panel
you wired yourself all run the same firmware file. Only the pin map is different, and you set it
in the web UI or through the API.
<!-- /only -->
<!-- only esp32-s3 -->
This page tells you which ESP32-S3 pins AWTRIX NG can use, how to enter your own pin map, and what
happens when a pin is not allowed.

There is one firmware for every ESP32-S3 board. Only the pin map is different, and you set it in
the web UI or through the API. The firmware comes in an `-s3-octal-` and an `-s3-quad-` variant.
Which file you need is explained in
[Install AWTRIX NG](../getting-started/flashing.md#which-of-the-two-s3-images).
<!-- /only -->

!!! warning "Changes apply after a reboot"
    When you save a new pin map, AWTRIX stores it and answers `200`. It keeps using the old map
    until you restart it.

## Set the pin map

1. Open the web UI and go to **System → GPIO**.
2. Pick a pin for each part you have wired. Choose **not connected** for parts you do not have.
3. Save.
4. Restart AWTRIX.

The dropdowns only offer pins that work for that part on your chip. See
[The web UI's pin dropdowns](#the-web-uis-pin-dropdowns).

To do the same through the API, see [Reading and writing the map](#reading-and-writing-the-map).

## Rules come from the chip

<!-- only esp32 -->
The ESP32 has these pin rules:

| | ESP32 |
|---|---|
| GPIO numbers | 0–39 |
| Input-only | 34–39 |
| ADC1 (analog input) | 32–39 |
| Reserved | 6–11 (SPI flash) |
| LED matrix data | 2, 4, 5, 13, 14, 15, 16, 18, 21, 25, 26, 27, 32, 33 |
| Strapping | 0, 2, 5, 12, 15 |
| Wake from deep sleep | 0, 2, 4, 12–15, 25–27, 32–39 |

For example, GPIO 34 is a good battery input, but it cannot drive a button or the buzzer.
<!-- /only -->
<!-- only esp32-s3 -->
The ESP32-S3 has these pin rules:

| | ESP32-S3 |
|---|---|
| GPIO numbers | 0–48, but 22–25 do not exist |
| Input-only | **none** – every pin can drive an output |
| ADC1 (analog input) | 1–10 |
| Reserved | 19–20 (USB-JTAG), 26–37 (flash + octal PSRAM), 43–44 (UART0) |
| LED matrix data | 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47 |
| Strapping | 0, 3, 45, 46 |
| Wake from deep sleep | 0–21 |

GPIO 26–37 are reserved even on a board without PSRAM.
<!-- /only -->

**Strapping pins** are read by the chip at power-on to decide how it starts. If you connect
something that pulls such a pin high or low, the board may not start. AWTRIX accepts these pins,
so the choice is yours.

**Wake pins** matter only for the select button (`pinBtnSelect`). If the select button is on a
wake pin, pressing it ends a [`POST /api/v1/device/sleep`](http.md#post-apiv1devicesleep) early.
On any other pin the button works normally while AWTRIX is awake, but it cannot wake it up. The
sleep then runs for its full `durationMs`. The left and right buttons never wake AWTRIX.

AWTRIX reports the rules for its own chip under `gpio` in
[`GET /api/v1/capabilities`](http.md#gpio-what-the-chip-can-do), and the chip type as `soc` in
[device state](device.md). Use those values in your own tools instead of copying this table.

## The pin map

Each field holds a **GPIO number** – not a label printed on the board and not a `Dx` pin name.
`-1` means "not connected".

<!-- only esp32 -->
The defaults are the Ulanzi TC001 wiring.

| Key | Type | Default | `-1` allowed | Meaning |
|---|---|---|---|---|
| `pinMatrix` | int | `32` | **no** | LED matrix data line. Must be on the [LED matrix list](#1-matrix-pin-whitelist). |
| `pinBtnLeft` | int | `26` | yes | Left button. Wire it to ground; pressed = LOW. AWTRIX turns on the internal pull-up. |
| `pinBtnSelect` | int | `27` | yes | Select (middle) button, wired like the left one. Also wakes AWTRIX from deep sleep – see the wake row [above](#rules-come-from-the-chip). |
| `pinBtnRight` | int | `14` | yes | Right button, wired like the left one. |
| `pinBattery` | int | `34` | yes | Battery voltage divider tap. Must be ADC1. |
| `pinLdr` | int | `35` | yes | Light sensor (LDR) tap. Must be ADC1. |
| `pinBuzzer` | int | `15` | yes | Passive buzzer. |
| `pinI2cSda` | int | `21` | yes | I²C data line for the temperature/humidity sensor. |
| `pinI2cScl` | int | `22` | yes | I²C clock line. |
| `pinDfRx` | int | `23` | yes | DFPlayer Mini serial RX. Used only while `dfplayer` is `true`, but always checked when set. |
| `pinDfTx` | int | `18` | yes | DFPlayer Mini serial TX. Used only while `dfplayer` is `true`, but always checked when set. |

The ESP32 has no I²S audio output. Leave `pinI2sBclk`, `pinI2sLrclk`, `pinI2sDout`, `pinI2sMclk`
and `pinAmpEnable` at `-1`. The web UI does not show them.
<!-- /only -->
<!-- only esp32-s3 -->
The defaults are a generic DIY layout with the analog inputs on ADC1 and the usual ESP32-S3 I²C
pins.

| Key | Type | Default | `-1` allowed | Meaning |
|---|---|---|---|---|
| `pinMatrix` | int | `21` | **no** | LED matrix data line. Must be on the [LED matrix list](#1-matrix-pin-whitelist). |
| `pinBtnLeft` | int | `11` | yes | Left button. Wire it to ground; pressed = LOW. AWTRIX turns on the internal pull-up. |
| `pinBtnSelect` | int | `12` | yes | Select (middle) button, wired like the left one. Also wakes AWTRIX from deep sleep – see the wake row [above](#rules-come-from-the-chip). |
| `pinBtnRight` | int | `13` | yes | Right button, wired like the left one. |
| `pinBattery` | int | `1` | yes | Battery voltage divider tap. Must be ADC1. |
| `pinLdr` | int | `2` | yes | Light sensor (LDR) tap. Must be ADC1. |
| `pinBuzzer` | int | `7` | yes | Passive buzzer. |
| `pinI2cSda` | int | `8` | yes | I²C data line for the temperature/humidity sensor. |
| `pinI2cScl` | int | `9` | yes | I²C clock line. |
| `pinDfRx` | int | `17` | yes | DFPlayer Mini serial RX. Used only while `dfplayer` is `true`, but always checked when set. |
| `pinDfTx` | int | `18` | yes | DFPlayer Mini serial TX. Used only while `dfplayer` is `true`, but always checked when set. |
| `pinI2sBclk` | int | `5` | yes | I²S bit clock to an external amplifier/DAC such as the MAX98357A. |
| `pinI2sLrclk` | int | `6` | yes | I²S word-select (left/right) clock. |
| `pinI2sDout` | int | `4` | yes | I²S data out. |
| `pinI2sMclk` | int | `-1` | yes | Master clock, for DACs that need one. |
| `pinAmpEnable` | int | `-1` | yes | Amplifier enable. Goes high at startup and stays high. |

The three I²S lines (`pinI2sBclk`, `pinI2sLrclk`, `pinI2sDout`) work as a set: set all three, or
set all three to `-1`. If you set only some of them, AWTRIX answers `422 validationFailed` and
names the missing one:

```json
{"error":{"code":"validationFailed","message":"set all three I2S pins or none","field":"pinI2sDout"}}
```

`pinI2sMclk` and `pinAmpEnable` are optional. You can set each one alone, but only when the three
I²S lines are set. Otherwise you get the same `422`.
<!-- /only -->

One more field belongs to the pin map:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `dfplayer` | bool | `false` | Switches the DFPlayer Mini on. With `true` **and** both `pinDfRx` and `pinDfTx` set, AWTRIX plays numbered tracks on the DFPlayer Mini. The passive buzzer on `pinBuzzer` keeps working either way. The DF pins are checked the same way whether this is on or off. |

### What `-1` does

| Field | What happens with `-1` |
|---|---|
| `pinMatrix` | **Rejected.** You cannot switch off the matrix. |
| `pinBtnLeft` / `pinBtnSelect` / `pinBtnRight` | The button always reads as not pressed. |
| `pinBattery` | No battery support: `batteryPercent`, `batteryVoltage`, `batteryPinMillivolts` and `lowBattery` are left out of device state, the battery entities disappear from Home Assistant, and the Battery app leaves the rotation. |
| `pinLdr` | No light sensor: `lightLevel` and `ldrRaw` are left out of device state, and the Light level and Brightness mode entities disappear from Home Assistant. `autoBrightness` has no effect – the panel uses `brightness` – and the web UI hides the auto-brightness switch. |
| `pinBuzzer` | No buzzer. Melodies and RTTTL tunes make no sound. |
| `pinI2cSda` / `pinI2cScl` | No sensor. Temperature and humidity stay empty. |
| `pinDfRx` / `pinDfTx` | No DFPlayer. The buzzer is not affected. |
<!-- only esp32-s3 -->
| `pinI2sBclk` / `pinI2sLrclk` / `pinI2sDout` | No I²S output. MP3s and internet radio are not available; their API answers `503 unavailable`. |
| `pinI2sMclk` | No master clock. |
| `pinAmpEnable` | The amplifier is never switched on. Amplifiers with an enable input stay silent. |
<!-- /only -->

<!-- only esp32 -->
## Board presets

**System → GPIO** in the web UI has two preset buttons. A preset fills in the form but does not
save it, so you can check the values first.

=== "Ulanzi TC001"

    The stock Ulanzi hardware. These are also the defaults: a new AWTRIX, or one whose stored map
    is not valid, starts with exactly this map.

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

    An AWTRIX 2 mainboard with a **WeMos D1 mini32**. The board is labeled with D-names, but
    AWTRIX needs GPIO numbers, so the table shows both. There is no battery and no buzzer; a
    DFPlayer Mini plays the sound.

    | Field | Value | Board label |
    |---|---|---|
    | `pinMatrix` | `21` | D2 |
    | `pinBtnLeft` | `26` | D0 |
    | `pinBtnSelect` | `16` | D4 |
    | `pinBtnRight` | `5` | D8 |
    | `pinBattery` | `-1` | – (no battery) |
    | `pinLdr` | `36` | A0 |
    | `pinBuzzer` | `-1` | – (DFPlayer instead) |
    | `pinI2cSda` | `17` | D3 |
    | `pinI2cScl` | `22` | D1 |
    | `pinDfRx` | `23` | |
    | `pinDfTx` | `18` | |
    | `dfplayer` | `true` | |

    `pinMatrix: 21` is the same pin as the default `pinI2cSda: 21`. If you change only
    `pinMatrix`, the request is rejected. Change both in the same request – send the whole map
    as shown in [Write a complete map](#write-a-complete-map).

<!-- /only -->
## Validation rules

AWTRIX checks every pin map before it stores anything. A rejected request changes nothing – not
even the other fields in the same request.

The exact values for your chip are under `gpio` in
[`GET /api/v1/capabilities`](http.md#gpio-what-the-chip-can-do).

AWTRIX combines the fields you send with the stored map and checks the result in this order:

1. **Each value on its own.** Every `pin*` value must be an integer: `-1` or a GPIO from `0` to
   <!-- only esp32 -->`39`<!-- /only --><!-- only esp32-s3 -->`48`<!-- /only -->. If not, you get `422 validationFailed` with the field name:

<!-- only esp32 -->
    ```json
    {"error":{"code":"validationFailed","message":"must be -1 or 0..39","field":"pinLdr"}}
    ```
<!-- /only -->
<!-- only esp32-s3 -->
    ```json
    {"error":{"code":"validationFailed","message":"must be -1 or 0..48","field":"pinLdr"}}
    ```
<!-- /only -->

<!-- only esp32-s3 -->
2. **The I²S set.** A partial I²S set gets `422 validationFailed`, as shown above.
3. **The rules 1 to 6 below.** The first failure is returned as `400 invalidPinConfig`:

    ```json
    {"error":{"code":"invalidPinConfig","message":"pinLdr: must be ADC1 (GPIO 1-10)"}}
    ```
<!-- /only -->
<!-- only esp32 -->
2. **The rules 1 to 6 below.** The first failure is returned as `400 invalidPinConfig`:

    ```json
    {"error":{"code":"invalidPinConfig","message":"pinBuzzer: GPIO 34-39 are input-only"}}
    ```
<!-- /only -->

Rule 1 is checked first. Rules 2 to 4 are then checked pin by pin in the order of the table
above, starting with `pinMatrix`. Rules 5 and 6 come last. So you always see only one problem at
a time; fix it and send again.

### 1. Matrix pin whitelist

`pinMatrix` must be on the **LED matrix data** list in the [chip table](#rules-come-from-the-chip).
Any other value, including `-1`, is rejected, and the message lists the allowed pins:

<!-- only esp32 -->
```
pinMatrix: unsupported pin (use 2,4,5,13,14,15,16,18,21,25,26,27,32,33)
```
<!-- /only -->
<!-- only esp32-s3 -->
```
pinMatrix: unsupported pin (use 13,14,15,16,17,18,21,38,39,40,41,42,47)
```
<!-- /only -->

### 2. Valid GPIO range

Each pin that is not `-1` must exist on the chip.<!-- only esp32-s3 --> GPIO `22`–`25` do not exist.<!-- /only -->

<!-- only esp32 -->
```
<field>: invalid ESP32 GPIO (0-39, or -1)
```
<!-- /only -->
<!-- only esp32-s3 -->
```
<field>: invalid ESP32-S3 GPIO (0-48 except 22-25, or -1)
```
<!-- /only -->

### 3. Reserved pins

The chip uses these pins itself. The message says what for:

<!-- only esp32 -->
| Range | Reserved for |
|---|---|
| `6`–`11` | the SPI flash |

```
<field>: GPIO 6-11 are reserved for the SPI flash
```
<!-- /only -->
<!-- only esp32-s3 -->
| Range | Reserved for |
|---|---|
| `19`–`20` | the USB-JTAG interface |
| `26`–`37` | the SPI flash and PSRAM |
| `43`–`44` | the UART0 console |

```
<field>: GPIO 26-37 are reserved for the SPI flash and PSRAM
```
<!-- /only -->

### 4. Input-only pins

<!-- only esp32 -->
GPIO `34`–`39` cannot drive an output and have no internal pull-up. Fields that must drive a line
or need a pull-up are rejected on these pins.

```
<field>: GPIO 34-39 are input-only
```

| Field | Needs output? | Why |
|---|---|---|
| `pinMatrix` | yes | Drives the LED data line. |
| `pinBtnLeft`, `pinBtnSelect`, `pinBtnRight` | yes | Need the internal pull-up. |
| `pinBuzzer` | yes | Drives the buzzer. |
| `pinI2cSda`, `pinI2cScl` | yes | I²C drives both lines. |
| `pinDfTx` | yes | AWTRIX sends on it. |
| `pinBattery` | no | Analog input only. |
| `pinLdr` | no | Analog input only. |
| `pinDfRx` | no | AWTRIX receives on it. |
<!-- /only -->
<!-- only esp32-s3 -->
The ESP32-S3 has no input-only pins, so this rule never applies.
<!-- /only -->

### 5. ADC1 requirement

`pinBattery` and `pinLdr` must be ADC1 pins: GPIO <!-- only esp32 -->`32`–`39`<!-- /only --><!-- only esp32-s3 -->`1`–`10`<!-- /only -->. ADC2 pins
cannot be read while Wi-Fi is on.

<!-- only esp32 -->
```
pinBattery: must be ADC1 (GPIO 32-39)
pinLdr: must be ADC1 (GPIO 32-39)
```
<!-- /only -->
<!-- only esp32-s3 -->
```
pinBattery: must be ADC1 (GPIO 1-10)
pinLdr: must be ADC1 (GPIO 1-10)
```
<!-- /only -->

### 6. No duplicates

Each pin that is not `-1` may be used only once.

```
duplicate pin <n> (<fieldA>, <fieldB>)
```

If one of the two fields is `pinMatrix`, the message also tells you how to fix it:

```
duplicate pin 21 (pinMatrix, pinI2cSda) - move both pins in one request
```

Both changes have to be in the same request, because either one on its own still collides.
<!-- only esp32 -->Coming from AWTRIX 2, set `pinI2cSda` to `17` together with `pinMatrix` `21`.<!-- /only -->

Any number of fields can be `-1` at the same time. A pin that is set is always checked, even when
the part behind it is switched off. <!-- only esp32 -->For example, `pinDfTx: 34` is rejected as input-only even
with `dfplayer: false`, and `pinBuzzer: 23` collides with the default `pinDfRx`.<!-- /only --><!-- only esp32-s3 -->For example, `pinBuzzer: 17` collides with the default
`pinDfRx`, even with `dfplayer: false`.<!-- /only --> If you have no DFPlayer, set both DF pins to `-1`.

## Reading and writing the map

The pin fields are normal keys of the [system configuration](system.md).

### Read the current map

```bash
curl http://<awtrix-ip>/api/v1/system
```

### Write a complete map

Send the whole map in one request. Then every rule is checked against the values you want, not a
mix of new and stored values.

<!-- only esp32 -->
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
<!-- /only -->
<!-- only esp32-s3 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{
        "pinMatrix": 21,
        "pinBtnLeft": 11,
        "pinBtnSelect": 12,
        "pinBtnRight": 13,
        "pinBattery": -1,
        "pinLdr": 2,
        "pinBuzzer": 7,
        "pinI2cSda": 8,
        "pinI2cScl": 9,
        "pinDfRx": -1,
        "pinDfTx": -1,
        "pinI2sBclk": 5,
        "pinI2sLrclk": 6,
        "pinI2sDout": 4,
        "pinI2sMclk": -1,
        "pinAmpEnable": -1,
        "dfplayer": false
      }'
```
<!-- /only -->

On success AWTRIX answers `200` with the new configuration. Then restart it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

Always send `Content-Type: application/json`. Without it, `curl -d` marks the body as a form, and
AWTRIX refuses the `PUT` with `415` ([Content-Type](conventions.md#content-type-is-mandatory)).

### A rejected write

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"pinBattery": 25}'
```

<!-- only esp32 -->
```json
{"error":{"code":"invalidPinConfig","message":"pinBattery: must be ADC1 (GPIO 32-39)"}}
```
<!-- /only -->
<!-- only esp32-s3 -->
```json
{"error":{"code":"invalidPinConfig","message":"pinBattery: must be ADC1 (GPIO 1-10)"}}
```
<!-- /only -->

Nothing was stored. AWTRIX stays exactly as it was, including any other fields in the same
request.

All messages a rejected pin map can return, and the other answers of this route, are listed in
[Errors – GPIO validation](errors.md#gpio-validation-invalidpinconfig).

## Recovery from a bad map

A pin map cannot make AWTRIX unusable.

AWTRIX checks the stored map again at every start. If it is not valid, for example a map saved on
a board with a different chip, AWTRIX starts with the default pins. The web UI is reachable and
you can fix the map. The stored map stays as it is, so AWTRIX keeps using the defaults until you
save a valid map.

A map can be valid but wrong for your hardware. Then AWTRIX starts, but the panel or the buttons
do nothing. If you cannot reach the web UI, a [factory reset](system.md#persistence-and-resets)
restores the default pins together with all other settings.

## Panel layout

A panel you built yourself also needs a description: the width of one panel, how many panels the
cable runs through, the corner where the data enters, whether the LED strip runs along rows or
columns, and whether every second row runs back the other way.

Every panel is 8 pixels high and `panelWidth × panels` is 32–128. The default is 32×8.

All panel keys, their ranges and the values for common builds are in
[Panel and orientation](system.md#panel-and-orientation). They are set with `PUT /api/v1/system`.
A new total width needs a restart; wiring and orientation changes apply at once.

## Wiring your own board

Work through this list in order:

1. **Read your chip's rules**: run `curl http://<awtrix-ip>/api/v1/capabilities` and look at
   `gpio`.
2. **Pick the matrix pin first**, from the LED matrix list. It limits your layout more than any
   other pin.
3. **Battery and LDR need ADC1** (<!-- only esp32 -->`32`–`39`<!-- /only --><!-- only esp32-s3 -->`1`–`10`<!-- /only -->). If you have
   only one ADC1 pin free, use it for the LDR: without it, auto-brightness is not available.
   Without a battery pin, only the battery display is missing.
4. **Buttons need a pull-up**<!-- only esp32 -->, so they cannot use `34`–`39`<!-- /only -->. Wire each button between
   the pin and ground. AWTRIX turns on the internal pull-up; LOW means pressed.<!-- only esp32-s3 --> Any existing
   pin works.<!-- /only --> If the select button should wake AWTRIX from deep sleep, pick a pin
   from the wake row of the [chip table](#rules-come-from-the-chip).
5. **Avoid reserved pins**: <!-- only esp32 -->`6`–`11` (flash).<!-- /only --><!-- only esp32-s3 -->`26`–`37` (flash and PSRAM), `19`–`20`
   (USB) and `43`–`44` (console).<!-- /only -->
6. **Set parts you do not have to `-1`** instead of leaving a pin that looks right.
7. **Send the whole map at once** and restart.
8. **Calibrate the analog inputs.** The pin map only says *where* to read. What the readings
   *mean* is set separately: `batteryDividerRatio` for the battery divider, and `ldrFactor`,
   `ldrGamma` and `ldrOnGround` for the light sensor. <!-- only esp32 -->The defaults match the Ulanzi wiring and
   will be wrong for your divider.<!-- /only --><!-- only esp32-s3 -->The defaults are general values and may be
   wrong for your divider.<!-- /only --> See [Brightness & sensors](../guides/brightness.md) and
   [Power & battery](../guides/power.md).

If your panel is mounted differently, you may also need `rotate` or `mirror` under
[Panel and orientation](system.md#panel-and-orientation), and `swapButtons` under
[Buttons](system.md#buttons).

## Sensor bus

AWTRIX finds I²C sensors by itself at startup. You only set `pinI2cSda` and `pinI2cScl`.

AWTRIX looks for these sensors in this order: **BME280** (`0x76`, then `0x77`), **BMP280** (same
two addresses), **HTU21DF** (its fixed address), **SHT31** (`0x44`). It uses the first one that
answers. If a BME280 and an SHT31 share the bus, the SHT31 is ignored.

| Sensor | Temperature | Humidity | Air pressure |
|---|---|---|---|
| BME280 | yes | yes | yes |
| BMP280 | yes | – | yes |
| HTU21DF | yes | yes | – |
| SHT31 | yes | yes | – |

Values the sensor does not measure are left out of device state. With no sensor, all of them
are left out, and `tempOffset` / `humOffset` have no effect.

## The web UI's pin dropdowns

Each pin field in the web UI is a dropdown. It lists only the pins your chip can use for that
part:

- analog pins for the battery and light sensor,
- output pins for the buttons, buzzer, I²C and the DFPlayer TX line,
- the LED matrix list for the matrix,
- **not connected** wherever `-1` is allowed.

Pins the chip needs for flash<!-- only esp32-s3 -->, PSRAM, USB or its console<!-- /only --> are never offered. If the stored map
contains a pin the list cannot offer – for example a map saved on a different board – that pin
is shown as its own entry marked as the stored value, so it is not overwritten by accident.

The API checks the same rules, so a direct API call cannot get around them.

## Related

- [System configuration](system.md) – Wi-Fi, MQTT, time, calibration and every other key of
  `PUT /api/v1/system`.
- [DIY build](../advanced/diy-build.md) – parts, power and wiring for your own clock.
- [Errors – GPIO validation](errors.md#gpio-validation-invalidpinconfig)
