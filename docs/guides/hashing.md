---
only: [tc002]
---

# Hashing and mining in scripts

This page lets a script compute checksums, signatures, keys and random bytes with the `crypto`
module, and search a Bitcoin block header for a winning nonce. It assumes you know how a script
is built. If not, start with [Your first script](scripting/index.md#your-first-script).

Only the TC002 has this module. Add `# @needs crypto` to the header of a script that imports it,
so other models show it as not suitable. On other models the import fails, so a script meant for
every model imports it inside `try` and leaves out `@needs`. See
[Scripts for every model](ble.md#scripts-for-every-model).

## How it behaves

A hashing call computes its result before it returns, and the display waits meanwhile. Mining
works the other way: `crypto.mine()` searches in the background and calls your callback for each
hit. It uses every core of the clock, but only the computing time the clock does not need itself,
so the display, the web interface and the sound keep running as before. A search goes on only
while your app keeps calling the mining functions, and only one app can search at a time.

## Hashing {#hashing}

Add `import crypto`. Each call takes strings or `bytes` and returns `bytes`. `.tohex()` turns
them into text.

| Call | Returns |
|---|---|
| `crypto.md5(data)` | the MD5 of `data`, 16 bytes |
| `crypto.sha1(data)` | the SHA-1 of `data`, 20 bytes |
| `crypto.sha256(data)` | the SHA-256 of `data`, 32 bytes |
| `crypto.sha256d(data)` | the SHA-256 of that SHA-256, as Bitcoin uses it |
| `crypto.hmac_sha1(key, data)` | the HMAC-SHA-1 signature of `data` with `key`, 20 bytes |
| `crypto.hmac_sha256(key, data)` | the HMAC-SHA-256 signature of `data` with `key`, 32 bytes |
| `crypto.pbkdf2_hmac_sha256(password, salt, iterations, length?)` | the PBKDF2 key of `password`, `length` bytes (default 32, up to 1024) |
| `crypto.random(length)` | `length` random bytes (1 to 1024) for keys and nonces |

`bytes` converts on every model: `bytes("48656c6c6f")` and `.tohex()` for hexadecimal,
`bytes().fromb64("SGVsbG8=")` and `.tob64()` for Base64, `.asstring()` for text.

Many web services want a request signed with HMAC-SHA-256. Keep the secret in a
[`@config`](scripting/storage.md#settings-the-user-can-change) field and sign the body in a method
of your app:

```berry
  def send(body)
    var signature = crypto.hmac_sha256(store.get("secret"), body).tohex()
    http.post("https://example.com/api", body, def (reply, status) log(str(status)) end,
              {'headers': {'X-Signature': signature}})
  end
```

A FRITZ!Box login (`login_sid.lua?version=2`) sends a challenge such as
`2$10000$5A1711$2000$5A1722`. Its answer is two PBKDF2 rounds:

```berry
  def fritz_response(challenge, password)
    import string
    var p = string.split(challenge, "$")
    var key = crypto.pbkdf2_hmac_sha256(password, bytes(p[2]), int(p[1]))
    key = crypto.pbkdf2_hmac_sha256(key, bytes(p[4]), int(p[3]))
    return p[4] + "$" + string.tolower(key.tohex())
  end
```

Two-factor codes (TOTP) are an HMAC-SHA-1 of the current 30-second step. The secret an
authenticator app shows is Base32:

```berry
  def totp(secret, ms)
    import string
    var key = bytes(), bits = 0, n = 0
    for i : 0 .. size(secret) - 1
      var v = string.find("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567", string.toupper(secret[i]))
      if v < 0 continue end
      bits = ((bits << 5) | v) & 0xfff
      n += 5
      if n >= 8 n -= 8 key.add((bits >> n) & 0xff) end
    end
    var step = bytes()
    step.add(0, -4)
    step.add(ms / 30000, -4)
    var h = crypto.hmac_sha1(key, step)
    return string.format("%06d", (h.get(h[19] & 0x0f, -4) & 0x7fffffff) % 1000000)
  end
```

Call it as `self.totp(store.get("secret"), epoch_ms())` once the clock knows the time. Before
that, `epoch_ms()` returns `-1`.

`iterations` runs from 1 to 1 000 000. A `length` above 32 lowers that limit in proportion. The
display waits while the key is computed, so compute it once and keep it.

Anything else than a string or `bytes` raises `value_error`, and so does an `iterations` or
`length` out of range. Hash in `setup()`, `loop()` or a callback, never in `draw()`.

## Proof of work {#proof-of-work}

`crypto.mine()` tries every nonce of an 80-byte Bitcoin block header and reports each
hash that meets a target. It does not talk to a mining pool. A script does that with
[TCP](tcp.md). The Solo Miner app on the AWTRIX Hub is a complete example.

```berry
import crypto

class Hashrate
  var header, rate

  def setup()
    self.rate = 0
    self.header = bytes()
    self.header.resize(80)
    crypto.mine(self.header, crypto.target(1.0), def (nonce, hash)
      if nonce == nil log("all nonces tried") else log("hit at " + nonce) end
    end)
  end

  def loop() self.rate = crypto.mine_rate() end

  def draw()
    clear()
    text(0, 6, str(int(self.rate / 1000)) + " kH/s")
  end
end

return Hashrate()
```

| Call | Does |
|---|---|
| `crypto.mine(header, target, callback, opts?)` | Starts searching `header` (80 `bytes`, its nonce field is ignored) for hashes at or below `target` (32 `bytes`, little-endian) and returns `true`. A new call replaces your running search. Returns `false` while another app is searching. `opts` takes `{'threads': n}`: `0` or leaving it out uses every core. |
| `crypto.mine_stop()` | Stops your search. |
| `crypto.mine_rate()` | Hashes per second over the last five seconds. |
| `crypto.mine_hashes()` | Hashes since your app started searching. |
| `crypto.mine_best()` | The highest difficulty any of those hashes reached. |
| `crypto.mine_threads()` | How many cores the clock has. |
| `crypto.target(difficulty)` | The 32-byte target of a pool difficulty. |
| `crypto.difficulty(hash)` | The difficulty a 32-byte hash reaches. |

For each hash that meets the target, the callback gets the nonce as eight hexadecimal characters,
the way a pool expects it in `mining.submit`, and the hash as 32 `bytes`. When every nonce has
been tried it gets `nil, "done"`: change the header, for example its extranonce, and start again.

To keep a search running, **call `crypto.mine_rate()` from `loop()`**, which runs every second
whether the app is shown or not. Five seconds without a call stop the search. Keep the rate and
the best difficulty in members and draw those. `draw()` alone does not keep the search going.

Removing or switching off the app stops its search. At most 64 hits wait for your callback and 16
are delivered per frame, so with a target that nearly every hash meets, some hits are dropped. A
wrong size or type raises `value_error`.

## Related

- [TCP in scripts](tcp.md): talk to a mining pool or any other line-based service
- [Network](scripting/network.md): HTTP, MQTT and Modbus in scripts
- [Limits](../reference/limits.md#scripting): every limit a script runs under
