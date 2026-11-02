const { boot, goto, flush } = require('./harness');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const audio = { rtttl: true, track: false, mp3: true, radio: true };
const PIPES = [
  { id: 'p1', name: 'Speech', stt_engine: 'stt.whisper', tts_engine: 'tts.piper' },
  { id: 'p2', name: 'Text only', stt_engine: null, tts_engine: null },
];
const HA = { url: 'http://ha.local:8123', tokenSet: true };

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
    if (vc.reject) return new Response(JSON.stringify({ error: { code: 'validationFailed', field: vc.reject, message: 'invalid value' } }), { status: 422 });
    for (const k of ['wakeWord', 'wakeSensitivity', 'url', 'pipeline', 'device', 'tokenSet', 'assistant']) vc.config[k] = next[k];
    return new Response('{"ok":true}');
  };
  return log;
}

function state(overrides = {}) {
  return { state: 'offline', error: '', lastError: '', pipelines: [], wakeWordAvailable: true, wakeListening: false, assistantApps: [], ...overrides,
    config: { wakeWord: false, wakeSensitivity: 'normal', url: '', pipeline: '', device: '', tokenSet: false, assistant: 'ha', ...overrides.config } };
}

const ready = (overrides = {}) => state({ state: 'ready', pipelines: PIPES, ...overrides, config: { ...HA, ...overrides.config } });

async function open(voice, vc) {
  const { window } = await boot({ caps: { transitions: [], audio, voice } });
  const log = voiceMock(window, vc);
  await goto(window, '#/system');
  await flush(80);
  const sec = window.document.querySelector('#sec-voice');
  const buttons = sec ? [...sec.querySelectorAll('button')] : [];
  const selects = sec ? [...sec.querySelectorAll('select')] : [];
  const tr = window.eval('t');
  const shown = node => {
    for (let n = node; n; n = n.parentElement) if (n.style?.display === 'none') return false;
    return true;
  };
  const status = sec && sec.querySelector('.badge');
  const wake = sec && sec.querySelector('input[type=checkbox]');
  return { window, log, vc, sec, tr, shown, status,
    posts: () => log.filter(l => l.method === 'POST'),
    toast: () => window.document.querySelector('.toast')?.textContent || '',
    hint: status && status.closest('.frow').querySelector('.help'),
    url: sec && sec.querySelector('input[inputmode=url]'), token: sec && sec.querySelector('input[type=password]'),
    dev: sec && sec.querySelector('input[placeholder="' + tr('vcDevP') + '"]'),
    wake, wakeHint: wake && wake.closest('.frow').querySelector('.help'),
    assist: selects[0], sens: selects[1], pipe: selects[2],
    box: sec && sec.querySelector('details'),
    connect: buttons.find(b => b.textContent === tr('vcConnect')),
    clear: buttons.find(b => b.textContent === tr('seRemove')) };
}

async function visit(voice, vc, check) {
  const c = await open(voice, vc);
  try {
    await check(c);
  } finally { c.window.close(); }
}

const card = (vc, check) => visit(true, vc, check);

async function pick(c, select, value) {
  select.value = value;
  select.dispatchEvent(new c.window.Event('change'));
  await flush(80);
}

async function type(c, input, value) {
  input.value = value;
  input.dispatchEvent(new c.window.Event('input'));
}

async function toggle(c, value) {
  c.wake.checked = value;
  c.wake.dispatchEvent(new c.window.Event('change'));
  await flush(80);
}

const repaint = c => toggle(c, false);

async function gating() {
  for (const voice of [undefined, false]) {
    await visit(voice, state(), async c => {
      assert.equal(c.sec, null, 'no card without the voice capability (' + voice + ')');
      assert.equal(c.log.length, 0, 'nothing is polled without the capability');
      assert.ok(c.window.document.querySelector('#sec-hub'), 'the rest of the page still renders');
    });
  }
}

async function homeAssistantFromScratch() {
  await card(state(), async c => {
    assert.ok(c.sec, 'the card appears with the capability');
    assert.equal(c.posts().length, 0, 'opening the page only reads the state');
    assert.equal(c.status.textContent, c.tr('vcUnset'), 'a new setup says so, not that a connection failed');
    assert.equal(c.hint.textContent, c.tr('vcUnsetH'), 'and what comes next');
    assert.equal(c.box.open, true, 'the fields are open');
    for (const node of [c.pipe, c.dev, c.clear, c.wake, c.sens]) assert.equal(c.shown(node), false, 'and nothing else asks for attention');
    assert.equal(c.connect.disabled, true, 'nothing to connect with yet');

    await type(c, c.url, 'http://ha.local:8123');
    assert.equal(c.connect.disabled, true, 'an address alone does not connect');
    await type(c, c.token, 'secret');
    assert.equal(c.connect.disabled, false, 'an address and a token do');

    c.connect.click();
    await flush(80);
    const post = c.posts()[0];
    assert.deepEqual(post.body, { url: 'http://ha.local:8123', token: 'secret' }, 'Connect sends what it connects with');
    assert.equal(post.headers['X-Awtrix-Voice'], '1', 'the device only takes voice settings with this header');
    assert.equal(c.token.value, '', 'the token field empties');
    assert.equal(c.token.placeholder, c.tr('oaKeep'), 'and says it holds one');
    assert.equal(c.shown(c.clear), true, 'which can go again');
    assert.equal(c.status.textContent, c.tr('mqbusy'), 'an address and a token without a connection yet are connecting');
    assert.equal(c.box.open, true, 'the fields stay until it worked');
    assert.equal(c.shown(c.wake), true, 'the wake word comes with the setup');

    Object.assign(c.vc, { state: 'ready', pipelines: PIPES, wakeListening: false });
    await repaint(c);
    assert.equal(c.status.textContent, c.tr('vcready'), 'connected');
    assert.ok(c.status.classList.contains('good'));
    assert.equal(c.hint.textContent, '', 'and nothing to do');
    assert.equal(c.box.open, false, 'the fields close once it worked');
    assert.equal(c.shown(c.pipe), true, 'the list of assistants is there now');
    assert.equal(c.shown(c.dev), true, 'and the room');

    c.box.open = true;
    await repaint(c);
    assert.equal(c.box.open, true, 'opened by hand, the fields stay open');
  });

  await card(ready({ state: 'connecting', error: 'connectionLost' }), async typing => {
    assert.equal(typing.box.open, true);
    typing.url.focus();
    Object.assign(typing.vc, { state: 'ready', error: '' });
    await repaint(typing);
    assert.equal(typing.box.open, true, 'a connection that comes back does not close the field being typed in');
    typing.url.blur();
    await repaint(typing);
    assert.equal(typing.box.open, false, 'leaving the field it does');
    typing.vc.state = 'connecting';
    typing.vc.error = 'connectionLost';
    await repaint(typing);
    typing.token.focus();
    typing.token.value = 'secret';
    typing.token.dispatchEvent(new typing.window.KeyboardEvent('keydown', { key: 'Enter' }));
    await flush(80);
    typing.vc.state = 'ready';
    typing.vc.error = '';
    await repaint(typing);
    assert.equal(typing.box.open, false, 'Enter counts as done with typing');
  });
}

async function states() {
  await card(ready({ config: { pipeline: 'p1' } }), async c => {
    assert.equal(c.status.textContent, c.tr('vcready'));
    assert.equal(c.box.open, false, 'a working setup hides its fields');
    assert.equal(c.url.value, HA.url);
    assert.equal(c.pipe.value, 'p1');
    assert.deepEqual([...c.pipe.options].map(o => o.value), ['', 'p1'], 'only assistants with speech in and out');
    assert.equal(c.token.value, '', 'the token is never shown');
    assert.equal(c.connect.disabled, false, 'a saved token is enough to connect again');
  });

  for (const busy of ['starting', 'listening', 'processing', 'speaking']) {
    await card(ready({ state: busy }), async c => {
      assert.equal(c.status.textContent, c.tr('vc' + busy), 'a request shows ' + busy);
      assert.equal(c.hint.textContent, '', 'and needs no explanation');
      assert.equal(c.box.open, false);
    });
  }

  await card(ready({ state: 'error', error: 'tokenRejected' }), async c => {
    assert.equal(c.status.textContent, c.tr('vcetokenRejected'), 'the reason is translated');
    assert.ok(c.status.classList.contains('bad'));
    assert.equal(c.hint.textContent, c.tr('vcxtokenRejected'), 'and says what to do');
    assert.equal(c.box.open, true, 'the fields to fix it are open');
    assert.equal(c.shown(c.wake), true, 'a broken connection keeps the wake word switch');
  });

  await card(ready({ state: 'connecting', error: 'connectionLost', pipelines: [] }), async c => {
    assert.equal(c.status.textContent, c.tr('vceconnectionLost'), 'a retry keeps the reason');
    assert.equal(c.hint.textContent, c.tr('vcxconnectionLost'));
    assert.equal(c.box.open, true);
    assert.equal(c.shown(c.pipe), false, 'no list of assistants without a connection');
    assert.equal(c.shown(c.dev), false, 'and no room');
  });

  await card(ready({ state: 'error', error: 'pipelineIncomplete', config: { pipeline: 'gone' } }), async c => {
    assert.equal(c.status.textContent, c.tr('vcepipelineIncomplete'));
    assert.equal(c.box.open, true);
    assert.equal(c.shown(c.pipe), true, 'with a list known, another assistant can be picked');
    assert.equal(c.pipe.value, 'gone', 'a saved assistant HA no longer lists stays selected');
  });

  await card(ready({ error: 'microphoneUpdate' }), async c => {
    assert.equal(c.status.textContent, c.tr('vcemicrophoneUpdate'), 'a reason wins over Ready');
    assert.equal(c.box.open, false, 'the connection fields would not fix it');
  });

  await card(ready({ state: 'error', error: 'somethingNew' }), async c => {
    assert.equal(c.status.textContent, 'somethingNew', 'an unknown code from newer firmware still shows');
    assert.equal(c.hint.textContent, '');
  });

  await card(ready({ lastError: 'nothingUnderstood' }), async c => {
    assert.equal(c.status.textContent, c.tr('vcready'), 'a failed try leaves the clock ready');
    assert.ok(c.status.classList.contains('good'));
    assert.equal(c.hint.textContent, c.tr('vcLast') + ' ' + c.tr('vcenothingUnderstood') + '. ' + c.tr('vcxnothingUnderstood'), 'and says what went wrong');
    assert.ok(c.hint.classList.contains('warn'));
    assert.equal(c.box.open, false);
  });

  await card(ready({ state: 'error', lastError: 'audioTooSlow' }), async c => {
    assert.equal(c.status.textContent, c.tr('mqbusy'), 'right after a failed try it connects again');
    assert.ok(c.hint.textContent.includes(c.tr('vceaudioTooSlow')));
    assert.equal(c.box.open, false, 'without opening the fields');
  });

  await card(state({ error: 'configUnavailable', config: { assistant: 'off' } }), async c => {
    assert.equal(c.shown(c.status), true, 'unreadable settings show even though they read as off');
    assert.equal(c.status.textContent, c.tr('vceconfigUnavailable'));
  });
}

async function wakeWord() {
  await card(ready({ wakeListening: true }), async c => {
    assert.equal(c.wake.checked, false, 'the wake word starts off');
    assert.equal(c.wakeHint.textContent, c.tr('vcWakeH'), 'it keeps its microphone help');
    assert.equal(c.shown(c.sens), false, 'no sensitivity while it is off');
    await toggle(c, true);
    assert.deepEqual(c.posts().map(p => p.body), [{ wakeWord: true }], 'the switch saves at once and only itself');
    assert.equal(c.wake.checked, true, 'the switch shows what the device has');
    assert.equal(c.shown(c.sens), true, 'on, the sensitivity shows');
    assert.equal(c.wakeHint.textContent, c.tr('vcWakeH'), 'a clock that listens needs no note');
  });

  await card(ready({ wakeListening: true, config: { wakeWord: true, wakeSensitivity: 'high' } }), async c => {
    assert.deepEqual([...c.sens.options].map(o => o.value), ['low', 'normal', 'high'], 'three levels');
    const levels = ['vcSenslow', 'vcSensnormal', 'vcSenshigh'];
    for (const key of levels) assert.notEqual(c.tr(key), key, key + ' has a text');
    assert.deepEqual([...c.sens.options].map(o => o.textContent), levels.map(c.tr));
    assert.equal(c.sens.value, 'high', 'the level of the device');
    await pick(c, c.sens, 'low');
    assert.deepEqual(c.posts().map(p => p.body), [{ wakeSensitivity: 'low' }], 'the level saves at once and only itself');
    assert.equal(c.toast(), c.tr('saved'));
    assert.equal(c.sens.value, 'low');
  });

  await card(ready({ state: 'connecting', error: 'connectionLost', config: { wakeWord: true } }), async c => {
    assert.equal(c.wakeHint.textContent, c.tr('vcWakeWait'), 'on but not listening: a note');
    assert.ok(c.wakeHint.classList.contains('warn'));
  });

  await card(ready({ error: 'microphoneUpdate', config: { wakeWord: true } }), async c => {
    assert.equal(c.wakeHint.textContent, c.tr('vcWakeWait'), 'also with a connection');
  });

  await card(ready({ state: 'speaking', config: { wakeWord: true } }), async c => {
    assert.equal(c.wakeHint.textContent, c.tr('vcWakeH'), 'a request running is no reason for a note');
    assert.equal(c.wakeHint.classList.contains('warn'), false);
  });

  await card(state({ state: 'error', error: 'appMissing', assistantApps: ['Chat'], config: { wakeWord: true, assistant: 'app:Gone' } }), async c => {
    assert.equal(c.wakeHint.textContent, c.tr('vcWakeWait'), 'an app that cannot answer: the same note');
  });

  await card(ready({ wakeWordAvailable: false, config: { wakeWord: true } }), async c => {
    for (const node of [c.wake, c.sens]) assert.equal(c.shown(node), false, 'without a wake word on the clock the rows are hidden');
  });
}

async function saving() {
  for (const [typed, sent] of [
    [' http://ha.local:8123 ', 'http://ha.local:8123'],
    ['ha.local:8123', 'http://ha.local:8123'],
    ['192.168.1.20:8123/', 'http://192.168.1.20:8123'],
    ['http://ha.local:8123/lovelace/0?x=1', 'http://ha.local:8123'],
    ['https://ha.example.com/', 'https://ha.example.com'],
    ['http://bad host', 'http://bad host'],
  ]) {
    await card(ready(), async c => {
      await type(c, c.url, typed);
      await type(c, c.token, ' secret ');
      c.connect.click();
      await flush(80);
      assert.deepEqual(c.posts()[0].body, { url: sent, token: 'secret' }, 'the address is reduced to what the clock takes: ' + typed);
      assert.equal(c.url.value, sent, 'and the field shows it');
    });
  }

  await card(ready(), async c => {
    c.connect.click();
    await flush(80);
    assert.equal('token' in c.posts()[0].body, false, 'an empty field keeps the saved token');
    c.url.dispatchEvent(new c.window.KeyboardEvent('keydown', { key: 'Enter' }));
    await flush(80);
    assert.equal(c.posts().length, 2, 'Enter in the field connects too');
  });

  await card(ready(), async c => {
    await type(c, c.url, 'http://other.local:8123');
    assert.equal(c.connect.disabled, true, 'a saved token does not move to another address');
    assert.equal(c.token.placeholder, c.tr('vcTokenAgain'), 'and the field asks for it again');
    await type(c, c.token, 'secret');
    assert.equal(c.connect.disabled, false);
    await type(c, c.token, '');
    await type(c, c.url, 'HTTP://ha.local:8123/');
    assert.equal(c.connect.disabled, false, 'the same address written differently keeps it');
    assert.equal(c.token.placeholder, c.tr('oaKeep'));
  });

  await card(state(), async c => {
    c.token.dispatchEvent(new c.window.KeyboardEvent('keydown', { key: 'Enter' }));
    await flush(80);
    assert.equal(c.posts().length, 0, 'not while nothing can connect');
  });

  await card(ready(), async c => {
    await pick(c, c.pipe, 'p1');
    assert.deepEqual(c.posts().map(p => p.body), [{ pipeline: 'p1' }], 'the assistant saves at once and only itself');
    assert.equal(c.toast(), c.tr('saved'));
  });

  await card(ready(), async c => {
    await pick(c, c.dev, ' http://ha.local:8123/config/devices/device/0123456789abcdef0123456789abcdef ');
    assert.deepEqual(c.posts().map(p => p.body), [{ device: '0123456789abcdef0123456789abcdef' }], 'a pasted device page keeps only its ID and saves at once');
    assert.equal(c.dev.value, '0123456789abcdef0123456789abcdef');
    await pick(c, c.dev, '');
    assert.deepEqual(c.posts()[1].body, { device: '' }, 'empty clears it');
  });

  for (const field of ['url', 'token']) {
    await card(state({ reject: field }), async c => {
      await type(c, c.url, 'http://ha.local:8123');
      await type(c, c.token, 'secret');
      c.connect.click();
      await flush(80);
      assert.equal(c.toast(), c.tr('vcf' + field), 'a refused ' + field + ' is explained');
      assert.equal(c.token.value, 'secret', 'a refused token stays in the field');
      assert.equal(c.connect.disabled, false);
    });
  }

  await card(ready({ reject: 'device' }), async c => {
    await pick(c, c.dev, 'not an id!');
    assert.equal(c.toast(), c.tr('vcfdevice'));
  });
}

async function removeToken() {
  await card(ready(), async c => {
    c.clear.click();
    await flush();
    assert.equal(c.posts().length, 0, 'the first click only arms');
    assert.equal(c.clear.textContent, c.tr('sure'));
    c.clear.click();
    await flush(80);
    assert.deepEqual(c.posts()[0].body, { clearToken: true }, 'removing the token switches nothing');
    assert.equal(c.assist.value, 'ha', 'Home Assistant stays the choice');
    assert.equal(c.shown(c.clear), false, 'nothing left to remove');
    assert.equal(c.token.placeholder, '');
    c.vc.state = 'offline';
    await repaint(c);
    assert.equal(c.connect.disabled, true);
    assert.equal(c.box.open, true, 'the fields open for a new one');
  });
}

async function assistant() {
  const ha = c => [c.url, c.token, c.connect, c.box];
  await card(state({ assistantApps: ['Chat', 'Talk'], config: { assistant: 'off', ...HA } }), async c => {
    assert.deepEqual([...c.assist.options].map(o => o.value), ['off', 'ha', 'app:Chat', 'app:Talk'],
      'Off, Home Assistant and every app that can answer');
    assert.notEqual(c.tr('mqoff'), 'mqoff', 'Off has a text');
    assert.equal(c.assist.options[0].textContent, c.tr('mqoff'));
    assert.equal(c.assist.value, 'off');
    for (const node of [c.status, c.wake, c.sens, ...ha(c)]) assert.equal(c.shown(node), false, 'off is the choice alone');

    await pick(c, c.assist, 'ha');
    assert.deepEqual(c.posts().map(p => p.body), [{ assistant: 'ha' }], 'the choice saves at once and only itself');
    for (const node of [c.status, c.wake, ...ha(c)]) assert.equal(c.shown(node), true, 'Home Assistant shows its state and fields');
    assert.equal(c.url.value, HA.url, 'off kept the address');

    Object.assign(c.vc, { state: 'ready', wakeListening: true });
    await pick(c, c.assist, 'app:Chat');
    assert.equal(c.status.textContent, c.tr('vcready'), 'an app has a state too');
    assert.equal(c.hint.textContent, '');
    for (const node of ha(c)) assert.equal(c.shown(node), false, 'an app needs none of the HA fields');
    assert.equal(c.shown(c.wake), true, 'the wake word starts an app too');

    await pick(c, c.assist, 'off');
    assert.equal(c.vc.config.assistant, 'off', 'and off is one choice away');
    assert.equal(c.posts().some(p => 'enabled' in p.body), false, 'the page never writes enabled');
  });

  await card(state({ state: 'error', error: 'appMissing', assistantApps: ['Chat'], config: { assistant: 'app:Gone' } }), async c => {
    assert.equal(c.assist.value, 'app:Gone', 'a saved app that is no longer installed stays selected');
    assert.equal(c.status.textContent, c.tr('vceappMissing'), 'and the status says it is missing');
    assert.ok(c.status.classList.contains('bad'));
    assert.equal(c.hint.textContent, c.tr('vcxappMissing'));
    await pick(c, c.assist, 'off');
    assert.deepEqual([...c.assist.options].map(o => o.value), ['off', 'ha', 'app:Chat'], 'once replaced it leaves the list');
  });

  for (const error of ['appOff', 'appBroken']) {
    await card(state({ state: 'error', error, assistantApps: ['Chat'], config: { assistant: 'app:Chat' } }), async c => {
      assert.equal(c.status.textContent, c.tr('vce' + error));
      assert.equal(c.hint.textContent, c.tr('vcx' + error));
    });
  }
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
    for (const code of codes) {
      assert.ok(STR['vce' + code], 'the web UI names ' + code);
      assert.ok(STR['vcx' + code], 'and says what to do about it');
    }
  } finally { window.close(); }
}

(async () => {
  await gating();
  await homeAssistantFromScratch();
  await states();
  await wakeWord();
  await saving();
  await removeToken();
  await assistant();
  await reasons();
  console.log('voice-card: gating, setup from scratch, states and reasons, wake word, saving, confirmed token removal, assistant choice and reason texts passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
