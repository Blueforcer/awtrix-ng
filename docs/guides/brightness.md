# Brightness<!-- only esp32 esp32-s3 --> & sensors<!-- /only -->

<!-- only esp32 esp32-s3 -->
This page shows how to make the display follow the light in the room, and how to read and correct
the temperature, humidity and pressure sensor.

## How it behaves

The display has one brightness, a level from 0 to 255. With auto-brightness off, the display uses
the level you set. With auto-brightness on, the light sensor decides: the display stays between
`minBrightness` and `maxBrightness`, stays dim until the room is really bright, and moves to a new
level slowly. A clock without a light sensor always uses the level you set. The temperature,
humidity and pressure sensor needs no setup: the clock finds it when it starts.

## Make the display follow the room {#make-the-panel-follow-the-room}

On the **Dashboard** of the web UI, switch on **Auto brightness**. Or send:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness": true}'
```

For most people that is all. The display follows the built-in light sensor (LDR) between
`minBrightness` (10) and `maxBrightness` (220). The change applies at once, without a reboot.

## Set a fixed brightness

On the **Dashboard**, switch off **Auto brightness** and move the **Brightness** slider. Or turn
auto-brightness off and set a level from 0 to 255 with a request:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness": false, "brightness": 120}'
```

The change applies at once.
<!-- /only -->
<!-- only tc002 -->
This page shows how to set the brightness of the display.

## Set the brightness

Turn the [knob](device-controls.md#the-knob), or move the **Brightness** slider on the
**Dashboard** of the web UI. Or set a level from 0 to 255 with a request:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"brightness": 120}'
```

The change applies at once.

## Good to know

- **`brightness` is a level from 0 to 255, not a percent.** The knob shows percent: 50 % on the
  knob is `brightness` 128.
- **The clock has no light sensor, so `autoBrightness` changes nothing.** It is accepted and saved,
  and the display always uses `brightness`.
<!-- /only -->

<!-- only esp32 esp32-s3 -->
## What auto-brightness does

Two things decide the result, and you can change both.

**A range.** The display never goes below `minBrightness` or above `maxBrightness`, whatever the
room does.

**A curve.** Within that range the display stays dim through the lower half of the light range and
only gets bright when the room is really bright. So a lamp at dusk does not make the display glare.
`ldrGamma` sets how strong this curve is.

With the defaults (`minBrightness` 10, `maxBrightness` 220, `ldrGamma` 2.2):

| Room `lightLevel` | Display brightness |
|---|---|
| 0 % (dark) | 10 |
| 25 % | 20 |
| 50 % | 56 |
| 100 % (full light) | 220 |

A half-lit room does not give a half-bright display. With `ldrGamma: 1.0` the same 50 % would give
115.

**Smooth changes.** The display moves to a new level slowly, over `brightnessSmoothing` (default
`10000` ms). Dawn or a lamp being switched on still comes through, just smoothly, while a camera
flash or someone walking past the sensor hardly shows. If a changing light in the room makes the
display pulse, for example a TV, raise `brightnessSmoothing`. Set it to `0` to follow the light at once.

Only the display is smoothed. `lightLevel` and `ldrRaw` are not, so an automation that reads the
sensor sees what the room really did. A `brightness` you set by hand is used as it is, with no
smoothing and no curve.

## Check what the sensor sees

Before you tune anything, look at the numbers. `GET /api/v1/device` shows the sensor reading, the
light level, and the brightness the display really uses:

```bash
curl http://<awtrix-ip>/api/v1/device
```

```json
{
  "brightness": 56,
  "lightLevel": 50.0,
  "ldrRaw": 2048
}
```

- `ldrRaw` is the sensor's own reading, `0` (dark) to `4095` (full scale). If it does not change
  when you cover the sensor, the problem is the wiring or `pinLdr`, not the tuning.
- `lightLevel` is the light level in percent, after `ldrFactor` and `ldrOnGround`. It is a
  **relative 0–100 value**, not lux. Use it for comparisons ("is it brighter than before?") and
  for thresholds you tested in your own room, not next to readings from a light meter.
- `brightness` is the brightness the display **really uses**: the auto-brightness result while
  `autoBrightness` is on and there is a light sensor, otherwise your setting. It is not simply what
  you wrote.

Cover the sensor with a finger, read again, and watch all three change. `lightLevel` and `ldrRaw`
are only there when `pinLdr` is set. Every field is in
[Device state](../reference/device.md#light-sensor-fields-conditional).

## Tune it

In the web UI, open **System → Brightness & sensors**. **Min brightness**, **Max brightness**,
**LDR factor**, **LDR gamma**, **LDR on GND** and **Brightness smoothing** are there. Or send the
fields to `PUT /api/v1/system`. They apply at once, without a reboot:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"minBrightness": 5, "maxBrightness": 180, "ldrGamma": 2.2}'
```

What to change:

| Problem | Change |
|---|---|
| Too dark at night | raise `minBrightness` |
| Too bright at night | lower `minBrightness`. `0` turns the display dark in a dark room |
| Too bright by day | lower `maxBrightness` |
| Gets bright too early at dusk | raise `ldrGamma` (3.0 is a slow curve) |
| Reacts too little | lower `ldrGamma` toward `1.0`, which follows the room in a straight line |
| Never reaches full brightness | raise `ldrFactor`. Check `lightLevel` first: if a bright room reads 60 %, your sensor needs a higher factor |
| Backwards: bright room, dim display | your sensor is wired to ground. Set `{"ldrOnGround": true}` to turn the reading around |

`minBrightness` must not be higher than `maxBrightness`, or the request is refused. Equal values
are fine and keep the display at one fixed brightness. All fields with types, ranges, defaults and
units: [System configuration › Auto-brightness](../reference/system.md#auto-brightness).

### `ldrFactor` and `ldrGamma` do different jobs

People often mix these two up.

- **`ldrFactor` calibrates the sensor.** It sets what counts as "full light" on *your* hardware.
  On a DIY board with a different sensor circuit, `lightLevel` may stop at 60 %, so the display never
  gets bright. Raise `ldrFactor` until a well-lit room reads close to 100. `lightLevel` never goes
  above 100 %, however large the factor is.
- **`ldrGamma` sets how bright the display is** *at a given light level*. It never changes
  `lightLevel` itself.

Neither is the display's `gamma` setting. That one corrects the LED colors and is in
[Settings › Color](../reference/settings.md#panel).

### Light sensor pin

<!-- only esp32 -->
`pinLdr` defaults to GPIO 35. You set it in the web UI like every other pin. It must be a pin that
can measure a voltage while Wi-Fi is on, which is GPIO 32–39. Other pins are refused.
<!-- /only -->
<!-- only esp32-s3 -->
`pinLdr` defaults to GPIO 2. You set it in the web UI like every other pin. It must be a pin that
can measure a voltage while Wi-Fi is on, which is GPIO 1–10. Other pins are refused.
<!-- /only -->
A change applies after a reboot. See
[GPIO & boards › The pin map](../reference/gpio.md#the-pin-map) and
[the ADC1 rule](../reference/gpio.md#5-adc1-requirement).

## Temperature, humidity & pressure

There is no sensor type to set and nothing to install. When the clock starts, it uses the first
sensor it finds. Four sensors are supported, checked in this order:

| Sensor | Address(es) | Temperature | Humidity | Pressure |
|---|---|---|---|---|
| BME280 | `0x76`, `0x77` | yes | yes | yes |
| BMP280 | `0x76`, `0x77` | yes | **no** | yes |
| HTU21DF | `0x40` | yes | yes | no |
| SHT31 | `0x44` | yes | yes | no |

The first match wins. This matters for one pair: the BME280 and the BMP280 use the same addresses,
so on a board with both you get the BME280.

A value your sensor cannot measure is **left out** of `GET /api/v1/device`, not shown as zero. A
BMP280 has no `humidity`. The sensors without pressure have no `pressureHpa`. Home Assistant gets
no entity for it either, and the **Humidity** app leaves the rotation on a board whose sensor
cannot measure humidity (see [Built-in apps](pushed-apps.md#built-in-apps)).

If no sensor answers, or if `pinI2cSda` or `pinI2cScl` is `-1` (sensor bus off), `temperature`,
`humidity` and `pressureHpa` are all left out. Every field:
[Device state › Environment fields](../reference/device.md#environment-fields-conditional).

### Correct a sensor that reads too high

A sensor in a warm case next to the LEDs reads too high. Set an offset to correct it. The offset is
added to the reading before it is shown in device state and in the built-in apps.

In the web UI, set **Temperature offset** and **Humidity offset** under
**System → Brightness & sensors**. Or send:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"tempOffset": -2.5, "humOffset": 0.0}'
```

<!-- only esp32 -->
- `tempOffset` is in °C. The default is `-9.0`, because on a stock Ulanzi TC001 the sensor sits
  next to the LEDs and reads that much too high.
<!-- /only -->
<!-- only esp32-s3 -->
- `tempOffset` is in °C. The default is `-9.0`, for a sensor that sits next to the LEDs. Calibrate
  it for your build as shown below.
<!-- /only -->
- `humOffset` is in %. The default is `0.0`.
- Both apply within a few seconds, without a reboot. They only do something when a sensor is present.

To calibrate:

1. Put a reference thermometer next to AWTRIX.
2. Wait several minutes so both settle.
3. Set `tempOffset` to `reference − reported`.

Ranges: [System configuration › Sensor calibration](../reference/system.md#sensor-calibration).

### Sensor bus pins

<!-- only esp32 -->
`pinI2cSda` (GPIO 21) and `pinI2cScl` (GPIO 22) are the defaults.
<!-- /only -->
<!-- only esp32-s3 -->
`pinI2cSda` (GPIO 8) and `pinI2cScl` (GPIO 9) are the defaults.
<!-- /only -->
You set them in the web UI like every other pin. Setting either to `-1` turns the bus off, and
with it the sensor. Both need pins that can output. A change applies after a reboot. See
[GPIO & boards › Sensor bus](../reference/gpio.md#sensor-bus).

## How often values update

The light sensor is read ten times a second, so the brightness follows the room without a visible
delay. Single odd readings are ignored, so a real change takes a moment to arrive in full.

The temperature, humidity and pressure sensor is read every two seconds. These values are not
averaged: you get the reading plus your offsets.

## Good to know

- **`brightness` is a level from 0 to 255, not a percent.** `255` is the brightest, `128` about
  half of it.
- **While auto-brightness is on, the `brightness` you send is saved but not used.** The room light
  decides. Switch auto-brightness off to use your level.
- **Without a light sensor (`pinLdr` is `-1`), auto-brightness changes nothing.** The display always
  uses `brightness`, and the web UI hides the auto-brightness switch and its fields.
- **`PATCH /api/v1/settings` refuses the tuning fields as an `unknown field`.** They are device
  configuration, like the pins: send them to `PUT /api/v1/system`.
- **The temperature offset is always in Celsius, also when the clock shows Fahrenheit.** `-2.5`
  means −2.5 °C.
<!-- /only -->

## Details

- [Settings › Brightness](../reference/settings.md#brightness): `autoBrightness`, `brightness`
<!-- only esp32 esp32-s3 -->
- [System configuration › Auto-brightness](../reference/system.md#auto-brightness): `minBrightness`, `maxBrightness`, `ldrFactor`, `ldrGamma`, `ldrOnGround`, `brightnessSmoothing`
- [System configuration › Sensor calibration](../reference/system.md#sensor-calibration): `tempOffset`, `humOffset`
- [Device state](../reference/device.md): `lightLevel`, `ldrRaw`, `brightness`, `temperature`, `humidity`, `pressureHpa`, and how often each one updates
- [GPIO & boards](../reference/gpio.md): `pinLdr`, `pinI2cSda`, `pinI2cScl`
<!-- /only -->

## Related

<!-- only esp32 esp32-s3 -->
- [Power & battery](power.md): the battery pin, which has the same wiring rule as `pinLdr`
<!-- /only -->
<!-- only tc002 -->
- [The knob](device-controls.md#the-knob): brightness and volume on the clock
- [Power & battery](power.md): switch the display off and on
<!-- /only -->
