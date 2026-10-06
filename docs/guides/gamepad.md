---
only: [tc002]
---

# Gamepad

This page shows how to connect up to two Bluetooth gamepads to the clock and play games on it,
alone or with two players. Your phone can be a gamepad too.

## How it behaves

The clock keeps paired gamepads connected by itself: a paired gamepad that you switch on connects
again, also after a restart of the clock. Every game on the clock reads the same gamepads, by
player number, so you pair a gamepad once and not for each game. A phone with the
[AWTRIX NG app](app.md) can stand in for either player's gamepad.

## Pair a gamepad

1. Put the gamepad in pairing mode. On an 8BitDo Ultimate 2: switch it to **BT**, then hold
   **Pair** until the LED blinks fast.
2. In the web UI, open **System → Gamepad** and press **Pair**.

The first new gamepad found within a minute is paired and remembered. To add a second gamepad,
do the same again. A gamepad that is paired already is not taken twice.

- **Controller 1** and **Controller 2** are the two places for a gamepad. Each row shows its
  gamepad, whether it is connected, and its player number while it is connected. **Forget**
  removes only that row's gamepad.
- Pairing or forgetting one gamepad leaves the other one connected.

## Player numbers

- When one gamepad is connected, it is **Player 1**.
- When both are connected, the gamepad whose first input arrived first is Player 1, the other one
  **Player 2**.
- When a gamepad disconnects, its controls read as released, and the other gamepad is Player 1.
  A gamepad that connects again joins as Player 2.
- After a restart, the numbers are given the same way. The gamepads stay paired.

## Which gamepads work

The gamepad must use **Bluetooth LE**, as the 8BitDo Ultimate 2 does in Bluetooth mode. Gamepads
that only use classic Bluetooth, such as many Switch and PlayStation controllers, cannot connect.
The clock supports Bluetooth LE only.

While a gamepad is paired, Bluetooth stays switched on. This uses about half a megabyte of the
clock's memory.

Buttons are named like on an Android gamepad: A, B, X, Y, L1, R1, L2, R2, SELECT, START, HOME, L3,
R3.

## Your phone as the gamepad

The [AWTRIX NG app](app.md) shows a gamepad on your phone: D-pad, A, B, X, Y, L1, R1, SELECT and
START.

1. Make sure the phone is in the same network as the clock, and scripting is switched on.
2. In the app, choose your clock and open the gamepad.

This works without Bluetooth.

- **Two phones** can play at once, one for each player. A phone takes the player that no phone and
  no Bluetooth gamepad plays yet.
- While a phone plays, it replaces the Bluetooth gamepad of that player. `gamepad.name(player)` is
  then the phone's name, and the other player keeps their own gamepad or phone.
- When a second phone picks a player that a phone plays already, it takes the player over.
- When you close the gamepad in the app, or the phone stops sending for a second, that player's
  Bluetooth gamepad counts again.
- In the web UI, **System → Gamepad** shows each phone that plays, with its player.

For developers, the connection is described under
[`POST /api/v1/gamepad/remote`](../reference/http.md#post-apiv1gamepadremote).

## When it goes wrong

| Symptom | What to do |
|---|---|
| **Pair** finds nothing | Check that the gamepad is in Bluetooth LE pairing mode and close to the clock. After the one-minute search ends, try again. |
| **Pair** is disabled | Two gamepads are paired, or a search is running. Forget a gamepad to make room. |
| The gamepad never connects | It probably uses classic Bluetooth only. Use a Bluetooth LE gamepad. |
| A game says `waiting` | The gamepad is paired but not connected. Switch it on. |
| The phone gamepad does nothing | Check that the phone is in the same network as the clock and that scripting is switched on. |

## In your own scripts

Scripts read the gamepad with `import gamepad`. For other Bluetooth devices, see
[Bluetooth LE in scripts](ble.md).

<!-- panel alt="The script shows the gamepad's state, unpaired, before a gamepad is ready" -->
```berry
import gamepad

class Dot
  var x, y, before

  def init()
    self.x = 26
    self.y = 8
    self.before = 0
  end

  def setup()
    timer.every(100, / -> self.step())
  end

  def step()
    var d = gamepad.dir()
    if d != nil
      self.x = (self.x + d[0] + width()) % width()
      self.y = (self.y + d[1] + height()) % height()
    end
    if gamepad.pressed(self.before, "A") sound.play({"rtttl": "a:d=16,o=5,b=200:c"}) end
    self.before = gamepad.buttons()
  end

  def draw()
    clear()
    if !gamepad.ready() text(1, 7, gamepad.state(), 0x404060) return end
    pixel(self.x, self.y, 0x40FF60)
  end
end

return Dot()
```

Until a gamepad is ready, the script shows the gamepad's state, for example `unpaired`. Then it
draws a green dot. The D-pad or the left stick moves it one pixel every 100 ms, and a press of
**A** plays a short beep.

The gamepad sends a new report about every 8 ms. Read it where your game steps, in a timer as
above: `loop()` runs only about once a second. Keep the `buttons()` value from last time to catch
a press once, however long it is held.

Every control and claim function takes an optional last argument `player`, `1` or `2`. Without
it, the function reads Player 1. Any other value raises `value_error`. `gamepad.bit(button)` has
no player argument.

For two players, keep each player's previous button mask separately. For example, in a game's
step (set `self.before = [0, 0]` in `init()`):

```berry
for player : 1..2
  var direction = gamepad.dir(player)
  var fire = gamepad.pressed(self.before[player - 1], "A", player)
  # Use direction and fire to update this player's character.
  self.before[player - 1] = gamepad.buttons(player)
end
```

| Call | Result |
|---|---|
| `gamepad.state(player=1)` | `unpaired`, `pairing`, `waiting` (paired, not connected), `connecting` or `ready`. |
| `gamepad.ready(player=1)` | `true` while that player's gamepad or phone is connected and sending. |
| `gamepad.name(player=1)` | Its Bluetooth name or the phone's name, `""` when there is neither. |
| `gamepad.buttons(player=1)` | The buttons held now, as bits: button number n is bit n. `gamepad.bit()` gives the bit of one button. |
| `gamepad.bit(button)` | The bit of a button name (`"A"`, `"START"`, ...) or number. An unknown name raises `value_error`, as an unknown axis or trigger name does. |
| `gamepad.down(button, player=1)` | Held right now. |
| `gamepad.pressed(before, button, player=1)` | Pressed since `before`, the value `buttons(player)` gave last time. |
| `gamepad.hat(player=1)` | The D-pad, 0-7 clockwise from up, `-1` when released. |
| `gamepad.dir(player=1)` | `[dx, dy]` from the D-pad, or from the left stick pushed past a third. Each is -1, 0 or 1. `nil` when neither is pushed. |
| `gamepad.axis(which, player=1)` | A stick axis, `"lx"`, `"ly"`, `"rx"` or `"ry"`, from -100 to 100. Up and left are negative. |
| `gamepad.trigger(which, player=1)` | `"lt"` or `"rt"`, from 0 released to 100 pulled. |
| `gamepad.claim(self, player=1)` | Takes that player slot for this game. Call it on every step while being played. |
| `gamepad.release(self, player=1)` | Gives that player slot back. |
| `gamepad.mine(self, player=1)` | `false` while another game holds that slot. A claim ends a second after it was last renewed. |

Claims are independent per player slot and do not reserve a specific gamepad or phone. A two-player
game renews `gamepad.claim(self, 1)` and `gamepad.claim(self, 2)` and releases both when it stops.

Without a ready gamepad, every control reads as untouched: no buttons, the D-pad released, sticks and
triggers at 0.

More about writing games and other apps is in the [Scripting guide](scripting/index.md).

## Good to know

- **The row number in the web UI is not the player number.** Controller 2 can be Player 1,
  because the player number goes by first input.
- **With two gamepads paired, Pair cannot be pressed.** Forget one gamepad first.

## Details

- [HTTP API → GET /api/v1/gamepad](../reference/http.md#get-apiv1gamepad): the gamepad routes, for
  your own tools

## Related

- [Scripting guide](scripting/index.md): write your own apps and games
- [Bluetooth LE in scripts](ble.md): other Bluetooth devices
- [Buttons, knob & clock](device-controls.md): the clock's own controls
