const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const { boot, goto } = require('./harness');

const header = fs.readFileSync(path.join(__dirname, '../../src/transport/http/WebUiAsset.h'), 'utf8');
function asset(name) {
  const match = header.match(new RegExp(name + '_GZ\\[\\] PROGMEM = \\{([\\s\\S]*?)\\};'));
  assert.ok(match, 'embedded asset exists');
  return zlib.gunzipSync(Buffer.from(match[1].match(/0x[\da-f]+/g).map(Number))).toString('utf8');
}

(async () => {
  for (const full of [false, true]) {
    const html = asset(full ? 'WEBUI_FULL' : 'WEBUI');
    const { window, store } = await boot({ html, caps: {
      transitions: [], audio: { mp3: full, rtttl: true, radio: full }, voice: full,
      microphone: full, display: { width: full ? 52 : 32, height: full ? 16 : 8 }
    }});
    try {
      if (full) store.settings.musicSource = 'auto';
      await goto(window, '#/system');
      assert.ok(window.document.querySelector('#sec-hub'), 'shared system controls render');
      assert.equal(!!window.document.querySelector('#sec-voice'), full, 'voice card matches the platform');
      const fields = [...window.document.querySelectorAll('.frow .key')].map(node => node.textContent);
      assert.equal(fields.includes('musicSource'), full, 'music-source field matches the platform');
      const rules = [...window.document.styleSheets].flatMap(sheet => [...sheet.cssRules]);
      assert.equal(rules.some(rule => rule.selectorText === '#sec-voice .ctl'), full,
        'voice styles ship only in the Linux asset');
      assert.equal(html.includes('musicSource'), full, 'music-source controls ship only in the Linux asset');
      assert.equal(html.includes('clockFace'), full, 'clock-face controls ship only in the Linux asset');
    } finally {
      window.close();
    }
  }
  console.log('build variants: ESP32 and Linux system pages passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
