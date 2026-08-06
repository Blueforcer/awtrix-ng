// The browser flasher on getting-started/flashing.md.
//
// esptool-js speaks the ESP32 ROM bootloader protocol over Web Serial, which is
// the same conversation `esptool write_flash 0x0` has over a USB port. The two
// paths therefore write the identical USB install image; this one just asks the chip
// which image it needs instead of asking the reader.
//
// The bundle is one self-contained ES module -- the flasher stubs are inlined in
// it, so nothing else is fetched from the CDN at flash time.
import { ESPLoader, Transport } from "https://unpkg.com/esptool-js@0.6.0/bundle.js";

// Written by the Docs workflow after it downloads the release assets:
// {"version": "v1.0.15", "assets": ["usb-awtrix-ng-4mb.bin", ...]}.
// Resolved against this module's own URL so the page works both under the
// GitHub Pages sub-path and on a site served from the root.
const FIRMWARE = new URL("../firmware/", import.meta.url);

// A write at 921600 aborts partway on the USB-serial bridge a TC001 uses, and
// the chip is erased by then. Same ceiling as the esptool instructions.
const BAUD = 460800;

// esptool-js raises the port to BAUD once its stub runs, and some bridges never
// come back from that switch -- every command after it fails. 115200 is the rate
// the port is opened at anyway, so asking for it leaves no switch to survive.
const ROM_BAUD = 115200;

// The release-asset naming from scripts/factory_image.py: the classic ESP32
// image carries the project name, the S3 one carries it with an -s3 suffix.
// Up to v1.0.14 the prefix was factory-, so both are tried and the first that
// the published release actually carries wins.
const ASSET_PREFIX = {
  "ESP32": ["usb-awtrix-ng-", "factory-awtrix-ng-"],
  "ESP32-S3": ["usb-awtrix-ng-s3-", "factory-awtrix-ng-s3-"],
};

// Where the partition table sits inside the USB install image, and the type/subtype
// pair naming the partition that holds the settings and the Wi-Fi credentials.
const PARTITION_TABLE = 0x8000;
const ENTRY = 32;
const ENTRY_MAGIC = 0x50AA;
const TYPE_DATA = 0x01;
const SUBTYPE_NVS = 0x02;
const SECTOR = 0x1000;

// The install image is merged, so the gap between the partition table and
// boot_app0 -- which is where nvs lives -- is 0xFF padding in it. Writing the
// image as one piece therefore erases the settings. Reading the table out of
// the image says which bytes to skip to keep them, and reading it from the
// image rather than from a constant here means a table that moves in
// scripts/gen_partitions.py moves this with it.
function nvsRange(image) {
  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  for (let at = PARTITION_TABLE; at + ENTRY <= image.length; at += ENTRY) {
    if (view.getUint16(at, true) !== ENTRY_MAGIC) break;
    if (image[at + 2] === TYPE_DATA && image[at + 3] === SUBTYPE_NVS) {
      const start = view.getUint32(at + 4, true);
      return { start, end: start + view.getUint32(at + 8, true) };
    }
  }
  return null;
}

// One part for a fresh install, two for a reflash that keeps the settings.
// Anything unexpected -- no nvs entry, or one the erase granularity cannot
// straddle -- writes the image whole rather than writing it wrong.
function partsFor(image, keepSettings) {
  const nvs = keepSettings && nvsRange(image);
  if (!nvs || nvs.start % SECTOR || nvs.end % SECTOR || nvs.end >= image.length) {
    return { fileArray: [{ data: image, address: 0 }], keptSettings: false };
  }
  return {
    fileArray: [
      { data: image.subarray(0, nvs.start), address: 0 },
      { data: image.subarray(nvs.end), address: nvs.end },
    ],
    keptSettings: true,
  };
}

// The stub and the baud switch both happen before a byte of the image is
// written, so a bridge that cannot follow gives up while the flash is still
// intact. One retry at ROM_BAUD costs a reset and buys those bridges a flash.
async function connect(transport, terminal, status) {
  const rates = [BAUD, ROM_BAUD];
  let failure;
  for (const [attempt, baudrate] of rates.entries()) {
    try {
      // Constructing this clears the terminal, so what the failed attempt has
      // to say about itself only survives when it is written afterwards.
      const loader = new ESPLoader({ transport, baudrate, terminal });
      if (failure) {
        terminal.writeLine(`Connecting at ${rates[attempt - 1]} baud failed: ${failure}`);
        terminal.writeLine(`Retrying at ${baudrate} baud, which skips the switch.\n`);
      }
      await loader.main();
      return loader;
    } catch (error) {
      if (attempt === rates.length - 1) throw error;
      failure = error.message || error;
      status.textContent = `Retrying at ${rates[attempt + 1]} baud…`;
      // The port has to be closed before the retry can open it again. It is
      // open unless the failure was the opening itself.
      try { await transport.disconnect(); } catch { /* never opened */ }
    }
  }
}

const root = document.getElementById("awtrix-flasher");

function el(tag, props = {}, children = []) {
  const node = Object.assign(document.createElement(tag), props);
  for (const child of children) node.append(child);
  return node;
}

function notice(text, kind = "warning") {
  return el("div", { className: `admonition ${kind}` }, [
    el("p", { className: "admonition-title", textContent: kind === "warning" ? "Not available" : "Note" }),
    el("p", { textContent: text }),
  ]);
}

if (root) {
  if (!("serial" in navigator)) {
    root.replaceChildren(notice(
      "This browser cannot talk to a serial port. Use Chrome, Edge or Opera on a desktop, " +
      "or the esptool instructions below."));
  } else {
    start();
  }
}

async function start() {
  let index;
  try {
    const response = await fetch(new URL("index.json", FIRMWARE), { cache: "no-store" });
    if (!response.ok) throw new Error(response.status);
    index = await response.json();
  } catch {
    root.replaceChildren(notice(
      "No firmware is published here yet. Use the esptool instructions below."));
    return;
  }

  const eraseBox = el("input", { type: "checkbox", id: "awtrix-flasher-erase" });
  const button = el("button", { className: "md-button md-button--primary", textContent: "Connect and flash" });
  const status = el("p", { textContent: `Firmware ${index.version}. Put the board on USB and press the button.` });
  const progress = el("progress", { max: 100, value: 0, hidden: true, style: "width:100%" });
  const log = el("pre", { hidden: true, style: "max-height:14em;overflow:auto" });
  const logText = el("code");
  log.append(logText);

  root.replaceChildren(
    button,
    el("p", {}, [
      eraseBox,
      el("label", { htmlFor: "awtrix-flasher-erase", textContent: " Erase the whole flash first (deletes settings, Wi-Fi, icons, melodies, palettes and scripts)" }),
    ]),
    status,
    progress,
    log,
  );

  const terminal = {
    clean: () => { logText.textContent = ""; },
    write: (data) => { logText.textContent += data; log.scrollTop = log.scrollHeight; },
    writeLine: (data) => terminal.write(data + "\n"),
  };

  button.addEventListener("click", () => flash({ button, status, progress, log, terminal, index, eraseBox }));
}

async function flash({ button, status, progress, log, terminal, index, eraseBox }) {
  let port;
  try {
    port = await navigator.serial.requestPort();
  } catch {
    return; // The port picker was dismissed. Nothing was opened, nothing to undo.
  }

  button.disabled = true;
  eraseBox.disabled = true;
  log.hidden = false;
  terminal.clean();

  const transport = new Transport(port, false);
  try {
    status.textContent = "Connecting…";
    const loader = await connect(transport, terminal, status);

    const chip = loader.chip.CHIP_NAME;
    const flashSize = await loader.detectFlashSize();
    const candidates = (ASSET_PREFIX[chip] || []).map(
      (prefix) => `${prefix}${flashSize.toLowerCase()}.bin`);
    const asset = candidates.find((name) => index.assets.includes(name));
    if (!asset) {
      status.textContent = `No image ships for a ${chip} with ${flashSize} of flash.`;
      return;
    }

    status.textContent = `${chip}, ${flashSize} flash. Downloading ${asset}…`;
    const response = await fetch(new URL(asset, FIRMWARE));
    if (!response.ok) throw new Error(`${asset}: HTTP ${response.status}`);
    const image = new Uint8Array(await response.arrayBuffer());

    const { fileArray, keptSettings } = partsFor(image, !eraseBox.checked);
    status.textContent = loader.baudrate === ROM_BAUD
      ? `Writing ${asset}… at ${ROM_BAUD} baud this takes several minutes.`
      : `Writing ${asset}…`;
    progress.hidden = false;
    const total = fileArray.reduce((sum, part) => sum + part.data.length, 0);
    const written = fileArray.map(() => 0);
    await loader.writeFlash({
      // `keep` leaves the flash mode, frequency and size in the image header
      // alone -- they were set when the image was merged, for exactly the flash
      // size that was just detected.
      fileArray,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: eraseBox.checked,
      compress: true,
      reportProgress: (file, done) => {
        written[file] = done;
        progress.value = (written.reduce((a, b) => a + b, 0) / total) * 100;
      },
    });

    await loader.after("hard_reset");
    status.textContent = keptSettings
      ? "Done. AWTRIX NG is booting, with your settings and Wi-Fi as they were."
      : "Done. AWTRIX NG is booting - it opens its own access point after about 15 seconds.";
  } catch (error) {
    status.textContent = `Failed: ${error.message || error}. ` +
      `The chip stays unbootable until a write succeeds - retry it.`;
  } finally {
    progress.hidden = true;
    button.disabled = false;
    eraseBox.disabled = false;
    try { await transport.disconnect(); } catch { /* already gone */ }
  }
}
