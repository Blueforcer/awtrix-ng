# Power & battery

This guide covers the four power-side jobs: **read the battery**, **make the percentage tell the
truth on your hardware**, **blank the panel without shutting anything down**, and **deep-sleep the
whole board for a while**.

## Check the battery

Everything battery-related is in the device state:

```bash
curl http://<awtrix-ip>/api/v1/device
```

The three battery keys in the response:

```json
{
  "batteryPinMillivolts": 2290,
  "batteryVoltage": 4.1,
  "batteryPercent": 88
}
```

They build on each other:

1. `batteryPinMillivolts` - what the ESP32 measured **at its own pin**, after the board's resistor
   divider has already dropped the voltage. This is not your battery voltage.
2. `batteryVoltage` - the reconstructed **cell** voltage: `batteryPinMillivolts / 1000 ×
   batteryDividerRatio`.
3. `batteryPercent` - state of charge, looked up from `batteryVoltage` on a Li-Ion discharge
   curve.

All three refresh every 2 seconds, and all three are present **only** when `pinBattery` is `>= 0`.
The ESP32 default is GPIO 34, so a board on the default wiring has them. Set `pinBattery: -1` (an AWTRIX 2
mainboard, for example) and the keys disappear from the response entirely - they are not reported
as `0` or `null`. The built-in **Battery** app also drops out of the rotation on such a board.

Full per-field table: [Device state → Battery fields](../reference/device.md#battery-fields-conditional).

## How the percentage is derived

There is **no fuel gauge** in the hardware. The percentage is inferred from `batteryVoltage` alone,
read off a resting Li-Ion discharge curve:

| Cell voltage | Reported |
|---|---|
| ≥ 4.20 V | 100 % |
| 3.84 V | 50 % |
| 3.73 V | 20 % |
| ≤ 3.27 V | 0 % |

Voltages between the listed points are interpolated; anything past either end stops at 100 % or
0 % rather than running off the scale.

Two consequences:

- **The percentage sags under load and recovers.** The curve describes a *resting* cell, and the
  panel is a bursty load.
- **You cannot get a runtime estimate from it.** Time-to-empty needs a fuel gauge, and this
  hardware has none.

Between 90 % and 20 % the cell only moves about 0.4 V, so a small error in the voltage moves the
percentage a long way. That is what the next section is for.

## Calibrate the divider ratio

If `batteryVoltage` looks wrong, **do not try to patch the percentage** - fix the ratio it is
derived from. `batteryDividerRatio` is `V_cell / V_pin` for your board's resistor divider.

You do not need a meter. A full Li-Ion cell rests at ~4.2 V, and that is the one voltage you know
without an instrument.

**Step 1** - charge AWTRIX fully, then read the pin side:

```bash
curl http://<awtrix-ip>/api/v1/device
```

**Step 2** - compute `ratio = 4.2 / (batteryPinMillivolts / 1000)`.

**Step 3** - write it:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"batteryDividerRatio":1.79}'
```

It applies live on the next 2-second sensor tick - no reboot.

The default is **1.79**, calibrated for the Ulanzi TC001. If yours is a stock TC001 and the
reading is far off, suspect the cell before the ratio. Accepted values run from `0.1` to `10`;
anything outside that range, including `0` or a negative number, is rejected with a 422.

Field table: [System configuration → Sensor calibration](../reference/system.md#sensor-calibration).

## Get told when the battery is low

On a board with a battery pin, `GET /api/v1/device` always carries a `lowBattery` boolean. Point
`lowBatteryThreshold` at a percentage and the flag reads `true` once `batteryPercent` drops below
that level (strictly less than, not inclusive):

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"lowBatteryThreshold": 15}'
```

```json
{
  "batteryPercent": 12,
  "lowBattery": true
}
```

The default is **`0`, which turns the signal off** and leaves `lowBattery` permanently `false`. The
flag tracks the same 2-second percentage as the other battery keys, and like them it disappears
from the response on a board with `pinBattery: -1`. In Home Assistant the same signal shows up as a
**Low battery** binary sensor (below).

Field table: [System configuration → Sensor calibration](../reference/system.md#sensor-calibration).

## Turn the matrix off and on

This is the light switch, not the power switch. The LEDs go black; the ESP32, Wi-Fi, MQTT and the
app loop all keep running, and AWTRIX stays fully reachable over HTTP.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H "Content-Type: application/json" \
  -d '{"power":false}'
```

Back on:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H "Content-Type: application/json" \
  -d '{"power":true}'
```

Read the current state from `GET /api/v1/display` (`power`) or `GET /api/v1/device`
(`matrixPower`) - both are booleans that are `false` while the panel is blanked.

Three things that interact with it:

- **From AWTRIX itself:** a **double-press of the select button** (the middle one) toggles the
  matrix on and off. It is the only way to wake the display without the network, and it is skipped
  while `blockNavigation` is on.
- **Wakeup notifications punch through.** A notification sent with `"wakeup": true` renders even
  while the matrix is powered off, for as long as it is the active notification. See
  [App & notification payload → Notification-only keys](../reference/payload.md#notification-only-keys).
- **Brightness is unaffected.** Blanking does not touch your brightness or auto-brightness
  settings; it just stops drawing. For brightness itself see
  [Brightness & sensors](brightness.md).

Full route details: [HTTP API → Display](../reference/http.md#display).

## Deep sleep

Deep sleep powers the ESP32 down for a fixed duration. Unlike blanking the matrix, AWTRIX is
**gone** while it sleeps: no HTTP, no MQTT, no apps.

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/sleep \
  -H "Content-Type: application/json" \
  -d '{"durationMs":60000}'
```

`durationMs` is required and must be a positive integer in milliseconds. There is no value that
means "sleep forever" or "sleep until I say so" - `0` is rejected with a 422. A valid request
answers `200 {"ok":true}` first and the board goes down only after that response has been written,
so your client gets its confirmation and then the connection drops. Full route details, including
every status code: [HTTP API → POST /api/v1/device/sleep](../reference/http.md#post-apiv1devicesleep).

!!! warning "A sleeping board is off the network"
    Nothing on the network reaches AWTRIX while it sleeps and there is no cancel, so without
    physical access a one-hour sleep means an hour of silence. Two wake sources are armed: the **timer** and
    the **select button**. The button only works when `pinBtnSelect` sits on a wake-capable GPIO,
    which the default wiring uses; move it elsewhere and the timer is the only way back. See
    [GPIO & boards → The pin map](../reference/gpio.md#the-pin-map).

The matrix is blanked just before the board goes down. On wake the ESP32 **boots fresh** - the same
sequence as a power cycle, so it re-reads its configuration, reconnects Wi-Fi, and the matrix comes
back on (a blanked panel is a runtime state and does not survive).

The same command exists over MQTT as the `device/sleep` op - see
[MQTT topics → Command topics](../reference/mqtt.md#command-topics).

## Battery in Home Assistant

When the board has a battery pin, the Home Assistant integration exposes three entities
automatically:

| Entity | Device class | Unit | Source |
|---|---|---|---|
| **Battery** | `battery` | % | `batteryPercent` |
| **Battery voltage** | `voltage` | V | `batteryVoltage` |
| **Low battery** | `battery` | - | `lowBattery` |

The **Low battery** binary sensor only trips once you set a non-zero `lowBatteryThreshold` (see
[Get told when the battery is low](#get-told-when-the-battery-is-low)). All three disappear on a
board with `pinBattery: -1`. The pin-side `batteryPinMillivolts` is not exposed as an HA entity - read
it from `GET /api/v1/device` when calibrating.

See [Home Assistant](home-assistant.md).

## Related

- [Device state](../reference/device.md#battery-fields-conditional) - every field in `GET /api/v1/device`
- [System configuration → Sensor calibration](../reference/system.md#sensor-calibration) - `batteryDividerRatio`
- [GPIO & boards → The pin map](../reference/gpio.md#the-pin-map) - `pinBattery`, and why it must be an ADC1 pin
- [Brightness & sensors](brightness.md) - the light sensor and auto-brightness
- [HTTP API → Display](../reference/http.md#display) - the matrix power route
