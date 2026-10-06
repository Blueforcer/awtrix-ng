---
only: [tc002]
---

# TCP in scripts

This page lets a script talk to a service that sends and receives lines of text over TCP: a
status feed, a telnet-style device on your network or a mining pool. It assumes you know how a
script is built. If not, start with [Your first script](scripting/index.md#your-first-script).

Add `import tcp` to the script and `# @needs tcp` to its header, so other models show it as not
suitable. Only the TC002 has this module. On other models the import fails, so a script meant for
every model imports it inside `try` and leaves out `@needs`. See
[Scripts for every model](ble.md#scripts-for-every-model). Connections are plain TCP, without TLS.

## Read a status line

This example connects to a device at `192.168.1.10`, port `1234`, asks for its status and shows
the answer. Replace the address, the port and the `status` command with your device's.

```berry
import tcp

class Reader
  var connection, label

  def setup()
    self.label = "CONNECT"
    self.connection = tcp.connect("192.168.1.10", 1234, def (event, data)
      if event == "open" tcp.send(self.connection, "status\n") end
      if event == "line" self.label = data end
      if event == "close"
        self.connection = nil
        self.label = data
      end
    end)
  end

  def draw()
    clear()
    text(0, 6, self.label)
  end
end

return Reader()
```

## How it behaves

A connection runs in the background. `tcp.connect()` returns at once, and the callback reports
what happens: `"open"` when the connection is up, `"line"` for each line that arrives, and
`"close"` when it ends. Text you send goes out as it is, so add the line ending yourself. A closed
connection stays closed until your script connects again.

## Connect, send and close

| Call | Does |
|---|---|
| `tcp.connect(host, port, callback, opts?)` | Opens a connection and returns its handle, or `nil` when the app already holds four connections. `host` is a name or an IP address, `port` 1–65535. `opts` takes `{'timeout': ms}`, 1–60000, default 10000. |
| `tcp.send(handle, text)` | Sends `text` as it is: add the `"\n"` yourself. You may call it before the connection is open. The text goes out once it is. Returns `false` when the handle is closed or not yours, or when 64 KiB are still waiting to go out. |
| `tcp.close(handle)` | Closes the connection. The callback is not called again. |

A wrong type or a value out of range raises `value_error`.

## What the callback receives

The callback gets two values, `event` and `data`:

| `event` | `data` |
|---|---|
| `"open"` | `nil`: the connection is up |
| `"line"` | one line of text, without its line ending (`\n` or `\r\n`). Empty lines arrive too |
| `"close"` | why the connection ended, see below. Nothing arrives after it |

Every complete line arrives before `"close"`. A last line without a line ending is dropped when
the connection ends.

## When the connection ends

| Reason | Means |
|---|---|
| `closed` | the other side closed the connection |
| `refused` | nothing accepts connections on that address and port |
| `timeout` | the connection was not up within the timeout. Looking up the name counts towards it |
| `dns` | the name could not be found |
| `overflow` | a line longer than 16 KiB arrived, or your script fell 64 KiB of lines behind |
| `error` | any other network problem, or a line containing a zero byte (the module carries text only) |

Connect again from `loop()`, not from the callback, and wait longer after each failure: 5 s,
then 10 s, up to a minute.

## Limits

- Four connections per app, sixteen on the clock.
- A line may be up to 16 KiB long. Up to 64 KiB of lines may wait for your script, and up to
  64 KiB of text may wait to be sent.
- At most 32 callbacks run per frame. The connections take turns, so a busy one does not hold up
  the others.
- Removing, reinstalling or switching off the app closes its connections.

## Related

- [Network](scripting/network.md): HTTP, MQTT and Modbus in scripts
- [Hashing and mining in scripts](hashing.md): what the Solo Miner app builds on
- [Limits](../reference/limits.md#scripting): every limit a script runs under
