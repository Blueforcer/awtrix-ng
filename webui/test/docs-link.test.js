const { boot, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  ✗ ' + msg); }
}

const DOCS = 'https://blueforcer.github.io/awtrix-ng/';
const audio = { rtttl: false, track: false, mp3: false, radio: false };

async function opened(platform) {
  const caps = { transitions: [], audio, gpio: null,
    display: { width: 32, height: 8, configurable: false } };
  if (platform) caps.platform = { id: platform };
  const { window } = await boot({ caps });
  let url = null;
  window.open = u => { url = u; return null; };
  window.document.querySelector('#docsbtn').click();
  window.close();
  return url;
}

async function main() {
  assert(await opened('esp32') === DOCS + 'esp32/', 'an ESP32 clock opens the ESP32 documentation');
  assert(await opened('esp32s3') === DOCS + 'esp32-s3/', 'an ESP32-S3 clock opens the ESP32-S3 documentation');
  assert(await opened('tc002') === DOCS + 'tc002/', 'a TC002 opens the TC002 documentation');
  assert(await opened('linux') === DOCS, 'an unknown platform opens the clock chooser');
  assert(await opened(null) === DOCS, 'a clock without a platform opens the clock chooser');
  await flush(20);
  console.log(`docs-link: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main();
