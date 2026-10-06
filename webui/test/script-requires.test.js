/* Script dependencies (# @requires): header parsing, what counts as installed,
   recursive Hub resolution, and the confirm-then-install flows in the editor.
   Run: node script-requires.test.js */
const { boot, goto, flush, stubXhr } = require('./harness');
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');

const hash = s => createHash('sha256').update(s).digest('hex');
const plain = v => JSON.parse(JSON.stringify(v));
const PAD = 'PadPadPad123', GAMEPAD = 'GamePad12345', DEMO = 'ZGAjqmr8hFKN';
const linked = (id, code) => `# @hub ${id} ${hash(code)}\n${code}`;
const HUB = {
  [PAD]: { title: 'Pad Controller', code: '# @module\n# @icons padicon\n# @requires Gamepad GamePad12345\nreturn m\n' },
  [GAMEPAD]: { title: 'Gamepad', code: '# @module\n# @requires pad PadPadPad123\n# @requires Snake\nreturn m\n' },
};
const SNAKE = '# @name Snake\n# @requires pad PadPadPad123\n# @requires Gamepad GamePad12345\n# @requires Foo\n# @requires Util\ndef draw() end\n';

function hubMock(window, hub, opts = {}) {
  const device = window.fetch, log = [];
  window.fetch = async (url, o = {}) => {
    const m = /^https:\/\/awtrix\.de\/api\/v1\/scripts\/([A-Za-z0-9]{12})\/(release|source)$/.exec(String(url));
    if (!m) {
      if (opts.failPut && o.method === 'PUT' && String(url) === '/api/v1/apps/script/' + opts.failPut)
        return new Response(JSON.stringify({ error: { message: 'storage full' } }), { status: 507 });
      return device(url, o);
    }
    log.push(m[2] + ' ' + m[1]);
    assert.equal(o.credentials, 'omit');
    const item = hub[m[1]];
    if (!item) return new Response('', { status: 404 });
    if (m[2] === 'release') {
      assert.equal(o.headers.Authorization, undefined, 'release metadata stays public');
      return new Response(JSON.stringify({ id: m[1], name: item.title, revision: 1, sha256: item.sha || hash(item.code), notes: '' }));
    }
    if (opts.status) return new Response('', { status: opts.status });
    assert.equal(o.headers.Authorization, 'Bearer req-token');
    return new Response(item.code);
  };
  return log;
}

function parserCases(window) {
  const req = src => plain(window.scriptRequires(src));
  assert.deepEqual(req('# @requires pad\n# @requires Gamepad AbC123xyz456\nx'),
    [{ name: 'pad' }, { name: 'Gamepad', hub: 'AbC123xyz456' }]);
  assert.deepEqual(req('\r\n#@REQUIRES\tpad\r\n#   @Requires  b-c_1   AbC123xyz456 extra tokens\r\n'),
    [{ name: 'pad' }, { name: 'b-c_1', hub: 'AbC123xyz456' }], 'tag case, tabs, CRLF, extra tokens');
  assert.deepEqual(req('# @name X\n\n# @requires a\ncode()\n# @requires b'), [{ name: 'a' }], 'header ends at the first code line');
  assert.deepEqual(req('x = 1\n# @requires a'), [], 'no header, no requirements');
  assert.deepEqual(req([
    '# @requires', '# @requiresx a', '# @requires bad.name', '# @requires ' + 'a'.repeat(33),
    '# @requires ok AbC123xyz45', '# @requires ok AbC123xyz4567', '# @requires ok AbC_23xyz456', '# @requires ok'].join('\n')),
  [{ name: 'ok' }], 'invalid names or hub ids drop the whole line');
  assert.deepEqual(req('# @requires a AbC123xyz456\n# @requires a\n# @requires a Zzz123xyz456'),
    [{ name: 'a', hub: 'AbC123xyz456' }], 'the first line of a name wins');
  assert.deepEqual(req('# @requires dup bad!\n# @requires dup'), [{ name: 'dup' }], 'a dropped line does not claim the name');
  const many = Array.from({ length: 10 }, (_, i) => '# @requires d' + i).join('\n');
  assert.deepEqual(req('# @requires d0\n' + many).map(r => r.name), ['d0', 'd1', 'd2', 'd3', 'd4', 'd5', 'd6', 'd7'],
    'at most 8, duplicates do not count');
}

function haveCases(window) {
  const have = window.scriptRequiresHave([
    { name: 'pad', origin: 'script' },
    { name: 'util-mod', origin: 'module', import: 'Util' },
    { name: 'plain-mod', origin: 'module' },
    { name: 'Time', origin: 'builtin' },
    { name: 'weather', origin: 'pushed' },
  ]);
  assert.deepEqual([...have].sort(), ['Util', 'pad', 'plain-mod'], 'scripts by name, modules by import; builtin/pushed never count');
}

async function resolveCases(window) {
  window.localStorage.awtrixHubToken = 'req-token';
  const hub = { ...HUB,
    Lvl1aaaaaaaa: { title: 'L1', code: '# @requires L2 Lvl2aaaaaaaa\n' },
    Lvl2aaaaaaaa: { title: 'L2', code: '# @requires L3 Lvl3aaaaaaaa\n' },
    Lvl3aaaaaaaa: { title: 'L3', code: '# @requires L4 Lvl4aaaaaaaa\n' },
    Lvl4aaaaaaaa: { title: 'L4', code: '# @requires L5 Lvl5aaaaaaaa\n' },
    Lvl5aaaaaaaa: { title: 'L5', code: '' },
    Badsha123456: { title: 'Bad', code: 'x', sha: 'a'.repeat(64) },
    Rootcycle123: { title: 'R', code: '# @requires Snake\n# @requires R Rootcycle123\n' },
  };
  const log = hubMock(window, hub);
  let plan = plain(await window.resolveScriptRequires(window.scriptRequires(SNAKE), new Set(['Snake', 'Util']), new Set()));
  assert.deepEqual(plan.deps.map(d => d.name), ['Gamepad', 'pad'], 'deepest first; cycles to pad and to the root stop');
  assert.deepEqual(plan.left, ['Foo'], 'no hub id cannot be installed');
  assert.equal(plan.deps[1].code, HUB[PAD].code);
  plan = plain(await window.resolveScriptRequires([{ name: 'R', hub: 'Rootcycle123' }], new Set(['Snake']), new Set()));
  assert.deepEqual(plan, { deps: [{ name: 'R', hub: 'Rootcycle123', code: hub.Rootcycle123.code }], left: [] },
    'a cycle back to the root or to itself stops');
  plan = plain(await window.resolveScriptRequires([{ name: 'L1', hub: 'Lvl1aaaaaaaa' }], new Set(), new Set()));
  assert.deepEqual(plan.deps.map(d => d.name), ['L4', 'L3', 'L2', 'L1'], 'depth limit 4');
  assert.ok(!log.some(l => l.includes('Lvl5')), 'nothing beyond depth 4 is fetched');
  plan = plain(await window.resolveScriptRequires([{ name: 'gone', hub: 'Gone12345678' }, { name: 'pad', hub: PAD }], new Set(['Snake']), new Set()));
  assert.deepEqual([plan.deps.map(d => d.name), plan.left], [['Gamepad', 'pad'], ['gone']],
    'an id the Hub no longer has is left to the user without stopping the rest');
  await assert.rejects(window.resolveScriptRequires([{ name: 'bad', hub: 'Badsha123456' }], new Set(), new Set()),
    e => /^bad: /.test(e.message) && e.message.includes('changed during download'));
  await assert.rejects(window.resolveScriptRequires([{ name: 'Time', hub: PAD }], new Set(), new Set(['Time'])),
    e => e.message === 'Time: A script with that name already exists');
}

async function editor({ apps = [], token = true, ...mock } = {}) {
  const ctx = await boot();
  const { window, store } = ctx;
  window.AbortSignal = AbortSignal;
  if (token) window.localStorage.awtrixHubToken = 'req-token';
  else window.localStorage.removeItem('awtrixHubToken');
  store.iconBytes.padicon = 'GIF89a-padicon';
  stubXhr(window, [], store);
  store.list = () => [...store.scripts.keys()].map(name => ({ name, origin: 'script', error: null })).concat(apps);
  ctx.hub = hubMock(window, HUB, mock);
  await goto(window, '#/scripts');
  await flush(100);
  return ctx;
}
const $ = (window, s) => window.document.querySelector(s);
const toasts = window => [...window.document.querySelectorAll('.toast')];
const tr = (window, key) => window.eval('t')(key);
const button = (window, key) => [...window.document.querySelectorAll('.toast .tacts button')].find(b => b.textContent === tr(window, key));
const says = (text, ...parts) => parts.every(part => text.includes(part));
const puts = netlog => netlog.filter(l => l.startsWith('PUT /api/v1/apps/script/')).map(l => l.replace('PUT /api/v1/apps/script/', ''));

async function saveSnake(window, src = SNAKE) {
  const name = $(window, '.edtop input[type=text]'), ta = $(window, '.edwrap textarea');
  name.value = 'Snake'; name.dispatchEvent(new window.Event('input', { bubbles: true }));
  ta.value = src; ta.dispatchEvent(new window.Event('input', { bubbles: true }));
  $(window, '.edtop button.pri').click();
  await flush(150);
}
const UTIL = { name: 'util-mod', origin: 'module', import: 'Util', error: null };

async function withDependencies() {
  const { window, store, netlog, hub } = await editor({ apps: [UTIL] });
  try {
    await saveSnake(window);
    const ask = toasts(window).at(-1);
    const rows = [...ask.querySelectorAll('li')].map(li => li.textContent);
    assert.equal(rows.length, 3, 'lists each missing dependency; a module import satisfies Util');
    assert.ok(says(rows[0], 'pad', 'Pad Controller', tr(window, 'reqHub')), rows[0]);
    assert.ok(says(rows[1], 'Gamepad', tr(window, 'reqHub')), rows[1]);
    assert.ok(says(rows[2], 'Foo', tr(window, 'reqSelf')), rows[2]);
    assert.deepEqual([...ask.querySelectorAll('.tacts button')].map(b => b.textContent),
      ['reqWith', 'reqOnly', 'cancel'].map(key => tr(window, key)));
    assert.deepEqual(puts(netlog), [], 'nothing is installed before the choice');
    button(window, 'reqWith').click();
    await flush(400);
    assert.deepEqual(puts(netlog), ['Gamepad', 'pad', 'Snake'], 'dependencies first, deepest first');
    assert.equal(store.scripts.get('pad'), linked(PAD, HUB[PAD].code), 'installed under its requirement name with the Hub link');
    assert.equal(store.scripts.get('Gamepad'), linked(GAMEPAD, HUB[GAMEPAD].code));
    assert.equal(store.scripts.get('Snake'), SNAKE);
    assert.ok(store.files['/ICONS'].has('padicon.gif'), 'dependency icons are installed');
    assert.ok(hub.includes('source ' + PAD) && hub.includes('source ' + GAMEPAD));
    assert.ok(says(toasts(window).at(-1).textContent, 'Snake', 'Gamepad', 'pad', tr(window, 'reqLeft'), 'Foo'));
  } finally { window.close(); }
}

async function onlyThisScript() {
  const { window, store, netlog, hub } = await editor({ apps: [UTIL] });
  try {
    await saveSnake(window);
    button(window, 'reqOnly').click();
    await flush(150);
    assert.deepEqual(puts(netlog), ['Snake']);
    assert.ok(!hub.some(l => l.startsWith('source')), 'no dependency source is downloaded');
    assert.equal(toasts(window).at(-1).textContent, tr(window, 'saved'));
    await saveSnake(window, SNAKE + '# edit\n');
    assert.equal(button(window, 'reqOnly'), undefined, 'the same answer is not asked again');
    assert.deepEqual(puts(netlog), ['Snake', 'Snake']);
    assert.equal(store.scripts.get('Snake'), SNAKE + '# edit\n');
  } finally { window.close(); }
}

async function cancelAndHubless() {
  const { window, netlog } = await editor({ apps: [UTIL] });
  try {
    await saveSnake(window, '# @requires Foo\n# @requires Util\nx\n');
    assert.deepEqual([...toasts(window).at(-1).querySelectorAll('.tacts button')].map(b => b.textContent),
      ['reqOnly', 'cancel'].map(key => tr(window, key)), 'no Hub id offers no dependency install');
    button(window, 'cancel').click();
    await flush(80);
    assert.deepEqual(puts(netlog), [], 'cancel installs nothing');
    assert.equal($(window, '.edtop button.pri').disabled, false);
    await saveSnake(window, '# @requires Util\nx\n');
    assert.deepEqual(puts(netlog), ['Snake'], 'satisfied requirements install without asking');
    assert.equal(button(window, 'cancel'), undefined);
  } finally { window.close(); }
}

async function authFailure(opts) {
  const { window, store, netlog } = await editor(opts);
  try {
    await saveSnake(window);
    button(window, 'reqWith').click();
    await flush(200);
    assert.deepEqual(puts(netlog), [], 'nothing is installed without Hub access');
    assert.ok(!store.scripts.has('Snake'));
    assert.ok(button(window, 'hubConnect'), 'points to the Hub connection');
  } finally { window.close(); }
}

async function dependencyFails() {
  const { window, store, netlog } = await editor({ failPut: 'pad' });
  try {
    await saveSnake(window, '# @requires pad PadPadPad123\nx\n');
    button(window, 'reqWith').click();
    await flush(400);
    assert.deepEqual(puts(netlog), ['Gamepad'], 'stops at the failing dependency');
    assert.ok(store.scripts.has('Gamepad') && !store.scripts.has('pad') && !store.scripts.has('Snake'));
    assert.ok(says(toasts(window).at(-1).textContent, 'pad', 'storage full'), 'the failure names the dependency and the reason');
  } finally { window.close(); }
}

async function missingNotice() {
  const { window, store, netlog } = await editor();
  try {
    store.scripts.set('Snake', SNAKE);
    store.list = () => [...store.scripts.keys()].map(name => ({ name, origin: 'script', error: null,
      meta: name !== 'Snake' ? { requires: [] } : { requires: [
        { name: 'pad', hub: PAD, missing: !store.scripts.has('pad') }, { name: 'Foo', missing: true }] } }));
    await goto(window, '#/');
    await goto(window, '#/scripts');
    await flush(100);
    [...window.document.querySelectorAll('.ftitem')].find(i => i.textContent.includes('Snake')).click();
    await flush(100);
    const note = $(window, '.req-note');
    assert.equal(note.hidden, false);
    assert.ok(says(note.querySelector('span').textContent, tr(window, 'reqMissing'), 'pad', 'Foo'));
    note.querySelector('button').click();
    await flush(400);
    assert.deepEqual(puts(netlog), ['Gamepad', 'pad'], 'installs what the Hub has, never the open script');
    assert.ok(says(toasts(window).at(-1).textContent, 'Gamepad', 'pad', tr(window, 'reqLeft'), 'Foo'));
    const left = note.querySelector('span').textContent;
    assert.ok(says(left, 'Foo') && !left.includes('pad'), 'only what is still missing is listed');
    assert.equal(note.querySelector('button'), null, 'no install action without a Hub id');
  } finally { window.close(); }
}

async function hubUpdateAddsRequirement() {
  const next = '# @requires pad PadPadPad123\nnew source\n';
  const { window, store, netlog } = await editor();
  try {
    store.caps.scriptUpdates = true;
    store.scripts.set('Demo', linked(DEMO, 'old source'));
    HUB[DEMO] = { title: 'Demo', code: next };
    const device = window.fetch, writes = [];
    window.fetch = async (url, o = {}) => {
      if (String(url).startsWith('/api/v1/apps/script-update/')) {
        writes.push(puts(netlog).length);
        store.scripts.set(url.split('/').pop(), JSON.parse(o.body).source);
        return new Response('{"ok":true}');
      }
      return device(url, o);
    };
    await goto(window, '#/');
    await goto(window, '#/scripts');
    await flush(150);
    [...window.document.querySelectorAll('.ftitem')].find(i => i.textContent.includes('Demo')).click();
    await flush();
    $(window, '.script-hub-panel button').click();
    $(window, '.toast .tacts button.pri').click();
    await flush(200);
    assert.equal(writes.length, 0, 'the update waits for the dependency answer');
    button(window, 'reqWith').click();
    await flush(400);
    assert.deepEqual(puts(netlog), ['Gamepad', 'pad']);
    assert.deepEqual(writes, [2], 'the update is written after its dependencies');
    assert.equal(store.scripts.get('Demo'), linked(DEMO, next));
    assert.ok(says(toasts(window).at(-1).textContent, 'Demo', 'Gamepad', 'pad', tr(window, 'reqLeft'), 'Snake'),
      'a nested requirement without a Hub id is reported');
  } finally { delete HUB[DEMO]; window.close(); }
}

(async () => {
  const { window } = await boot();
  try {
    window.AbortSignal = AbortSignal;
    parserCases(window);
    haveCases(window);
    await resolveCases(window);
  } finally { window.close(); }
  await withDependencies();
  await onlyThisScript();
  await cancelAndHubless();
  await authFailure({ status: 401 });
  await authFailure({ token: false });
  await dependencyFails();
  await missingNotice();
  await hubUpdateAddsRequirement();
  console.log('script-requires: parser, inventory and resolution cases and 8 workflows passed (with dependencies, only this script, cancel, 401, no key, failed dependency, missing notice, Hub update)');
})().catch(error => { console.error(error); process.exitCode = 1; });
