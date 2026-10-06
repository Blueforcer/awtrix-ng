const { boot, goto, flush } = require('./harness');
const fs = require('node:fs');
const path = require('node:path');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  ✗ ' + msg); }
}

async function openDashboard(autoBrightness) {
  const ctx = await boot();
  ctx.store.settings.autoBrightness = autoBrightness;
  await goto(ctx.window, '#/system');
  await goto(ctx.window, '#/');
  return ctx;
}

const autoSwitch = window => window.document.querySelector('input[aria-label="Auto brightness"]');

async function testShowsStoredAutoBrightness() {
  const { window, store } = await openDashboard(true);
  const control = autoSwitch(window);
  assert(!!control, 'dashboard shows the Auto brightness switch');
  assert(control && control.checked, 'Auto brightness reflects the stored setting');
  const brightness = window.document.querySelector('input[type=range][aria-label="Brightness"]');
  assert(brightness && brightness.disabled,
    'manual brightness is disabled while Auto brightness is on');
  control.checked = false;
  control.dispatchEvent(new window.Event('change', { bubbles: true }));
  await flush(20);
  assert(store.settingsPatch && store.settingsPatch.autoBrightness === false,
    'changing Auto brightness PATCHes the setting');
  assert(!brightness.disabled, 'manual brightness is enabled again in manual mode');
  window.close();
}

// Stopping the rotation is one tap from the dashboard: it flips the autoTransition setting.
async function testRotationPauseButton() {
  const ctx = await boot();
  ctx.store.settings.autoTransition = true;
  await goto(ctx.window, '#/system');
  await goto(ctx.window, '#/');
  const { window, store } = ctx;
  const pause = () => window.document.querySelector('button[aria-label="Pause app rotation"]');
  const play = () => window.document.querySelector('button[aria-label="Resume app rotation"]');
  assert(!!pause() && !play(), 'a rotating clock offers to stop switching apps');
  pause().click();
  await flush(20);
  assert(store.settingsPatch && store.settingsPatch.autoTransition === false,
    'pausing turns autoTransition off');
  assert(!!play() && play().getAttribute('aria-pressed') === 'true', 'and the button now resumes');
  play().click();
  await flush(20);
  assert(store.settingsPatch.autoTransition === true, 'resuming turns it back on');
  window.close();

  const paused = await boot();
  paused.store.settings.autoTransition = false;
  await goto(paused.window, '#/system');
  await goto(paused.window, '#/');
  await flush(20);
  assert(!!paused.window.document.querySelector('button[aria-label="Resume app rotation"]'),
    'a clock that is not rotating shows the resume button');
  paused.window.close();
}

// Frame count, total delay and size of a GIF, read from its blocks without decoding the pixels.
function gifInfo(b) {
  const info = { w: b[6] | b[7] << 8, h: b[8] | b[9] << 8, frames: 0, ms: 0, bytes: b.length };
  let p = 13;
  if (b[10] & 0x80) p += 3 * (2 << (b[10] & 7));
  const skip = () => { for (let n = b[p++]; n; n = b[p++]) p += n; };
  for (;;) {
    const tag = b[p++];
    if (tag === 0x3B || tag === undefined) break;
    if (tag === 0x21) { if (b[p++] === 0xF9) info.ms += (b[p + 2] | b[p + 3] << 8) * 10; skip(); continue; }
    if (tag !== 0x2C) throw new Error('unexpected block 0x' + tag.toString(16));
    info.frames++;
    const packed = b[p + 8];
    p += 9;
    if (packed & 0x80) p += 3 * (2 << (packed & 7));
    p++;
    skip();
  }
  return info;
}

// The Hub takes a capture as a cover or gallery picture only within its rules: at most 240
// frames, 10 s, 20 million pixels a frame, 60 million decoded pixels in all and 8,000,000 bytes.
async function capture({ width, height, step, native = false, busy = false }) {
  const pixels = () => Array.from({ length: width * height }, (_, i) => busy ? (Math.random() * 0xFFFFFF) | 0 : (i % 7) * 0x102030);
  const { window, store } = await boot({ canvasContext: { fillRect() {}, fillStyle: '' },
    screen: { width, height, pixels: pixels() } });
  const files = [], pngs = [];
  window.URL.createObjectURL = blob => { files.push(blob); return 'blob:capture'; };
  window.URL.revokeObjectURL = () => {};
  window.HTMLCanvasElement.prototype.toBlob = function (done) { pngs.push([this.width, this.height]); done(new window.Blob(['png'])); };
  let now = 1e6;
  window.Date.now = () => now;
  const device = window.fetch;
  window.fetch = (url, o) => {
    if (String(url).includes('/display/screen')) { now += step; if (busy) store.screen.pixels = pixels(); }
    return device(url, o);
  };
  await goto(window, '#/');
  await flush(300);
  const button = label => window.document.querySelector('button[aria-label="' + label + '"]');
  if (native) button('Original size, one pixel per LED. For the Hub.').click();
  button('Save screenshot').click();
  button('Record GIF').click();
  for (let i = 0; i < 400 && files.length < 2; i++) await flush(30);
  const gif = files.length > 1 ? gifInfo(await new Promise(res => {
    const fr = new window.FileReader(); fr.onload = () => res(new Uint8Array(fr.result)); fr.readAsArrayBuffer(files[1]);
  })) : null;
  window.close();
  return { gif, png: pngs[0] };
}

function withinHubRules(name, c) {
  assert(c.gif && c.gif.frames <= 240, name + ': at most 240 frames (' + (c.gif && c.gif.frames) + ')');
  assert(c.gif && c.gif.ms <= 10000, name + ': at most 10 s (' + (c.gif && c.gif.ms) + ' ms)');
  assert(c.gif && c.gif.w * c.gif.h <= 2e7 && c.gif.w * c.gif.h * c.gif.frames <= 6e7,
    name + ': within 20 million pixels a frame and 60 million in all');
  assert(c.gif && c.gif.bytes <= 8e6, name + ': under 8 MB (' + (c.gif && c.gif.bytes) + ' B)');
  assert(c.png && c.png[0] * c.png[1] <= 16777216, name + ': a screenshot within 16.7 million pixels');
}

async function testCaptureLimits() {
  const wide = await capture({ width: 128, height: 32, step: 40 });
  withinHubRules('128×32', wide);
  assert(wide.gif.frames === 240, 'at the full rate a recording stops after 240 frames');
  assert(wide.gif.w === 896 && wide.gif.h === 224, 'a 128×32 recording of 240 frames is scaled 7× (' + wide.gif.w + '×' + wide.gif.h + ')');
  assert(wide.png[0] === 8192 && wide.png[1] === 2048, 'a 128×32 screenshot is scaled 64× (' + wide.png + ')');

  const slow = await capture({ width: 32, height: 8, step: 50 });
  withinHubRules('32×8, slow answers', slow);
  assert(slow.gif.frames === 199 && slow.gif.ms === 10000, 'a slower recording stops at 10 seconds (' + slow.gif.frames + ' frames)');
  assert(slow.gif.w === 640 && slow.gif.h === 160, 'a 32×8 recording keeps its 20× scale');
  assert(slow.png[0] === 3200 && slow.png[1] === 800, 'a 32×8 screenshot keeps its 100× scale');

  const native = await capture({ width: 128, height: 32, step: 40, native: true });
  withinHubRules('128×32 at 1:1', native);
  assert(native.gif.w === 128 && native.gif.h === 32 && native.png[0] === 128, '1:1 keeps one pixel per LED');

  const busy = await capture({ width: 64, height: 32, step: 40, busy: true });
  withinHubRules('64×32, every LED changing', busy);
  assert(busy.gif.w < 64 * 11, 'a recording too busy for 8 MB at its scale is written smaller (' + busy.gif.w + ' px wide)');
}

function testWifiThresholdsAgreeWithPanel() {
  const panel = fs.readFileSync(path.join(__dirname, '../../src/core/net/SignalStrength.h'), 'utf8');
  const html = fs.readFileSync(path.join(__dirname, '../index.html'), 'utf8');
  const words = html.match(/const wifiWord=([^;]+);/)[1];
  const expected = ['Excellent', 'Good', 'Fair'].map(name =>
    Number(panel.match(new RegExp('kWifi' + name + ' = (-\\d+)'))[1]));
  const actual = [...words.matchAll(/r>=(-\d+)/g)].map(match => Number(match[1]));
  assert(JSON.stringify(actual) === JSON.stringify(expected), 'Wi-Fi descriptions and panel bars share thresholds');
}

async function main() {
  testWifiThresholdsAgreeWithPanel();
  await testShowsStoredAutoBrightness();
  await testRotationPauseButton();
  await testCaptureLimits();
  await flush(20);
  console.log(`dashboard: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main();
