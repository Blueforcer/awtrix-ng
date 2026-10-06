# Modbus in scripts

This page lets a script read measurements from an energy meter, inverter or other device that
speaks Modbus TCP on your network, and show them on AWTRIX. It assumes you know how a script is
built. If not, start with [Your first script](scripting/index.md#your-first-script).

Add `import modbus` to the script. Each app can read its own device, with its own address, port
and unit ID. There is nothing to set up globally. Only reading is supported.

Before you start:

1. Enable Modbus TCP on the device you want to read.
2. Look up its register list in the manufacturer's manual.
3. Note its IP address, the register address, the read function and any scaling factor.

## Display a measurement

This example reads one holding register every ten seconds. Replace `192.168.1.50` and address `0`
with your device's values. It assumes the register holds tenths of a degree: `235` becomes `23.5`.

```berry
import modbus

class Temperature
  var label, next_read, busy

  def init()
    self.label = "--"
    self.next_read = 0
    self.busy = false
  end

  def received(values, error)
    self.busy = false
    if error != 0
      self.label = "--"
      log("Modbus error: " + str(error))
      return
    end
    self.label = str(modbus.int16(values[0]) / 10.0)
  end

  def loop()
    if self.busy || now_ms() < self.next_read return end
    self.next_read = now_ms() + 10000
    self.busy = true
    modbus.readHoldingRegisters("192.168.1.50", 0, 1,
      / values, error -> self.received(values, error))
  end

  def draw()
    clear()
    text(1, 6, self.label, 0xFFFFFF)
  end
end

return Temperature()
```

## How it behaves

A read runs in the background, and the display keeps running meanwhile. When the answer arrives,
or the read fails, the callback runs once. On success `error` is `0`. On failure `values` is `nil`
and `error` is the [reason code](#when-a-read-fails). Start a read in `loop()`, never in `draw()`,
and wait for the answer before you start the next one.

## Choose what to read

All four calls take `(host, address, count, callback, opts?)`:

| Call | Reads | Maximum count |
|---|---|---|
| `modbus.readHoldingRegisters(...)` | holding registers, function 03 | 125 |
| `modbus.readInputRegisters(...)` | input registers, function 04 | 125 |
| `modbus.readCoils(...)` | coils, function 01 | 2000 |
| `modbus.readDiscreteInputs(...)` | digital inputs, function 02 | 2000 |

- `host` is an IP address or host name, without `http://`.
- `address` **starts at zero**. If the manual calls the first holding register `40001`, its address
  is usually `0`. Some manuals already give zero-based addresses. Use those as they are.
- Addresses run from `0` to `65535`. `count` is at least `1` and must not go past `65535`.
- The callback gets a list in address order: `values[0]` is the first value you asked for.
  Registers are numbers from `0` to `65535`. Coils and digital inputs are `0` or `1`.

`opts` sets the TCP port and unit ID. Defaults are port `502` and unit `1`. Use the unit ID your
device needs, especially behind a gateway.

```berry
modbus.readInputRegisters("192.168.1.60", 100, 2,
  / values, error -> self.received(values, error),
  {'port': 502, 'unit': 2})
```

Ports run from `1` to `65535`, unit IDs from `0` to `255`. Every call can use different settings.

## Convert the values

Follow the data type and register order in your device's manual:

| Device data type | Conversion |
|---|---|
| unsigned 16-bit integer | `values[0]` |
| signed 16-bit integer | `modbus.int16(values[0])` |
| signed 32-bit integer | `modbus.int32(values[0], values[1])` |
| 32-bit float | `modbus.float32(values[0], values[1])` |

For a 32-bit value, read two registers. The helpers take the high word first. If your device sends
the low word first, pass `values[1], values[0]`. Apply scaling afterwards, for example `/ 10.0` or
`/ 1000.0`.

## Share one device across several apps

When several apps show readings from the same device, **read once and share the results**. A
[background script](scripting/several-apps.md#running-without-ever-being-shown) polls the device and publishes
the values through [`shared`](scripting/several-apps.md#talking-to-other-apps). The display apps only read what
they need. This avoids duplicate requests and saves memory.

A [module](scripting/several-apps.md#sharing-code-between-scripts) can also use `modbus` inside a
function an app calls. But a module has no `loop()`, and calling it from every display app would
still send separate requests. Use a background script for regular polling.

For example, save this background script as **`meter`**. It reads two neighboring holding
registers: power in watts and voltage in tenths of a volt. Adjust the host, address and conversion
to your device's manual.

```berry
# @headless true
import modbus

class Meter
  var next_read, busy

  def init()
    self.next_read = 0
    self.busy = false
  end

  def received(values, error)
    self.busy = false
    if error != 0 return end
    shared.set("power", values[0])
    shared.set("voltage", values[1] / 10.0)
  end

  def loop()
    if self.busy || now_ms() < self.next_read return end
    self.next_read = now_ms() + 10000
    self.busy = true
    modbus.readHoldingRegisters("192.168.1.50", 0, 2,
      / values, error -> self.received(values, error))
  end
end

return Meter()
```

The power app needs no Modbus import. It shows `--` if no reading has arrived, or if the last good
reading is more than 30 seconds old:

```berry
class Power
  var label
  def init() self.label = "--" end
  def loop()
    var age = shared.age("meter.power")
    if age != nil && age <= 30000
      self.label = str(shared.get("meter.power")) + " W"
    else
      self.label = "--"
    end
  end
  def draw()
    clear()
    text(1, 6, self.label, 0xFFFFFF)
  end
end

return Power()
```

For a voltage app, use `meter.voltage` instead of `meter.power` and `" V"` instead of `" W"`. If
you name the background script differently, use that name before the dot.

## When a read fails

`error` is `0` on success.

- `-1`: the read could not be done. Check the host, port, Wi-Fi and arguments, and try again at your
  next interval. Reads can take longer while other apps fetch data.
- A positive code comes from the Modbus device. Common ones: `1` (unsupported function), `2`
  (unknown register), `3` (unsupported value or count), `4` (device failure), `6` (device busy).
  See the device manual for details.

The web UI's *Log* tab tells you why. Every failed read writes one line starting with `modbus:`,
with the reason, the code, how long it took and what was asked of which device:

```
modbus: response timeout (-1, 2003ms) reg=100 count=2 unit=3 fc=4 host=192.168.1.60:502
```

| Reason | What it means |
|---|---|
| `response timeout` | The device did not answer within two seconds. |
| `connection failed` | No connection to that host and port. |
| `closed without response` | The device closed the connection without answering. |
| `illegal register address/range` | Code `2`: part of the range does not exist on the device. |
| `offline` | The clock is not connected to Wi-Fi. |
| `queue full` | Too many reads were waiting at once. Read less often. |

A read of several registers fails with code `2` as soon as one of them does not exist, even when
the first one does. Read a smaller range, or check the register map in the manual. Waiting longer
between reads does not help.

---

## Related

- [Scripting guide](scripting/index.md): every other call a script can use
- [Share values with other apps](scripting/several-apps.md#talking-to-other-apps): `shared.set()`,
  `shared.get()` and `shared.age()`
- [Background scripts](scripting/several-apps.md#running-without-ever-being-shown): a script that
  runs without a turn on the display
- [Limits](../reference/limits.md#scripting): every limit a script runs under
