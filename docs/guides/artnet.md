# Art-Net

Art-Net drives every LED on the panel directly from software, in real time. Instead of sending an
app or a notification and letting AWTRIX render it, **you** render the frame on your computer and
ship the raw pixels over the network. AWTRIX becomes a plain 32×8 display.

Use it for live visualisers, VJ software, screen mirroring, a custom animation loop in Python -
anything where you want frame-by-frame control and the app rotation is in your way.

Art-Net ships **disabled**. Turn it on with the [`artnet` flag](../reference/system.md#art-net) in
the device config (`PUT /api/v1/system`, or the **Art-Net** toggle under *Misc* in the web UI);
AWTRIX then listens on UDP port **6454** whenever it is on your Wi-Fi. There is no
authentication - see [Security](#security).

---

## Light up the panel in 30 seconds

Art-Net is a UDP protocol, so `curl` cannot send frames to it - `curl` speaks TCP only. First
switch Art-Net on, then confirm you have the right address:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"artnet":true}'
curl http://<awtrix-ip>/api/v1/device
```

If the last call returns JSON, AWTRIX is reachable. Now paste this into `artnet.py` and run it with
`python artnet.py` - it fills the whole panel solid red for ten seconds:

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

The panel turns red instantly and stays red while the script runs. Stop the script and about five
seconds later the normal app rotation comes back on its own - you do not have to release anything.

---

## Keep sending

Art-Net is not a mode you enter and leave. It is a five-second hold window: while your last frame
is less than 5 seconds old the panel shows your pixels and nothing else - no apps, no
notifications, no transitions. Five seconds after your last frame, the app rotation resumes.

Two things follow from that.

**Keep sending.** Even for a static image, resend at least once every 5 seconds or AWTRIX drifts
back to its apps. Most senders just run a steady frame loop, which handles this for free.

**You cannot release early.** There is no stop packet and no release command. Stop sending and wait
out the 5 seconds, or send one last frame of the content you want and let it expire. If you need
the panel back *now*, power the display off and on with
[`PATCH /api/v1/display`](../reference/http.md#display).

Two things beat Art-Net while it is holding: a display that is powered off, which keeps the panel
black, and mood light. And in provisioning (AP) mode - the open `awtrixng-xxxxxx` hotspot shown
before Wi-Fi is configured - port 6454 is not open at all. So if your frames appear to do nothing,
check the display is on and that AWTRIX has joined your network. See
[Finding AWTRIX](../getting-started/discovery.md).

---

## Universes and pixel mapping

Art-Net carries 512 DMX channels per universe, and each pixel takes three of them, so one universe
covers 170 pixels. The standard **32 × 8 = 256 pixel** panel therefore spans two universes:

| Universe | Pixels | Channels used | Notes |
|---|---|---|---|
| `0` | 0 – 169 | 510 of 512 | Full universe |
| `1` | 170 – 255 | 258 of 512 | Only 86 pixels - the rest is ignored |

Channel order is plain **RGB**, three channels per pixel, no white channel. The first pixel is DMX
channels 1–3, the second 4–6, and so on.

Pixels run in **row-major order**: pixel 0 is the top-left corner, the last pixel of the top row is
the top-right, the next pixel begins the second row, and the final pixel is the bottom-right. In
code, pixel *p* sits at:

```
x = p % width
y = p / width
```

You address the logical 32×8 grid, not the raw LED strip. AWTRIX applies its wiring map and its
colour pipeline afterwards, exactly as it does for apps, so changing your panel's wiring config
leaves your Art-Net code working unchanged. Widening the panel (`panelWidth × panels`, see
[Panel and orientation](../reference/system.md#panel-and-orientation)) adds pixels, and the mapping
and universe count scale with it: a wider panel spans universe `2` and beyond.

If your controller offers separate Net / Sub-Net / Universe boxes, leave Net and Sub-Net at **0**
and set Universe to 0 and 1.

---

## Using a lighting controller

Any Art-Net-capable software (Resolume, QLC+, Jinx!, xLights, TouchDesigner, Madrix…) can drive the
panel. AWTRIX answers Art-Net discovery and announces itself as **AWTRIX NG**, so it usually turns
up in the controller's node list on its own; entering the IP by hand works just as well.

| Setting | Value |
|---|---|
| Protocol | Art-Net |
| Node address | the IP address of your AWTRIX - or let discovery find it |
| Port | `6454` |
| Net / Sub-Net | `0` |
| Universes | `0` and `1` |
| Pixel order | RGB |
| Matrix size | 32 × 8 |
| Pixel layout | Horizontal, left-to-right, top-to-bottom (no serpentine) |

The "no serpentine" row matters and often surprises people: even though the physical strip inside a
Ulanzi TC001 *is* wired as a zigzag, you must configure your controller for a plain progressive
layout. AWTRIX already handles the snake.

---

## Driving it from your own code

### An animation loop

Keep the `artdmx` and `send_frame` helpers from the quickstart and swap the red loop for this
scrolling rainbow, which keeps the override alive for as long as it runs:

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

### Send all 256 pixels every frame

At the start of a session - the first frame after the hold window has lapsed - the panel is cleared
to black, so a partial first frame never reveals a frozen fragment of whatever app was on screen.

Within a session, though, Art-Net only writes the pixels it receives. A pixel you set in one frame
keeps its colour until you overwrite it, so a later partial frame leaves earlier Art-Net pixels in
place. Send both universes and a full 256 pixels every frame unless you specifically want that
persistence effect.

### Frame rate

Frames are drawn as they arrive, so your sender sets the pace. 30–50 fps is a sensible target.
There is no queue and no buffering: if you send faster than the panel can draw, the extra frames
are overwritten or dropped in transit. Faster is not smoother.

---

## What Art-Net does not control

Art-Net sets **pixel colour only**. Everything else stays under AWTRIX's control while your frames
are playing:

- **Brightness** still comes from the brightness setting and, if enabled, the ambient light sensor,
  so auto-brightness can dim your frames as the room darkens. For predictable output, turn
  auto-brightness off and pin a fixed brightness. See
  [Brightness settings](../reference/settings.md#brightness).
- **Saturation, gamma, colour correction and tint** are applied to your pixels on the way to the
  LEDs, so what you send is not bit-for-bit what lights up. A panel left at `saturation: 0` shows
  your frames in greys. See
  [Display color pipeline](../reference/visuals.md#display-color-pipeline).
- **Wiring layout** is applied afterwards, as described above.
- **Sound, buttons, MQTT and the HTTP API** all keep working normally during an override.

---

## Security

Art-Net ships **disabled**, so out of the box nothing is listening on UDP 6454. Once you turn the
`artnet` flag on there is no authentication - the protocol has none - so anything on your network
that can reach the port can take over the panel for 5 seconds at a time, and the HTTP API's Basic
auth does not cover it.

Leave the flag off unless you need it, and be deliberate about enabling it on a network you do not
trust. To lock it back down, send `{"artnet":false}` to `PUT /api/v1/system` (or clear the
**Art-Net** toggle in the web UI) - the listener closes at once and the panel returns to the app
rotation.

---

## Troubleshooting

**Nothing happens at all.** First check Art-Net is enabled (`{"artnet":true}` on `PUT
/api/v1/system`) - it ships off. Then check the
display is powered on - that beats Art-Net. Confirm the IP with `curl
http://<awtrix-ip>/api/v1/device`, and confirm AWTRIX is on your Wi-Fi and not sitting in its
provisioning hotspot.

**The panel flickers back to apps.** Your frame rate has gaps longer than 5 seconds, or packets are
being lost. Send continuously.

**Only the left three-quarters of the panel responds.** You are sending universe 0 only. Pixels
170–255 live in universe 1.

**Colours are wrong.** Check pixel order is RGB, not GRB or RGBW. If colours are right but dull or
shifted, that is the colour pipeline, not Art-Net.

**The image is scrambled or snaked.** Your controller is applying its own serpentine mapping on top
of the one AWTRIX already applies. Set it to a plain progressive left-to-right layout.

**My controller cannot find AWTRIX.** Make sure Art-Net is enabled (`{"artnet":true}` on `PUT
/api/v1/system`) - with the flag off, AWTRIX neither listens nor answers discovery. With it on the
node should appear on its own; if your controller still misses it, add the IP manually.

---

## Related

- [Finding AWTRIX](../getting-started/discovery.md) - get the IP or hostname
- [Panel and orientation](../reference/system.md#panel-and-orientation) - wiring layouts
- [Display color pipeline](../reference/visuals.md#display-color-pipeline) - gamma and correction
- [Brightness settings](../reference/settings.md#brightness) - pin a fixed brightness
- [HTTP API: Display](../reference/http.md#display) - power the panel on and off
