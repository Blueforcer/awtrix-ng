/* Home Assistant Voice card on the System page: capability gating, state rendering,
   failure reasons, the on/off switch, saving, removing the token with
   confirm, and a text for every reason the firmware can report.
   Run: node voice-card.test.js */
const { boot, goto, flush } = require('./harness');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const audio = { rtttl: true, track: false, mp3: true, radio: true };
const PIPES = [
  { id: 'p1', name: 'Speech', stt_engine: 'stt.whisper', tts_engine: 'tts.piper' },
  { id: 'p2', name: 'Text only', stt_engine: null, tts_engine: null },
];

function voiceMock(window, vc) {
  const device = window.fetch, log = [];
  window.fetch = async (url, o = {}) => {
    const p = String(url), method = (o.method || 'GET').toUpperCase();
    if (p !== '/api/v1/voice') return device(url, o);
    if (method === 'GET') { log.push({ method }); return new Response(JSON.stringify(vc)); }
    const body = JSON.parse(o.body);
    log.push({ method, body, headers: o.headers || {} });
    const next = { ...vc.config, ...body };
    if (body.clearToken) next.tokenSet = false;
    if (body.token) next.tokenSet = true;
    const field = vc.reject || (next.enabled && (!next.url || !next.tokenSet) ? 'enabled' : null);
    if (field) return new Response(JSON.stringify({ error: { code: 'validationFailed', field, message: 'invalid value' } }), { status: 422 });
    for (const k of ['enabled', 'url', 'pipeline', 'tokenSet']) vc.config[k] = next[k];
    return new Response('{"ok":true}');
  };
  return log;
}

function state(overrides = {}) {
  return { state: 'offline', error: '', pipelines: PIPES, ...overrides,
    config: { enabled: false, url: '', pipeline: '', tokenSet: false, ...overrides.config } };
}

async function open(voice, vc) {
  const { window } = await boot({ caps: { transitions: [], audio, voice } });
  const log = voiceMock(window, vc);
  await goto(window, '#/system');
  await flush(80);
  const sec = window.document.querySelector('#sec-voice');
  const buttons = sec ? [...sec.querySelectorAll('button')] : [];
  const inputs = sec ? [...sec.querySelectorAll('input')] : [];
  const tr = window.eval('t');
  return { window, log, vc, sec, tr,
    posts: () => log.filter(l => l.method === 'POST'),
    toast: () => window.document.querySelector('.toast')?.textContent || '',
    badges: sec ? [...sec.querySelectorAll('.badge')] : [],
    url: inputs.find(i => i.type === 'text'), token: inputs.find(i => i.type === 'password'),
    dev: inputs.filter(i => i.type === 'text')[1],
    toggle: inputs.find(i => i.type === 'checkbox'), pipe: sec && sec.querySelector('select'),
    save: buttons.find(b => b.textContent === tr('vcSave')),
    clear: buttons.find(b => b.textContent === tr('vcTokenClear')) };
}

async function gating() {
  for (const voice of [undefined, false]) {
    const { window, log, sec } = await open(voice, state());
    try {
      assert.equal(sec, null, 'no card without the voice capability (' + voice + ')');
      assert.equal(log.length, 0, 'nothing is polled without the capability');
      assert.ok(window.document.querySelector('#sec-hub'), 'the rest of the page still renders');
    } finally { window.close(); }
  }
}

async function rendering() {
  let c = await open(true, state());
  try {
    assert.ok(c.sec, 'the card appears with the capability');
    assert.equal(c.badges[0].textContent, c.tr('vcoffline'));
    assert.equal(c.toggle.checked, false);
    assert.equal(c.badges[1].textContent, c.tr('vcTokenNone'));
    assert.equal(c.clear.disabled, true, 'nothing to remove');
    assert.deepEqual([...c.pipe.options].map(o => o.value), ['', 'p1'], 'only pipelines with speech in and out');
    assert.equal(c.posts().length, 0, 'opening the page only reads the state');
  } finally { c.window.close(); }

  c = await open(true, state({ state: 'ready', config: { enabled: true, url: 'http://ha.local:8123', pipeline: 'p1', tokenSet: true } }));
  try {
    assert.equal(c.badges[0].textContent, c.tr('vcready'));
    assert.ok(c.badges[0].classList.contains('good'));
    assert.equal(c.toggle.checked, true);
    assert.equal(c.url.value, 'http://ha.local:8123');
    assert.equal(c.pipe.value, 'p1');
    assert.equal(c.token.value, '', 'the token is never shown');
    assert.equal(c.token.placeholder, c.tr('vcTokenKeep'), 'a saved token is kept when the field stays empty');
  } finally { c.window.close(); }

  c = await open(true, state({ state: 'error', error: 'tokenRejected', config: { enabled: true, url: 'http://ha', tokenSet: true } }));
  try {
    assert.equal(c.badges[0].textContent, c.tr('vcetokenRejected'), 'the reason is translated');
    assert.ok(c.badges[0].classList.contains('bad'));
  } finally { c.window.close(); }

  c = await open(true, state({ state: 'ready', error: 'microphoneUpdate', config: { enabled: true, url: 'http://ha', tokenSet: true } }));
  try {
    assert.equal(c.badges[0].textContent, c.tr('vcemicrophoneUpdate'), 'a reason wins over Ready');
  } finally { c.window.close(); }

  c = await open(true, state({ state: 'error', error: 'somethingNew', config: { enabled: true, url: 'http://ha', tokenSet: true } }));
  try {
    assert.equal(c.badges[0].textContent, 'somethingNew', 'an unknown code from newer firmware still shows');
  } finally { c.window.close(); }

  c = await open(true, state({ error: 'connectionLost' }));
  try {
    assert.equal(c.badges[0].textContent, c.tr('vcoffline'), 'switched off, an old reason is not shown');
  } finally { c.window.close(); }

  c = await open(true, state({ config: { pipeline: 'gone' } }));
  try {
    assert.equal(c.pipe.value, 'gone', 'a saved pipeline HA no longer lists stays selected');
  } finally { c.window.close(); }
}

async function onOff() {
  let c = await open(true, state({ state: 'ready', config: { enabled: true, url: 'http://ha.local:8123', tokenSet: true } }));
  try {
    c.toggle.checked = false;
    c.toggle.dispatchEvent(new c.window.Event('change'));
    await flush(80);
    assert.deepEqual(c.posts().map(p => p.body), [{ enabled: false }], 'the switch saves at once and only itself');
    assert.equal(c.vc.config.enabled, false);
    assert.equal(c.toast(), c.tr('saved'));
  } finally { c.window.close(); }

  c = await open(true, state());
  try {
    c.toggle.checked = true;
    c.toggle.dispatchEvent(new c.window.Event('change'));
    await flush(80);
    assert.equal(c.toast(), c.tr('vcfenabled'));
    assert.equal(c.toggle.checked, false, 'a refused switch falls back to what the device has');
  } finally { c.window.close(); }
}

async function saving() {
  let c = await open(true, state());
  try {
    c.url.value = ' http://ha.local:8123 ';
    c.token.value = 'secret';
    c.pipe.value = 'p1';
    c.save.click();
    await flush(80);
    const post = c.posts()[0];
    assert.deepEqual(post.body, { url: 'http://ha.local:8123', pipeline: 'p1', device: '', enabled: true, token: 'secret' },
      'Save and connect switches Voice on');
    assert.equal(post.headers['X-Awtrix-Voice'], '1', 'the device only takes voice settings with this header');
    assert.equal(c.token.value, '', 'the token field empties after saving');
    assert.equal(c.badges[1].textContent, c.tr('vcTokenSet'));
    assert.equal(c.toggle.checked, true);
  } finally { c.window.close(); }

  c = await open(true, state({ config: { tokenSet: true, url: 'http://ha.local:8123' } }));
  try {
    c.save.click();
    await flush(80);
    assert.equal('token' in c.posts()[0].body, false, 'an empty field keeps the saved token');
  } finally { c.window.close(); }

  c = await open(true, state({ config: { tokenSet: true, url: 'http://ha.local:8123' } }));
  try {
    c.dev.value = ' http://ha.local:8123/config/devices/device/0123456789abcdef0123456789abcdef ';
    c.save.click();
    await flush(80);
    assert.equal(c.posts()[0].body.device, '0123456789abcdef0123456789abcdef', 'a pasted device page keeps only its ID');
    assert.equal(c.dev.value, '0123456789abcdef0123456789abcdef');
  } finally { c.window.close(); }

  for (const field of ['url', 'token']) {
    c = await open(true, state({ reject: field }));
    try {
      c.token.value = 'secret';
      c.save.click();
      await flush(80);
      assert.equal(c.toast(), c.tr('vcf' + field), 'a refused ' + field + ' is explained in the UI language');
      assert.equal(c.token.value, 'secret', 'a refused token stays in the field');
      assert.equal(c.save.disabled, false);
    } finally { c.window.close(); }
  }
}

async function removeToken() {
  const c = await open(true, state({ state: 'ready', config: { enabled: true, url: 'http://ha.local:8123', tokenSet: true } }));
  try {
    c.clear.click();
    await flush();
    assert.equal(c.posts().length, 0, 'the first click only arms');
    assert.equal(c.clear.textContent, c.tr('sure'));
    c.clear.click();
    await flush(80);
    assert.deepEqual(c.posts()[0].body, { clearToken: true, enabled: false }, 'voice cannot stay on without a token');
    assert.equal(c.toggle.checked, false);
    assert.equal(c.badges[1].textContent, c.tr('vcTokenNone'));
  } finally { c.window.close(); }
}

function firmwareCodes() {
  const source = fs.readFileSync(path.join(__dirname, '../../src/platform/tc002/voice/VoiceErrors.def'), 'utf8');
  const records = source.split(/\r?\n/).filter(line => line.trim() && !line.startsWith('//'));
  return records.map(line => {
    const record = /^AWTRIX_VOICE_ERROR\(([A-Za-z]+)\)$/.exec(line);
    assert.ok(record, 'valid voice error data record: ' + line);
    return record[1];
  });
}

async function reasons() {
  const { window } = await boot();
  try {
    const STR = window.eval('STR');
    const codes = firmwareCodes();
    assert.ok(codes.length > 0, 'voice errors are exported');
    assert.equal(new Set(codes).size, codes.length, 'error codes are unique');
    for (const code of codes) assert.ok(STR['vce' + code], 'the web UI explains ' + code);
  } finally { window.close(); }
}

(async () => {
  await gating();
  await rendering();
  await onOff();
  await saving();
  await removeToken();
  await reasons();
  console.log('voice-card: gating, states and reasons, on/off, saving, confirmed token removal and reason texts passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
