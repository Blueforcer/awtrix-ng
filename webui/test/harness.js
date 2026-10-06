/* Boots the real webui/index.html inside jsdom so the editor's client-side
   state machine can be driven from Node. The device API is either an in-memory
   mock (default, offline) or a running AWTRIX NG, such as awtrix-linux on :8080 (--live).

   The shared assembler refreshes index.html from webui/src before it is read. */
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');
const { createHash } = require('node:crypto');
const { execFileSync } = require('node:child_process');

const HTML_PATH = path.join(__dirname, '..', 'index.html');
execFileSync(process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3'),
  [path.join(__dirname, '../../scripts/webui_source.py')], { stdio: 'inherit' });
let timers;

function useFakeTimers() {
  const { mock } = require('node:test');
  mock.timers.enable({ apis: ['setTimeout', 'setInterval', 'Date'], now: Date.now() });
  timers = mock.timers;
  return () => { timers.reset(); timers = undefined; };
}

const withPageGlobals = html => html
  .replace(/('use strict';\r?\n)\(\(\)=>\{\r?\n/, '$1')
  .replace(/\r?\n\}\)\(\);(\r?\n<\/script>)/, '$1');
const loadHtml = () => withPageGlobals(fs.readFileSync(HTML_PATH, 'utf8'));

function makeVirtualConsole() {
  const vc = new VirtualConsole();
  vc.on('jsdomError', e => {
    // Surface genuine script errors; ignore jsdom "Not implemented" noise.
    if (!/Not implemented/.test(e.message)) console.error('[jsdomError]', e.message);
  });
  return vc;
}

// Shared beforeParse: give the page the globals jsdom omits.
function installGlobals(fetchImpl, canvasContext) {
  return window => {
    if (timers) window.Date = Date;
    window.fetch = fetchImpl(window);
    window.TextEncoder = TextEncoder;
    window.TextDecoder = TextDecoder;
    window.AbortController = AbortController;
    window.scrollTo = () => {};
    window.matchMedia = () => ({ matches: false, addEventListener() {}, removeEventListener() {} });
    // jsdom has no layout, so it ships no ResizeObserver. Without a stub the UI
    // throws on construction and every run prints a jsdomError that is not one.
    window.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
    if (canvasContext) window.HTMLCanvasElement.prototype.getContext = () => canvasContext;
  };
}

// Runs timers and lets fetch, FileReader and promise continuations complete between ticks.
async function flush(ms = 40) {
  if (!timers) return new Promise(resolve => setTimeout(resolve, ms));
  for (let elapsed = 0; elapsed < ms; elapsed += 5) {
    timers.tick(Math.min(5, ms - elapsed));
    await new Promise(resolve => setImmediate(resolve));
  }
}

// ---- in-memory backend ----------------------------------------------------
function makeStore() {
  const scripts = new Map(); // name -> source
  return {
    scripts,
    // Set `apps` to serve a hand-written inventory instead of one derived from
    // the installed scripts; `order` holds the last PUT /api/v1/apps/order body and `active`
    // the last PUT /api/v1/apps/active body.
    apps: null,
    order: null,
    active: null,
    // name -> {fields, warnings}, what GET /api/v1/apps/{name}/config answers; builtinConfigs
    // the same for /api/v1/apps/builtin/{name}/config. `configPatch` holds the last PATCH body so a
    // test can see what was sent; a `configFail` message makes the next PATCH answer 422.
    configs: {},
    builtinConfigs: {},
    configPatch: null,
    configFail: null,
    // name -> object, what GET /api/v1/apps/{name}/data answers; `dataPatch` holds the last PATCH.
    data: {},
    dataPatch: null,
    // Last PATCH /api/v1/settings body; the store itself is updated with it too.
    settingsPatch: null,
    system: { hostname: 'awtrix-ng' },
    screen: null,
    // What GET /api/v1/capabilities answers; override before goto() to test gating.
    caps: { transitions: [],
            audio: { mp3: true, rtttl: true, song: false, speech: false, track: false, radio: true,
                     url: false, effect: false, clip: false } },
    settings: { volume: 60, radioVolume: 80, appVolume: 100, alertVolume: 100 },
    // What GET /api/v1/display answers, for the display settings page.
    display: { power: true, overlay: null, overlaySettings: { speed: 1 } },
    // dir -> Map(name -> size), the file API's flash view.
    files: { '/ICONS': new Map(), '/MP3': new Map() },
    // A script's own sounds, /SCRIPTS/<script>/<sound>.mp3: script -> Map(file -> {size, sha256, data}).
    // titles holds each script's @name for GET /api/v1/audio/mp3; usage is what the listings report.
    sounds: new Map(),
    titles: {},
    usage: { usedBytes: 1024, totalBytes: 1048576 },
    // Answer for an upload before it is stored: a function (entry) -> {status, body} or null.
    uploadFail: null,
    melodies: [], // [{name, rtttl, valid, notes, durationMs, bytes}]
    played: [],   // bodies POSTed to /api/v1/audio/play, except stations
    audioStops: 0,
    stopBodies: [], // bodies POSTed to /api/v1/audio/stop
    // What GET /api/v1/audio answers: one status per group, and the station list.
    radio: { radio: { playing: false, station: '', title: '', error: '' },
             app: { playing: false, name: '', error: '' },
             alert: { playing: false, name: '', error: '' }, stations: [] },
    radioPlay: null,   // last POST /api/v1/audio/play carrying a station
    stationsPut: null, // last PUT /api/v1/audio/stations body
    device: { ipAddress: '192.168.1.5', version: '1.1.1', soc: 'esp32',
              updateImage: 'firmware-awtrix-ng.bin' },
    githubLatest: null,
    // The icon database lives outside the device: the browser talks to it
    // directly, so it is mocked by absolute URL rather than by path.
    iconDb: { v: 1, icons: [] }, // what index.json answers
    iconBytes: {},               // slug -> bytes served from icons/<slug>.<ext>
    localIconBytes: {},          // actual on-device content, independent of names
    iconOrigins: new Map(),      // durable firmware metadata, survives page navigation
    originFailure: false,
    iconExt: {},                 // slug -> 'gif' (default) or 'jpg'; the Hub serves only that one
    submitted: [],               // FormData bodies POSTed to the submit service
    submittedHeaders: [],        // the headers each of those carried
    submitReply: null,           // override what the submit service answers
    submitCode: 0,               // HTTP status for a rejected submission (default 409)
    list() {
      if (this.apps) return this.apps;
      return [...scripts.keys()].map(name => ({ name, origin: 'script', error: null }));
    },
  };
}

// Must track ICONDB_URL_DEFAULT in index.html: the gallery builds absolute URLs
// against it, so a stale prefix here mocks nothing and every icon 404s.
const ICON_DB = 'https://awtrix.de/icons/';

function mockFetch(store, netlog, win) {
  const resp = (body, ok = true, status = 200) => ({
    ok, status,
    text: async () => (typeof body === 'string' ? body : JSON.stringify(body)),
  });
  // The gallery reads .json() and .blob(), which the device mock never needs.
  // The blob has to be the page's own Blob, or FormData.append refuses it.
  const ext = (body, ok = true, status = 200) => ({
    ok, status,
    json: async () => body,
    blob: async () => new win.Blob([String(body == null ? '' : body)], { type: 'image/gif' }),
    arrayBuffer: async () => new TextEncoder().encode(String(body == null ? '' : body)).buffer,
    text: async () => JSON.stringify(body),
  });
  return async function fetch(input, opts = {}) {
    const url = typeof input === 'string' ? input : input.url;
    const method = (opts.method || 'GET').toUpperCase();

    if (/^https?:\/\//.test(url) && !url.startsWith('http://localhost')) {
      netlog.push(method + ' ' + url);
      if (url.startsWith('https://api.github.com/'))
        return ext(store.githubLatest || { message: 'Not Found' }, !!store.githubLatest, store.githubLatest ? 200 : 404);
      if (url.endsWith('/submit') && method === 'POST') {
        store.submitted.push(opts.body);
        store.submittedHeaders.push(opts.headers || {});
        const reply = store.submitReply
          || { ok: true, status: 'published', slug: 'demo', pr: 'https://example.invalid/icons/demo' };
        // submitCode carries the HTTP status; the body's own `status` field is
        // the Hub's word for the icon ("published"), not a code.
        const code = reply.ok === false ? (store.submitCode || 409) : 200;
        return ext(reply, reply.ok !== false, code);
      }
      if (url.startsWith(ICON_DB)) {
        const rest = url.slice(ICON_DB.length);
        if (rest === 'index.json') return ext(store.iconDb);
        const meta = rest.match(/^([^/]+)\/metadata\.json$/);
        if (meta) {
          const slug = decodeURIComponent(meta[1]);
          if (!(slug in store.iconBytes)) return ext({}, false, 404);
          return ext({slug,filename:slug+'.'+(store.iconExt[slug]||'gif'),sha256:createHash('sha256').update(store.iconBytes[slug]).digest('hex')});
        }
        // The Hub serves the bytes directly under the catalogue prefix - no
        // second 'icons/' segment. This pattern is what pins that.
        const icon = rest.match(/^([^/]+)\.(gif|jpg)$/);
        if (icon) {
          store.iconDownloadRequests ||= [];
          store.iconDownloadRequests.push({url, options:opts});
          if (!opts.headers?.Authorization || (store.requiredIconToken && opts.headers.Authorization !== 'Bearer '+store.requiredIconToken)) return ext({error:'authenticationRequired'},false,401);
          const slug = decodeURIComponent(icon[1]);
          if (!(slug in store.iconBytes)) return ext({ error: 'notFound' }, false, 404);
          if ((store.iconExt[slug] || 'gif') !== icon[2])
            return ext({ error: 'notFound' }, false, 404);
          return ext(store.iconBytes[slug]);
        }
      }
      return ext({ error: 'notFound' }, false, 404);
    }

    const u = new URL(url, 'http://localhost');
    const p = u.pathname;
    const q = u.searchParams;
    netlog.push(method + ' ' + p + (u.search || ''));

    if (store.oauth && (p === '/api/v1/oauth' || p.startsWith('/api/v1/oauth/'))) {
      store.oauthCalls.push({ method, path: p, headers: opts.headers || {}, body: opts.body });
      const [answer, status] = store.oauth(method, p, opts.body ? JSON.parse(opts.body) : null);
      return resp(answer, status < 400, status);
    }
    if (p === '/api/v1/device') return resp(store.device);
    if (p === '/api/v1/capabilities')
      return store.caps ? resp(store.caps) : resp({ error: { message: 'offline' } }, false, 503);
    if (p === '/api/v1/system') return resp(store.system);
    if (p === '/api/v1/display/screen' && store.screen) return resp(store.screen);
    if (p === '/api/v1/display' && method === 'GET') return resp(store.display);
    if (p === '/api/v1/settings' && method === 'GET') return resp(store.settings);
    if (p === '/api/v1/settings' && method === 'PATCH') {
      store.settingsPatch = JSON.parse(opts.body || '{}');
      Object.assign(store.settings, store.settingsPatch);
      return resp({ ok: true });
    }
    if (p === '/api/v1/scripts/shared') return resp([]);
    if (p.startsWith('/api/v1/notifications')) {
      (store.notifications ||= []).push({ method, path: p, body: opts.body ? JSON.parse(opts.body) : null });
      return resp({ ok: true });
    }
    if (p === '/api/v1/icons/rename' && method === 'POST') {
      const {from,to}=JSON.parse(opts.body),icons=store.files['/ICONS'],stem=to.replace(/\.[^.]+$/,'');
      if (!icons.has(from)) return resp({error:{code:'notFound',message:'icon not found'}},false,404);
      if (icons.has(stem+'.gif')||icons.has(stem+'.jpg')) return resp({error:{code:'nameTaken',message:'name taken',field:'to'}},false,409);
      icons.set(to,icons.get(from));icons.delete(from);
      if (from in store.localIconBytes) {store.localIconBytes[to]=store.localIconBytes[from];delete store.localIconBytes[from];}
      const origin=store.iconOrigins.get(from);
      if (origin) {store.iconOrigins.delete(from);store.iconOrigins.set(to,{...origin,name:to});}
      return resp({ok:true});
    }
    if (p === '/api/v1/icons/origins') {
      if (store.originFailure) return resp({error:{message:'storage full'}},false,507);
      if (method === 'PUT') {const o=JSON.parse(opts.body);store.iconOrigins.set(o.name,o);return resp({ok:true});}
      if (method === 'DELETE') {store.iconOrigins.delete(q.get('name'));return resp({ok:true});}
      return resp({icons:[...store.iconOrigins.values()]});
    }

    if (p === '/api/v1/files') {
      if (method === 'GET') {
        const dir = q.get('dir') || '/ICONS';
        const files = [...(store.files[dir] || new Map())].map(([name, size]) => ({ name, size }));
        return resp({ files, usedBytes: 1000, totalBytes: 8388608 });
      }
      if (method === 'DELETE') {
        const full = q.get('path') || '';
        const slash = full.lastIndexOf('/');
        const dir = full.slice(0, slash), name = full.slice(slash + 1);
        if (!store.files[dir] || !store.files[dir].delete(name))
          return resp({ error: { code: 'notFound', message: full } }, false, 404);
        if (dir === '/ICONS') {store.iconOrigins.delete(name);delete store.localIconBytes[name];}
        return resp({ ok: true });
      }
    }

    // Static asset served off the device, read back when an icon is submitted.
    if (p.startsWith('/ICONS/')) {
      const name = decodeURIComponent(p.slice(7));
      if (!store.files['/ICONS'].has(name)) return ext({ error: 'notFound' }, false, 404);
      const slug=name.replace(/\.[^.]+$/,'');
      return ext(store.localIconBytes[name] ?? store.iconBytes[slug] ?? ('GIF89a-' + name));
    }

    // A script's source and sounds as /SCRIPTS serves them: what a backup reads back, and what a
    // browser preview plays.
    const scriptFile = p.match(/^\/SCRIPTS\/([^/]+)\/([^/]+)$/);
    if (scriptFile) {
      const hit = store.sounds.get(decodeURIComponent(scriptFile[1]))?.get(decodeURIComponent(scriptFile[2]));
      return hit ? ext(hit.data) : ext({ error: 'notFound' }, false, 404);
    }
    const scriptSource = p.match(/^\/SCRIPTS\/([^/]+)\.ax$/);
    if (scriptSource) {
      const src = store.scripts.get(decodeURIComponent(scriptSource[1]));
      return src == null ? ext({ error: 'notFound' }, false, 404) : ext(src);
    }
    const scriptSounds = p.match(/^\/api\/v1\/apps\/script\/([^/]+)\/sounds(?:\/([^/]+))?$/);
    if (scriptSounds) {
      const name = decodeURIComponent(scriptSounds[1]);
      const folder = store.sounds.get(name) || new Map();
      // As the device: sounds kept from a deleted script stay reachable until they are deleted.
      if (!isScript(store, name) && !folder.size && !(scriptSounds[2] && method === 'DELETE'))
        return resp({ error: { code: 'notFound', message: 'no such script' } }, false, 404);
      if (!scriptSounds[2] && method === 'DELETE') {
        store.sounds.delete(name);
        return resp({ ok: true });
      }
      if (scriptSounds[2] && method === 'DELETE') {
        const file = decodeURIComponent(scriptSounds[2]) + '.mp3';
        if (!folder.delete(file)) return resp({ error: { code: 'notFound', message: 'no such MP3' } }, false, 404);
        if (!folder.size) store.sounds.delete(name);
        return resp({ ok: true });
      }
      if (!scriptSounds[2] && method === 'GET')
        return resp({ files: [...folder].map(([file, v]) => ({ name: file, size: v.size, sha256: v.sha256 })), ...store.usage });
      return resp({ error: { code: 'methodNotAllowed', message: 'allowed: GET, POST, DELETE' } }, false, 405);
    }

    if (p === '/api/v1/audio/melodies' && method === 'GET') return resp({ melodies: store.melodies });
    if (p === '/api/v1/audio/mp3' && method === 'GET') {
      const m = store.files['/MP3'] || new Map();
      const scripts = [...store.sounds].filter(([, folder]) => folder.size)
        .map(([name, folder]) => ({ name, title: isScript(store, name) ? store.titles[name] || name : name,
          orphan: !isScript(store, name), files: [...folder].map(([file, v]) => ({ name: file, size: v.size })) }));
      return resp({ files: [...m].map(([name, size]) => ({ name, size })), scripts, ...store.usage });
    }
    if (p === '/api/v1/audio/play') {
      const body = JSON.parse(opts.body || '{}');
      if (body.station !== undefined) {
        store.radioPlay = body;
        store.radio.radio.playing = true;
        store.radio.radio.station = String(body.station);
      } else {
        store.played.push(body);
        store.radio.alert = { playing: true, name: body.file !== undefined ? body.file : Object.keys(body)[0], error: '' };
      }
      return resp({ ok: true });
    }
    if (p === '/api/v1/audio/stop') {
      const body = JSON.parse(opts.body || '{}');
      store.audioStops++;
      store.stopBodies.push(body);
      const all = body.group === undefined;
      if (all || body.group === 'radio') store.radio.radio.playing = false;
      if (all || body.group === 'app') store.radio.app = { playing: false, name: '', error: '' };
      if (all || body.group === 'alert') store.radio.alert = { playing: false, name: '', error: '' };
      return resp({ ok: true });
    }
    const mp3 = p.match(/^\/api\/v1\/audio\/mp3\/(.+)$/);
    if (mp3 && method === 'DELETE') {
      const name = decodeURIComponent(mp3[1]) + '.mp3';
      if (!store.files['/MP3'].delete(name))
        return resp({ error: { code: 'notFound', message: name } }, false, 404);
      return resp({ ok: true });
    }
    const melo = p.match(/^\/api\/v1\/audio\/melodies\/(.+)$/);
    if (melo) {
      const name = decodeURIComponent(melo[1]);
      if (method === 'PUT') {
        const body = JSON.parse(opts.body || '{}');
        store.melodies = store.melodies.filter(m => m.name !== name);
        store.melodies.push({ name, rtttl: name + ':' + (body.rtttl || ''), valid: true });
        return resp({ ok: true });
      }
      if (method === 'DELETE') {
        store.melodies = store.melodies.filter(m => m.name !== name);
        return resp({ ok: true });
      }
    }

    if (p === '/api/v1/audio' && method === 'GET') return resp(store.radio);
    if (p === '/api/v1/audio/stations' && method === 'GET') return resp({ stations: store.radio.stations });
    if (p === '/api/v1/audio/stations' && method === 'PUT') {
      store.stationsPut = JSON.parse(opts.body || '{}');
      store.radio.stations = store.stationsPut.stations || [];
      return resp({ ok: true });
    }
    if (p.startsWith('/api/v1/logs')) return resp({ lines: [], next: 0 });
    if (p === '/api/v1/apps') return resp(store.list());
    if (p === '/api/v1/apps/order' && method === 'PUT') {
      store.order = JSON.parse(opts.body || '{}');
      return resp({ ok: true });
    }
    if (p === '/api/v1/apps/active' && method === 'PUT') {
      store.active = JSON.parse(opts.body || '{}');
      return resp({ ok: true });
    }

    // Above the /apps/{name} catch-all, exactly as the device routes it.
    const dat = p.match(/^\/api\/v1\/apps\/(.+)\/data$/);
    if (dat) {
      const name = decodeURIComponent(dat[1]);
      if (!store.scripts.has(name)) return resp({ error: { code: 'notFound', message: 'no such script' } }, false, 404);
      const have = store.data[name] || (store.data[name] = {});
      if (method === 'PATCH') {
        store.dataPatch = JSON.parse(opts.body || '{}');
        for (const [k, v] of Object.entries(store.dataPatch)) {
          if (v === null) delete have[k];
          else have[k] = v;
        }
        return resp({ ok: true, name, error: null });
      }
      return resp(have);
    }
    const cfg = p.match(/^\/api\/v1\/apps\/(builtin\/)?(.+)\/config$/);
    if (cfg) {
      const name = decodeURIComponent(cfg[2]);
      const have = cfg[1] ? store.builtinConfigs[name]
        : store.configs[name] || (store.scripts.has(name) && method === 'GET' ? {} : null);
      if (!have) return resp({ error: { code: 'notFound', message: 'no such script' } }, false, 404);
      if (method === 'PATCH') {
        store.configPatch = JSON.parse(opts.body || '{}');
        if (store.configFail)
          return resp({ error: { code: 'validationFailed', message: store.configFail } }, false, 422);
        // The device stores what it accepted, not what was sent - a number
        // outside min/max is clamped - and the panel re-reads it after saving.
        for (const f of have.fields || []) {
          let v = store.configPatch;
          for (const k of f.path || [f.key]) v = v !== null && typeof v === 'object' && k in v ? v[k] : undefined;
          if (v === undefined) continue;
          f.value = typeof v === 'number'
            ? Math.min(f.max ?? v, Math.max(f.min ?? v, v))
            : v;
        }
        return resp({ ok: true, name, error: null });
      }
      return resp({ name, fields: have.fields || [], warnings: have.warnings || [] });
    }

    const script = p.match(/^\/api\/v1\/apps\/script\/(.+)$/);
    if (script) {
      const name = decodeURIComponent(script[1]);
      if (method === 'PUT') { store.scripts.set(name, opts.body || ''); return resp({}); }
      return resp(store.scripts.get(name) || ''); // GET: raw Berry source
    }
    // PUT /api/v1/apps/script-update/{name}: replaces only while expected_source still matches;
    // null creates a name that does not exist yet.
    const update = p.match(/^\/api\/v1\/apps\/script-update\/(.+)$/);
    if (update && method === 'PUT') {
      const name = decodeURIComponent(update[1]), body = JSON.parse(opts.body || '{}');
      const changed = body.expected_source === null ? store.scripts.has(name) : store.scripts.get(name) !== body.expected_source;
      if (changed) return resp({ error: { code: 'scriptChanged', message: 'script changed' } }, false, 409);
      store.scripts.set(name, body.source);
      return resp({ ok: true });
    }
    const del = p.match(/^\/api\/v1\/apps\/(.+)$/);
    if (del && method === 'DELETE') {
      const name = decodeURIComponent(del[1]);
      store.scripts.delete(name); // its sounds stay until DELETE .../sounds
      return resp({});
    }
    return resp({ error: { code: 'not_found', message: p } }, false, 404);
  };
}

// Whether the device knows a script of that name: scripts and modules, not pushed or built-in apps.
const isScript = (store, name) =>
  store.list().some(a => a.name === name && (a.origin === 'script' || a.origin === 'module'));

// Stores a sound in a script's folder the way the device would, with its SHA-256.
function putSound(store, script, file, data) {
  if (!store.sounds.has(script)) store.sounds.set(script, new Map());
  store.sounds.get(script).set(file, { size: Buffer.byteLength(data), sha256: createHash('sha256').update(data).digest('hex'), data });
}

async function boot(opts) {
  const store = makeStore();
  // The boot IIFE fetches capabilities immediately, so a test that wants
  // different caps has to hand them in before the page comes up.
  if (opts && 'caps' in opts) store.caps = opts.caps;
  if (opts && 'system' in opts) store.system = opts.system;
  if (opts && 'screen' in opts) store.screen = opts.screen;
  if (opts && 'device' in opts) Object.assign(store.device, opts.device);
  // oauth(method, path, body) answers /api/v1/oauth... with [json, status].
  if (opts && 'oauth' in opts) { store.oauth = opts.oauth; store.oauthCalls = []; }
  const netlog = [];
  store.netlog = netlog; // stubXhr logs uploads here too, so fetches and uploads share one order
  const dom = new JSDOM(opts && opts.html !== undefined ? opts.html : loadHtml(), {
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    url: (opts && opts.url) || 'http://localhost/',
    virtualConsole: makeVirtualConsole(),
    beforeParse: installGlobals(window => mockFetch(store, netlog, window), opts && opts.canvasContext),
  });
  await flush(60); // boot render() + device/capabilities/system fetches
  return { dom, window: dom.window, store, netlog };
}

// ---- live backend ------------------------------------------------------------
// Same webui source, but fetch() forwards to a running AWTRIX NG. Node's global
// fetch handles gzip/JSON and enforces no CORS, so the local index.html talks to
// the real device API end-to-end.
async function bootLive(base = 'http://localhost:8080/') {
  const netlog = [];
  const forward = () => (input, opts) => {
    const url = typeof input === 'string' ? new URL(input, base).href : input;
    netlog.push((opts && opts.method ? opts.method.toUpperCase() : 'GET') + ' ' + url);
    return globalThis.fetch(url, opts);
  };
  const dom = new JSDOM(loadHtml(), {
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    url: base,
    virtualConsole: makeVirtualConsole(),
    beforeParse: installGlobals(forward),
  });
  await flush(120); // boot + real HTTP roundtrips to the live backend
  return { dom, window: dom.window, netlog };
}

// SPA tab switch (in-app nav), which - unlike a full reload - keeps module state.
async function goto(window, hash) {
  window.location.hash = hash;
  window.dispatchEvent(new window.Event('hashchange'));
  await flush(80);
}

// uploadFile() goes through XMLHttpRequest, which mockFetch never sees. This swaps in a fake
// that records {method, url, files:[{field,name}]} into log and answers 200. Pass the store
// to have the upload land in its file view too, the way the device would store it: /MP3 for
// /api/v1/audio/mp3, the script's folder for /api/v1/apps/script/{name}/sounds (404 when the
// script is not installed), and store.uploadFail can answer an upload with an error or
// {network:true} for a connection that drops.
function stubXhr(window, log, store) {
  window.XMLHttpRequest = class {
    constructor() { this.upload = {}; this.status = 200; this.responseText = '{"ok":true}'; }
    open(method, url) { this.method = method; this.url = url; }
    send(body) {
      const entry = { method: this.method, url: this.url, files: [], timeout: this.timeout };
      if (body && typeof body.entries === 'function')
        for (const [field, v] of body.entries()) entry.files.push({ field, name: v && v.name });
      log.push(entry);
      const reads=[];
      const read = file => new Promise(resolve=>{const reader=new window.FileReader();
        reader.onload=()=>resolve(new TextDecoder().decode(reader.result));reader.readAsArrayBuffer(file);});
      if (store) {
        store.netlog?.push('XHR ' + this.method + ' ' + this.url + ' ' + entry.files.map(f => f.name).join(','));
        const fail = store.uploadFail && store.uploadFail(entry);
        if (fail && fail.network) { setTimeout(() => this.onerror && this.onerror(), 0); return; }
        if (fail) { this.status = fail.status; this.responseText = JSON.stringify(fail.body || {}); }
        const at = new URL(this.url, 'http://localhost');
        const dir = at.pathname === '/api/v1/audio/mp3' ? '/MP3' : at.searchParams.get('dir');
        const sounds = at.pathname.match(/^\/api\/v1\/apps\/script\/([^/]+)\/sounds$/);
        const script = sounds && decodeURIComponent(sounds[1]);
        if (sounds && !fail && !isScript(store, script)) {
          this.status = 404; this.responseText = '{"error":{"code":"notFound","message":"no such script"}}';
        } else if (!fail && body && typeof body.entries === 'function') {
          for (const [,file] of body.entries()) if (file.name) {
            if (sounds) reads.push(read(file).then(data => putSound(store, script, file.name, data)));
            else if (dir && store.files[dir]) {
              store.files[dir].set(file.name,file.size);
              if (dir === '/ICONS') reads.push(read(file).then(data => { store.localIconBytes[file.name] = data; }));
            }
          }
        }
      }
      Promise.all(reads).then(()=>setTimeout(() => this.onload && this.onload(), 0));
    }
  };
}

module.exports = { boot, bootLive, goto, flush, stubXhr, putSound, useFakeTimers };
