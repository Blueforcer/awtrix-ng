---
only: [esp32, esp32-s3]
---

# Art-Net

Art-Net lets software on your computer set every LED of the display directly, in real time. Instead
of sending an app or a notification, **you** draw each frame on your computer and send the pixels
over the network. The examples below use a 32×8 display.

Use it for music visualizers, VJ software, screen mirroring or your own animation in Python:
anything where you want to control every frame yourself.

Art-Net is **off** by default. Switch it on with the **Art-Net** toggle under *Misc* in the web
UI, or with the [`artnet` setting](../reference/system.md#art-net) on `PUT /api/v1/system`.
AWTRIX then listens on UDP port **6454** while it is on your Wi-Fi. There is no password. See
[Security](#security).

---

## Light up the display in 30 seconds {#light-up-the-panel-in-30-seconds}

`curl` cannot send Art-Net frames, so you use a small Python script for that. First switch
Art-Net on and check that you have the right address:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"artnet":true}'
curl http://<awtrix-ip>/api/v1/device
```

If the last call returns JSON, AWTRIX is reachable. Now paste this into `artnet.py` and run it with
`python artnet.py`. It fills the whole display solid red for ten seconds:

```python
import socket, struct, time

DEVICE = "192.168.1.42"   # the IP address of your AWTRIX
PORT   = 6454
WIDTH, HEIGHT = 32, 8

def artdmx(universe, data):
    """Build one ArtDMX packet."""
    return (b"Art-Net\0"
            + struct.pack("<H", 0x5000)       # opcode: ArtDMX
            + struct.pack(">H", 14)           # protocol version
            + bytes([0, 0])                   # sequence, physical
            + struct.pack("<H", universe)     # universe (16-bit, little-endian)
            + struct.pack(">H", len(data))    # data length (big-endian)
            + bytes(data))

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def send_frame(pixels):
    """pixels: a list of 256 (r, g, b) tuples, left-to-right then top-to-bottom."""
    flat = [c for px in pixels for c in px]
    sock.sendto(artdmx(0, flat[:510]),  (DEVICE, PORT))   # pixels 0-169
    sock.sendto(artdmx(1, flat[510:]),  (DEVICE, PORT))   # pixels 170-255

red = [(255, 0, 0)] * (WIDTH * HEIGHT)
for _ in range(300):          # ~10 s at 30 fps
    send_frame(red)
    time.sleep(1 / 30)
```

The display turns red instantly and stays red while the script runs. Stop the script, and about five
seconds later the rotation comes back by itself. You do not have to release anything.

---

## How it behaves {#keep-sending}

There is no Art-Net mode to switch on and off. While your last frame is less than 5 seconds old,
the display shows your pixels and nothing else: no apps, no notifications, no transitions. Five
seconds after your last frame, the apps come back, so send even a still image again at least every
5 seconds. There is no stop command: to get the display back at once, turn the display off and on
with [`PATCH /api/v1/display`](../reference/http.md#display). A display that is turned off stays
black, and the mood light wins over Art-Net too. In setup mode, while the clock offers its own
`awtrixng-xxxxxx` hotspot, AWTRIX does not listen for Art-Net at all.

---

## Universes and pixel mapping

Art-Net carries 512 DMX channels per universe, and each pixel takes three of them, so one universe
covers 170 pixels. Start at universe 0 and use `ceil(width × height / 170)`
universes for the active geometry. Each full universe carries 510 RGB bytes.
The standard **32 × 8 = 256 pixel** display therefore spans two universes:

| Universe | Pixels | Channels used | Notes |
|---|---|---|---|
| `0` | 0 – 169 | 510 of 512 | Full universe |
| `1` | 170 – 255 | 258 of 512 | Only 86 pixels. The rest is ignored |

Channel order is plain **RGB**, three channels per pixel, no white channel. The first pixel is DMX
channels 1–3, the second 4–6, and so on.

Pixels run in **row-major order**: pixel 0 is the top-left corner, the last pixel of the top row is
the top-right, the next pixel begins the second row, and the final pixel is the bottom-right. In
code, pixel *p* sits at:

```
x = p % width
y = p / width
```

You address the 32×8 grid as you see it, not the LED strip inside. AWTRIX takes care of the
wiring and applies its color settings, just as it does for apps, so a change to your display's
wiring settings does not affect your Art-Net code. Widening the display (`panelWidth × panels`, see
[Panel and orientation](../reference/system.md#panel-and-orientation)) adds pixels, and the mapping
and universe count scale with it: a wider display spans universe `2` and beyond.

If your controller offers separate Net / Sub-Net / Universe boxes, leave Net and Sub-Net at **0**
and set Universe to 0 and 1.

---

## Using a lighting controller

Any Art-Net-capable software (Resolume, QLC+, Jinx!, xLights, TouchDesigner, Madrix…) can drive the
display. AWTRIX answers Art-Net discovery as **AWTRIX NG**, so it usually appears in the
controller's node list by itself. While Art-Net is off, AWTRIX does not answer discovery. If your
controller still misses it, enter its IP address by hand.

| Setting | Value |
|---|---|
| Protocol | Art-Net |
| Node address | the IP address of your AWTRIX, or let discovery find it |
| Port | `6454` |
| Net / Sub-Net | `0` |
| Universes | `0` and `1` |
| Pixel order | RGB |
| Matrix size | 32 × 8 |
| Pixel layout | Horizontal, left-to-right, top-to-bottom (no serpentine) |

Mind the "no serpentine" row. The LED strip inside your display may be wired as a zigzag, but
AWTRIX already handles that. Set your controller to a plain left-to-right layout.

---

## Driving it from your own code

### An animation loop

Keep the `artdmx` and `send_frame` helpers from the quickstart and swap the red loop for this
scrolling rainbow. It keeps the display for as long as it runs:

```python
import colorsys

t = 0.0
while True:
    frame = []
    for y in range(HEIGHT):
        for x in range(WIDTH):
            hue = ((x / WIDTH) + t) % 1.0
            r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
            frame.append((int(r * 255), int(g * 255), int(b * 255)))
    send_frame(frame)
    t += 0.01
    time.sleep(1 / 40)
```

### Send a complete frame

When your first frame arrives (after at least 5 seconds without frames), the display is cleared to
black first, so no leftover of the previous app stays visible.

After that, Art-Net changes only the pixels it receives. A pixel you set keeps its color until you
set it again, so a partial frame leaves the earlier pixels in place. For the 32×8 example, send both universes and all 256 pixels every frame
unless you want that persistence effect. For other ESP32 geometries, send the
active `capabilities.display.width × height` pixels across the required universes.

### Frame rate

Frames are shown as they arrive, so your program sets the pace. 30–50 fps is a good target. If you
send faster than the display can show, the extra frames are skipped. Faster is not smoother.

---

## What Art-Net does not control

Art-Net sets **pixel colors only**. Everything else stays under AWTRIX's control while your frames
play:

- **Brightness** still comes from the brightness setting and, if enabled, the ambient light sensor,
  so auto-brightness can dim your frames as the room darkens. For predictable output, turn
  auto-brightness off and pin a fixed brightness. See
  [Brightness settings](../reference/settings.md#brightness).
- **Saturation, gamma, color correction and tint** still apply to your pixels, so the LEDs do not
  show exactly the values you send. A display left at `saturation: 0` shows
  your frames in grays. See
  [Display color pipeline](../reference/visuals.md#display-color-pipeline).
- **Wiring layout** is handled by AWTRIX, as described above.
- **Sound, buttons, MQTT and the HTTP API** keep working normally while you send frames.

---

## Security

Art-Net is **off** by default, so nothing listens on UDP port 6454. Art-Net itself has no
password. Once you switch it on, any device on your network can take over the display, 5 seconds at
a time. The web UI login does not protect it.

Leave Art-Net off unless you need it, especially on a network you do not trust. To switch it off,
clear the **Art-Net** toggle in the web UI, or send `{"artnet":false}` to `PUT /api/v1/system`.
AWTRIX stops listening at once and the apps come back.

---

## Good to know {#when-it-goes-wrong}

- **Nothing happens at all.** Check that Art-Net is on, that the display is switched on, and that
  AWTRIX answers `curl http://<awtrix-ip>/api/v1/device` on your Wi-Fi, not in its setup hotspot.
- **The display flickers back to apps.** There are gaps of more than 5 seconds between your
  frames, or frames get lost: send without pauses.
- **Only the top five rows of the display respond.** You send universe 0 only, which ends ten
  pixels into the sixth row: send pixels 170–255 in universe 1.
- **Colors are wrong.** Set the pixel order to RGB, not GRB or RGBW. If colors are right but dull
  or shifted, check the color settings (saturation, gamma, tint).
- **The image is scrambled or zigzagged.** Your controller adds its own serpentine layout on top
  of AWTRIX's: set it to a plain left-to-right layout.

---

## Related

- [Find your clock](../getting-started/discovery.md): get the IP or hostname
- [Panel and orientation](../reference/system.md#panel-and-orientation): wiring layouts
- [Display color pipeline](../reference/visuals.md#display-color-pipeline): gamma and correction
- [Brightness settings](../reference/settings.md#brightness): pin a fixed brightness
- [HTTP API: Display](../reference/http.md#display): power the display on and off
