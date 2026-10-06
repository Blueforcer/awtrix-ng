---
only: [esp32, esp32-s3]
---

# DIY build

This page shows you how to build your own AWTRIX clock: what to buy, how to wire it, and what to
set after the first start.

!!! tip "Rather not build one?"
    You can buy a ready-made clock instead: <!-- only esp32 -->[Get a clock](../getting-started/buy.md)<!-- /only --><!-- only esp32-s3 -->[Get a clock](site:getting-started/buy/)<!-- /only -->.

AWTRIX NG runs on **any WS2812-style LED panel 8 pixels high and 32 to 128 pixels wide**, on an
<!-- only esp32 -->ESP32<!-- /only --><!-- only esp32-s3 -->ESP32-S3<!-- /only --> board you wired yourself.

Your build needs only two things: an **LED panel on a supported data pin** and **power**.
Buttons, light sensor, battery, sound and the temperature sensor are optional. You can leave them
off and add them later. You set the pins in the web UI, not in the firmware.

!!! info "Two pages go with this one"
    [GPIO & boards](../reference/gpio.md) has every pin rule. [System configuration](../reference/system.md)
    describes every setting you write below. This page gives you the build order.

---

## 1. Pick the board

<!-- only esp32 -->
Any classic ESP32 board with 4, 8 or 16 MB of flash works, for example an ESP32 DevKit with a
WROOM-32 module.

| | ESP32 |
|---|---|
| Firmware image | `usb-awtrix-ng-4mb.bin` (or 8/16 MB) |
| Usable GPIO | 0-39, of which 34-39 are input-only |
| ADC for battery + LDR | GPIO 32-39 |
| Sound | Passive buzzer, DFPlayer Mini |
| USB | external USB-serial bridge on most boards |

A board with 4 MB of flash is fine. More flash gives room for more icons and scripts.
<!-- /only -->
<!-- only esp32-s3 -->
| | ESP32-S3 |
|---|---|
| Firmware image | `usb-awtrix-ng-s3-octal-<flash>.bin`, or the `-s3-quad-` one if that finds no PSRAM |
| Usable GPIO | 0-48 except 22-25, **no input-only pins** |
| ADC for battery + LDR | GPIO 1-10 |
| Sound | Passive buzzer, DFPlayer Mini, and MP3s and internet radio with PSRAM and an I2S DAC |
| PSRAM | needed for MP3s and the radio |
| USB | native USB |

**We recommend an ESP32-S3 DevKitC-1 N16R8** (16 MB flash, 8 MB octal PSRAM). It has room for
many icons and scripts, and its PSRAM (extra memory) is needed for
[Internet radio](../guides/radio.md).

!!! warning "Buy a board with PSRAM if you want radio"
    An `N16R8` or `N8R8` has PSRAM, a plain `N16` does not. Without PSRAM the MP3 and Radio
    sections stay hidden. GPIO 26–37 are reserved on every ESP32-S3, so a board without PSRAM
    gives you no extra pins.

    There are two firmware files, `-octal-` and `-quad-`. The label on the board does
    not always tell you which one fits. Install the `-octal-` file, then look at **PSRAM** on the
    **Dashboard** of the web UI. If it shows a size, you are done. If it shows `none` on a board
    that has PSRAM, install the `-quad-` file. Details:
    [Install AWTRIX NG](../getting-started/flashing.md#which-of-the-two-s3-images).
<!-- /only -->

---

## 2. Bill of materials

### Required

| Part | Notes |
|---|---|
<!-- only esp32 -->
| **ESP32 board**, for example an ESP32 DevKit | See above. |
<!-- /only -->
<!-- only esp32-s3 -->
| **ESP32-S3 DevKitC-1 N16R8** (or another ESP32-S3 board) | See above. |
<!-- /only -->
| **WS2812B panel, 8 pixels high** | 32 x 8 is the classic size. Several smaller panels in a row work too. The total width must be 32–128 px. SK6812 and compatible clones work. **APA102, SK9822 and anything else with a separate clock line do not.** |
| **5 V power supply** | Sizing is in [section 4](#4-power-the-panel-first). 5 V / 3-4 A for a 32 x 8 build. |
| **1000 uF / 6.3 V+ electrolytic capacitor** | Across 5 V and GND at the panel input. |
| **330-470 ohm resistor** | In series with the data line, at the board end. |
| A diffuser | 2-3 mm milky acrylic, or a 3D-printed grid + paper. Bare WS2812B pixels are unreadable as text. |

### Optional: add what you want

| Part | Enables | Guide |
|---|---|---|
| 3 x momentary push button | App navigation, menus, deep-sleep wake | [Buttons](../reference/system.md#buttons) |
| LDR (for example GL5528) + 10 k resistor | Auto-brightness | [Brightness & sensors](../guides/brightness.md) |
| BME280 / BMP280 / HTU21DF / SHT31 | Temperature, humidity, pressure apps | [Sensor bus](../reference/gpio.md#sensor-bus) |
| Passive piezo buzzer | RTTTL melodies and beeps | [Sound](../guides/sounds.md) |
<!-- only esp32-s3 -->
| **MAX98357A** I2S DAC + 4-8 ohm speaker | Your own MP3s and internet radio (needs PSRAM) | [Internet radio](../guides/radio.md) |
<!-- /only -->
| DFPlayer Mini + microSD + speaker | numbered tracks alongside the buzzer | [DFPlayer boards](../guides/sounds.md#dfplayer-boards) |
| Li-Ion cell + TP4056 charger + 2 x 100 k | Battery operation and reporting | [Power & battery](../guides/power.md) |
| 74AHCT125 level shifter | Reliable 5 V data on long runs | [section 4](#4-power-the-panel-first) |

You do not need all of it. For each part you leave out, set its pin to `-1` (not connected). Its
values then disappear from device state, its Home Assistant entities are not created, and its
built-in app leaves the rotation.

---

## 3. The standard pinout

These are the default pins. If you wire your board exactly like this, you do **not need to set
any pins**. Install the firmware and it works.

<!-- only esp32-s3 -->
| Function | GPIO | Direction | Notes |
|---|---|---|---|
| **LED data** | **21** | out | From the [LED matrix list](#pins-you-cannot-freely-choose). |
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
| I2S MCLK | -1 | out | Only for DACs with an MCLK input. |
| Amplifier enable | -1 | out | Only for amplifiers with an enable input. |
<!-- /only -->
<!-- only esp32 -->
The same wiring as the Ulanzi TC001, so a TC001 needs no pin changes either.

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| **LED data** | **32** | out | From the [LED matrix list](#pins-you-cannot-freely-choose). |
| Button left | 26 | in, pull-up | Active LOW, wire to GND. |
| Button select | 27 | in, pull-up | Also the deep-sleep wake pin. |
| Button right | 14 | in, pull-up | |
| Battery tap | 34 | ADC1 | Input-only pin, which is fine for an ADC. |
| LDR tap | 35 | ADC1 | Must be GPIO 32-39. |
| Buzzer | 15 | out | Passive piezo. |
| I2C SDA | 21 | bidirectional | Environment sensor. |
| I2C SCL | 22 | bidirectional | |
| DFPlayer RX | 23 | in | Set to `-1` if unused. |
| DFPlayer TX | 18 | out | Set to `-1` if unused. |
<!-- /only -->

### System diagram

Only the panel and its power supply are required. Everything else is optional.

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
<text class="awx-t" x="460" y="210" text-anchor="middle"><!-- only esp32 -->ESP32<!-- /only --><!-- only esp32-s3 -->ESP32-S3<!-- /only --></text>
<text class="awx-s" x="460" y="228" text-anchor="middle"><!-- only esp32 -->DevKit<!-- /only --><!-- only esp32-s3 -->DevKitC-1 N16R8<!-- /only --></text>
<rect class="awx-box" x="660" y="200" width="220" height="100" rx="6"/>
<text class="awx-t" x="770" y="238" text-anchor="middle">WS2812B panel</text>
<text class="awx-s" x="770" y="258" text-anchor="middle">32 x 8 = 256 LEDs</text>
<text class="awx-s" x="770" y="278" text-anchor="middle">8 px high, 32–128 px wide</text>
<path class="awx-sg" d="M560 330 H596"/>
<rect class="awx-box" x="596" y="322" width="36" height="16" rx="2"/>
<path class="awx-sg" d="M632 330 H648 V270 H660"/>
<text class="awx-p" x="614" y="316" text-anchor="middle">470 R</text>
<text class="awx-p" x="666" y="266">DIN</text>
<text class="awx-p" x="552" y="334" text-anchor="end">GPIO <!-- only esp32 -->32<!-- /only --><!-- only esp32-s3 -->21<!-- /only --></text>
<rect class="awx-box" x="60" y="240" width="230" height="110" rx="6"/>
<text class="awx-t" x="76" y="268">3 x push button</text>
<text class="awx-s" x="76" y="290">each to GND, internal pull-up</text>
<text class="awx-s" x="76" y="310">left / select / right</text>
<text class="awx-s" x="76" y="334">select = deep-sleep wake</text>
<path class="awx-sg" d="M290 270 H360"/>
<path class="awx-sg" d="M290 292 H360"/>
<path class="awx-sg" d="M290 314 H360"/>
<text class="awx-p" x="368" y="274">GPIO <!-- only esp32 -->26<!-- /only --><!-- only esp32-s3 -->11<!-- /only --></text>
<text class="awx-p" x="368" y="296">GPIO <!-- only esp32 -->27<!-- /only --><!-- only esp32-s3 -->12<!-- /only --></text>
<text class="awx-p" x="368" y="318">GPIO <!-- only esp32 -->14<!-- /only --><!-- only esp32-s3 -->13<!-- /only --></text>
<rect class="awx-box" x="60" y="386" width="230" height="54" rx="6"/>
<text class="awx-t" x="76" y="410">LDR divider</text>
<text class="awx-s" x="76" y="430">GL5528 + 10 k, ADC1</text>
<path class="awx-sg" d="M290 412 H360"/>
<text class="awx-p" x="368" y="416">GPIO <!-- only esp32 -->35<!-- /only --><!-- only esp32-s3 -->2<!-- /only --></text>
<rect class="awx-box" x="60" y="470" width="230" height="54" rx="6"/>
<text class="awx-t" x="76" y="494">Battery divider</text>
<text class="awx-s" x="76" y="514">100 k / 100 k, ADC1</text>
<path class="awx-sg" d="M290 496 H360"/>
<text class="awx-p" x="368" y="500">GPIO <!-- only esp32 -->34<!-- /only --><!-- only esp32-s3 -->1<!-- /only --></text>
<rect class="awx-box" x="660" y="330" width="220" height="52" rx="6"/>
<text class="awx-t" x="676" y="354">Passive piezo buzzer</text>
<text class="awx-s" x="676" y="372">RTTTL melodies</text>
<path class="awx-sg" d="M560 356 H660"/>
<text class="awx-p" x="552" y="360" text-anchor="end">GPIO <!-- only esp32 -->15<!-- /only --><!-- only esp32-s3 -->7<!-- /only --></text>
<rect class="awx-box" x="660" y="410" width="220" height="64" rx="6"/>
<text class="awx-t" x="676" y="436">BME280 / SHT31</text>
<text class="awx-s" x="676" y="458">I2C, auto-detected at boot</text>
<path class="awx-sg" d="M560 428 H660"/>
<path class="awx-sg" d="M560 452 H660"/>
<text class="awx-p" x="552" y="432" text-anchor="end">GPIO <!-- only esp32 -->21<!-- /only --><!-- only esp32-s3 -->8<!-- /only --> SDA</text>
<text class="awx-p" x="552" y="456" text-anchor="end">GPIO <!-- only esp32 -->22<!-- /only --><!-- only esp32-s3 -->9<!-- /only --> SCL</text>
<!-- only esp32-s3 -->
<rect class="awx-box" x="660" y="500" width="220" height="82" rx="6"/>
<text class="awx-t" x="676" y="526">MAX98357A + speaker</text>
<text class="awx-s" x="676" y="548">I2S, needs PSRAM</text>
<text class="awx-s" x="676" y="568">MP3s and internet radio</text>
<path class="awx-sg" d="M560 516 H660"/>
<path class="awx-sg" d="M560 538 H660"/>
<path class="awx-sg" d="M560 560 H660"/>
<text class="awx-p" x="552" y="520" text-anchor="end">GPIO 5 BCLK</text>
<text class="awx-p" x="552" y="542" text-anchor="end">GPIO 6 LRC</text>
<text class="awx-p" x="552" y="564" text-anchor="end">GPIO 4 DIN</text>
<!-- /only -->
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

**Power.** Wire these first. Never feed the panel through the ESP32 board.

| From | To | Wire |
|---|---|---|
| PSU +5 V | Panel 5 V, both ends on a wide panel | 18 AWG |
| PSU GND | Panel GND **and** board GND | 18 AWG |
| PSU +5 V / GND | 1000 uF capacitor, at the panel input | short leads |
| PSU +5 V | Board 5 V / VIN pin | 22 AWG |

**Signals**

| Peripheral | Its pin | Board pin | In line |
|---|---|---|---|
<!-- only esp32-s3 -->
| Panel | DIN | GPIO 21 | 470 ohm series resistor |
| Button left | one leg | GPIO 11 | other leg to GND |
| Button select | one leg | GPIO 12 | other leg to GND |
| Button right | one leg | GPIO 13 | other leg to GND |
| LDR divider | tap | GPIO 2 | 10 k to GND |
| Battery divider | tap | GPIO 1 | 100 k / 100 k |
| Buzzer | + | GPIO 7 | - to GND |
| I2C sensor | SDA | GPIO 8 | 4.7 k pull-up if the breakout has none |
| I2C sensor | SCL | GPIO 9 | same |
| MAX98357A | BCLK | GPIO 5 | - |
| MAX98357A | LRC / WS | GPIO 6 | - |
| MAX98357A | DIN | GPIO 4 | - |
| MAX98357A | SD | free pin, `pinAmpEnable` | only if the breakout needs it |
| NS4168 | CTRL | free pin, `pinAmpEnable` | - |
| DFPlayer Mini | RX | GPIO 18 (TX) | 1 k in series |
| DFPlayer Mini | TX | GPIO 17 (RX) | - |
<!-- /only -->
<!-- only esp32 -->
| Panel | DIN | GPIO 32 | 470 ohm series resistor |
| Button left | one leg | GPIO 26 | other leg to GND |
| Button select | one leg | GPIO 27 | other leg to GND |
| Button right | one leg | GPIO 14 | other leg to GND |
| LDR divider | tap | GPIO 35 | 10 k to GND |
| Battery divider | tap | GPIO 34 | 100 k / 100 k |
| Buzzer | + | GPIO 15 | - to GND |
| I2C sensor | SDA | GPIO 21 | 4.7 k pull-up if the breakout has none |
| I2C sensor | SCL | GPIO 22 | same |
| DFPlayer Mini | RX | GPIO 18 (TX) | 1 k in series |
| DFPlayer Mini | TX | GPIO 23 (RX) | - |
<!-- /only -->

<!-- only esp32 -->
The buzzer and the DFPlayer are separate outputs, and you can build both. Set the pins of every
part you did not build to `-1`.
<!-- /only -->
<!-- only esp32-s3 -->
The buzzer, the DFPlayer and the I2S DAC are separate outputs, and you can build any of them
together. Set the pins of every part you did not build to `-1`.
<!-- /only -->

### Pins you cannot freely choose

AWTRIX checks these rules every time you save pins, and again at every start. All rules and the
exact error messages are in [GPIO & boards](../reference/gpio.md#validation-rules).

<!-- only esp32-s3 -->
| Rule | Pins |
|---|---|
| **Matrix pin** must be on the LED matrix list | 13, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47 |
| **Battery and LDR** must be ADC1 | 1-10 |
| **Reserved**, never assignable | 19-20 (USB-JTAG), 26-37 (flash + PSRAM), 43-44 (console) |
<!-- /only -->
<!-- only esp32 -->
| Rule | Pins |
|---|---|
| **Matrix pin** must be on the LED matrix list | 2, 4, 5, 13, 14, 15, 16, 18, 21, 25, 26, 27, 32, 33 |
| **Battery and LDR** must be ADC1 | 32-39 |
| **Reserved**, never assignable | 6-11 (SPI flash) |
| **Input-only**, so no buttons, buzzer, I2C or TX there | 34-39 |
<!-- /only -->

Two more points. AWTRIX accepts these pins, but your hardware may not like them:

* **Strapping pins:** <!-- only esp32 -->0, 2, 5, 12, 15<!-- /only --><!-- only esp32-s3 -->0, 3, 45, 46<!-- /only -->. The chip reads them
  at power-on. Anything that holds them high or low can stop the board from starting.
* **Deep-sleep wake:** only a select button on GPIO <!-- only esp32 -->0, 2, 4, 12–15, 25–27, 32–39<!-- /only --><!-- only esp32-s3 -->0–21<!-- /only -->
  can end a [`/device/sleep`](../reference/http.md#post-apiv1devicesleep) early. On any
  other pin the button works normally while AWTRIX is awake, but it cannot wake it up.

AWTRIX reports the rules for its own chip. Use this instead of copying the tables into your own
tools:

```bash
curl http://<awtrix-ip>/api/v1/capabilities
```

---

## 4. Power the panel first

Most DIY problems come from the power wiring.

**AWTRIX NG does not limit LED current.** A full-white picture at brightness 255 draws the full
current, whether it comes from the mood light, Art-Net or a script. Plan your supply for that.

| Build | LEDs | Worst case (all white, full brightness) | Realistic clock use |
|---|---|---|---|
| 32 x 8 | 256 | ~15 A at 5 V | 0.3-0.8 A |
| 64 x 8 | 512 | ~30 A at 5 V | 0.6-1.5 A |

You do not need a supply for the full maximum. **5 V / 3–4 A for a 32 x 8 panel** covers every
normal app and a bright notification. To stay inside it, limit the brightness instead of buying a
bigger supply:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness":false,"brightness":120}'
```

With auto-brightness on, `brightness` is ignored. Cap the panel with `maxBrightness` in the
[system configuration](../reference/system.md#auto-brightness) instead.

Follow these wiring rules:

1. **Never power the panel through the ESP32 board's 5 V pin.** Connect the panel directly to the
   supply. The board is fed from the same supply.
2. **Common ground.** Connect board GND and panel GND. Without it the data signal has no
   reference and the panel shows random pixels.
3. **1000 uF across 5 V/GND at the panel input**, and a **330–470 ohm resistor in series with the
   data line** at the board end. Both protect the first LED from voltage spikes.
4. **Feed power at both ends** of panels wider than 32 px. Use proper wire: 18 AWG for the 5 V
   line, not breadboard jumpers.
5. **Data level.** A WS2812B needs at least 0.7 × its supply voltage on DIN: 3.5 V at 5 V. An
   ESP32 sends only 3.3 V. Short cables usually work anyway. If the first pixels flicker or show
   wrong colors, add a **74AHCT125** level shifter. Or lower the panel supply to about 4.5 V
   with a diode in series, so 3.3 V is high enough.

!!! danger "Li-Ion safety"
    A battery build needs a protected cell and a proper charger (TP4056 with protection, or a
    dedicated charger chip). Never connect a cell directly to a GPIO. Connect it only through the
    divider in [section 5](#battery-monitoring). AWTRIX does not control or watch the charging.

---

## 5. Wire the options

### Buttons

Wire three push buttons, each between its GPIO and **GND**. AWTRIX turns on the internal
pull-up, so a pressed button reads LOW. You need no external resistors. If a button is noisy, add
a 100 nF capacitor across it.

If the panel ends up upside down, fix it in the settings instead of resoldering: `rotate` turns
the picture *and* swaps left and right, `swapButtons` swaps only the buttons.

### Light sensor (auto-brightness)

Build a voltage divider from a GL5528-type LDR and a 10 k resistor, and connect the middle to
the ADC pin:

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
<text class="awl-t" x="244" y="142">GPIO <!-- only esp32 -->35<!-- /only --><!-- only esp32-s3 -->2<!-- /only --></text>
<text class="awl-c" x="244" y="158">ADC1</text>
<path class="awl-w" d="M100 138 V164"/>
<rect class="awl-r" x="82" y="164" width="36" height="52" rx="3"/>
<text class="awl-t" x="128" y="194">10 k</text>
<path class="awl-w" d="M100 216 V228"/>
<path class="awl-w" d="M80 228 H120 M86 236 H114 M92 244 H108"/>
</svg>
</div>

Wired this way, more light gives a higher voltage. This matches the default
`ldrOnGround: false`. If you swap the two parts, set `{"ldrOnGround": true}`.

Then calibrate: `ldrFactor` sets what counts as full light on *your* divider, `ldrGamma` shapes
the curve. Both take effect at once. The full steps are in
[Brightness & sensors](../guides/brightness.md).

!!! note "No LDR? Auto-brightness has no effect"
    With `pinLdr: -1` the board has no light sensor. `autoBrightness` changes nothing and the panel
    uses `brightness`. The web UI hides the auto-brightness switch and its fields.

### Battery monitoring

Build a 2:1 divider from the cell to an ADC1 pin:

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
<text class="awb-t" x="244" y="142">GPIO <!-- only esp32 -->34<!-- /only --><!-- only esp32-s3 -->1<!-- /only --></text>
<text class="awb-c" x="244" y="158">2.1 V at 4.2 V cell</text>
<path class="awb-w" d="M100 138 V164"/>
<rect class="awb-r" x="82" y="164" width="36" height="52" rx="3"/>
<text class="awb-t" x="128" y="194">100 k</text>
<path class="awb-w" d="M100 216 V228"/>
<path class="awb-w" d="M80 228 H120 M86 236 H114 M92 244 H108"/>
</svg>
</div>

4.2 V at the cell becomes 2.1 V at the pin, safely inside the ADC range. Tell AWTRIX the ratio,
then correct it with a fully charged cell:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"batteryDividerRatio":2.0}'
```

`batteryDividerRatio` is `V_cell / V_pin`. To correct it:

1. Charge the cell fully.
2. Read `batteryPinMillivolts` from `GET /api/v1/device`.
3. Write `4.2 / (batteryPinMillivolts / 1000)` as `batteryDividerRatio`.

The percentage is estimated from the voltage of a typical Li-Ion cell, so it is not exact. See
[Power & battery](../guides/power.md).

### Environment sensor

Connect one of **BME280** (`0x76`/`0x77`), **BMP280** (same addresses), **HTU21DF** or **SHT31**
(`0x44`) to the I2C pins. AWTRIX finds it at startup, looking in that order, and uses the first
one that answers. So connect only one sensor. Most breakout boards have pull-up resistors. If
yours does not, add 4.7 k from each line to 3V3.

All four measure temperature. BME280, HTU21DF and SHT31 also measure humidity. BME280 and BMP280
also measure air pressure. If the sensor reads too warm because of heat from the clock, correct it
with `tempOffset` and `humOffset`.

### Sound options

| Option | Hardware | What you get | Requires |
|---|---|---|---|
| **Passive buzzer** | Piezo on `pinBuzzer` | RTTTL melodies, notification beeps | anything |
| **DFPlayer Mini** | Module on `pinDfRx`/`pinDfTx` + microSD | MP3 playback by track number | `dfplayer: true` and both pins set |
<!-- only esp32-s3 -->
| **I2S DAC** | MAX98357A on the three I2S pins | MP3s and internet radio streams | **PSRAM** and all three I2S pins set |
<!-- /only -->

**Buzzer**: a *passive* piezo (not an active buzzer with its own oscillator), connected directly
to the GPIO. It is quiet. A small NPN transistor with a 100 ohm base resistor makes it louder.

**DFPlayer Mini**: 5 V supply, a 1 k resistor in the line to the module's RX, and the speaker on
SPK1/SPK2. It plays numbered tracks from its microSD card. The buzzer keeps playing melodies and
RTTTL tunes. Set `dfplayer: true`.

<!-- only esp32-s3 -->
**MAX98357A**: BCLK, LRC and DIN to the three I2S pins, plus 3V3/GND and a 4–8 ohm speaker. You
need no extra amplifier. Set all three I2S pins, or all three to `-1`. If you set only some,
AWTRIX rejects the change with `422`. A UDA1334A or PCM5102A works the same way.

Two optional pins go with it. A DAC with an **MCLK** input uses `pinI2sMclk`. An amplifier with
an enable input, for example **CTRL** on an NS4168 or **SD** on a MAX98357A, uses `pinAmpEnable`. AWTRIX sets
that pin high at startup, so the amplifier plays.
<!-- /only -->

---

## 6. Describe the panel

Tell AWTRIX how your panel is built. Every panel is 8 pixels high. The total width can be
32–128.

| Key | Range | Default | Meaning |
|---|---|---|---|
| `panelWidth` | 1-128 | `32` | Width of one panel. A new total width needs a restart. |
| `panels` | 1-128 | `1` | How many identical panels the cable runs through, left to right. `panelWidth x panels` must be 32–128. |
| `panelStart` | `topLeft` `topRight` `bottomLeft` `bottomRight` | `topLeft` | Corner of the first LED. |
| `panelWiring` | `rows` `columns` | `rows` | Whether the strip runs along rows or down columns. |
| `panelColorOrder` | `rgb` `rbg` `grb` `gbr` `brg` `bgr` | `grb` | Color order the LEDs expect. |
| `panelSerpentine` | bool | `true` | Every second row (or column) runs back the other way, the usual zigzag. |
| `panelChainReverse` | bool | `false` | The cable enters the chain of panels at the other end. Does not change the wiring inside a panel. |
| `panelChainSerpentine` | bool | `false` | Every second panel is turned by 180°, so its output sits next to the next panel's input. |
| `mirror` / `rotate` | bool | `false` | For a panel mounted the wrong way round. Each does the same as picking a different `panelStart`. `rotate` also swaps the left and right button. |

`panelStart`, `panelWiring`, `panelColorOrder` and `panelSerpentine` describe one panel. The two
chain keys describe how the panels are joined. With a single panel the chain keys change nothing.

Common builds:

| Build | Configuration |
|---|---|
| Standard 32 x 8 panel | the defaults |
| Four chained 8 x 8 tiles | `panelWidth` 8, `panels` 4, `panelSerpentine` false |
| Four 8 x 8 tiles, each wired from its right edge | `panelWidth` 8, `panels` 4, `panelStart` `topRight`, `panelChainReverse` true |
| Tiles mounted alternately, output next to input | `panelChainSerpentine` true |
| 32 x 8 wired in columns | `panelWiring` `columns` |
| Panel shows red as green and green as red | `panelColorOrder` `rgb` |
| 64 px wide panel | `panelWidth` 64 |
| Four chained 32 x 8 panels (128 x 8) | `panelWidth` 32, `panels` 4 |

If the picture is scrambled, try `panelSerpentine` first, then `panelStart`, then
`panelWiring`. If each panel looks right but the panels are in the wrong order, or every second
one is upside down, try `panelChainReverse` and `panelChainSerpentine`. These keys take effect
**at once**, so you can watch the panel while you change them. Only a new total width needs a
restart.

---

## 7. Flash and configure

1. **Install the firmware for your chip.** See [Install AWTRIX NG](../getting-started/flashing.md), with
   the browser installer or `esptool`. In the browser installer, choose **Fresh install** for a new
   board.
2. **Connect to Wi-Fi** through the setup hotspot. See
   [Connect to Wi-Fi](../getting-started/first-boot.md).
3. **Write the pin map.** Send all pins in one request. Some rules compare pins with each other
   (no duplicates, all three I2S pins). If you send only part of the map, a pin you have not yet
   moved can cause a rejection.

<!-- only esp32-s3 -->
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
        "pinI2sMclk": -1,
        "pinAmpEnable": -1,
        "dfplayer": false,
        "panelWidth": 32,
        "panels": 1
      }'
```
<!-- /only -->
<!-- only esp32 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{
        "pinMatrix": 32,
        "pinBtnLeft": 26,
        "pinBtnSelect": 27,
        "pinBtnRight": 14,
        "pinBattery": -1,
        "pinLdr": 35,
        "pinBuzzer": 15,
        "pinI2cSda": 21,
        "pinI2cScl": 22,
        "pinDfRx": -1,
        "pinDfTx": -1,
        "dfplayer": false,
        "panelWidth": 32,
        "panels": 1
      }'
```
<!-- /only -->

4. **Restart.** A new pin map takes effect only after a restart:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

5. **Calibrate** what you wired: `batteryDividerRatio` for the divider, `ldrFactor` / `ldrGamma` /
   `ldrOnGround` for the light sensor, `tempOffset` / `humOffset` for the sensor. <!-- only esp32 -->The defaults
   match the Ulanzi TC001 and will be wrong for your parts.<!-- /only --><!-- only esp32-s3 -->The defaults are
   general values and may be wrong for your parts.<!-- /only -->
6. **Adjust the colors** if the panel looks too cold or too warm: `colorCorrection` and
   `colorTint` in [Settings](../reference/settings.md).

You can also edit the pins under **System → GPIO** in the web UI. Each field is a dropdown that
offers only the pins your chip can use for that part.

!!! success "A pin map cannot make AWTRIX unusable"
    AWTRIX checks the stored map at every start. If it is not valid, for example a map saved on a
    board with another chip, AWTRIX starts with the default pins and stays reachable, so you can
    fix it. It keeps using the defaults until you save a valid map.

---

## 8. Verify the build

Work down this list. Each step tests one part of the hardware.

| Check | How | Expected |
|---|---|---|
| Chip and rules | `curl http://<ip>/api/v1/capabilities` | `soc` matches your board. `gpio` lists the ranges from [section 3](#pins-you-cannot-freely-choose) |
| Panel geometry | Web UI **System → Panel** | `32 × 8 = 256 LEDs`, or your size |
| Every pixel | Send a full-white notification, or a mood light frame | No dead pixels, no color shift down the run |
| Color order | Push red text | Red, not green or blue |
| Buttons | Press each | The app rotation moves; `state/buttons/<button>` fires over MQTT |
| Light sensor | `curl http://<ip>/api/v1/device` while covering the LDR | `lightLevel` falls towards 0 |
| Battery | same call | `batteryVoltage` near 4.2 V on a full cell |
| Sensor | same call | `temperature` present and plausible |
| Sound | Play a melody<!-- only esp32-s3 -->, or a radio station<!-- /only --> | Audible |

---

## Troubleshooting a fresh build

| Symptom | Cause |
|---|---|
| Panel dark, device reachable | Wrong `pinMatrix`, no common ground, or the panel has no 5 V of its own |
| First pixel wrong color, rest fine | Missing series resistor or the 1000 uF capacitor |
| Flicker, colors drift down the strip | 3.3 V data on 5 V pixels. Add a level shifter or drop the panel supply to ~4.5 V |
| Picture scrambled or mirrored | `panelSerpentine`, then `panelStart`, then `panelWiring` |
| Red, green or blue appear as another color | Select the panel's `panelColorOrder`. Red and green swapped usually needs `rgb` |
| Panels each correct but in the wrong order | `panelChainReverse`: the cable enters the chain at the other end |
| Every second panel upside down | `panelChainSerpentine`: the tiles are mounted alternately |
| Board resets on bright frames | Supply too small, or panel current flowing through the dev board |
| Buttons dead or inverted | Wired to 3V3 instead of GND. The buttons must pull the pin to ground |
| Left/right reversed | `swapButtons`, or `rotate` if the whole panel is upside down |
| Panel stuck dim with `autoBrightness` on | LDR not wired to `pinLdr`, or `ldrOnGround` set the wrong way |
| Percentage nonsense | `batteryDividerRatio` still at the default |
| No temperature | Sensor not on the bus, missing pull-ups, or a second chip answering first |
<!-- only esp32-s3 -->
| Radio section missing, `/api/v1/audio/play` returns `503` | The I2S pins are `-1`, or **PSRAM** on the **Dashboard** says `none`. Then the board has no PSRAM, or it needs the `-quad-` firmware |
| Radio plays, speaker silent | The amplifier has an enable input. Wire it and set `pinAmpEnable` |
<!-- /only -->
| `invalidPinConfig` on a write | The message names the field and the rule. See [Errors](../reference/errors.md#gpio-validation-invalidpinconfig) |

---

## Related

* [GPIO & boards](../reference/gpio.md) – every pin rule, the web UI dropdowns, recovery
* [System configuration](../reference/system.md) – all fields you wrote above, plus Wi-Fi, MQTT, NTP
* [Brightness & sensors](../guides/brightness.md) and [Power & battery](../guides/power.md) – calibration
<!-- only esp32-s3 -->
* [Internet radio](../guides/radio.md) – stations, volume, what the board needs
<!-- /only -->
* [Scripting guide](../guides/scripting/index.md) – write your own apps
* [Building from source](../developers/building.md) – if you want to change the firmware itself
