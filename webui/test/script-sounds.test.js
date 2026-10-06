/* A script's own sounds in the web UI: the Audio page groups them per script, the Scripts page
   links there, the Hub update and the Hub install (dependencies, "Install as a new script")
   bring them along with the same rules as the Hub's own installer, and a backup keeps them.
   Run: node script-sounds.test.js */
const { boot, goto, flush, stubXhr, putSound, useFakeTimers } = require('./harness');
const restoreTimers = useFakeTimers();
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');

const hash = s => createHash('sha256').update(s).digest('hex');
const ID = 'RacerRacer12', PAD = 'PadPadPad123';
const linked = (id, code) => `# @hub ${id} ${hash(code)}\n${code}`;
const $ = (window, s) => window.document.querySelector(s);
const $$ = (window, s) => [...window.document.querySelectorAll(s)];
const lastToast = window => $$(window, '.toast').at(-1);
const tr = (window, key) => window.eval('t')(key);
const toastButton = (window, key) => $$(window, '.toast .tacts button').find(b => b.textContent === tr(window, key));
const writes = netlog => netlog.filter(l => /^(PUT|DELETE|XHR) /.test(l) && !/icons\/origins/.test(l));
const hubGets = netlog => netlog.filter(l => l.startsWith('HUB '));
const caps = ({ mp3 = true } = {}) => ({ transitions: [], scriptUpdates: true,
  audio: { mp3, rtttl: true, song: false, speech: false, track: false, radio: mp3, url: false, effect: false, clip: false } });

// The Hub's script API: release (public), source and sounds (with the connection key).
function hubMock(window, store, hub) {
  const device = window.fetch;
  const res = (body, status = 200) => ({ ok: status < 300, status,
    json: async () => JSON.parse(body), text: async () => body, blob: async () => new window.Blob([body]) });
  window.fetch = async (url, o = {}) => {
    const m = /^https:\/\/awtrix\.de\/api\/v1\/scripts\/(\w{12})\/(release|source|sounds\/([\w-]+)\.mp3)$/.exec(String(url));
    if (!m) return device(url, o);
    const item = hub[m[1]];
    store.netlog.push('HUB ' + (m[3] ? 'sound ' + m[3] : m[2]) + ' ' + m[1]);
    if (!item) return res('', 404);
    if (m[2] === 'release') {
      assert.equal(o.headers.Authorization, undefined, 'release metadata stays public');
      return res(JSON.stringify({ id: m[1], name: item.title, revision: 2, sha256: hash(item.code), notes: '',
        ...(item.sounds ? { sounds: item.sounds.map(s => ({ name: s.name, sha256: hash(s.data), bytes: s.bytes ?? Buffer.byteLength(s.data) })) } : {}) }));
    }
    assert.equal(o.headers.Authorization, 'Bearer hub-token', 'downloads carry the connection key');
    if (m[2] === 'source') return res(item.code);
    const s = item.sounds.find(x => x.name === m[3]);
    return s ? res(s.served ?? s.data) : res('', 404);
  };
}

async function scriptsPage({ mp3 = true, scripts = {}, sounds = [], usage, hub = {}, apps } = {}) {
  const ctx = await boot({ caps: caps({ mp3 }) });
  const { window, store } = ctx;
  window.AbortSignal = AbortSignal;
  window.localStorage.awtrixHubToken = 'hub-token';
  for (const [n, src] of Object.entries(scripts)) store.scripts.set(n, src);
  for (const [s, f, d] of sounds) putSound(store, s, f, d);
  if (usage) store.usage = usage;
  if (apps) store.list = () => [...store.scripts.keys()].map(name => ({ name, origin: 'script', error: null })).concat(apps);
  stubXhr(window, ctx.xhr = [], store);
  hubMock(window, store, hub);
  return ctx;
}
async function openScript(window, name) {
  await goto(window, '#/scripts');
  await flush(250);
  $$(window, '.ftitem').find(i => i.querySelector('.nm').textContent === name).click();
  await flush(120);
}
const panelText = window => $(window, '.script-hub-panel').textContent;
async function clickUpdate(window) {
  $(window, '.script-hub-panel button').click();
  const confirm = lastToast(window).textContent;
  $(window, '.toast .tacts button.pri').click();
  await flush(700);
  return confirm;
}
const folder = (store, s) => [...(store.sounds.get(s) || new Map()).entries()].map(([f, v]) => f + '=' + v.data).sort();

// ---- the Hub update ---------------------------------------------------------------------------

const RACER = { title: 'AWTRIX GP', code: 'new code\n', sounds: [
  { name: 'boost', data: 'ID3-boost-1' }, { name: 'theme', data: 'ID3-theme-2' }, { name: 'jump', data: 'ID3-jump-1' }] };
const OLD_SOUNDS = [['Racer', 'boost.mp3', 'ID3-boost-1'], ['Racer', 'theme.mp3', 'ID3-theme-1'], ['Racer', 'crash.mp3', 'ID3-crash-1']];

async function updateOrder() {
  const { window, store, netlog, xhr } = await scriptsPage({ scripts: { Racer: linked(ID, 'old code\n') },
    sounds: OLD_SOUNDS, hub: { [ID]: RACER } });
  try {
    await openScript(window, 'Racer');
    assert.ok(panelText(window).includes(tr(window, 'suReady')));
    netlog.length = 0;
    const confirm = await clickUpdate(window);
    assert.doesNotMatch(confirm, /left out/, 'a device that plays MP3s gets the sounds, no note');
    assert.deepEqual(writes(netlog), [
      'XHR POST /api/v1/apps/script/Racer/sounds theme.mp3',
      'XHR POST /api/v1/apps/script/Racer/sounds jump.mp3',
      'PUT /api/v1/apps/script-update/Racer',
      'DELETE /api/v1/apps/script/Racer/sounds/crash',
    ], 'new and changed sounds first, then the script, then the late delete');
    assert.deepEqual(hubGets(netlog).filter(l => l.startsWith('HUB sound')), ['HUB sound theme ' + ID, 'HUB sound jump ' + ID],
      'a sound whose sha256 already matches is neither downloaded nor sent');
    assert.deepEqual(folder(store, 'Racer'), ['boost.mp3=ID3-boost-1', 'jump.mp3=ID3-jump-1', 'theme.mp3=ID3-theme-2']);
    assert.equal(store.scripts.get('Racer'), linked(ID, RACER.code));
    assert.deepEqual(xhr.map(x => x.timeout), [16000, 16000], 'a small sound gets the base upload time');
    assert.equal(lastToast(window).textContent, tr(window, 'suDone'));
    assert.equal($(window, '[role=status].script-hub-panel').hidden, true, 'the progress line goes away when done');
  } finally { window.close(); }
}

async function soundOnlyUpdate() {
  const code = 'same code\n';
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: linked(ID, code) },
    sounds: [['Racer', 'boost.mp3', 'ID3-boost-1']], hub: { [ID]: { ...RACER, code } } });
  try {
    await openScript(window, 'Racer');
    assert.ok(panelText(window).includes(tr(window, 'suReady')), 'the same source with other sounds is an update');
    netlog.length = 0;
    await clickUpdate(window);
    assert.deepEqual(writes(netlog), [
      'XHR POST /api/v1/apps/script/Racer/sounds theme.mp3',
      'XHR POST /api/v1/apps/script/Racer/sounds jump.mp3',
      'PUT /api/v1/apps/script-update/Racer'], 'the script is written again so it starts with its sounds');
    await flush(300);
    assert.ok(panelText(window).includes(tr(window, 'suCurrent')), 'afterwards the check finds nothing to do');
  } finally { window.close(); }
  const same = await scriptsPage({ scripts: { Racer: linked(ID, code) },
    sounds: RACER.sounds.map(s => ['Racer', s.name + '.mp3', s.data]), hub: { [ID]: { ...RACER, code } } });
  try {
    await openScript(same.window, 'Racer');
    assert.ok(panelText(same.window).includes(tr(same.window, 'suCurrent')), 'matching sounds are no update');
  } finally { same.window.close(); }
}

async function spaceCheck() {
  const big = { ...RACER, sounds: [{ name: 'theme', data: 'ID3-theme-2', bytes: 20000 }] };
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: linked(ID, 'old code\n') },
    hub: { [ID]: big }, usage: { usedBytes: 1040000, totalBytes: 1048576 } });
  try {
    await openScript(window, 'Racer');
    netlog.length = 0;
    await clickUpdate(window);
    assert.deepEqual(writes(netlog), [], 'nothing is written when the sounds do not fit');
    const room = lastToast(window).textContent;
    assert.ok(room.includes('24.0 KB') && room.includes('8.4 KB'), 'the refusal names what the sounds need and what is free');
    assert.equal(store.scripts.get('Racer'), linked(ID, 'old code\n'));
  } finally { window.close(); }
}

async function retryContinues() {
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: linked(ID, 'old code\n') },
    sounds: OLD_SOUNDS, hub: { [ID]: RACER } });
  try {
    let once = true;
    store.uploadFail = e => e.files[0].name === 'jump.mp3' && once && !(once = false)
      ? { status: 507, body: { error: { code: 'insufficientStorage', message: 'storage full' } } } : null;
    await openScript(window, 'Racer');
    netlog.length = 0;
    await clickUpdate(window);
    const failure = lastToast(window).textContent;
    assert.ok(failure.includes('jump.mp3') && failure.includes(tr(window, 'sndFull')), 'the failure names the file and the reason');
    assert.ok(toastButton(window, 'retry'), 'a failed update offers to try again');
    assert.equal(store.scripts.get('Racer'), linked(ID, 'old code\n'), 'a failed update leaves the installed script');
    assert.deepEqual(folder(store, 'Racer'), ['boost.mp3=ID3-boost-1', 'crash.mp3=ID3-crash-1', 'theme.mp3=ID3-theme-2'],
      'and what was already sent');
    await flush(300);
    netlog.length = 0;
    toastButton(window, 'retry').click();
    await flush(700);
    assert.deepEqual(writes(netlog), [
      'XHR POST /api/v1/apps/script/Racer/sounds jump.mp3',
      'PUT /api/v1/apps/script-update/Racer',
      'DELETE /api/v1/apps/script/Racer/sounds/crash'], 'try again continues where it stopped');
    assert.equal(store.scripts.get('Racer'), linked(ID, RACER.code));
  } finally { window.close(); }
}

async function lateDeleteRetry() {
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: linked(ID, 'old code\n') },
    sounds: OLD_SOUNDS, hub: { [ID]: RACER } });
  try {
    await openScript(window, 'Racer');
    const device = window.fetch;
    let refuse = true;
    window.fetch = async (url, o = {}) => refuse && o.method === 'DELETE' && String(url).endsWith('/sounds/crash')
      ? (refuse = false, { ok: false, status: 500, text: async () => '{"error":{"code":"internalError","message":"could not remove it"}}' })
      : device(url, o);
    await clickUpdate(window);
    assert.match(lastToast(window).textContent, /could not remove it/);
    assert.equal(store.scripts.get('Racer'), linked(ID, RACER.code), 'the script itself was already updated');
    await flush(300);
    assert.ok(panelText(window).includes(tr(window, 'suReady')), 'the sound left over is still an update');
    netlog.length = 0;
    toastButton(window, 'retry').click();
    await flush(700);
    assert.deepEqual(writes(netlog), ['PUT /api/v1/apps/script-update/Racer', 'DELETE /api/v1/apps/script/Racer/sounds/crash'],
      'the retry of an update already written sends no sound again and finishes the delete');
    assert.deepEqual(folder(store, 'Racer'), ['boost.mp3=ID3-boost-1', 'jump.mp3=ID3-jump-1', 'theme.mp3=ID3-theme-2']);
  } finally { window.close(); }
}

async function badDownload() {
  const hub = { [ID]: { ...RACER, sounds: [{ name: 'theme', data: 'ID3-theme-2', served: 'tampered' }] } };
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: linked(ID, 'old code\n') }, hub });
  try {
    await openScript(window, 'Racer');
    netlog.length = 0;
    await clickUpdate(window);
    assert.deepEqual(writes(netlog), [], 'a download that does not match its sha256 is never sent to AWTRIX');
    assert.ok(lastToast(window).textContent.includes(tr(window, 'suIntegrity')), 'the mismatch is reported');
    assert.equal(store.scripts.get('Racer'), linked(ID, 'old code\n'));
  } finally { window.close(); }
}

async function noMp3Device() {
  const { window, store, netlog } = await scriptsPage({ mp3: false, scripts: { Racer: linked(ID, 'old code\n') },
    hub: { [ID]: RACER } });
  try {
    await openScript(window, 'Racer');
    netlog.length = 0;
    const confirm = await clickUpdate(window);
    assert.ok(confirm.includes(tr(window, 'sndOff')), 'the dialog says so');
    assert.deepEqual(writes(netlog), ['PUT /api/v1/apps/script-update/Racer'], 'the script alone is installed');
    assert.ok(!hubGets(netlog).some(l => l.startsWith('HUB sound')), 'no sound is downloaded');
    assert.equal(store.sounds.size, 0);
  } finally { window.close(); }
  const code = 'same code\n';
  const current = await scriptsPage({ mp3: false, scripts: { Racer: linked(ID, code) }, hub: { [ID]: { ...RACER, code } } });
  try {
    await openScript(current.window, 'Racer');
    assert.ok(panelText(current.window).includes(tr(current.window, 'suCurrent')), 'sounds a device cannot play are no update');
  } finally { current.window.close(); }
}

async function largeSoundTimeouts() {
  const hub = { [ID]: { ...RACER, sounds: [{ name: 'theme', data: 'ID3-theme-2', bytes: 1048576 }] } };
  const { window, xhr } = await scriptsPage({ scripts: { Racer: linked(ID, 'old code\n') }, hub,
    usage: { usedBytes: 1024, totalBytes: 8388608 } });
  try {
    await openScript(window, 'Racer');
    await clickUpdate(window);
    assert.deepEqual(xhr.map(x => x.timeout), [79000], 'the upload time grows with the file: 15 s plus 1 s per 16 KB');
  } finally { window.close(); }
}

// ---- a fresh install: "Install as a new script" and dependencies ------------------------------

async function freshCopy() {
  const modified = linked(ID, 'old code\n') + '# my change\n';
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: modified }, hub: { [ID]: RACER } });
  try {
    await openScript(window, 'Racer');
    netlog.length = 0;
    await clickUpdate(window);
    assert.deepEqual(writes(netlog), [
      'PUT /api/v1/apps/script-update/Racer-hub',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds boost.mp3',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds theme.mp3',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds jump.mp3',
      'PUT /api/v1/apps/script/Racer-hub'], 'script, its sounds, then the same source again for a start with them');
    assert.equal(store.scripts.get('Racer-hub'), linked(ID, RACER.code));
    assert.equal(store.scripts.get('Racer'), modified, 'the local copy stays as it was');
    assert.deepEqual(folder(store, 'Racer-hub'), ['boost.mp3=ID3-boost-1', 'jump.mp3=ID3-jump-1', 'theme.mp3=ID3-theme-2']);
    const restart = tr(window, 'sndRestart').replace('{s}', 'Racer-hub');
    assert.ok(!$$(window, '.toast').some(t => t.textContent.includes(restart)), 'a start with its sounds needs no word');
  } finally { window.close(); }
}

// The script is installed with its sounds; only the start with them did not happen.
async function freshCopyRestartFails() {
  const modified = linked(ID, 'old code\n') + '# my change\n';
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: modified }, hub: { [ID]: RACER } });
  try {
    const device = window.fetch;
    window.fetch = (url, o = {}) => o.method === 'PUT' && String(url) === '/api/v1/apps/script/Racer-hub'
      ? Promise.resolve({ ok: false, status: 507, text: async () => '{"error":{"code":"insufficientStorage","message":"not enough free memory"}}' })
      : device(url, o);
    await openScript(window, 'Racer');
    netlog.length = 0;
    await clickUpdate(window);
    const texts = $$(window, '.toast').map(t => t.textContent);
    assert.ok(texts.includes(tr(window, 'sndRestart').replace('{s}', 'Racer-hub')), 'a failed second start is said, not swallowed');
    assert.ok(!writes(netlog).some(l => l.startsWith('DELETE')), 'the installed script and its sounds stay');
    assert.equal(store.scripts.get('Racer-hub'), linked(ID, RACER.code));
    assert.deepEqual(folder(store, 'Racer-hub'), ['boost.mp3=ID3-boost-1', 'jump.mp3=ID3-jump-1', 'theme.mp3=ID3-theme-2']);
    assert.equal(texts.at(-1), tr(window, 'suDone'), 'the install itself still counts as done');
  } finally { window.close(); }
}

async function freshCopyRollback() {
  const modified = linked(ID, 'old code\n') + '# my change\n';
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: modified }, hub: { [ID]: RACER } });
  try {
    store.uploadFail = e => e.files[0].name === 'theme.mp3' ? { status: 415, body: { error: { code: 'unsupportedMediaType' } } } : null;
    await openScript(window, 'Racer');
    netlog.length = 0;
    await clickUpdate(window);
    assert.deepEqual(writes(netlog), [
      'PUT /api/v1/apps/script-update/Racer-hub',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds boost.mp3',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds theme.mp3',
      'DELETE /api/v1/apps/script/Racer-hub/sounds',
      'DELETE /api/v1/apps/Racer-hub'], 'a failed fresh install removes the script and the sounds it got');
    assert.ok(!store.scripts.has('Racer-hub') && !store.sounds.has('Racer-hub'), 'nothing is left half-installed');
    const refused = lastToast(window).textContent;
    assert.ok(refused.includes('theme.mp3') && refused.includes(tr(window, 'sndNoMp3')));
    assert.ok(toastButton(window, 'retry'));
  } finally { window.close(); }
}

const PAD_ITEM = { title: 'Pad', code: '# @module\nreturn m\n', sounds: [{ name: 'click', data: 'ID3-click' }] };

async function freshCopyWithDependencyRollback() {
  const modified = linked(ID, 'old code\n') + '# my change\n';
  const racer = { ...RACER, code: '# @requires Pad ' + PAD + '\nnew code\n' };
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: modified }, hub: { [ID]: racer, [PAD]: PAD_ITEM } });
  try {
    store.uploadFail = e => e.url.includes('/Racer-hub/') && e.files[0].name === 'jump.mp3' ? { status: 507, body: {} } : null;
    await openScript(window, 'Racer');
    netlog.length = 0;
    $(window, '.script-hub-panel button').click();
    $(window, '.toast .tacts button.pri').click();
    await flush(300);
    toastButton(window, 'reqWith').click();
    await flush(800);
    assert.deepEqual(writes(netlog), [
      'PUT /api/v1/apps/script/Pad', 'XHR POST /api/v1/apps/script/Pad/sounds click.mp3', 'PUT /api/v1/apps/script/Pad',
      'PUT /api/v1/apps/script-update/Racer-hub',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds boost.mp3',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds theme.mp3',
      'XHR POST /api/v1/apps/script/Racer-hub/sounds jump.mp3',
      'DELETE /api/v1/apps/script/Racer-hub/sounds', 'DELETE /api/v1/apps/Racer-hub',
      'DELETE /api/v1/apps/script/Pad/sounds', 'DELETE /api/v1/apps/Pad'],
    'a failed fresh install also removes the dependencies it installed, with their sounds');
    assert.ok(!store.scripts.has('Pad') && !store.sounds.has('Pad') && !store.scripts.has('Racer-hub'));
    assert.equal(store.scripts.get('Racer'), modified);
  } finally { window.close(); }
}
async function saveWithDependency(failSound) {
  const { window, store, netlog } = await scriptsPage({ hub: { [PAD]: PAD_ITEM } });
  try {
    if (failSound) store.uploadFail = () => ({ network: true });
    await goto(window, '#/scripts');
    await flush(150);
    const name = $(window, '.edtop input[type=text]'), ta = $(window, '.edwrap textarea');
    name.value = 'Snake'; name.dispatchEvent(new window.Event('input', { bubbles: true }));
    ta.value = '# @requires Pad ' + PAD + '\ndef draw() end\n'; ta.dispatchEvent(new window.Event('input', { bubbles: true }));
    $(window, '.edtop button.pri').click();
    await flush(200);
    netlog.length = 0;
    toastButton(window, 'reqWith').click();
    await flush(700);
    if (failSound) {
      assert.deepEqual(writes(netlog), ['PUT /api/v1/apps/script/Pad', 'XHR POST /api/v1/apps/script/Pad/sounds click.mp3',
        'DELETE /api/v1/apps/script/Pad/sounds', 'DELETE /api/v1/apps/Pad'],
        'a dependency whose sounds fail is removed again with them, the script is not saved');
      assert.ok(!store.scripts.has('Pad') && !store.scripts.has('Snake'));
      const failed = lastToast(window).textContent;
      assert.ok(['Pad', 'click.mp3', tr(window, 'neterr')].every(part => failed.includes(part)),
        'the failure names the dependency, the file and the reason');
    } else {
      assert.deepEqual(writes(netlog), ['PUT /api/v1/apps/script/Pad', 'XHR POST /api/v1/apps/script/Pad/sounds click.mp3',
        'PUT /api/v1/apps/script/Pad', 'PUT /api/v1/apps/script/Snake'], 'a dependency installed in the same run brings its sounds');
      assert.deepEqual(folder(store, 'Pad'), ['click.mp3=ID3-click']);
    }
  } finally { window.close(); }
}

async function dependencyDialogNoMp3() {
  const { window } = await scriptsPage({ mp3: false, hub: { [PAD]: PAD_ITEM } });
  try {
    await goto(window, '#/scripts');
    await flush(150);
    const name = $(window, '.edtop input[type=text]'), ta = $(window, '.edwrap textarea');
    name.value = 'Snake'; name.dispatchEvent(new window.Event('input', { bubbles: true }));
    ta.value = '# @requires Pad ' + PAD + '\ndef draw() end\n'; ta.dispatchEvent(new window.Event('input', { bubbles: true }));
    $(window, '.edtop button.pri').click();
    await flush(200);
    const item = lastToast(window).querySelector('li').textContent;
    assert.ok(['Pad', tr(window, 'reqHub'), tr(window, 'sndOff')].every(part => item.includes(part)),
      'the dependency is named, comes from the Hub and leaves its sounds out');
  } finally { window.close(); }
}

// ---- the Audio page -------------------------------------------------------------------------

async function audioPage(hash = '#/audio', setup = () => {}) {
  const ctx = await boot({ caps: caps() });
  const { window, store } = ctx;
  store.files['/MP3'].set('boost.mp3', 1000);
  store.scripts.set('Racer', 'code'); store.titles.Racer = 'AWTRIX GP';
  store.scripts.set('lemmix', 'code');
  putSound(store, 'Racer', 'boost.mp3', 'x'.repeat(1500));
  putSound(store, 'Racer', 'theme.mp3', 'y'.repeat(2596));
  putSound(store, 'lemmix', 'dig.mp3', 'z'.repeat(10));
  setup(store);
  stubXhr(window, ctx.xhr = [], store);
  ctx.previews = [];
  window.Audio = class { constructor(src) { ctx.previews.push(src); } play() { return Promise.resolve(); } pause() {} };
  await goto(window, hash);
  await flush(120);
  return ctx;
}
const groups = window => $$(window, '#sec-mp3 details.agrp');
const summaries = window => groups(window).map(g => g.querySelector('summary').textContent);

async function audioGroups() {
  const { window, store, netlog, previews } = await audioPage();
  try {
    assert.deepEqual($$(window, '#sec-mp3 > .rows > div > .alib > .arow').map(r => r.dataset.name), ['boost'], 'own sounds stay one list');
    assert.deepEqual(summaries(window), ['AWTRIX GP · 2 · 4.0 KB', 'lemmix · 1 · 10 B'],
      'one group per script: its title, or the install name without one, count and size');
    assert.ok(groups(window).every(g => !g.open), 'groups start collapsed');
    const racer = groups(window)[0];
    racer.open = true;
    await flush(20);
    const row = racer.querySelector('.arow[data-name="Racer/boost"]');
    row.querySelector('.device-play').click();
    await flush(60);
    assert.deepEqual(store.played.at(-1), { file: 'Racer/boost' }, 'play on AWTRIX names the script');
    row.querySelector('.preview-play').click();
    assert.equal(previews.at(-1), '/SCRIPTS/Racer/boost.mp3?v=1500.0', 'the browser preview plays the file from the script folder');
    const del = row.querySelector('button.danger');
    del.click();
    assert.ok(del.textContent.includes('AWTRIX GP'), 'the confirmation names the owner');
    assert.equal(del.getAttribute('aria-label'), del.textContent, 'and says it to a screen reader');
    netlog.length = 0;
    del.click();
    await flush(120);
    assert.ok(netlog.includes('DELETE /api/v1/apps/script/Racer/sounds/boost'), 'delete addresses the script folder');
    assert.equal(store.files['/MP3'].has('boost.mp3'), true, 'the own sound of the same name stays');
    assert.deepEqual(summaries(window), ['AWTRIX GP · 1 · 2.5 KB', 'lemmix · 1 · 10 B']);
    assert.equal(groups(window)[0].open, true, 'an open group stays open across the reload');
  } finally { window.close(); }
}

async function playingRow() {
  const { window } = await audioPage('#/audio', store => {
    store.radio.app = { playing: true, name: 'Racer/boost', error: '' };
  });
  try {
    const own = $(window, '#sec-mp3 .arow[data-name="boost"]');
    const scripted = $(window, '#sec-mp3 .arow[data-name="Racer/boost"]');
    assert.ok(scripted.classList.contains('on') && !own.classList.contains('on'), 'the row in the right group is marked');
    assert.equal(scripted.querySelector('.device-play').getAttribute('aria-pressed'), 'true', 'not by colour alone');
  } finally { window.close(); }
}

async function deepLink() {
  const { window } = await audioPage('#/audio/lemmix');
  try {
    assert.deepEqual(groups(window).map(g => g.open), [false, true], 'a link opens its group');
    assert.equal($(window, '#nav a.on').getAttribute('href'), '#/audio');
  } finally { window.close(); }
}

// Apps as /api/v1/apps lists them: two scripts with sounds, one without (with an @name), a module.
const withPlain = store => {
  store.scripts.set('Plain', 'code');
  store.scripts.set('steer', '# @module\nreturn m\n');
  store.titles.Plain = 'Plain Clock';
  store.list = () => [...store.scripts.keys()].map(name => ({ name, origin: name === 'steer' ? 'module' : 'script',
    error: null, meta: { name: store.titles[name] || '' } }));
};

async function firstSound() {
  const { window, store } = await audioPage('#/audio/Plain', withPlain);
  try {
    assert.deepEqual(summaries(window), ['AWTRIX GP · 2 · 4.0 KB', 'lemmix · 1 · 10 B', 'Plain Clock · 0 · 0 B'],
      'a linked script without sounds gets an empty group, titled with its @name');
    const plain = groups(window)[2];
    assert.ok(plain.open && plain.querySelector('.drop'), 'it is open, with an upload zone');
    const drop = file => {
      const e = new window.Event('drop', { bubbles: true, cancelable: true });
      e.dataTransfer = { files: [file] };
      groups(window)[2].querySelector('.drop').dispatchEvent(e);
    };
    drop(new window.File(['ID3-tick'], 'tick.mp3', { type: 'audio/mpeg' }));
    await flush(200);
    assert.deepEqual(folder(store, 'Plain'), ['tick.mp3=ID3-tick'], 'the first sound goes into the script\'s folder');
    assert.equal(summaries(window)[2], 'Plain Clock · 1 · 8 B', 'after the first upload it is the normal group');
    const del = groups(window)[2].querySelector('button.danger');
    del.click(); del.click();
    await flush(200);
    assert.ok(!store.sounds.has('Plain'));
    assert.equal(summaries(window)[2], 'Plain Clock · 0 · 0 B', 'the group stays while the page is open');
  } finally { window.close(); }
  for (const [hash, what] of [['#/audio/nosuch', 'a name that is no script'], ['#/audio/steer', 'a module']]) {
    const { window } = await audioPage(hash, withPlain);
    try {
      assert.deepEqual(summaries(window), ['AWTRIX GP · 2 · 4.0 KB', 'lemmix · 1 · 10 B'], what + ' shows nothing extra');
    } finally { window.close(); }
  }
}

async function groupUpload() {
  const { window, store, xhr } = await audioPage('#/audio/Racer');
  try {
    const zone = groups(window)[0].querySelector('.drop');
    assert.equal(zone.getAttribute('role'), 'button');
    assert.equal(zone.getAttribute('tabindex'), '0', 'the upload zone is reachable by keyboard');
    const drop = file => {
      const e = new window.Event('drop', { bubbles: true, cancelable: true });
      e.dataTransfer = { files: [file] };
      zone.dispatchEvent(e);
    };
    drop(new window.File(['ID3-horn'], 'horn.mp3', { type: 'audio/mpeg' }));
    await flush(150);
    assert.equal(xhr.at(-1).url, '/api/v1/apps/script/Racer/sounds', 'the upload goes into the group\'s script');
    assert.deepEqual(folder(store, 'Racer').map(f => f.split('=')[0]), ['boost.mp3', 'horn.mp3', 'theme.mp3']);
    assert.match(summaries(window)[0], /^AWTRIX GP · 3 · /, 'the group lists it right away');
    store.uploadFail = () => ({ status: 415, body: { error: { code: 'unsupportedMediaType' } } });
    drop(new window.File(['text'], 'note.mp3', { type: 'audio/mpeg' }));
    await flush(150);
    const note = lastToast(window).textContent;
    assert.ok(note.includes('note.mp3') && note.includes(tr(window, 'sndNoMp3')));
    store.uploadFail = () => ({ status: 400, body: { error: { code: 'invalidName', message: 'invalid file name' } } });
    drop(new window.File(['ID3-song'], 'My Song.mp3', { type: 'audio/mpeg' }));
    await flush(150);
    const named = lastToast(window).textContent;
    assert.ok(named.includes('My Song.mp3') && named.includes(tr(window, 'sndName')), 'a refused name says what is allowed');
    store.uploadFail = null;
    store.scripts.delete('Racer');
    drop(new window.File(['ID3-late'], 'late.mp3', { type: 'audio/mpeg' }));
    await flush(150);
    const late = lastToast(window).textContent;
    assert.ok(late.includes('late.mp3') && late.includes(tr(window, 'sndNoScript')), 'a script deleted meanwhile is named');
  } finally { window.close(); }
}

// ---- the Scripts page -----------------------------------------------------------------------

const soundLink = window => $$(window, '.edtop button.ico').find(b => /^(Sounds|Add sounds)/.test(b.getAttribute('aria-label') || '')
  && b.style.display !== 'none');
const soundTarget = (window, button) => { button.click(); return window.location.hash; };

async function scriptsLink() {
  const { window, store } = await scriptsPage({ scripts: { Racer: 'def draw() end\n', Plain: 'def draw() end\n' },
    sounds: [['Racer', 'boost.mp3', 'a'], ['Racer', 'theme.mp3', 'b'], ['steer', 'x.mp3', 'c']] });
  try {
    withPlain(store);
    await openScript(window, 'Racer');
    assert.ok(soundLink(window)?.getAttribute('aria-label').includes('2'), 'a script with sounds names how many');
    assert.equal(soundLink(window).querySelector('.cnt')?.textContent, '2', 'and shows the number on the button');
    assert.equal(soundTarget(window, soundLink(window)), '#/audio/Racer', 'it opens the script\'s group');
    window.location.hash = '#/scripts';
    await flush(50);
    await openScript(window, 'Plain');
    assert.equal(soundLink(window)?.getAttribute('aria-label'), tr(window, 'sndAdd'), 'a script without sounds offers to add some');
    assert.equal(soundLink(window).querySelector('.cnt'), null, 'without a number');
    await openScript(window, 'steer');
    assert.equal(soundLink(window), undefined, 'a module gets no link: its code plays from the app\'s folder');
  } finally { window.close(); }
  const quiet = await scriptsPage({ mp3: false, scripts: { Plain: 'def draw() end\n' } });
  try {
    await openScript(quiet.window, 'Plain');
    assert.equal(soundLink(quiet.window), undefined, 'a clock that plays no MP3s gets no link');
  } finally { quiet.window.close(); }
  // The capabilities can arrive after the script is open; the link follows them then.
  const late = await boot({ caps: null });
  try {
    late.store.caps = caps({ mp3: false });
    late.store.scripts.set('Plain', 'def draw() end\n');
    let release;
    const held = new Promise(res => { release = res; }), device = late.window.fetch;
    late.window.fetch = async (url, o) => {
      if (String(url) === '/api/v1/capabilities') await held;
      return device(url, o);
    };
    await openScript(late.window, 'Plain');
    assert.equal(soundLink(late.window)?.getAttribute('aria-label'), tr(late.window, 'sndAdd'), 'before the capabilities arrive, the button is there');
    release();
    await flush(200);
    assert.equal(soundLink(late.window), undefined, 'and it goes once they say the clock plays no MP3s');
  } finally { late.window.close(); }
}

// A rename is a new name and the old one removed; the sounds move along instead of going with it.
async function renameKeepsSounds() {
  const { window, store, netlog } = await scriptsPage({ scripts: { Racer: 'def draw() end\n' },
    sounds: [['Racer', 'boost.mp3', 'ID3-boost'], ['Racer', 'theme.mp3', 'ID3-theme']] });
  try {
    await openScript(window, 'Racer');
    const item = $$(window, '.ftitem').find(i => i.querySelector('.nm').textContent === 'Racer');
    item.querySelector('button[title="Rename"]').click();
    const input = item.querySelector('input.ftren');
    input.value = 'Rally';
    netlog.length = 0;
    input.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    await flush(400);
    assert.deepEqual(writes(netlog), ['PUT /api/v1/apps/script/Rally', 'XHR POST /api/v1/apps/script/Rally/sounds boost.mp3',
      'XHR POST /api/v1/apps/script/Rally/sounds theme.mp3', 'DELETE /api/v1/apps/script/Racer/sounds',
      'DELETE /api/v1/apps/Racer'], 'the sounds move before the old name goes');
    assert.deepEqual(folder(store, 'Rally'), ['boost.mp3=ID3-boost', 'theme.mp3=ID3-theme']);
    assert.ok(!store.scripts.has('Racer') && !store.sounds.has('Racer'));

    store.uploadFail = () => ({ status: 507, body: {} });
    await openScript(window, 'Rally');
    const name = $(window, '.edtop input[type=text]');
    name.value = 'Rally2'; name.dispatchEvent(new window.Event('input', { bubbles: true }));
    netlog.length = 0;
    $(window, '.edtop button.pri').click();
    await flush(300);
    assert.deepEqual(writes(netlog), ['PUT /api/v1/apps/script/Rally2', 'XHR POST /api/v1/apps/script/Rally2/sounds boost.mp3',
      'DELETE /api/v1/apps/script/Rally2/sounds', 'DELETE /api/v1/apps/Rally2'],
      'when the sounds cannot move, the new name is taken back and the old one stays');
    assert.ok(store.scripts.has('Rally') && !store.scripts.has('Rally2'));
    assert.deepEqual(folder(store, 'Rally'), ['boost.mp3=ID3-boost', 'theme.mp3=ID3-theme']);
    const full = lastToast(window).textContent;
    assert.ok(full.includes('boost.mp3') && full.includes(tr(window, 'sndFull')));
  } finally { window.close(); }
}

// ---- backup --------------------------------------------------------------------------------

function parseZip(buf) {
  const out = new Map();
  for (let p = 0; p + 30 <= buf.length && buf.readUInt32LE(p) === 0x04034b50;) {
    const size = buf.readUInt32LE(p + 18), nameLen = buf.readUInt16LE(p + 26), extra = buf.readUInt16LE(p + 28);
    const name = buf.slice(p + 30, p + 30 + nameLen).toString('utf8');
    out.set(name, buf.slice(p + 30 + nameLen + extra, p + 30 + nameLen + extra + size).toString('utf8'));
    p += 30 + nameLen + extra + size;
  }
  return out;
}
const blobBytes = (window, blob) => new Promise(res => {
  const fr = new window.FileReader(); fr.onload = () => res(Buffer.from(fr.result)); fr.readAsArrayBuffer(blob);
});

async function backupRoundTrip() {
  const { window, store } = await boot();
  try {
    store.scripts.set('Racer', 'def draw() end\n');
    store.files['/SCRIPTS'] = new Map([['Racer.ax', 15]]);
    putSound(store, 'Racer', 'boost.mp3', 'ID3-boost');
    putSound(store, 'Racer', 'theme.mp3', 'ID3-theme');
    const entries = await window.collectBackup({ scripts: true });
    const zip = parseZip(await blobBytes(window, window.zipStore(entries)));
    assert.deepEqual([...zip.keys()], ['manifest.json', 'SCRIPTS/Racer.ax', 'SCRIPTS/Racer/boost.mp3', 'SCRIPTS/Racer/theme.mp3'],
      'the scripts category carries each script\'s sounds, after its source');
    // What the firmware's restore does with these entries, applied to a blank device.
    const fresh = await boot();
    try {
      for (const [name, data] of zip) {
        const src = /^SCRIPTS\/([\w-]+)\.ax$/.exec(name), snd = /^SCRIPTS\/([\w-]+)\/([\w-]+\.mp3)$/.exec(name);
        if (src) fresh.store.scripts.set(src[1], data);
        else if (snd) putSound(fresh.store, snd[1], snd[2], data);
      }
      assert.equal(fresh.store.scripts.get('Racer'), 'def draw() end\n');
      assert.deepEqual(folder(fresh.store, 'Racer'), folder(store, 'Racer'), 'the sounds come back byte for byte');
      assert.deepEqual([...fresh.store.sounds.get('Racer').values()].map(v => v.sha256),
        [...store.sounds.get('Racer').values()].map(v => v.sha256));
    } finally { fresh.window.close(); }
    store.sounds.get('Racer').get('theme.mp3').data = null;
    const device = window.fetch;
    window.fetch = (url, o) => String(url).endsWith('/theme.mp3')
      ? Promise.resolve({ ok: false, status: 404, arrayBuffer: async () => new ArrayBuffer(0) }) : device(url, o);
    await assert.rejects(window.collectBackup({ scripts: true }), /theme\.mp3: HTTP 404/,
      'a file that cannot be read stops the backup instead of storing an error page');
  } finally { window.close(); }
}

// Deleting a script never takes its sounds unasked: a script with sounds asks what becomes of them.
async function deleteAsksAboutSounds() {
  const trash = (window, name) => $$(window, '.ftitem').find(i => i.querySelector('.nm').textContent === name)
    .querySelector('button.danger');
  const { window, store, netlog } = await scriptsPage({
    scripts: { Racer: 'def draw() end\n', Clock: 'def draw() end\n', Maze: 'def draw() end\n' },
    sounds: [['Racer', 'boost.mp3', 'ID3-boost'], ['Maze', 'step.mp3', 'ID3-step']] });
  try {
    await goto(window, '#/scripts');
    await flush(250);
    netlog.length = 0;
    trash(window, 'Racer').click();
    await flush(60);
    const question = lastToast(window).firstChild.textContent;
    assert.ok(question.includes('Racer') && question.includes('1'), 'the question names the script and how many sounds');
    assert.deepEqual(writes(netlog), [], 'nothing is deleted before an answer');
    toastButton(window, 'cancel').click();
    await flush(60);
    assert.ok(store.scripts.has('Racer') && store.sounds.has('Racer'), 'Cancel keeps both');

    trash(window, 'Racer').click();
    await flush(60);
    toastButton(window, 'keepSnd').click();
    await flush(200);
    assert.deepEqual(writes(netlog), ['DELETE /api/v1/apps/Racer'], 'Keep sounds deletes only the script');
    assert.deepEqual(folder(store, 'Racer'), ['boost.mp3=ID3-boost'], 'and the sounds stay on the clock');

    netlog.length = 0;
    trash(window, 'Maze').click();
    await flush(60);
    toastButton(window, 'delWithSnd').click();
    await flush(200);
    assert.deepEqual(writes(netlog), ['DELETE /api/v1/apps/script/Maze/sounds', 'DELETE /api/v1/apps/Maze'],
      'Delete with sounds takes the sounds first, then the script');
    assert.ok(!store.scripts.has('Maze') && !store.sounds.has('Maze'));

    netlog.length = 0;
    const plain = trash(window, 'Clock');
    plain.click();
    await flush(60);
    assert.deepEqual(writes(netlog), [], 'a script without sounds arms the button as before');
    plain.click();
    await flush(200);
    assert.deepEqual(writes(netlog), ['DELETE /api/v1/apps/Clock']);
  } finally { window.close(); }
}

// Sounds kept from a deleted script stay in the Audio tab, marked, until they are deleted there.
async function keptSoundsInAudio() {
  const { window, store, netlog } = await audioPage('#/audio', s => {
    s.scripts.delete('lemmix');
  });
  try {
    assert.deepEqual(summaries(window), ['AWTRIX GP · 2 · 4.0 KB', 'lemmix · script removed · 1 · 10 B'],
      'a folder without its script is listed and marked');
    const kept = groups(window)[1];
    assert.ok(!kept.querySelector('.mp3bar'), 'nothing can be uploaded to a script that is gone');
    assert.ok(groups(window)[0].querySelector('.mp3bar'), 'an installed script still takes uploads');
    kept.open = true;
    await flush(20);
    const del = kept.querySelector('.arow button.danger');
    del.click();
    netlog.length = 0;
    del.click();
    await flush(150);
    assert.ok(netlog.includes('DELETE /api/v1/apps/script/lemmix/sounds/dig'), 'its sounds are deleted one by one');
    assert.ok(!store.sounds.has('lemmix'));
    assert.deepEqual(summaries(window), ['AWTRIX GP · 2 · 4.0 KB'], 'and the group goes with the last one');
  } finally { window.close(); }
}

(async () => {
  const cases = [deleteAsksAboutSounds, keptSoundsInAudio, updateOrder, soundOnlyUpdate, spaceCheck, retryContinues, lateDeleteRetry, badDownload, noMp3Device,
    largeSoundTimeouts, freshCopy, freshCopyRestartFails, freshCopyRollback, freshCopyWithDependencyRollback,
    () => saveWithDependency(false), () => saveWithDependency(true),
    dependencyDialogNoMp3, audioGroups, playingRow, deepLink, firstSound, groupUpload, scriptsLink, renameKeepsSounds, backupRoundTrip];
  for (const c of cases) await c();
  console.log(`script-sounds: ${cases.length} scenarios passed (delete asks about sounds, kept sounds in Audio, update order, sound-only update, space check, retry, late delete retry, ` +
    'bad download, no MP3, timeouts, fresh copy, restart note, rollback, rollback with dependencies, dependency sounds, dependency rollback, ' +
    'dependency dialog, Audio groups, playing row, deep link, first sound, group upload, Scripts link, rename, backup round trip)');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(restoreTimers);
