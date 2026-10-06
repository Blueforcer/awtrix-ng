# Power & battery

<!-- only esp32 esp32-s3 -->
This page shows how to read the battery, get a low-battery warning, switch the display off and on,
and put the clock to sleep for a while.
<!-- /only -->
<!-- only tc002 -->
This page shows how to read the battery, get a low-battery warning, and switch the display off and
on.
<!-- /only -->

## How it behaves

Switching the display off only darkens the LEDs. Wi-Fi, MQTT and the apps keep running, and AWTRIX
stays reachable over HTTP.
<!-- only esp32 esp32-s3 -->
Deep sleep is different: it switches the whole clock off for a set time, and nothing reaches it
until it wakes. The battery percentage is worked out from the battery voltage alone, so it is an
estimate ([How the percentage is derived](#how-the-percentage-is-derived)).
<!-- /only -->
The low-battery warning stays off until you set a threshold.

## Check the battery

All battery values are in the device state:

```bash
curl http://<awtrix-ip>/api/v1/device
```

The battery part of the answer:

<!-- only esp32 esp32-s3 -->
```json
{
  "batteryPinMillivolts": 2290,
  "batteryVoltage": 4.1,
  "batteryPercent": 88
}
```

- `batteryPercent` is how full the battery is.
- `batteryVoltage` is the battery voltage.
- `batteryPinMillivolts` is the voltage the ESP32 measures at its pin. This is lower than the
  battery voltage. You only need it to [calibrate the reading](#calibrate-the-divider-ratio).

The values update every 2 seconds.

The battery values are only there when a battery pin is set (`pinBattery` is `0` or higher).
<!-- /only -->
<!-- only esp32 -->
The default is GPIO 34, so a board with the default wiring has them. With `pinBattery: -1` (for
example an AWTRIX 2 mainboard) the keys are left out of the answer. They are not shown as `0` or
`null`, and the built-in **Battery** app leaves the rotation.
<!-- /only -->
<!-- only esp32-s3 -->
The default is GPIO 1, so a board with the default wiring has them. With `pinBattery: -1` the keys
are left out of the answer. They are not shown as `0` or `null`, and the built-in **Battery** app
leaves the rotation.
<!-- /only -->
<!-- only tc002 -->
```json
{
  "batteryVoltage": 4.1,
  "batteryPercent": 88
}
```

- `batteryPercent` is how full the battery is.
- `batteryVoltage` is the battery voltage.

The values come from the clock's battery controller. The
[Status app](device-controls.md#the-status-app) shows the battery level and whether USB power is
connected.
<!-- /only -->

Every field: [Device state → Battery fields](../reference/device.md#battery-fields-conditional).

## Get told when the battery is low

<!-- only esp32 esp32-s3 -->
On a board with a battery pin, `GET /api/v1/device` always has a `lowBattery` value.
<!-- /only -->
<!-- only tc002 -->
`GET /api/v1/device` always has a `lowBattery` value.
<!-- /only -->
`lowBattery` becomes `true` when `batteryPercent` is below `lowBatteryThreshold` (below, not
equal). Set the threshold to a percentage under **Low-battery threshold** in
**System → Brightness & sensors** of the web UI, or send:

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

- The default is **`0`, which turns the warning off**: `lowBattery` stays `false`.
<!-- only esp32 esp32-s3 -->
- `lowBattery` updates every 2 seconds, like the other battery values, and is left out on a board
  with `pinBattery: -1`.
<!-- /only -->
<!-- only tc002 -->
- Below the threshold, the battery in the [Status app](device-controls.md#the-status-app) blinks,
  unless USB power is connected.
<!-- /only -->
- In Home Assistant the same warning is the **Low battery** sensor. See
  [Battery in Home Assistant](#battery-in-home-assistant).

## Turn the display off and on {#turn-the-matrix-off-and-on}

This switches the LEDs off, not the clock. Your brightness<!-- only esp32 esp32-s3 --> and
auto-brightness<!-- /only --> settings stay as they are.

- **On the clock**, press the select (middle) button twice quickly. This is the only way to switch
  the display back on without the network.
- **In the web UI**, use the **Power** switch on the **Dashboard**.

### With the API

Off:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H "Content-Type: application/json" \
  -d '{"power":false}'
```

On:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H "Content-Type: application/json" \
  -d '{"power":true}'
```

To see the current state, read `power` in `GET /api/v1/display` or `matrixPower` in
`GET /api/v1/device`. Both are `false` while the display is off.

<!-- only esp32 esp32-s3 -->
## Deep sleep

Deep sleep switches the clock off for a fixed time. Unlike switching the display off, AWTRIX is
**unreachable** while it sleeps: no HTTP, no MQTT, no apps.

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/sleep \
  -H "Content-Type: application/json" \
  -d '{"durationMs":60000}'
```

- `durationMs` is required: the sleep time in milliseconds, a whole number above 0. There is no
  value for "sleep forever". `0` is refused with a 422.
- The clock answers `200 {"ok":true}` first and then goes to sleep, so your request gets its answer
  before the connection drops.
- All status codes: [HTTP API → POST /api/v1/device/sleep](../reference/http.md#post-apiv1devicesleep).

Two things wake the clock: the **timer** and the **select button**. The button only works when
`pinBtnSelect` is on a GPIO that can wake the ESP32, which the default wiring is. If you move it,
only the timer wakes the clock. See [GPIO & boards → The pin map](../reference/gpio.md#the-pin-map).

The display goes dark just before the clock sleeps. When it wakes, the ESP32 **starts fresh**, just
like after switching the power off and on: it loads its settings, connects to Wi-Fi, and the
display is on again, even if you had switched it off before.

Over MQTT the same command is the `device/sleep` op. See
[MQTT topics → Command topics](../reference/mqtt.md#command-topics).
<!-- /only -->

## Battery in Home Assistant

<!-- only esp32 esp32-s3 -->
When the board has a battery pin, Home Assistant gets three entities automatically:
<!-- /only -->
<!-- only tc002 -->
Home Assistant gets four battery entities automatically:
<!-- /only -->

| Entity | Device class | Unit | Source |
|---|---|---|---|
| **Battery** | `battery` | % | `batteryPercent` |
| **Battery voltage** | `voltage` | V | `batteryVoltage` |
| **Low battery** | `battery` | - | `lowBattery` |
<!-- only tc002 -->
| **Charging** | `battery_charging` | - | `usbPower` |
<!-- /only -->

- **Low battery** only turns on once you set a `lowBatteryThreshold` above 0 (see
  [Get told when the battery is low](#get-told-when-the-battery-is-low)).
<!-- only esp32 esp32-s3 -->
- All three are missing on a board with `pinBattery: -1`.
- `batteryPinMillivolts` is not an entity. Read it from `GET /api/v1/device` when you calibrate.
<!-- /only -->

See [Home Assistant](home-assistant.md).

<!-- only esp32 esp32-s3 -->
## How the percentage is derived

The clock cannot measure how much charge is left. It works out the percentage from the battery
voltage alone, using the typical voltage of a Li-Ion cell at rest:

| Battery voltage | Shown |
|---|---|
| 4.20 V or more | 100 % |
| 3.84 V | 50 % |
| 3.73 V | 20 % |
| 3.27 V or less | 0 % |

Between these points the value is interpolated. It never goes above 100 % or below 0 %.

What this means for you:

- **The percentage drops while the display is busy and comes back afterwards.** The table is for a
  battery at rest, and the display draws power in bursts.
- **There is no "time left" estimate.** The voltage alone cannot tell you that.
- **Small voltage errors matter.** Between 90 % and 20 % the voltage only changes by about 0.4 V.
  If `batteryVoltage` is a little off, the percentage is off a lot. Calibrate it as shown below.

## Calibrate the divider ratio

This is for boards with a battery pin.

The board lowers the battery voltage before the ESP32 measures it, and `batteryDividerRatio` says
by how much: battery voltage divided by pin voltage. You do not need a meter. A full Li-Ion battery
at rest has about 4.2 V, and that is enough.

1. Charge AWTRIX fully. Then read `batteryPinMillivolts`:

    ```bash
    curl http://<awtrix-ip>/api/v1/device
    ```

2. Calculate `ratio = 4.2 / (batteryPinMillivolts / 1000)`.
3. Save it as **Battery divider** under **System → Brightness & sensors** in the web UI, or send:

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/system \
      -H "Content-Type: application/json" \
      -d '{"batteryDividerRatio":1.79}'
    ```

The new ratio applies within 2 seconds, without a reboot.

<!-- only esp32 -->
- The default is **1.79**, which fits the Ulanzi TC001. On a stock TC001 with a reading far off,
  suspect the battery before the ratio.
<!-- /only -->
<!-- only esp32-s3 -->
- The default is **1.79**.
<!-- /only -->
- Allowed values are `0.1` to `10`. Anything else, including `0` or a negative number, is refused
  with a 422.
<!-- /only -->

## Good to know

- **A double press of select does nothing while Block buttons is on.** Switch the display with the
  **Power** switch or a request instead.
- **Notifications are not shown while the display is off.** Add `"wakeup": true` to show one
  anyway. See [Wake a dark display](notifications.md#waking-a-dark-panel).
<!-- only esp32 esp32-s3 -->
- **A sleeping clock cannot be reached or woken over the network, and you cannot cancel the
  sleep.** Without access to the clock, one hour of sleep means one hour of silence, so try short
  times first.
- **If `batteryVoltage` looks wrong, correct `batteryDividerRatio`, not the percentage.** See
  [Calibrate the divider ratio](#calibrate-the-divider-ratio).
<!-- /only -->
<!-- only tc002 -->
- **There is no sleep timer.** `POST /api/v1/device/sleep` does not put this clock to sleep. It only
  restarts AWTRIX NG. To save power, switch the display off.
<!-- /only -->

## Details

- [Device state → Battery fields](../reference/device.md#battery-fields-conditional): every
  battery field in `GET /api/v1/device`
<!-- only esp32 esp32-s3 -->
- [System configuration → Sensor calibration](../reference/system.md#sensor-calibration):
  `batteryDividerRatio` and `lowBatteryThreshold`
- [GPIO & boards → The pin map](../reference/gpio.md#the-pin-map): `pinBattery`, and why it must be
  an ADC1 pin
<!-- /only -->
<!-- only tc002 -->
- [System configuration → Battery warning](../reference/system.md#sensor-calibration):
  `lowBatteryThreshold`
<!-- /only -->
- [HTTP API → Display](../reference/http.md#display): the display power route

## Related

<!-- only esp32 esp32-s3 -->
- [Brightness & sensors](brightness.md): the light sensor and auto-brightness
<!-- /only -->
<!-- only tc002 -->
- [Brightness](brightness.md): set the brightness
<!-- /only -->
- [Notifications](notifications.md): a notification that wakes the dark display
