/* Speech voice card: capability gating, the active voice, upload, back to the built-in voice and a test line.
   Run: node speech-voice-card.test.js */
const { boot, goto, flush } = require(process.env.HARNESS || './harness');
const assert = require('node:assert/strict');

const builtin = () => ({ name: 'Conny', language: 'en', version: '1', bytes: 1189344, uploaded: false, maxBytes: 2000000 });

function mock(window, voice) {
  const device = window.fetch, log = [], faults = { rejectUpload: false };
  window.fetch = async (url, o = {}) => {
    const path = String(url), method = (o.method || 'GET').toUpperCase();
    if (path === '/api/v1/audio/play') {
      log.push({ method, path, body: JSON.parse(o.body) });
      return new Response('{"ok":true}');
    }
    if (path !== '/api/v1/speech/voice') return device(url, o);
    log.push({ method, path, form: o.body instanceof window.FormData });
    if (method === 'POST') {
      if (faults.rejectUpload) {
        faults.rejectUpload = false;
        return new Response('{"error":{"code":"validationFailed","message":"not an ATTS file"}}', { status: 400 });
      }
      Object.assign(voice, { name: 'Conny', language: 'de', bytes: 790000, uploaded: true });
    } else if (method === 'DELETE') Object.assign(voice, builtin());
    else if (method !== 'GET') return new Response('{}', { status: 405 });
    return new Response(JSON.stringify(voice));
  };
  return { log, faults };
}

async function open(speech, voice) {
  const { window } = await boot({ caps: { transitions: [], audio: { rtttl: true, track: false, mp3: true, radio: true, speech } } });
  const network = mock(window, voice);
  await goto(window, '#/system');
  await flush(80);
  const sec = window.document.querySelector('#sec-speechvoice');
  const tr = window.eval('t');
  const btn = label => sec && [...sec.querySelectorAll('button')].find(b => b.textContent === label);
  return { window, ...network, sec, tr, btn, badge: sec && sec.querySelector('.badge') };
}

function pick(window, input, name) {
  const file = new window.File([new Uint8Array([65, 84, 84, 83])], name);
  Object.defineProperty(input, 'files', { configurable: true, value: [file] });
  input.dispatchEvent(new window.Event('change'));
}

(async () => {
  for (const speech of [undefined, false]) {
    const c = await open(speech, builtin());
    try {
      assert.equal(c.sec, null, 'no card on a clock without speech');
      assert.ok(!c.log.length);
    } finally { c.window.close(); }
  }

  const c = await open(true, builtin());
  try {
    assert.equal(c.badge.textContent, 'Conny · EN · ' + c.tr('svBuiltinTag'));
    const back = c.btn(c.tr('svBuiltin'));
    assert.equal(back.style.display, 'none', 'nothing to remove while the built-in voice speaks');

    const input = c.sec.querySelector('input[type=file]');
    assert.equal(input.accept, '.atts');
    c.faults.rejectUpload = true;
    pick(c.window, input, 'broken.atts'); await flush(80);
    assert.equal(c.badge.textContent, 'Conny · EN · ' + c.tr('svBuiltinTag'), 'a refused pack changes nothing');
    pick(c.window, input, 'conny-de.atts'); await flush(80);
    assert.ok(c.log.some(l => l.method === 'POST' && l.form), 'the pack goes up as a form');
    assert.equal(c.badge.textContent, 'Conny · DE');
    assert.equal(back.style.display, '');

    back.click(); await flush();
    assert.ok(!c.log.some(l => l.method === 'DELETE'), 'the first click only asks for confirmation');
    back.click(); await flush(80);
    assert.equal(c.log.filter(l => l.method === 'DELETE').length, 1);
    assert.equal(c.badge.textContent, 'Conny · EN · ' + c.tr('svBuiltinTag'));

    c.btn(c.tr('svSay')).click(); await flush();
    assert.deepEqual(c.log.filter(l => l.path === '/api/v1/audio/play').pop().body, { speech: c.tr('svLineP') });
    const line = c.sec.querySelector('input[type=text]');
    line.value = ' Guten Morgen! ';
    c.btn(c.tr('svSay')).click(); await flush();
    assert.deepEqual(c.log.filter(l => l.path === '/api/v1/audio/play').pop().body, { speech: 'Guten Morgen!' });
  } finally { c.window.close(); }
  console.log('speech voice card: all passed');
})().catch(e => { console.error(e); process.exit(1); });
