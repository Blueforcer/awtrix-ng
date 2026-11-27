/* Script host: the System section finds and pairs a host, script calls of a paired clock go to the
   host with the key the clock keeps, script sounds stay on the clock, moves in both directions keep
   every script on at least one side, and a clock without the route (TC002) shows no section.
   Run: node script-host.test.js */
const { boot, goto, flush, stubXhr, putSound, useFakeTimers } = require('./harness');
const restoreTimers = useFakeTimers();

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  FAIL: ' + msg); }
}

const KEY = 'a'.repeat(64), NEWKEY = 'b'.repeat(64);
const HOST = 'http://10.0.0.5:8301', HOSTD = 'http://10.0.0.5:8300';
const OFFER = { name: 'pi', address: '10.0.0.5', port: 8300 };
const PAIRED = { hostname: 'awtrix-ng', scriptHost: '10.0.0.5:8301', scriptHostKey: KEY };
const REMOTE = { mode: 'remote', host: '10.0.0.5:8301', link: 'up', offers: [] };
const tr = (window, key) => window.eval('t')(key);
const toasts = window => [...window.document.querySelectorAll('.toast')];
function toastLog(window) {
  const seen = [];
  new window.MutationObserver(list => list.forEach(m => m.addedNodes.forEach(n => seen.push(n.textContent))))
    .observe(window.document.querySelector('#toasts'), { childList: true });
  return seen;
}
const toastButton = (window, label) => [...window.document.querySelectorAll('.toast .tacts button')].find(b => b.textContent === label);
const buttonIn = (node, label) => [...node.querySelectorAll('button')].find(b => b.textContent === label);
const inputIn = (window, node, label) => [...node.querySelectorAll('input')].find(i => i.getAttribute('aria-label') === tr(window, label));
const notFound = [{ error: { code: 'notFound', message: 'not found' } }, 404];

// awtrix-host: the supervisor on :8300 pairs and forgets clocks, the clock's own process on :8301
// takes the key the supervisor handed out.
const AT_A = { host: HOST, hostd: HOSTD, scriptHost: '10.0.0.5:8301' };
const AT_B = { host: 'http://10.0.0.7:8301', hostd: 'http://10.0.0.7:8300', scriptHost: '10.0.0.7:8301' };
function makeHost({ key = KEY, pairKey = key, at = AT_A } = {}) {
  const HOST = at.host, HOSTD = at.hostd;
  const h = { scripts: new Map(), modules: new Set(), sounds: new Map(), data: {}, values: {}, fields: {}, meta: {},
    calls: [], uploads: [], forgot: [], pairs: [], restored: null, fail: null };
  h.answer = (method, url, opts) => {
    const headers = opts.headers || {};
    if (url.startsWith(HOSTD)) {
      const path = url.slice(HOSTD.length);
      if (method === 'POST' && path === '/api/v1/pair') {
        h.pairs.push(JSON.parse(opts.body));
        return [{ url: HOST, scriptHost: at.scriptHost, key: pairKey, port: 8301 }, 200];
      }
      const forget = path.match(/^\/api\/v1\/pair\/(\w+)\?keep=([01])$/);
      if (method === 'DELETE' && forget) {
        h.forgot.push({ uid: forget[1], keep: forget[2], auth: headers.Authorization });
        return [{ ok: true }, 200];
      }
      return null;
    }
    if (!url.startsWith(HOST)) return null;
    const full = url.slice(HOST.length), path = full.split('?')[0];
    h.calls.push({ method, path: full, headers });
    if (headers.Authorization !== 'Bearer ' + key)
      return [{ error: { code: 'unauthorized', message: 'authentication required' } }, 401];
    const failed = h.fail && h.fail(method, path);
    if (failed) return failed;
    if (path === '/api/v1/version') return [{ version: '1.2.3' }, 200];
    if (path === '/api/v1/apps')
      return [[...h.scripts.keys()].map(name => ({ name, origin: h.modules.has(name) ? 'module' : 'script', meta: h.meta[name] })), 200];
    if (/^\/api\/v1\/(oauth|secrets)\//.test(path)) return [{ ok: true }, 200];
    if (path === '/api/v1/restore' && method === 'POST') {
      h.restored = opts.files[0].file;
      return [{ ok: true, warnings: ['host note'] }, 200];
    }
    if (path === '/api/v1/files')
      return [{ files: [...h.scripts.keys()].map(n => ({ name: n + '.ax', size: h.scripts.get(n).length })) }, 200];
    if (path === '/api/v1/audio/mp3')
      return [{ files: [], scripts: [...h.sounds].map(([name, f]) => ({ name, files: [...f.keys()].map(n => ({ name: n, size: 4 })) })) }, 200];
    let m = path.match(/^\/SCRIPTS\/([^/]+)\.ax$/);
    if (m) return h.scripts.has(m[1]) ? [h.scripts.get(m[1]), 200] : notFound;
    if ((m = path.match(/^\/SCRIPTS\/([^/]+)\/([^/]+)$/))) {
      const data = h.sounds.get(m[1])?.get(m[2]);
      return data ? [data, 200] : notFound;
    }
    if ((m = path.match(/^\/api\/v1\/apps\/script\/([^/]+)\/sounds$/))) {
      const name = decodeURIComponent(m[1]);
      if (!h.scripts.has(name)) return notFound;
      if (method === 'POST') {
        if (!h.sounds.has(name)) h.sounds.set(name, new Map());
        for (const f of opts.files) { h.sounds.get(name).set(f.name, 'snd'); h.uploads.push(name + '/' + f.name); }
        return [{ ok: true }, 200];
      }
      if (method === 'DELETE') { h.sounds.delete(name); return [{ ok: true }, 200]; }
      return [{ files: [...(h.sounds.get(name) || new Map()).keys()].map(n => ({ name: n, size: 4 })),
        usedBytes: 0, totalBytes: 1 << 30 }, 200];
    }
    if ((m = path.match(/^\/api\/v1\/apps\/script\/([^/]+)$/))) {
      const name = decodeURIComponent(m[1]);
      if (method === 'PUT') { h.scripts.set(name, opts.body); return [{ ok: true }, 200]; }
      return h.scripts.has(name) ? [h.scripts.get(name), 200] : notFound;
    }
    if ((m = path.match(/^\/api\/v1\/apps\/([^/]+)\/(config|data)$/))) {
      const name = decodeURIComponent(m[1]), into = m[2] === 'data' ? h.data : h.values;
      if (!h.scripts.has(name)) return notFound;
      if (method === 'PATCH') { Object.assign(into[name] ||= {}, JSON.parse(opts.body)); return [{ ok: true }, 200]; }
      if (m[2] === 'data') return [{ ...(h.data[name] || {}) }, 200];
      return [{ name, warnings: [],
        fields: (h.fields[name] || []).map(f => ({ ...f, value: h.values[name]?.[f.key] ?? f.default })) }, 200];
    }
    if ((m = path.match(/^\/api\/v1\/apps\/([^/]+)$/)) && method === 'DELETE') {
      h.scripts.delete(decodeURIComponent(m[1]));
      return [{ ok: true }, 200];
    }
    return notFound;
  };
  return h;
}

// A clock with scripts of its own: A with a setting, saved data and a sound, and the module M.
function clockScripts(store) {
  store.scripts.set('A', 'src A');
  store.scripts.set('M', 'src M');
  store.list = () => [...store.scripts.keys()].map(name => ({ name, origin: name === 'M' ? 'module' : 'script', error: null }));
  store.configs.A = { fields: [{ key: 'speed', type: 'int', default: 1, value: 5 }] };
  store.data.A = { best: 42 };
  putSound(store, 'A', 'jump.mp3', 'JUMP');
}

async function pairPage(opts = {}) {
  const host = makeHost(opts.host);
  host.fail = opts.hostFail || null;
  const ctx = await boot({ external: host.answer, fail: opts.fail,
    scripthost: { mode: 'off', host: '', link: 'off', offers: [OFFER] },
    device: { uid: 'a1b2c3', boardType: 'awtrixng', ipAddress: '192.168.1.39' } });
  clockScripts(ctx.store);
  host.fields.A = [{ key: 'speed', type: 'int', default: 1 }];
  ctx.window.AbortSignal = AbortSignal;
  stubXhr(ctx.window, ctx.uploads = [], ctx.store);
  await goto(ctx.window, '#/system');
  await flush(80);
  const section = ctx.window.document.querySelector('#sec-scripthost');
  inputIn(ctx.window, section, 'shCode').value = '123456';
  ctx.store.netlog.length = 0;
  buttonIn(section, tr(ctx.window, 'shPair')).click();
  await flush(600);
  return { ...ctx, host, section };
}

async function testNoRouteNoSection() {
  const { window } = await boot({});
  await goto(window, '#/system');
  await flush(80);
  assert(!window.document.querySelector('#sec-scripthost'), 'a clock without the route shows no script host');
  window.close();
}

async function testNothingBeforeFirstRender() {
  const host = makeHost();
  const { window, netlog } = await boot({ system: PAIRED, scripthost: REMOTE, external: host.answer });
  assert(!netlog.some(l => l.includes('/api/v1/scripthost') || l.includes('secrets=1')),
    'the dashboard renders without asking for the script host');
  await goto(window, '#/apps');
  await flush(80);
  assert(netlog.filter(l => l === 'GET /api/v1/scripthost?secrets=1').length === 1, 'the Apps tab reads the host and key once');
  assert(!netlog.some(l => l.startsWith('GET /api/v1/system')), 'without the other secrets of the clock');
  assert(netlog.includes('GET ' + HOST + '/api/v1/apps'), 'and lists the host scripts');
  assert(!netlog.includes('GET /api/v1/scripthost'), 'the link state is not needed there');
  await goto(window, '#/system');
  await flush(80);
  assert(netlog.includes('GET /api/v1/scripthost'), 'the System page asks for the link state');
  assert(window.document.querySelector('#sec-scripthost'), 'and shows the section');
  window.close();
}

async function testScriptCallsGoToTheHost() {
  const calls = [];
  const external = (method, url, opts) => {
    calls.push({ method, url, headers: opts.headers || {} });
    return [{ ok: true }, 200];
  };
  const { window, netlog } = await boot({ external, system: PAIRED });
  await window.eval("api('/api/v1/apps/script/Foo')");
  await window.eval("api('/api/v1/apps/Foo/config')");
  await window.eval("api('/api/v1/apps/Foo/data')");
  await window.eval("api('/api/v1/apps', {host: 1})");
  await window.eval("api('/api/v1/apps/builtin/Time/config')").catch(() => {});
  await window.eval("api('/api/v1/apps')").catch(() => {});
  await window.eval("oauthSend('Foo', 'POST', {clientId: 'x'})");
  await window.eval("secretSend('Foo', 'PUT', {token: 'y'})");
  await window.eval("api('/api/v1/apps/script/Foo/sounds')").catch(() => {});
  await window.eval("api('/api/v1/apps/script/Foo/sounds/jump', {method: 'DELETE'})").catch(() => {});
  for (const path of ['/SCRIPTS/Foo/jump.mp3', '/SCRIPTS/Foo.ax'])
    calls.push({ url: (await window.eval('shTo')(path))[0], headers: { Authorization: 'Bearer ' + KEY } });
  const urls = calls.map(c => c.url);
  assert(urls.includes(HOST + '/api/v1/apps/script/Foo'), 'a script source comes from the host');
  assert(urls.includes(HOST + '/api/v1/apps/Foo/config'), 'so do its settings');
  assert(urls.includes(HOST + '/api/v1/apps/Foo/data'), 'and its data');
  assert(urls.includes(HOST + '/api/v1/apps'), 'the script list is asked of the host on purpose');
  assert(calls.every(c => c.headers.Authorization === 'Bearer ' + KEY), 'always with the key the clock keeps');
  assert(netlog.includes('GET /api/v1/apps/builtin/Time/config'), 'a built-in app stays on the clock');
  assert(netlog.includes('GET /api/v1/apps'), 'and so does the rotation');
  assert(netlog.includes('GET /api/v1/apps/script/Foo/sounds') && netlog.includes('DELETE /api/v1/apps/script/Foo/sounds/jump'),
    'script sounds are listed and deleted on the clock');
  assert(urls.includes('/SCRIPTS/Foo/jump.mp3') && urls.includes(HOST + '/SCRIPTS/Foo.ax'),
    'a sound file is read from the clock, a source from the host');
  const own = calls.filter(c => /\/(oauth|secrets)\//.test(c.url));
  assert(own.length === 2 && own.every(c => Object.keys(c.headers).sort().join() === 'Authorization,Content-Type'),
    'sign-in and API-key writes carry only the key and the content type, no marker header');
  assert(!Object.keys(window.localStorage).some(k => k.startsWith('awtrixSh')), 'the browser keeps no key');
  window.close();
}

async function testUnpairedClockAsksForNoSecrets() {
  for (const scripthost of [{ mode: 'off', host: '', link: 'off', offers: [] }, undefined]) {
    const urls = [];
    const { window, netlog } = await boot({ system: { hostname: 'awtrix-ng', scriptHostKey: KEY }, scripthost,
      external: (method, url) => { urls.push(url); return ['', 200]; } });
    await goto(window, '#/apps');
    await flush(80);
    await window.eval("api('/api/v1/apps/script/Foo')").catch(() => {});
    await window.eval("api('/api/v1/apps/Foo/config')").catch(() => {});
    const kind = scripthost ? 'an unpaired clock' : 'a clock without a script host (TC002)';
    assert(!netlog.some(l => l.startsWith('GET /api/v1/system')), kind + ' is not asked for its secrets');
    assert(netlog.filter(l => l.startsWith('GET /api/v1/scripthost')).length === 1, kind + ' is asked once whether it is paired');
    assert(!urls.length && netlog.includes('GET /api/v1/apps/script/Foo'), kind + ' keeps script calls');
    assert(await window.eval('SH.key') === '', kind + ' hands out no key, not even an old one');
    window.close();
  }
}

async function testDefaultPort() {
  const urls = [];
  const { window } = await boot({ system: { scriptHost: 'pi.local', scriptHostKey: KEY },
    external: (method, url) => { urls.push(url); return ['', 200]; } });
  await window.eval("api('/api/v1/apps/script/Foo')");
  assert(urls[0] === 'http://pi.local:8301/api/v1/apps/script/Foo', 'a host without a port is reached on 8301');
  window.close();
}

async function testScriptsOffOnPairedClock() {
  const host = makeHost();
  const { window } = await boot({ system: PAIRED, external: host.answer, device: { scriptingRunning: false } });
  await window.eval('shReady()');
  assert(await window.eval('scriptsOff()') === true, 'a paired clock that runs no scripts says so');
  window.close();
}

async function testPairMovesEverything() {
  const { window, store, host, uploads } = await pairPage();
  assert(host.pairs.length === 1 && host.pairs[0].code === '123456' && host.pairs[0].uid === 'a1b2c3' &&
    host.pairs[0].board === 'awtrixng', 'the pairing carries code, uid and board');
  assert(host.pairs[0].width === 32 && host.pairs[0].height === 8, 'and the panel size');
  assert(host.pairs[0].clock === 'http://192.168.1.39', 'and the address the clock itself reports');
  assert(host.scripts.get('A') === 'src A' && host.scripts.get('M') === 'src M', 'both scripts reach the host');
  const puts = host.calls.filter(c => c.method === 'PUT').map(c => c.path);
  assert(puts[0] === '/api/v1/apps/script/M', 'the module goes first');
  assert(!host.uploads.length && !uploads.length && !host.calls.some(c => c.path.includes('sounds')),
    'no sound goes to the host');
  assert(host.values.A && host.values.A.speed === 5, 'the setting goes along');
  assert(host.data.A && host.data.A.best === 42 && !('speed' in host.data.A), 'and the saved data, without the setting');
  assert(!store.scripts.has('A') && !store.scripts.has('M'), 'the clock gives its copies up');
  assert(store.sounds.get('A')?.has('jump.mp3') && !store.netlog.some(l => l.includes('/sounds')),
    'the sound stays on the clock, which plays it');
  const order = store.netlog.filter(l => /^(DELETE|PUT|POST) \/api\/v1\/(apps|system|device)/.test(l));
  assert(order.indexOf('PUT /api/v1/system') < order.indexOf('DELETE /api/v1/apps/A') &&
    order.indexOf('DELETE /api/v1/apps/M') < order.indexOf('POST /api/v1/device/reboot'),
    'the clock is paired, then its copies go, then it restarts');
  assert(order.indexOf('DELETE /api/v1/apps/A') < order.indexOf('DELETE /api/v1/apps/M'), 'a module goes after the scripts that import it');
  assert(store.systemPut && store.systemPut.scriptHost === '10.0.0.5:8301' && store.systemPut.scriptHostKey === KEY,
    'the clock is told its host and key');
  assert(store.reboots === 1 && !host.forgot.length, 'and restarts');
  assert(host.calls.every(c => !Object.keys(c.headers).some(k => /^X-Awtrix/i.test(k))), 'no marker header goes to the host');
  window.close();
}

async function testFailedCopyKeepsTheClock() {
  const { window, store, host } = await pairPage({
    hostFail: (method, path) => method === 'PATCH' && path === '/api/v1/apps/A/config'
      ? [{ error: { code: 'insufficientStorage', message: 'disk full' } }, 507] : null });
  assert(store.scripts.get('A') === 'src A' && store.scripts.get('M') === 'src M' && store.sounds.has('A'),
    'a setting that cannot be written leaves every script and sound on the clock');
  assert(!store.netlog.some(l => l.startsWith('DELETE /')), 'nothing is deleted on the clock');
  assert(!host.scripts.has('A') && host.scripts.has('M'), 'the half copy on the host is removed');
  assert(host.forgot.length === 1 && host.forgot[0].keep === '1' && host.forgot[0].auth === 'Bearer ' + KEY,
    'the host forgets the clock but keeps what it had');
  assert(!store.systemPut && !store.reboots, 'the clock stays unpaired');
  assert(toasts(window).some(t => t.textContent.includes('disk full')), 'the error says why');
  window.close();
}

async function testFailedDeleteKeepsBothCopies() {
  const { window, store, host } = await pairPage({
    fail: (method, path) => method === 'DELETE' && path === '/api/v1/apps/A'
      ? [{ error: { code: 'internalError', message: 'flash busy' } }, 500] : null });
  assert(store.scripts.get('A') === 'src A' && host.scripts.get('A') === 'src A', 'a delete that fails keeps both copies');
  assert(!store.scripts.has('M'), 'the other copies still go');
  assert(!host.forgot.length && store.systemPut && store.systemPut.scriptHostKey === KEY && store.reboots === 1,
    'the pairing completes');
  window.close();
}

async function testFailedPairingWriteKeepsTheHost() {
  const { window, store, host } = await pairPage({
    fail: (method, path) => method === 'PUT' && path === '/api/v1/system'
      ? [{ error: { code: 'validationFailed', message: 'invalid scriptHost' } }, 422] : null });
  assert(host.scripts.get('A') === 'src A' && host.scripts.get('M') === 'src M', 'the host keeps the copies');
  assert(host.forgot.length === 1 && host.forgot[0].keep === '1', 'and is only told to forget the key');
  assert(store.scripts.get('A') === 'src A' && store.scripts.get('M') === 'src M' &&
    !store.netlog.some(l => l.startsWith('DELETE /')), 'the clock keeps its scripts');
  assert(!store.reboots, 'the clock does not restart');
  window.close();
}

async function remotePage(host, opts = {}) {
  const ctx = await boot({ system: PAIRED, scripthost: opts.scripthost || REMOTE, external: opts.external || host.answer,
    fail: opts.fail,
    device: { uid: 'a1b2c3', boardType: 'awtrixng', ipAddress: '192.168.1.39' } });
  ctx.window.AbortSignal = AbortSignal;
  stubXhr(ctx.window, ctx.uploads = [], ctx.store);
  await goto(ctx.window, '#/system');
  await flush(80);
  ctx.section = ctx.window.document.querySelector('#sec-scripthost');
  return ctx;
}

function hostScripts(host) {
  host.scripts.set('A', 'src A');
  host.scripts.set('M', 'src M');
  host.modules.add('M');
  host.fields.A = [{ key: 'speed', type: 'int', default: 1 }];
  host.values.A = { speed: 7 };
  host.data.A = { best: 9 };
  host.sounds.set('A', new Map([['jump.mp3', 'JUMP']]));
}

async function unpair(window, section, choice) {
  const button = buttonIn(section, tr(window, 'shUnpair'));
  button.click();
  button.click();
  await flush(20);
  toastButton(window, tr(window, choice)).click();
  await flush(9000);
}

async function testUnpairBringsScriptsBack() {
  const host = makeHost();
  hostScripts(host);
  let busy = 1;
  const { window, store, section } = await remotePage(host, {
    fail: (method, path) => method === 'PUT' && path === '/api/v1/apps/script/M' && busy-- > 0
      ? [{ error: { code: 'serviceBusy', message: 'busy' } }, 503] : null });
  store.configs.A = { fields: [{ key: 'speed', type: 'int', default: 1, value: 1 }] };
  putSound(store, 'A', 'boost.mp3', 'BOOST');
  assert(section.querySelector('.badge.good'), 'the link shows as connected');
  assert(buttonIn(section, tr(window, 'shMoveTo')) && !buttonIn(section, tr(window, 'shRepair')),
    'a host that knows the clock needs no new pairing, only offers a move');
  store.netlog.length = 0;
  const said = toastLog(window);
  await unpair(window, section, 'shBack');
  assert(store.systemPut && store.systemPut.scriptHost === '' && store.reboots === 1, 'the clock is unpaired and restarts');
  assert(store.scripts.get('A') === 'src A' && store.scripts.get('M') === 'src M', 'both scripts come back');
  assert(store.netlog.filter(l => l === 'PUT /api/v1/apps/script/M').length === 2, 'a busy clock is asked again');
  assert(store.sounds.get('A')?.has('boost.mp3') && !store.sounds.get('A').has('jump.mp3') &&
    !store.netlog.some(l => l.includes('/sounds')) && !host.calls.some(c => c.path.includes('sounds') || c.path.includes('.mp3')),
    'the sounds on the clock stay as they are');
  assert(store.configs.A.fields[0].value === 7 && store.data.A && store.data.A.best === 9, 'and the setting and data');
  const order = store.netlog.filter(l => /^PUT \/api\/v1\/(system|apps\/script\/)/.test(l));
  assert(order[0] === 'PUT /api/v1/system', 'scripts come back only after the clock left the host');
  assert(host.forgot.length === 1 && host.forgot[0].keep === '0' && host.forgot[0].auth === 'Bearer ' + KEY,
    'the host forgets the clock and its copies');
  assert(said.includes(tr(window, 'shBackDone')), 'and says so');
  window.close();
}

async function testUnpairNamesWhatStayed() {
  const host = makeHost();
  hostScripts(host);
  const { window, store, section } = await remotePage(host, {
    fail: (method, path) => method === 'PUT' && path === '/api/v1/apps/script/A'
      ? [{ error: { code: 'insufficientStorage', message: 'flash full' } }, 507] : null });
  await unpair(window, section, 'shBack');
  assert(!store.scripts.has('A') && store.scripts.get('M') === 'src M', 'what fits comes back');
  assert(host.scripts.has('A') && host.forgot.length === 1 && host.forgot[0].keep === '1', 'the rest stays on the host');
  const note = toasts(window).find(t => t.textContent.includes(tr(window, 'shLeft').replace('{s}', '')));
  assert(note && note.textContent.includes('A (flash full)'), 'the note names the script and why');
  await flush(10000);
  assert(note.isConnected && buttonIn(note, tr(window, 'close')), 'and stays until it is closed');
  window.close();
}

async function testUnpairLeavingScripts() {
  const host = makeHost();
  hostScripts(host);
  const { window, store, section } = await remotePage(host);
  await unpair(window, section, 'shLeave');
  assert(store.systemPut && store.systemPut.scriptHost === '', 'the clock is unpaired');
  assert(!store.scripts.size && host.scripts.size === 2, 'the scripts stay on the host');
  assert(host.forgot.length === 1 && host.forgot[0].keep === '1' && host.forgot[0].auth === 'Bearer ' + KEY,
    'and the host stops its process for the clock');
  window.close();
}

const OFFER_B = { name: 'mac', address: '10.0.0.7', port: 8300 };

async function movePage({ down = false, bFail = null, fail = null } = {}) {
  const a = makeHost(), b = makeHost({ key: NEWKEY, at: AT_B });
  hostScripts(a);
  b.fields.A = [{ key: 'speed', type: 'int', default: 1 }];
  b.fail = bFail;
  const external = (method, url, opts) => {
    if (down && url.startsWith('http://10.0.0.5')) throw new TypeError('Failed to fetch');
    return a.answer(method, url, opts) || b.answer(method, url, opts);
  };
  const ctx = await remotePage(a, { external, fail, scripthost: { ...REMOTE, offers: [OFFER, OFFER_B] } });
  return { ...ctx, a, b };
}

async function move(window, section, { take = true } = {}) {
  const sw = [...section.querySelectorAll('.switch input')][0];
  if (!take) sw.checked = false;
  inputIn(window, section, 'shCode').value = '777777';
  window.document.querySelector('#toasts').replaceChildren();
  buttonIn(section, tr(window, 'shMoveTo')).click();
  await flush(600);
}

async function testMoveTakesTheScriptsAlong() {
  const { window, store, section, a, b } = await movePage();
  const found = [...section.querySelectorAll('button')].map(x => x.textContent);
  assert(found.includes('mac') && !found.includes('pi'), 'the other host is offered, the current one is not');
  assert(inputIn(window, section, 'shAddr').value === '10.0.0.7:8300', 'and filled in');
  const sw = [...section.querySelectorAll('.switch input')];
  assert(sw.length === 1 && sw[0].checked, 'taking the scripts along is on by default');
  store.netlog.length = 0;
  await move(window, section);
  assert(b.pairs.length === 1 && b.pairs[0].code === '777777' && b.pairs[0].uid === 'a1b2c3', 'the clock pairs with B');
  assert(b.scripts.get('A') === 'src A' && b.scripts.get('M') === 'src M', 'both scripts reach B');
  assert(b.calls.filter(c => c.method === 'PUT').map(c => c.path)[0] === '/api/v1/apps/script/M', 'the module first');
  assert(b.values.A && b.values.A.speed === 7, 'the setting goes along');
  assert(b.data.A && b.data.A.best === 9 && !('speed' in b.data.A), 'and the saved data');
  assert(b.calls.every(c => c.headers.Authorization === 'Bearer ' + NEWKEY), "B is called with B's key");
  const log = store.netlog;
  const readA = log.findIndex(l => l === 'GET ' + HOST + '/api/v1/apps/A/data');
  assert(readA >= 0 && readA < log.indexOf('POST ' + AT_B.hostd + '/api/v1/pair'), 'A is read before B is paired');
  assert(store.systemPut && store.systemPut.scriptHost === AT_B.scriptHost && store.systemPut.scriptHostKey === NEWKEY,
    "the clock takes B's address and key");
  const forget = log.indexOf('DELETE ' + HOSTD + '/api/v1/pair/a1b2c3?keep=0');
  assert(forget > log.indexOf('PUT /api/v1/system') && forget < log.indexOf('POST /api/v1/device/reboot'),
    'A is told to forget after the clock moved, before it restarts');
  assert(a.forgot.length === 1 && a.forgot[0].keep === '0' && a.forgot[0].auth === 'Bearer ' + KEY,
    "A forgets the clock and its copies, with A's key");
  assert(!b.forgot.length && store.reboots === 1, 'B keeps the clock, which restarts');
  assert(!store.scripts.size && !log.some(l => /^(PUT|DELETE) \/api\/v1\/apps\//.test(l)) &&
    !b.calls.some(c => c.path.includes('sounds')), 'nothing lands on the clock, the sounds stay there');
  window.close();
}

async function testMoveLeavingTheScripts() {
  const { window, store, section, a, b } = await movePage();
  await move(window, section, { take: false });
  assert(b.pairs.length === 1 && !b.scripts.size, 'B starts without scripts');
  assert(!a.calls.some(c => c.path.startsWith('/api/v1/apps/')), 'A is not read');
  assert(a.forgot.length === 1 && a.forgot[0].keep === '1', 'A keeps them but stops running them');
  assert(store.systemPut && store.systemPut.scriptHostKey === NEWKEY && store.reboots === 1, 'the clock moves to B');
  window.close();
}

async function testMoveWhileTheOldHostIsGone() {
  for (const choice of ['cancel', 'shWithout']) {
    const { window, store, section, a, b } = await movePage({ down: true });
    await move(window, section);
    const ask = toasts(window).find(x => x.textContent.includes(tr(window, 'shGone')));
    assert(ask && !b.pairs.length && !store.systemPut, 'an old host that does not answer is named before anything changes');
    toastButton(window, tr(window, choice)).click();
    await flush(600);
    if (choice === 'cancel') {
      assert(!b.pairs.length && !store.systemPut && !store.reboots, 'cancel leaves the clock where it is');
      assert(!buttonIn(section, tr(window, 'shMoveTo')).disabled, 'and the move can be tried again');
    } else {
      assert(b.pairs.length === 1 && !b.scripts.size, 'without them, the clock pairs with B alone');
      assert(store.systemPut && store.systemPut.scriptHostKey === NEWKEY && store.reboots === 1, 'and moves there');
      assert(store.netlog.includes('DELETE ' + HOSTD + '/api/v1/pair/a1b2c3?keep=1') && !a.forgot.length,
        'A is asked to keep the scripts, which it cannot hear');
    }
    window.close();
  }
}

async function testFailedMoveStaysWithTheOldHost() {
  const { window, store, section, a, b } = await movePage({
    bFail: (method, path) => method === 'PATCH' && path === '/api/v1/apps/A/config'
      ? [{ error: { code: 'insufficientStorage', message: 'disk full' } }, 507] : null });
  await move(window, section);
  assert(!b.scripts.has('A') && b.forgot.length === 1 && b.forgot[0].keep === '1' &&
    b.forgot[0].auth === 'Bearer ' + NEWKEY, 'B drops the half copy and forgets the clock');
  assert(!store.systemPut && !store.reboots, 'the clock stays with A');
  assert(!a.forgot.length && a.scripts.size === 2, 'A keeps the clock and every script');
  assert(toasts(window).some(x => x.textContent.includes('disk full')), 'the error says why');
  window.close();
}

async function testUnknownKeyOffersPairAgain() {
  const host = makeHost({ key: NEWKEY, pairKey: NEWKEY });
  hostScripts(host);
  const { window, store, section } = await remotePage(host);
  const badge = [...section.querySelectorAll('.badge.bad')].find(b => b.textContent === tr(window, 'shAuth'));
  assert(badge, 'a host that refuses the key says it does not know the clock');
  const code = inputIn(window, section, 'shCode'), addr = inputIn(window, section, 'shAddr');
  assert(code && addr && addr.value === '10.0.0.5:8300', 'and offers to pair again at the host');
  try {
    Object.defineProperty(window, 'localStorage', { configurable: true, get() { throw new Error('SecurityError'); } });
  } catch (e) {}
  code.value = '654321';
  store.netlog.length = 0;
  buttonIn(section, tr(window, 'shRepair')).click();
  await flush(600);
  assert(host.pairs.length === 1 && host.pairs[0].code === '654321', 'pairing again works without browser storage');
  assert(store.systemPut && store.systemPut.scriptHostKey === NEWKEY && store.reboots === 1, 'the clock takes the new key');
  assert(!host.calls.some(c => c.method === 'PUT') && !store.netlog.some(l => l.startsWith('DELETE')),
    'scripts stay where they are');
  window.close();
}

async function testUnknownKeyInAppsTab() {
  const host = makeHost({ key: NEWKEY });
  const { window } = await boot({ system: PAIRED, external: host.answer });
  await goto(window, '#/apps');
  await flush(80);
  const again = toastButton(window, tr(window, 'shRepair'));
  assert(again && toasts(window).some(t => t.textContent.includes(tr(window, 'shAuth'))), 'the Apps tab offers to pair again');
  again.click();
  assert(window.location.hash === '#/system', 'which leads to the System page');
  window.close();
}

async function testNeedsFromTheClock() {
  const host = makeHost();
  host.scripts.set('A', 'src A');
  host.meta.A = { needs: [{ name: 'audio.mp3', missing: true }, { name: 'microphone', missing: false }], display: null };
  const { window, store } = await boot({ system: PAIRED, external: host.answer });
  store.apps = [{ name: 'A', origin: 'script', enabled: true, inLoop: true, slot: 0 }];
  await goto(window, '#/apps');
  await flush(80);
  const chip = [...window.document.querySelectorAll('.chip.warn')].find(b => b.textContent === tr(window, 'chipFit'));
  chip?.click();
  await flush(20);
  const info = window.document.querySelector('.rowinfo.warn')?.textContent || '';
  assert(info.includes(tr(window, 'capMic')), 'a need the clock lacks shows');
  assert(!info.includes(tr(window, 'capMp3')), 'a need only the host lacks does not');
  window.close();
}

async function testBackupReadsTheHost() {
  const host = makeHost();
  hostScripts(host);
  const { window, netlog, store } = await boot({ system: PAIRED, external: host.answer });
  putSound(store, 'A', 'boost.mp3', 'BOOST');
  const entries = await window.eval("collectBackup({scripts: true}).then(e => e.map(x => [x.name, typeof x.data === 'string' ? x.data : String.fromCharCode(...x.data)]))");
  const names = entries.map(e => e[0]);
  assert(names.includes('SCRIPTS/A.ax') && names.includes('SCRIPTS/M.ax'), 'a backup holds the scripts of the host');
  assert(entries.some(e => e[0] === 'SCRIPTS/A/boost.mp3' && e[1] === 'BOOST') && !names.includes('SCRIPTS/A/jump.mp3'),
    'and the sounds of the clock');
  assert(!netlog.some(l => l.startsWith('GET /api/v1/files')), 'the clock is not asked for the sources');
  assert(netlog.includes('GET /api/v1/audio/mp3') && netlog.includes('GET /SCRIPTS/A/boost.mp3'), 'but for the sounds');
  assert(host.calls.some(c => c.path === '/api/v1/files?dir=%2FSCRIPTS') &&
    !host.calls.some(c => c.path === '/api/v1/audio/mp3'), 'the host lists only the sources');
  window.close();
}

async function testSoundUploadGoesStraightToTheClock() {
  const host = makeHost();
  const { window, store } = await boot({ system: PAIRED, external: host.answer });
  const uploads = [];
  stubXhr(window, uploads, store);
  store.apps = [{ name: 'N', origin: 'script', enabled: true, inLoop: true }];
  let done = false, error = null;
  window.eval('uploadFile')(new window.Blob(['NEW']), '/api/v1/apps/script/N/sounds', 'jump.mp3').then(() => { done = true; });
  await flush(40);
  assert(done && uploads.length === 1 && uploads[0].url === '/api/v1/apps/script/N/sounds' &&
    store.sounds.get('N')?.has('jump.mp3') && !host.calls.length, 'a sound goes to the clock at once');
  assert(!store.netlog.some(l => l.startsWith('GET /api/v1/apps/script/N/sounds')), 'without asking first');
  store.apps = [];
  window.eval('uploadFile')(new window.Blob(['X']), '/api/v1/apps/script/Gone/sounds', 'x.mp3').catch(e => { error = e; });
  await flush(40);
  assert(error && error.message === tr(window, 'sndNoScript') && uploads.length === 2,
    'a refusal of the clock is shown at once');
  window.close();
}

async function settle(promise) {
  let out;
  promise.then(v => { out = { v }; }, e => { out = { e }; });
  for (let i = 0; !out && i < 200; i++) await flush(20);
  if (!out || out.e) throw out ? out.e : new Error('never settled');
  return out.v;
}

async function testRestoreSplitsTheBackup() {
  const host = makeHost();
  const { window, store } = await boot({ system: PAIRED, external: host.answer });
  const uploads = [];
  stubXhr(window, uploads, store);
  const names = (w, file) => settle(w.eval('f => f.arrayBuffer().then(b => unzipStore(b).map(e => e.name).join())')(file));
  const zip = w => w.eval(`zipStore([{name: 'manifest.json', data: '{}'}, {name: 'config/settings.json', data: '{}'},
    {name: 'SCRIPTS/A.ax', data: 'src A'}, {name: 'SCRIPTS/A/jump.mp3', data: 'JUMP'}])`);
  const result = await settle(window.eval('restoreBackup')(zip(window), () => {}));
  const clock = uploads.find(u => u.url === '/api/v1/restore');
  assert(clock && await names(window, clock.files[0].file) === 'manifest.json,config/settings.json,SCRIPTS/A/jump.mp3',
    'the clock gets all but the script sources');
  assert(host.restored && await names(window, host.restored) === 'manifest.json,SCRIPTS/A.ax',
    'the host gets the sources');
  assert(uploads.find(u => u.url.startsWith(HOST)).headers.Authorization === 'Bearer ' + KEY, 'with the key');
  assert(result.warnings.includes('host note'), "the host's notes are shown too");
  window.close();
  const plain = await boot({});
  const kept = [];
  stubXhr(plain.window, kept, plain.store);
  await settle(plain.window.eval('restoreBackup')(zip(plain.window), () => {}));
  assert(kept.length === 1 && await names(plain.window, kept[0].files[0].file) ===
    'manifest.json,config/settings.json,SCRIPTS/A.ax,SCRIPTS/A/jump.mp3', 'an unpaired clock gets the whole backup');
  plain.window.close();
}

(async () => {
  const cases = [testNoRouteNoSection, testNothingBeforeFirstRender, testScriptCallsGoToTheHost, testDefaultPort,
    testScriptsOffOnPairedClock, testPairMovesEverything, testFailedCopyKeepsTheClock, testFailedDeleteKeepsBothCopies,
    testFailedPairingWriteKeepsTheHost, testUnpairBringsScriptsBack, testUnpairNamesWhatStayed, testUnpairLeavingScripts,
    testMoveTakesTheScriptsAlong, testMoveLeavingTheScripts, testMoveWhileTheOldHostIsGone, testFailedMoveStaysWithTheOldHost,
    testUnknownKeyOffersPairAgain, testUnknownKeyInAppsTab, testNeedsFromTheClock, testBackupReadsTheHost,
    testSoundUploadGoesStraightToTheClock, testRestoreSplitsTheBackup, testUnpairedClockAsksForNoSecrets];
  for (const c of cases) await c();
  console.log(`script-host: ${pass} passed, ${fail} failed`);
  process.exitCode = fail ? 1 : 0;
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(restoreTimers);
