---
only: [tc002]
---

# Bluetooth LE in scripts

This page lets your scripts use Bluetooth LE (BLE, the low-energy Bluetooth that sensors and
fitness devices use). A script can:

- scan for nearby BLE devices,
- connect to a device and read or write its values,
- receive measurements as they change,
- offer values of its own to phones and other devices.

**TC002 only.** On other models `import ble` fails, and the device capabilities have no `ble`
key.

## Read a heart-rate sensor

The quickest start is the ready-made example.

1. Download the [heart-rate example](../examples/heart-rate.ax).
2. In the web UI, open **Scripts**, create a new script and paste the source. Save it.
3. Switch on your BLE heart-rate sensor (put it on, or wake it up).
4. If needed, open the app's settings and set **Device name** to the beginning of the name your
   sensor shows. Use **Ignore** to skip devices whose name matches.

The example is laid out for the TC002's 52 × 16 display. It looks for devices that offer the
heart-rate service `180d`, receives the heart-rate value `2a37`, and connects again when the
connection is lost.

Whether a sensor works depends on the services it offers and the security it asks for.

## How it behaves {#when-bluetooth-is-on}

Bluetooth starts by itself when a script scans, connects, advertises or offers a service. It
switches off again 30 seconds after the last scan, connection or service has ended. A call that
starts something returns a handle, a number that belongs to the script that created it. Removing
an app releases everything it started.

## Gamepads

A Bluetooth gamepad needs no script of its own. Pair it under **System → Gamepad** in the web UI
and read it with `import gamepad`. See [Gamepad](gamepad.md).

## Scripts for every model

A script meant for every model imports `ble` inside `try`. If the import fails, `ble` stays
`nil`, and the script shows that Bluetooth is not available:

```berry
var ble = nil
try
  import ble as b
  ble = b
except .. end

class Heart
  def draw()
    clear()
    if ble == nil
      text(1, 6, "no BLE", 0x666666)
      return
    end
    # ... draw the reading
  end
end

return Heart()
```

## Calls and results

Some words used below:

- **Advertise:** send a short signal so that nearby devices can find this one.
- **GATT:** the way BLE devices offer data. A device has *services* (for example "heart rate"),
  and each service has *characteristics* (single values, for example "heart-rate measurement").
  Both are named by a UUID such as `180d`.
- **UUID:** the ID of a service or characteristic, either short (`180d`) or long
  (`0000180d-0000-1000-8000-00805f9b34fb`).
- **Address type:** a device's Bluetooth address is either fixed (public) or random. The scan
  report's `random` field says which, and `ble.connect()` needs the same value.
- **Bond:** a stored pairing with another device.

Calls that start something return a handle, or `nil` when the call is refused at once. The
reason is written to the script log. Most callbacks receive `(value, error)`. On success,
`error` is `nil`. On a later failure, `value` is `nil` and `error` is a text. Keep callbacks
short, as with HTTP and MQTT callbacks. Byte values are Berry `bytes`. Values you send can also
be hexadecimal strings.

| Call | Result / purpose |
|---|---|
| `ble.available()` | Whether Bluetooth is present. It does not mean that another device can be reached. |
| `ble.state()` | `off`, `starting`, `on`, `failed`, or `unavailable`. |
| `ble.address()` | This device's Bluetooth address when known, otherwise `nil`. |
| `ble.scan(cb, opts)` | Calls `cb(device, error)` for each device found. Stop it with `ble.stop(handle)`. |
| `ble.connect(addr, cb, opts)` | Calls `cb(connection, error)` when the connection is ready and its services are known. Pass the scan report's `random` flag in `opts`. `secure: true` asks for an encrypted connection. |
| `ble.on_disconnect(conn, fn)` | Calls `fn(reason)` when the connection is lost. |
| `ble.disconnect(conn)` | Closes this script's connection. Returns a boolean. |
| `ble.services(conn)` | List of services with `uuid` and `chars`. The characteristic `props` use the letters listed under [Advertise and serve](#advertise-and-serve). Returns `nil` on failure. |
| `ble.read(conn, svc, chr, cb)` | Calls `cb(bytes, error)` with the characteristic's value. |
| `ble.write(conn, svc, chr, data, cb, noresp)` | Calls `cb(true, nil)` on success. Pass `true` as the last argument, `noresp`, to write without waiting for a confirmation from the other device. |
| `ble.subscribe(conn, svc, chr, cb, opts)` | Calls `cb(bytes, error)` each time the value changes. Stop it with `ble.stop(handle)`. `opts` may set `interval` in milliseconds: at most one value per interval, always the newest. The last value of a burst arrives when its interval is over. A device that only sends to paired phones is paired automatically. |
| `ble.pair(conn, cb)` | Asks for encryption and pairing. Calls `cb(true, nil)` or `cb(nil, error)`. |
| `ble.bonds()` | List of addresses of paired devices. |
| `ble.forget(addr)` | Deletes the pairing with that address. Returns a boolean. |
| `ble.advertise(opts)` | Starts advertising. Returns a handle for `ble.stop(handle)`. |
| `ble.serve(uuid, chars, cb)` | Offers a service of your own. `cb` receives an event map. Returns a handle. |
| `ble.set(service, chr, data)` | Changes a value in a service this script offers, and informs subscribed devices when allowed. Returns a boolean. |
| `ble.stop(handle)` | Stops what this script's handle started. Returns a boolean. |

## Scan filters

All scan options are optional:

| Option | Meaning |
|---|---|
| `active` | also ask devices for their extra scan data |
| `uuid` | a service UUID, or a list of them |
| `addr` | one device address |
| `name` | the beginning of the device name |
| `mfg` | the manufacturer ID |
| `dedupe` | the minimum time in milliseconds between two reports for the same device. Default 1,000, `0` turns it off |

Reports contain `addr`, `random` and `rssi` (signal strength), plus other fields when the device
sends them: `name`, `uuids`, `adv`, `rsp`, `mfg` and `svc`. Always check whether an optional
field is there. To connect to a device from a scan, pass `{"random": device["random"]}`.

```berry
# @headless true
import ble
class ScanDemo
  def setup()
    if ble.available()
      var scan = ble.scan(def (dev, err)
        if dev != nil log(dev.find("name", dev["addr"])) end
      end, {"uuid": "180d", "active": true})
      if scan != nil timer.after(10000, / -> ble.stop(scan)) end
    end
  end
end
return ScanDemo()
```

This is a [background script](scripting/several-apps.md#running-without-ever-being-shown): it
never draws and writes the name of each device it finds to the log.

## Advertise and serve

Advertising options are `name`, `uuids`, `solicit` (services the device wants to use on the
connecting phone), `connectable`, `mfg` (manufacturer ID mapped to a byte value) and `svc`
(service UUID mapped to a byte value). An advertisement can carry only a little data. A call with
too much data is refused. Offering a service and advertising it are two separate calls.

Each characteristic in `ble.serve()` is a map with `uuid`, `props` and an optional starting
`value`. The `props` letters are:

| Letter | Meaning |
|---|---|
| `r` | read |
| `w` | write |
| `x` | write without response |
| `n` | notify |
| `i` | indicate (notify with confirmation) |
| `e` | encrypted connection required |

Without `props`, a characteristic is `r`. The service callback receives an event map:

- A write: `write` and `data`.
- A device starts or stops listening: `subscribe` and `on`.
- An error: `error`. Check for it before you read the other fields.

### Security

With `e`, reading, writing, subscribing and receiving values need an encrypted connection.
Encryption does not tell you who is on the other side. There is no passkey or code comparison
shown. Only offer data and actions that any nearby device may use, and only install scripts
you trust. Pairings are stored on the device. `ble.forget()` removes one.

## Limits and errors

Each script can have at most four scans, two connections, eight subscriptions, one
advertisement and four services at the same time. The device has at most four outgoing
connections in total, shared by all scripts. Scan reports reach a script at most 20 times per
second. Keep only what your app needs.

| Error or symptom | What to check |
|---|---|
| `import ble` fails | This model has no Bluetooth for scripts. Only the TC002 has. Show "not available" in the app. |
| `not connected` / `connect failed` | Is the device switched on and in range? Is the address type right? Is the connection still open? |
| `bad address`, `bad uuid`, `bad data`, `bad property` | Check the address, UUID, hex/bytes value and property letters. |
| `no such characteristic` / `cannot notify` | Look at `ble.services(conn)` and choose a characteristic that supports the action. |
| `pairing failed` or a security error | The device may need a pairing method or permissions this connection cannot provide. |
| `too many scans`, `too many connections`, `too many subscriptions`, `too many adverts`, `too many services` | Stop handles you do not need. Do not start things again and again. |
| `service already served`, `attribute table full`, `no advertising slot free` | Services and advertisements are shared with other scripts. Stop the ones you do not need. |

## Good to know

- **Start scans and connections once.** Start them in `setup()`, `loop()` or a callback, not on
  every call, and never in `draw()`.
- **Stop scans and advertisements when you are done.** Call `ble.stop(handle)` as soon as you do
  not need them.
- **`ble.connect()` needs the address type from the scan.** Pass `{"random": device["random"]}`
  from the scan report, or the connection fails.

## Related

- [Scripting guide](scripting/index.md): the full script API
- [Device capabilities](../reference/device.md): check whether a device has `ble`
- [Heart-rate example](../examples/heart-rate.ax): the source of the example above
- [Gamepad](gamepad.md): Bluetooth gamepads
