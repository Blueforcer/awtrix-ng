# Brightness & sensors

Your panel sits in a room whose light changes all day. This guide covers the two jobs that come
with that: making the display follow the room, and reading the temperature, humidity and pressure
sensor that shares the same board.

## Make the panel follow the room

One switch. Turn on auto-brightness:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness": true}'
```

That is the whole task for most people. The panel now follows the built-in light sensor (LDR)
between `minBrightness` (10) and `maxBrightness` (220). It applies instantly - no reboot.

To go back to a fixed brightness, turn the switch off and set a level. `brightness` is a raw
0–255 level, **not a percent**:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness": false, "brightness": 120}'
```

While `autoBrightness` is on, `brightness` is still accepted and still echoed back by `GET`, but
it is not used - the light level wins. Both keys live in
[Settings › Brightness](../reference/settings.md#brightness).

## What auto-brightness does

Two things shape the result, and both are yours to change.

**A range.** The panel never goes below `minBrightness` or above `maxBrightness`, whatever the
room does.

**A curve.** Within that range the panel stays dim through the lower half of the light range and
only ramps up once the room is genuinely bright, so a lamp at dusk does not produce a glaring
panel. `ldrGamma` decides how pronounced that bias is.

With the defaults (`minBrightness` 10, `maxBrightness` 220, `ldrGamma` 2.2):

| Room `lightLevel` | Panel brightness |
|---|---|
| 0 % (dark) | 10 |
| 25 % | 20 |
| 50 % | 56 |
| 100 % (full light) | 220 |

A half-lit room does not get a half-bright panel. At `ldrGamma: 1.0` the same 50 % would produce
115.

Changes are eased in rather than jumped to. The panel moves towards a new level over
`brightnessSmoothing` (default `10000` ms), so dawn or a lamp being switched on still comes
through - it just arrives smoothly - while a camera flash or someone walking past the sensor
barely registers. If a changing light source in the room (a television, typically) makes the panel
visibly breathe, raise `brightnessSmoothing`; set it to `0` to follow the light instantly.

Only the panel is smoothed. `lightLevel` and `ldrRaw` are not, so an automation reading the sensor
sees what the room actually did. A manually set `brightness` is neither smoothed nor curved - it
goes straight to the panel.

## Check what the sensor sees

Before you tune anything, look at the numbers. `GET /api/v1/device` reports what the sensor reads,
the light level derived from it, and the brightness the panel is actually running at:

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

- `ldrRaw` - the light sensor's own reading, `0` (dark) to `4095` (full scale). If it does not move
  when you cover the sensor, the problem is wiring or `pinLdr`, not tuning.
- `lightLevel` - the light level in percent, after `ldrFactor` and `ldrOnGround` have been applied.
  It is a **relative 0–100 measure**, not lux, so use it for comparisons - "is it brighter than it
  was", "does covering the sensor move the number" - and for thresholds you tuned against your own
  room, not beside readings from a calibrated meter.
- `brightness` - the **effective** panel brightness: the auto-brightness result while
  `autoBrightness` is on, your setting otherwise. It is not an echo of what you wrote.

Cover the sensor with a finger, poll again, and watch all three move. `lightLevel` and `ldrRaw`
appear only when `pinLdr` is set; field-by-field detail is in
[Device state](../reference/device.md#light-sensor-fields-conditional).

!!! warning "No light sensor + auto-brightness = a panel stuck at `minBrightness`"
    A board with no light sensor (`pinLdr` set to `-1`) looks exactly like a pitch-dark room to
    AWTRIX: with `autoBrightness` on the panel sits at `minBrightness` and never moves. On such a
    board, leave `autoBrightness` off and set `brightness` yourself. `GET /api/v1/device` omits
    `lightLevel` and `ldrRaw` there, and the web UI drops the light tile from the overview.

## Tune it

The auto-brightness fields are **device configuration**, not settings, so they go to
`PUT /api/v1/system`. They apply live - no reboot:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"minBrightness": 5, "maxBrightness": 180, "ldrGamma": 2.2}'
```

Common adjustments:

- **Too dark at night** → raise `minBrightness`. **Still too visible at night** → lower it; `0`
  is allowed and blanks the panel in a dark room.
- **Too bright by day** → lower `maxBrightness`.
- **Ramps up too eagerly at dusk** → raise `ldrGamma` (3.0 is a lazy curve). **Too sluggish** →
  lower it toward `1.0`, which follows the room in a straight line.
- **Never reaches maximum** → raise `ldrFactor`. Check `lightLevel` first - if a bright room reads
  60 %, that is your factor.
- **Backwards: bright room, dim panel** → your sensor is wired to ground. Set
  `{"ldrOnGround": true}` to invert the reading.

`minBrightness` must not exceed `maxBrightness`; an inverted pair is rejected, and equal values are
fine and hold the panel at a fixed brightness. Full table with types, ranges, defaults and units:
[System configuration › Auto-brightness](../reference/system.md#auto-brightness).

### `ldrFactor` and `ldrGamma` do different jobs

Mixing them up is the most common tuning mistake.

- **`ldrFactor` calibrates the sensor.** It decides what counts as "full light" on *your*
  hardware. A DIY board with a different light sensor circuit may never reach the top of the
  scale, so its `lightLevel` tops out at 60 % and the panel never goes bright - raise `ldrFactor`
  until a well-lit room reads near 100. `lightLevel` can never exceed 100 % no matter how large
  you make the factor.
- **`ldrGamma` is display policy.** It decides how bright the panel should be *at a given light
  level*. It never changes `lightLevel` itself.

Neither is the panel's own `gamma` setting, which is colour correction for the LEDs and lives in
[Settings › Panel](../reference/settings.md#panel).

### Where the LDR pin lives

`pinLdr` defaults to GPIO 35 (the ESP32 default wiring) and is set in the web UI like every other
pin - it is not a build option. It has to be a pin that can still measure a voltage while Wi-Fi
is on (GPIO 32–39 on ESP32, GPIO 1–10 on ESP32-S3); anything else is rejected. A change applies
after a reboot. See [GPIO & boards › The pin map](../reference/gpio.md#the-pin-map) and
[the ADC1 rule](../reference/gpio.md#5-adc1-requirement).

## Temperature, humidity & pressure

There is no sensor type to configure and nothing to install. At boot AWTRIX takes the first
sensor it recognises on the bus. Four are supported, and they are looked for in this order:

| Sensor | Address(es) | Temperature | Humidity | Pressure |
|---|---|---|---|---|
| BME280 | `0x76`, `0x77` | yes | yes | yes |
| BMP280 | `0x76`, `0x77` | yes | **no** | yes |
| HTU21DF | `0x40` | yes | yes | no |
| SHT31 | `0x44` | yes | yes | no |

The first match wins, which matters for one pair: the BME280 and the BMP280 answer at the same
addresses, so on a board carrying both you get the BME280.

A reading the detected sensor cannot take is **absent** from `GET /api/v1/device` rather than
reported as zero - a BMP280 has no `humidity`, and the humidity-only parts have no `pressureHpa`.
Home Assistant gets no entity for it either, and the **Humidity** app drops out of the rotation on
a board whose sensor cannot measure humidity (see [Built-in apps](pushed-apps.md#built-in-apps)).
If no sensor answers - or if `pinI2cSda`/`pinI2cScl` is `-1`, which disables the bus entirely -
`temperature`, `humidity` and `pressureHpa` are all absent. Field by field:
[Device state › Environment fields](../reference/device.md#environment-fields-conditional).

### Correct a sensor that reads high

A sensor sitting inside a warm enclosure next to an LED matrix reads high. That is what the
offsets are for - they are added to the sensor reading before it reaches device state and the
built-in apps:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"tempOffset": -2.5, "humOffset": 0.0}'
```

`tempOffset` is in °C and `humOffset` in %. `tempOffset` defaults to `-9.0` - the sensor sits next
to the LEDs and reads that much high on a stock Ulanzi TC001 - and `humOffset` to `0.0`. Both take
effect within a couple of seconds, no reboot, and an offset only does anything while a sensor is
actually present.

The offset is added to the sensor's Celsius reading, so `-2.5` always means −2.5 °C. The
`useCelsius` setting only changes how the Temperature app displays the result afterwards.

To calibrate: park a reference thermometer next to AWTRIX, let both settle for several
minutes, then set `tempOffset` to `reference − reported`. Ranges:
[System configuration › Sensor calibration](../reference/system.md#sensor-calibration).

### Where the I²C pins live

`pinI2cSda` (GPIO 21) and `pinI2cScl` (GPIO 22) are the ESP32 defaults and are set in the
web UI like every other pin. Setting either to `-1` disables the bus and therefore the sensor.
Both need output-capable pins, and a change applies after a reboot. See
[GPIO & boards › Sensor bus](../reference/gpio.md#sensor-bus).

## How often values update

The light sensor is read ten times a second, so brightness tracks the room without a visible lag -
you should not see the panel step up two seconds after you switch on a lamp. Single stray readings
are discarded before they count, which is why a genuine change takes a moment to fully land.

The temperature, humidity and pressure sensor is read every two seconds. Nothing is averaged
there; you get the reading plus your offsets.

## Trying it without hardware

The [simulator](../advanced/simulator.md) fakes both sensors, so you can tune the curve at your
desk. Its light sensor starts at `1200`. Push values in and watch `GET /api/v1/device` react:

```bash
# pitch dark
curl -X PUT http://localhost:8080/sim/sensors \
  -H "Content-Type: application/json" -d '{"ldrRaw": 0}'

# full light
curl -X PUT http://localhost:8080/sim/sensors \
  -H "Content-Type: application/json" -d '{"ldrRaw": 4095}'

# a warm, humid room
curl -X PUT http://localhost:8080/sim/sensors \
  -H "Content-Type: application/json" -d '{"temperature": 28.5, "humidity": 55}'
```

The simulator always reports a sensor as present, so the Temperature and Humidity apps are
always in the rotation there. Brightness changes show up in its terminal matrix; the web UI
preview draws the panel content only and is not dimmed by the brightness level.

## Related

- [Settings › Brightness](../reference/settings.md#brightness) - `autoBrightness`, `brightness`
- [System configuration › Auto-brightness](../reference/system.md#auto-brightness) - `minBrightness`, `maxBrightness`, `ldrFactor`, `ldrGamma`, `ldrOnGround`, `brightnessSmoothing`
- [System configuration › Sensor calibration](../reference/system.md#sensor-calibration) - `tempOffset`, `humOffset`
- [Device state](../reference/device.md) - `lightLevel`, `ldrRaw`, `brightness`, `temperature`, `humidity`, `pressureHpa`
- [GPIO & boards](../reference/gpio.md) - `pinLdr`, `pinI2cSda`, `pinI2cScl`
- [Power & battery](power.md) - the battery pin, which has the same wiring restriction as `pinLdr`
