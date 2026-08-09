# Build your own AWTRIX

AWTRIX NG is not tied to the Ulanzi TC001. It drives **any 32-128 x 8 WS2812-style panel** on a
board you wired yourself, and every peripheral around that panel is optional. This page is the
hardware side of that: what to buy, what to connect where, and what to configure once it boots.

The firmware itself needs exactly two things from your build: a **matrix on a supported data
pin**, and **power**. Buttons, light sensor, battery, sound and the environment sensor are all
add-ons you can leave off and add later - the pin map is runtime configuration, not a compile-time
choice.

!!! info "Read this alongside two reference pages"
    [GPIO & boards](../reference/gpio.md) is the authoritative rule set for pins, and
    [System configuration](../reference/system.md) is the field-by-field reference for everything
    you write afterwards. This page is the build order; those two are the details.

---

## 1. Pick the chip first

Everything else follows from it. Both are supported first-class, with their own firmware image.

| | ESP32 (classic) | **ESP32-S3** |
|---|---|---|
| Firmware image | `usb-awtrix-ng-4mb.bin` (or 8/16 MB) | `usb-awtrix-ng-s3-octal-<flash>.bin`, or the `-s3-quad-` one if that finds no PSRAM |
| Usable GPIO | 0-39, of which 34-39 are input-only | 0-48 except 22-25, **no input-only pins** |
| ADC for battery + LDR | GPIO 32-39 | GPIO 1-10 |
| Panel, apps, scripting, MQTT, Art-Net | yes | yes |
| **Internet radio / I2S audio** | no | **yes**, with PSRAM and an I2S DAC |
| PSRAM | not required, not used | needed for the radio |
| USB | external USB-serial bridge on most boards | native USB |

**Recommendation: an ESP32-S3 DevKitC-1 N16R8** (16 MB flash, 8 MB octal PSRAM). It is the board
the S3 image is built against, it has flash for a large icon and script library, and PSRAM is what
makes [Internet radio](../guides/radio.md) available. A classic ESP32 with 4 MB is entirely fine
for a clock that never plays a stream.

!!! warning "Buy an S3 board with PSRAM if you want radio"
    An `N16R8`/`N8R8` has it, a plain `N16` does not - and the Radio section stays hidden without it.
    The firmware reserves GPIO 26-37 for flash and PSRAM on every S3 build regardless, so a
    PSRAM-less board buys you no extra pins.

    An S3 reaches its PSRAM over one of two wirings, and the image has to match - what is printed
    on the board does not settle it. Write the `-octal-` image, then look at **PSRAM** on the device
    page: a size means done, `none` on a board that has PSRAM means write the `-quad-` one. Details:
    [Flashing](../getting-started/flashing.md#which-of-the-two-s3-images).

---

## 2. Bill of materials

### Required

| Part | Notes |
|---|---|
| **ESP32-S3 DevKitC-1 N16R8** (or any ESP32) | See above. |
| **WS2812B panel, 8 pixels high** | 32 x 8 is the classic size. Total width must be 32-128 px. SK6812 and compatible clones work; **APA102 / SK9822 / anything with a separate clock line does not.** |
| **5 V power supply** | Sizing is in [section 4](#4-power-the-panel-first). 5 V / 3-4 A for a 32 x 8 build. |
| **1000 uF / 6.3 V+ electrolytic capacitor** | Across 5 V and GND at the panel input. |
| **330-470 ohm resistor** | In series with the data line, at the board end. |
| A diffuser | 2-3 mm milky acrylic, or a 3D-printed grid + paper. Bare WS2812B pixels are unreadable as text. |

### Optional - add what you want

| Part | Enables | Guide |
|---|---|---|
| 3 x momentary push button | App navigation, menus, deep-sleep wake | [Buttons](../reference/system.md#buttons) |
| LDR (e.g. GL5528) + 10 k resistor | Auto-brightness | [Brightness & sensors](../guides/brightness.md) |
| BME280 / BMP280 / HTU21DF / SHT31 | Temperature, humidity, pressure apps | [Sensor bus](../reference/gpio.md#sensor-bus) |
| Passive piezo buzzer | RTTTL melodies and beeps | [MP3s & melodies](../guides/sounds.md) |
| **MAX98357A** I2S DAC + 4-8 ohm speaker | Internet radio (S3 + PSRAM only) | [Internet radio](../guides/radio.md) |
| DFPlayer Mini + microSD + speaker | numbered tracks alongside the buzzer | [DFPlayer boards](../guides/sounds.md#dfplayer-boards) |
| Li-Ion cell + TP4056 charger + 2 x 100 k | Battery operation and reporting | [Power & battery](../guides/power.md) |
| 74AHCT125 level shifter | Reliable 5 V data on long runs | [section 4](#4-power-the-panel-first) |

You do not need all of it, and nothing here is soldered into the firmware: a peripheral you skip
gets `-1` in the pin map and disappears cleanly - its keys vanish from device state, its Home
Assistant entities are not published, and its built-in app drops out of the rotation.

---

## 3. The standard pinout

These are the firmware defaults. A board wired exactly like this needs **no pin configuration at
all** - flash it and it works.

=== "ESP32-S3 (recommended)"

    | Function | GPIO | Direction | Notes |
    |---|---|---|---|
    | **Matrix data** | **21** | out | From the [driver whitelist](#pins-you-cannot-freely-choose). |
    | Button left | 11 | in, pull-up | Active LOW, wire to GND. |
    | Button select | 12 | in, pull-up | Also the deep-sleep wake pin. |
    | Button right | 13 | in, pull-up | |
    | Battery tap | 1 | ADC1 | Must be GPIO 1-10. |
    | LDR tap | 2 | ADC1 | Must be GPIO 1-10. |
    | Buzzer | 7 | out | Passive piezo. |
    | I2C SDA | 8 | bidirectional | Environment sensor. |
    | I2C SCL | 9 | bidirectional | |
    | DFPlayer RX | 17 | in | Set to `-1` if unused. |
    | DFPlayer TX | 18 | out | Set to `-1` if unused. |
    | I2S BCLK | 5 | out | To the DAC's BCLK. |
    | I2S LRCLK | 6 | out | To the DAC's LRC / WS. |
    | I2S DOUT | 4 | out | To the DAC's DIN. |

=== "ESP32 (classic)"

    Identical to the Ulanzi TC001 wiring - a conversion of that hardware needs no changes either.

    | Function | GPIO | Direction | Notes |
    |---|---|---|---|
    | **Matrix data** | **32** | out | |
    | Button left | 26 | in, pull-up | |
    | Button select | 27 | in, pull-up | Deep-sleep wake pin. |
    | Button right | 14 | in, pull-up | |
    | Battery tap | 34 | ADC1 | Input-only pin, which is fine for an ADC. |
    | LDR tap | 35 | ADC1 | |
    | Buzzer | 15 | out | |
    | I2C SDA | 21 | bidirectional | |
    | I2C SCL | 22 | bidirectional | |
    | DFPlayer RX | 23 | in | |
    | DFPlayer TX | 18 | out | |
    | I2S | - | - | **Not available on the ESP32.** No radio. |

### System diagram

Required blocks are the panel and its supply; everything to the left and below is optional.

<div class="awx-figure">
<svg viewBox="0 0 920 650" role="img" aria-label="AWTRIX NG DIY wiring block diagram" style="width:100%;height:auto;font-family:var(--md-text-font-family,system-ui)">
<style>
.awx-box{fill:var(--md-code-bg-color);stroke:var(--md-default-fg-color--lighter);stroke-width:1.5}
.awx-mcu{fill:var(--md-default-bg-color);stroke:var(--md-default-fg-color);stroke-width:2}
.awx-t{fill:var(--md-default-fg-color);font-size:13px;font-weight:600}
.awx-s{fill:var(--md-default-fg-color--light);font-size:11px}
.awx-p{fill:var(--md-default-fg-color--light);font-size:11px;font-family:var(--md-code-font-family,monospace)}
.awx-pw{stroke:#e5484d;stroke-width:2.5;fill:none;stroke-linecap:round}
.awx-gn{stroke:#8b8f96;stroke-width:2.5;fill:none;stroke-linecap:round}
.awx-sg{stroke:#3b82f6;stroke-width:2;fill:none;stroke-linecap:round}
.awx-jp{fill:#e5484d}
.awx-jg{fill:#8b8f96}
</style>
<rect class="awx-box" x="60" y="30" width="190" height="100" rx="6"/>
<text class="awx-t" x="76" y="62">5 V supply</text>
<text class="awx-s" x="76" y="84">3-4 A for 32 x 8</text>
<text class="awx-s" x="76" y="104">18 AWG to the panel</text>
<path class="awx-pw" d="M250 50 H880"/>
<path class="awx-gn" d="M250 110 H880"/>
<text class="awx-p" x="258" y="42">+5 V</text>
<text class="awx-p" x="258" y="128">GND</text>
<path class="awx-pw" d="M600 50 V72"/>
<path class="awx-pw" d="M584 72 H616"/>
<path class="awx-gn" d="M584 84 H616"/>
<path class="awx-gn" d="M600 84 V110"/>
<text class="awx-p" x="624" y="82">1000 uF</text>
<path class="awx-pw" d="M400 50 V170"/>
<path class="awx-gn" d="M440 110 V170"/>
<text class="awx-p" x="404" y="162">5V</text>
<text class="awx-p" x="444" y="162">GND</text>
<path class="awx-pw" d="M760 50 V200"/>
<path class="awx-gn" d="M812 110 V200"/>
<text class="awx-p" x="764" y="192">5V</text>
<text class="awx-p" x="816" y="192">GND</text>
<rect class="awx-mcu" x="360" y="170" width="200" height="400" rx="8"/>
<text class="awx-t" x="460" y="210" text-anchor="middle">ESP32-S3</text>
<text class="awx-s" x="460" y="228" text-anchor="middle">DevKitC-1 N16R8</text>
<rect class="awx-box" x="660" y="200" width="220" height="100" rx="6"/>
<text class="awx-t" x="770" y="238" text-anchor="middle">WS2812B panel</text>
<text class="awx-s" x="770" y="258" text-anchor="middle">32 x 8 = 256 LEDs</text>
<text class="awx-s" x="770" y="278" text-anchor="middle">height is always 8 px</text>
<path class="awx-sg" d="M560 330 H596"/>
<rect class="awx-box" x="596" y="322" width="36" height="16" rx="2"/>
<path class="awx-sg" d="M632 330 H648 V270 H660"/>
<text class="awx-p" x="614" y="316" text-anchor="middle">470 R</text>
<text class="awx-p" x="666" y="266">DIN</text>
<text class="awx-p" x="552" y="334" text-anchor="end">GPIO 21</text>
<rect class="awx-box" x="60" y="240" width="230" height="110" rx="6"/>
<text class="awx-t" x="76" y="268">3 x push button</text>
<text class="awx-s" x="76" y="290">each to GND, INPUT_PULLUP</text>
<text class="awx-s" x="76" y="310">left / select / right</text>
<text class="awx-s" x="76" y="334">select = deep-sleep wake</text>
<path class="awx-sg" d="M290 270 H360"/>
<path class="awx-sg" d="M290 292 H360"/>
<path class="awx-sg" d="M290 314 H360"/>
<text class="awx-p" x="368" y="274">GPIO 11</text>
<text class="awx-p" x="368" y="296">GPIO 12</text>
<text class="awx-p" x="368" y="318">GPIO 13</text>
<rect class="awx-box" x="60" y="386" width="230" height="54" rx="6"/>
<text class="awx-t" x="76" y="410">LDR divider</text>
<text class="awx-s" x="76" y="430">GL5528 + 10 k, ADC1</text>
<path class="awx-sg" d="M290 412 H360"/>
<text class="awx-p" x="368" y="416">GPIO 2</text>
<rect class="awx-box" x="60" y="470" width="230" height="54" rx="6"/>
<text class="awx-t" x="76" y="494">Battery divider</text>
<text class="awx-s" x="76" y="514">100 k / 100 k, ADC1</text>
<path class="awx-sg" d="M290 496 H360"/>
<text class="awx-p" x="368" y="500">GPIO 1</text>
<rect class="awx-box" x="660" y="330" width="220" height="52" rx="6"/>
<text class="awx-t" x="676" y="354">Passive piezo buzzer</text>
<text class="awx-s" x="676" y="372">RTTTL melodies</text>
<path class="awx-sg" d="M560 356 H660"/>
<text class="awx-p" x="552" y="360" text-anchor="end">GPIO 7</text>
<rect class="awx-box" x="660" y="410" width="220" height="64" rx="6"/>
<text class="awx-t" x="676" y="436">BME280 / SHT31</text>
<text class="awx-s" x="676" y="458">I2C, auto-detected at boot</text>
<path class="awx-sg" d="M560 428 H660"/>
<path class="awx-sg" d="M560 452 H660"/>
<text class="awx-p" x="552" y="432" text-anchor="end">GPIO 8 SDA</text>
<text class="awx-p" x="552" y="456" text-anchor="end">GPIO 9 SCL</text>
<rect class="awx-box" x="660" y="500" width="220" height="82" rx="6"/>
<text class="awx-t" x="676" y="526">MAX98357A + speaker</text>
<text class="awx-s" x="676" y="548">I2S, S3 with PSRAM only</text>
<text class="awx-s" x="676" y="568">internet radio</text>
<path class="awx-sg" d="M560 516 H660"/>
<path class="awx-sg" d="M560 538 H660"/>
<path class="awx-sg" d="M560 560 H660"/>
<text class="awx-p" x="552" y="520" text-anchor="end">GPIO 5 BCLK</text>
<text class="awx-p" x="552" y="542" text-anchor="end">GPIO 6 LRC</text>
<text class="awx-p" x="552" y="564" text-anchor="end">GPIO 4 DIN</text>
<path class="awx-pw" d="M60 618 H100"/>
<text class="awx-s" x="108" y="622">+5 V</text>
<path class="awx-gn" d="M160 618 H200"/>
<text class="awx-s" x="208" y="622">GND</text>
<path class="awx-sg" d="M256 618 H296"/>
<text class="awx-s" x="304" y="622">signal</text>
<circle class="awx-jp" cx="400" cy="50" r="4"/>
<circle class="awx-jp" cx="600" cy="50" r="4"/>
<circle class="awx-jp" cx="760" cy="50" r="4"/>
<circle class="awx-jg" cx="440" cy="110" r="4"/>
<circle class="awx-jg" cx="600" cy="110" r="4"/>
<circle class="awx-jg" cx="812" cy="110" r="4"/>
<text class="awx-s" x="372" y="622">Peripheral ground returns are omitted - every block shares one ground with the board.</text>
</svg>
</div>

### Connection list

**Power** - run these before anything else, and never through the dev board's regulator.

| From | To | Wire |
|---|---|---|
| PSU +5 V | Panel 5 V, both ends on a wide panel | 18 AWG |
| PSU GND | Panel GND **and** board GND | 18 AWG |
| PSU +5 V / GND | 1000 uF capacitor, at the panel input | short leads |
| PSU +5 V | Board 5 V / VIN pin | 22 AWG |

**Signals**

| Peripheral | Its pin | MCU pin (S3) | MCU pin (ESP32) | In line |
|---|---|---|---|---|
| Panel | DIN | GPIO 21 | GPIO 32 | 470 ohm series resistor |
| Button left | one leg | GPIO 11 | GPIO 26 | other leg to GND |
| Button select | one leg | GPIO 12 | GPIO 27 | other leg to GND |
| Button right | one leg | GPIO 13 | GPIO 14 | other leg to GND |
| LDR divider | tap | GPIO 2 | GPIO 35 | 10 k to GND |
| Battery divider | tap | GPIO 1 | GPIO 34 | 100 k / 100 k |
| Buzzer | + | GPIO 7 | GPIO 15 | - to GND |
| I2C sensor | SDA | GPIO 8 | GPIO 21 | 4.7 k pull-up if the breakout has none |
| I2C sensor | SCL | GPIO 9 | GPIO 22 | same |
| MAX98357A | BCLK | GPIO 5 | not available | - |
| MAX98357A | LRC / WS | GPIO 6 | not available | - |
| MAX98357A | DIN | GPIO 4 | not available | - |
| DFPlayer Mini | RX | GPIO 18 (TX) | GPIO 18 (TX) | 1 k in series |
| DFPlayer Mini | TX | GPIO 17 (RX) | GPIO 23 (RX) | - |

The DFPlayer and the I2S DAC are alternatives, not companions: the firmware picks one sound
backend. Leave the pins of the one you did not build at `-1`.

### Pins you cannot freely choose

Four hard rules, all enforced on every write and re-checked at boot. The full rule set, the exact
error messages and the ESP32 equivalents are in [GPIO & boards](../reference/gpio.md#validation-rules).

| Rule | ESP32-S3 | ESP32 |
|---|---|---|
| **Matrix pin** must come from the compiled driver list | 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47 | 2, 4, 5, 13, 14, 15, 16, 18, 21, 25, 26, 27, 32, 33 |
| **Battery and LDR** must be ADC1 | 1-10 | 32-39 |
| **Reserved**, never assignable | 19-20 (USB-JTAG), 26-37 (flash + PSRAM), 43-44 (console) | 6-11 (SPI flash) |
| **Input-only**, so no buttons/buzzer/I2C/TX there | none | 34-39 |

Two more the firmware accepts but your hardware may not:

* **Strapping pins** - 0, 3, 45, 46 on the S3; 0, 2, 5, 12, 15 on the ESP32. They are sampled at
  reset, so anything holding them high or low can stop the board from booting. Accepted by
  validation, punished by physics.
* **Deep-sleep wake** - only a select button on GPIO 0-21 (S3) or 0, 2, 4, 12-15, 25-27, 32-39
  (ESP32) can end a [`/device/sleep`](../reference/http.md#post-apiv1devicesleep) early. Any other
  pin works normally while awake and simply cannot wake the board.

Never hardcode these tables into your own tooling - the running device reports its own rules:

```bash
curl http://<awtrix-ip>/api/v1/capabilities
```

---

## 4. Power the panel first

This is where DIY builds fail, not in the firmware.

**AWTRIX NG does not limit LED current.** There is no software power cap between a white frame and
your supply, so size the supply for the worst case you can actually reach: full white at
brightness 255, through moodlight, Art-Net or a script.

| Build | LEDs | Worst case (all white, full brightness) | Realistic clock use |
|---|---|---|---|
| 32 x 8 | 256 | ~15 A at 5 V | 0.3-0.8 A |
| 64 x 8 | 512 | ~30 A at 5 V | 0.6-1.5 A |

Nobody builds for the theoretical maximum. **5 V / 3-4 A for a 32 x 8 panel** is the sane
compromise: it covers every normal app plus a bright notification, and the way to stay inside it
is to cap brightness rather than to oversize the supply.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness":true,"brightness":120}'
```

The wiring rules that go with it:

1. **Never power the panel through the dev board's 5 V pin.** Feed the panel from the supply
   directly; the board taps the same supply.
2. **Common ground.** Board GND and panel GND must be joined, or the data line has no reference
   and the panel shows noise.
3. **1000 uF across 5 V/GND at the panel input**, and a **330-470 ohm resistor in series with the
   data line** at the board end. Both suppress the inrush edge that kills the first pixel.
4. **Inject power at both ends** on anything wider than 32 px, and use proper wire - 18 AWG for
   the 5 V run, not breadboard jumpers.
5. **Data level.** WS2812B wants 0.7 x VDD on DIN, which is 3.5 V - above the 3.3 V an ESP32
   drives. Short runs usually work anyway. If the first pixels flicker or show wrong colours, add
   a **74AHCT125** level shifter, or drop the panel's supply to ~4.5 V with a series diode so 3.3 V
   clears the threshold.

!!! danger "Li-Ion safety"
    A battery build needs a protected cell and a proper charger (TP4056 with protection, or a
    dedicated PMIC). Never connect a raw cell to a GPIO - only through the divider in
    [section 5](#battery-monitoring). Charging is outside what the firmware does or monitors.

---

## 5. Wire the options

### Buttons

Three momentary buttons from the GPIO to **GND**. The firmware configures `INPUT_PULLUP`, so
pressed reads LOW. No external resistors needed; a 100 nF cap across each button quiets a noisy
mechanical switch. Debounce and the double-press window are fixed in firmware.

If the panel ends up mounted upside down, fix it in configuration rather than by resoldering -
`rotate` flips the picture *and* swaps left/right, `swapButtons` swaps them on their own.

### Light sensor (auto-brightness)

A GL5528-class LDR and a 10 k resistor as a divider into the ADC pin:

<div class="awx-figure">
<svg viewBox="0 0 360 250" role="img" aria-label="LDR voltage divider schematic" style="width:100%;max-width:360px;height:auto;font-family:var(--md-text-font-family,system-ui)">
<style>
.awl-w{stroke:var(--md-default-fg-color);stroke-width:1.8;fill:none;stroke-linecap:round}
.awl-r{fill:var(--md-default-bg-color);stroke:var(--md-default-fg-color);stroke-width:1.8}
.awl-t{fill:var(--md-default-fg-color);font-size:12px}
.awl-c{fill:var(--md-default-fg-color--light);font-size:11px}
.awl-n{fill:var(--md-default-fg-color)}
</style>
<text class="awl-t" x="100" y="24" text-anchor="middle">3V3</text>
<path class="awl-w" d="M100 32 V60"/>
<rect class="awl-r" x="82" y="60" width="36" height="52" rx="3"/>
<path class="awl-w" d="M62 66 l16 12 M62 82 l16 12"/>
<polygon class="awl-n" points="78,78 69.8,75.6 73.4,70.8"/>
<polygon class="awl-n" points="78,94 69.8,91.6 73.4,86.8"/>
<text class="awl-t" x="128" y="82">LDR</text>
<text class="awl-c" x="128" y="98">GL5528</text>
<path class="awl-w" d="M100 112 V138"/>
<circle class="awl-n" cx="100" cy="138" r="4"/>
<path class="awl-w" d="M100 138 H236"/>
<text class="awl-t" x="244" y="142">GPIO 2</text>
<text class="awl-c" x="244" y="158">ADC1</text>
<path class="awl-w" d="M100 138 V164"/>
<rect class="awl-r" x="82" y="164" width="36" height="52" rx="3"/>
<text class="awl-t" x="128" y="194">10 k</text>
<path class="awl-w" d="M100 216 V228"/>
<path class="awl-w" d="M80 228 H120 M86 236 H114 M92 244 H108"/>
</svg>
</div>

Wired this way, brighter means a higher voltage, which is what the firmware expects
(`ldrOnGround: false`). Swap the two parts and set `{"ldrOnGround": true}` instead - the reading
is simply inverted.

Then calibrate: `ldrFactor` decides what counts as full light on *your* divider, `ldrGamma` shapes
the response curve. Both apply live. Full procedure in
[Brightness & sensors](../guides/brightness.md).

!!! warning "No LDR? Switch auto-brightness off"
    With `pinLdr: -1` the reading is indistinguishable from a pitch-dark room, so an enabled
    `autoBrightness` pins the panel at `minBrightness` forever. Set `brightness` manually instead.

### Battery monitoring

A plain 2:1 divider from the cell into an ADC1 pin:

<div class="awx-figure">
<svg viewBox="0 0 360 250" role="img" aria-label="Battery voltage divider schematic" style="width:100%;max-width:360px;height:auto;font-family:var(--md-text-font-family,system-ui)">
<style>
.awb-w{stroke:var(--md-default-fg-color);stroke-width:1.8;fill:none;stroke-linecap:round}
.awb-r{fill:var(--md-default-bg-color);stroke:var(--md-default-fg-color);stroke-width:1.8}
.awb-t{fill:var(--md-default-fg-color);font-size:12px}
.awb-c{fill:var(--md-default-fg-color--light);font-size:11px}
.awb-n{fill:var(--md-default-fg-color)}
</style>
<text class="awb-t" x="100" y="24" text-anchor="middle">BAT+</text>
<text class="awb-c" x="100" y="40" text-anchor="middle">4.2 V max</text>
<path class="awb-w" d="M100 48 V60"/>
<rect class="awb-r" x="82" y="60" width="36" height="52" rx="3"/>
<text class="awb-t" x="128" y="90">100 k</text>
<path class="awb-w" d="M100 112 V138"/>
<circle class="awb-n" cx="100" cy="138" r="4"/>
<path class="awb-w" d="M100 138 H236"/>
<text class="awb-t" x="244" y="142">GPIO 1</text>
<text class="awb-c" x="244" y="158">2.1 V at 4.2 V cell</text>
<path class="awb-w" d="M100 138 V164"/>
<rect class="awb-r" x="82" y="164" width="36" height="52" rx="3"/>
<text class="awb-t" x="128" y="194">100 k</text>
<path class="awb-w" d="M100 216 V228"/>
<path class="awb-w" d="M80 228 H120 M86 236 H114 M92 244 H108"/>
</svg>
</div>

4.2 V at the cell becomes 2.1 V at the pin - inside the ADC's range with margin. Tell the firmware
the ratio, then correct it against a known-full cell:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"batteryDividerRatio":2.0}'
```

`batteryDividerRatio` is `V_cell / V_pin`. Charge fully, read `batteryPinMillivolts` from
`GET /api/v1/device`, and write `4.2 / (batteryPinMillivolts / 1000)`. The percentage comes off a
Li-Ion discharge curve, not a fuel gauge - see [Power & battery](../guides/power.md).

### Environment sensor

Any one of **BME280** (`0x76`/`0x77`), **BMP280** (same addresses), **HTU21DF** or **SHT31**
(`0x44`) on the I2C bus. Detection is automatic at boot in that order - the first chip that answers
wins, so put only one on the bus. Most breakouts carry their own pull-ups; add 4.7 k to 3V3 on both
lines if yours does not.

Temperature comes from all four; humidity from BME280, HTU21DF and SHT31; pressure from BME280 and
BMP280. Trim self-heating with `tempOffset` and `humOffset`.

### Sound - three mutually useful options

| Option | Hardware | What you get | Requires |
|---|---|---|---|
| **Passive buzzer** | Piezo on `pinBuzzer` | RTTTL melodies, notification beeps | anything |
| **DFPlayer Mini** | Module on `pinDfRx`/`pinDfTx` + microSD | MP3 playback by track number | `dfplayer: true` and both pins set |
| **I2S DAC** | MAX98357A on the three I2S pins | Internet radio streams | **ESP32-S3 + PSRAM** |

**Buzzer**: a *passive* piezo (not a self-driving active buzzer) straight on the GPIO. It is quiet
by design; a small NPN transistor with a 100 ohm base resistor makes it usable.

**DFPlayer Mini**: 5 V supply, a 1 k resistor in the line into the module's RX, and the speaker on
SPK1/SPK2. It replaces the buzzer entirely - it plays files, not notes, so RTTTL melodies make no
sound on such a board. Set `dfplayer: true`.

**MAX98357A**: BCLK, LRC and DIN to the three I2S pins, plus 3V3/GND and a 4-8 ohm speaker. The
board needs no separate amplifier. The three I2S pins are validated as a **set** - all three
assigned, or all three `-1`; a partial set is rejected with a `422`. A UDA1334A or PCM5102A works
identically.

---

## 6. Describe the panel

The pixel height is fixed at 8. Everything else about your matrix is configuration:

| Key | Range | Default | Meaning |
|---|---|---|---|
| `panelWidth` | 1-128 | `32` | Width of one panel. |
| `panels` | 1-128 | `1` | How many identical panels the cable runs through, left to right. `panelWidth x panels` must land in 32-128. |
| `panelStart` | `topLeft` `topRight` `bottomLeft` `bottomRight` | `topLeft` | Corner the first LED sits in. |
| `panelWiring` | `rows` `columns` | `rows` | Whether the strip runs along rows or down columns. |
| `panelSerpentine` | bool | `true` | Every second run comes back the other way - the usual zigzag. |
| `panelChainReverse` | bool | `false` | The cable enters the chain at the other end. Does not change how a panel is wired inside. |
| `panelChainSerpentine` | bool | `false` | Every second panel is mounted rotated 180°, so its output sits beside the next panel's input. |
| `mirror` / `rotate` | bool | `false` | A convenience for a panel mounted the wrong way round; each is equivalent to picking a different `panelStart`. `rotate` additionally swaps the left and right button. |

`panelStart`, `panelWiring` and `panelSerpentine` describe one panel; the two chain keys describe
how the panels are joined to each other. On a single-panel build the chain keys cannot change
anything.

Common builds:

| Build | Configuration |
|---|---|
| Standard 32 x 8 panel | the defaults |
| Four chained 8 x 8 tiles | `panelWidth` 8, `panels` 4, `panelSerpentine` false |
| Four 8 x 8 tiles, each wired from its right edge | `panelWidth` 8, `panels` 4, `panelStart` `topRight`, `panelChainReverse` true |
| Tiles mounted alternately, output next to input | `panelChainSerpentine` true |
| 32 x 8 wired in columns | `panelWiring` `columns` |
| 64 px wide panel | `panelWidth` 64 |

If the picture comes out scrambled, try `panelSerpentine` first, then `panelStart`, then
`panelWiring`. If each panel then looks right but the panels are in the wrong order, or every
second one is upside down, that is `panelChainReverse` and `panelChainSerpentine`. All of them
re-apply on the **next frame**, so you can watch the panel while you change them. Only a change
to the total width needs a reboot.

---

## 7. Flash and configure

1. **Flash the image for your chip** - [Flashing](../getting-started/flashing.md), browser flasher
   or `esptool`. Tick *erase the whole flash* on a fresh board.
2. **Join Wi-Fi** through the setup access point - [First boot](../getting-started/first-boot.md).
3. **Write the pin map.** Send it complete, in one request: cross-field rules (duplicates, the I2S
   trio) are checked against the merged map, so a partial write can be rejected for a conflict you
   are in the middle of resolving.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{
        "pinMatrix": 21,
        "pinBtnLeft": 11,
        "pinBtnSelect": 12,
        "pinBtnRight": 13,
        "pinBattery": -1,
        "pinLdr": 2,
        "pinBuzzer": 7,
        "pinI2cSda": 8,
        "pinI2cScl": 9,
        "pinDfRx": -1,
        "pinDfTx": -1,
        "pinI2sBclk": 5,
        "pinI2sLrclk": 6,
        "pinI2sDout": 4,
        "dfplayer": false,
        "panelWidth": 32,
        "panels": 1
      }'
```

4. **Reboot** - the pin map is read at startup and nothing in it applies before that:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

5. **Calibrate** what you wired: `batteryDividerRatio` for the divider, `ldrFactor` / `ldrGamma` /
   `ldrOnGround` for the light sensor, `tempOffset` / `humOffset` for the sensor. The defaults are
   the Ulanzi TC001's and will be wrong for your parts.
6. **Tune the colours** if the panel runs cold or warm: `colorCorrection` and `colorTint` in
   [Settings](../reference/settings.md).

The same map is editable under **System -> GPIO** in the web UI, where each field is a dropdown
already filtered to what your chip can do with that peripheral.

!!! success "You cannot brick it with a pin map"
    The stored map is validated again at boot. If it does not pass - hand-edited, or an ESP32 map
    on an S3 - the board falls back to the chip's defaults, comes up, and stays reachable so you
    can fix it. The bad map is kept, not rewritten, so the fallback repeats until you save a valid
    one.

---

## 8. Verify the build

Work down this list; each step isolates one part of the hardware.

| Check | How | Expected |
|---|---|---|
| Chip and rules | `curl http://<ip>/api/v1/capabilities` | `soc` matches your board; `gpio` lists the ranges from [section 3](#pins-you-cannot-freely-choose) |
| Panel geometry | Web UI **System -> Panel** | `32 x 8 = 256 LEDs`, or your size |
| Every pixel | Send a full-white notification, or a moodlight frame | No dead pixels, no colour shift down the run |
| Colour order | Push red text | Red, not green or blue |
| Buttons | Press each | The app rotation moves; `state/buttons/<button>` fires over MQTT |
| Light sensor | `curl http://<ip>/api/v1/device` while covering the LDR | `lightLevel` falls towards 0 |
| Battery | same call | `batteryVoltage` near 4.2 V on a full cell |
| Sensor | same call | `temperature` present and plausible |
| Sound | Play a melody, or a radio station | Audible |

---

## Troubleshooting a fresh build

| Symptom | Cause |
|---|---|
| Panel dark, device reachable | Wrong `pinMatrix`, no common ground, or the panel has no 5 V of its own |
| First pixel wrong colour, rest fine | Missing series resistor or the 1000 uF cap; data edge too sharp |
| Flicker, colours drift down the strip | 3.3 V data on 5 V pixels - add a level shifter or drop the panel supply to ~4.5 V |
| Picture scrambled or mirrored | `panelSerpentine`, then `panelStart`, then `panelWiring` |
| Panels each correct but in the wrong order | `panelChainReverse` - the cable enters the chain at the other end |
| Every second panel upside down | `panelChainSerpentine` - the tiles are mounted alternately |
| Board resets on bright frames | Supply too small, or panel current flowing through the dev board |
| Buttons dead or inverted | Wired to 3V3 instead of GND - `INPUT_PULLUP` expects a pull to ground |
| Left/right reversed | `swapButtons`, or `rotate` if the whole panel is upside down |
| Panel stuck dim with `autoBrightness` on | No LDR, or `ldrOnGround` set the wrong way |
| Percentage nonsense | `batteryDividerRatio` still at the default |
| No temperature | Sensor not on the bus, missing pull-ups, or a second chip answering first |
| Radio section missing, `/api/v1/audio/play` returns `503` | Not an S3 image, the I2S pins are `-1`, or the device page shows **PSRAM: none** - no PSRAM, or the `-quad-` image is the one this board needs |
| `invalidPinConfig` on a write | The message names the field and the rule - [Errors](../reference/errors.md#gpio-validation-invalidpinconfig) |

---

## Where to go next

* [GPIO & boards](../reference/gpio.md) - every validation rule, the web UI dropdowns, recovery
* [System configuration](../reference/system.md) - all fields you wrote above, plus Wi-Fi, MQTT, NTP
* [Brightness & sensors](../guides/brightness.md) and [Power & battery](../guides/power.md) - calibration
* [Internet radio](../guides/radio.md) - stations, volume, the S3 requirements
* [App scripting](../guides/scripting.md) - make the panel do something only your build can
* [Building from source](building.md) - if you want to change the firmware itself
