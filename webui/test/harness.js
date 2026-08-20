/* Boots the real webui/index.html inside jsdom so the editor's client-side
   state machine can be driven from Node. The device API is either an in-memory
   mock (default, offline) or a live simulator (pass a base URL / use --sim).

   The page's source of truth is webui/index.html; the gzipped WebUiAsset.h that
   the firmware serves is regenerated from it at build time, so testing the file
   directly is faithful and needs no rebuild between edits. */
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const HTML_PATH = path.join(__dirname, '..', 'index.html');

const loadHtml = () => fs.readFileSync(HTML_PATH, 'utf8');

function makeVirtualConsole() {
  const vc = new VirtualConsole();
  vc.on('jsdomError', e => {
    // Surface genuine script errors; ignore jsdom "Not implemented" noise.
    if (!/Not implemented/.test(e.message)) console.error('[jsdomError]', e.message);
  });
  return vc;
}

// Shared beforeParse: give the page the globals jsdom omits.
function installGlobals(fetchImpl) {
  return window => {
    window.fetch = fetchImpl(window);
    window.TextEncoder = TextEncoder;
    window.TextDecoder = TextDecoder;
    window.AbortController = AbortController;
    window.scrollTo = () => {};
    window.matchMedia = () => ({ matches: false, addEventListener() {}, removeEventListener() {} });
    // jsdom has no layout, so it ships no ResizeObserver. Without a stub the UI
    // throws on construction and every run prints a jsdomError that is not one.
    window.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
  };
}

// Let jsdom's real promises/timers settle.
const flush = (ms = 40) => new Promise(r => setTimeout(r, ms));

// ---- in-memory backend ----------------------------------------------------
function makeStore() {
  const scripts = new Map(); // name -> source
  return {
    scripts,
    // Set `apps` to serve a hand-written inventory instead of one derived from
    // the installed scripts; `order` holds the last PUT /api/v1/apps/order body.
    apps: null,
    order: null,
    // name -> {fields, warnings}, what GET /api/v1/apps/{name}/config answers.
    // `configPatch` holds the last PATCH body so a test can see what was sent.
    configs: {},
    configPatch: null,
    // Last PATCH /api/v1/settings body; the store itself is updated with it too.
    settingsPatch: null,
    // What GET /api/v1/capabilities answers; override before goto() to test gating.
    caps: { transitions: [],
            audio: { buzzer: true, track: false, mp3: true, radio: true } },
    settings: { soundEnabled: true, buzzerVolume: 80, dfplayerVolume: 80, mp3Volume: 70,
                radioVolume: 60, radioMeta: true },
    // dir -> Map(name -> size), the file API's flash view.
    files: { '/ICONS': new Map(), '/MP3': new Map() },
    melodies: [], // [{name, rtttl, valid, notes, durationMs, bytes}]
    played: [],   // bodies POSTed to /api/v1/audio/play
    radio: { available: true, mp3: { playing: false, name: '' },
             radio: { playing: false, station: '', title: '', error: '' }, stations: [] },
    radioPlay: null,   // last POST /api/v1/audio/play carrying a station or a url
    stationsPut: null, // last PUT /api/v1/audio/stations body
    // The icon database lives outside the device: the browser talks to it
    // directly, so it is mocked by absolute URL rather than by path.
    iconDb: { v: 1, icons: [] }, // what index.json answers
    iconBytes: {},               // slug -> bytes served from icons/<slug>.<ext>
    iconExt: {},                 // slug -> 'gif' (default) or 'jpg'; the Hub serves only that one
    submitted: [],               // FormData bodies POSTed to the submit service
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
const ICON_DB = 'https://flows.blueforcer.de/icons/';

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
    text: async () => JSON.stringify(body),
  });
  return async function fetch(input, opts = {}) {
    const url = typeof input === 'string' ? input : input.url;
    const method = (opts.method || 'GET').toUpperCase();

    if (/^https?:\/\//.test(url) && !url.startsWith('http://localhost')) {
      netlog.push(method + ' ' + url);
      if (url.endsWith('/submit') && method === 'POST') {
        store.submitted.push(opts.body);
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
        // The Hub serves the bytes directly under the catalogue prefix - no
        // second 'icons/' segment. This pattern is what pins that.
        const icon = rest.match(/^([^/]+)\.(gif|jpg)$/);
        if (icon) {
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

    if (p === '/api/v1/device') return resp({ ipAddress: '192.168.1.5', firmware: 'test' });
    if (p === '/api/v1/capabilities')
      return store.caps ? resp(store.caps) : resp({ error: { message: 'offline' } }, false, 503);
    if (p === '/api/v1/system') return resp({ hostname: 'awtrix-ng' });
    if (p === '/api/v1/settings' && method === 'GET') return resp(store.settings);
    if (p === '/api/v1/settings' && method === 'PATCH') {
      store.settingsPatch = JSON.parse(opts.body || '{}');
      Object.assign(store.settings, store.settingsPatch);
      return resp({ ok: true });
    }
    if (p === '/api/v1/scripts/shared') return resp([]);

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
        return resp({ ok: true });
      }
    }

    // Static asset served off the device, read back when an icon is submitted.
    if (p.startsWith('/ICONS/')) {
      const name = decodeURIComponent(p.slice(7));
      if (!store.files['/ICONS'].has(name)) return ext({ error: 'notFound' }, false, 404);
      return ext('GIF89a-' + name);
    }

    if (p === '/api/v1/audio/melodies' && method === 'GET') return resp({ melodies: store.melodies });
    if (p === '/api/v1/audio/mp3' && method === 'GET') {
      const m = store.files['/MP3'] || new Map();
      return resp({ files: [...m].map(([name, size]) => ({ name, size })),
                    usedBytes: 1024, totalBytes: 1048576 });
    }
    if (p === '/api/v1/audio/play') {
      const body = JSON.parse(opts.body || '{}');
      if (body.station !== undefined || body.url !== undefined || body.index !== undefined) {
        store.radioPlay = body;
        store.radio.radio.playing = true;
      } else {
        store.played.push(body);
      }
      return resp({ ok: true });
    }
    if (p === '/api/v1/audio/stop') { store.radio.radio.playing = false; return resp({ ok: true }); }
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

    // Above the /apps/{name} catch-all, exactly as the device routes it.
    const cfg = p.match(/^\/api\/v1\/apps\/(.+)\/config$/);
    if (cfg) {
      const name = decodeURIComponent(cfg[1]);
      const have = store.configs[name];
      if (!have) return resp({ error: { code: 'notFound', message: 'no such script' } }, false, 404);
      if (method === 'PATCH') {
        store.configPatch = JSON.parse(opts.body || '{}');
        // The device stores what it accepted, not what was sent - a number
        // outside min/max is clamped - and the panel re-reads it after saving.
        for (const [k, v] of Object.entries(store.configPatch)) {
          const f = (have.fields || []).find(x => x.key === k);
          if (!f) continue;
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
    const del = p.match(/^\/api\/v1\/apps\/(.+)$/);
    if (del && method === 'DELETE') { store.scripts.delete(decodeURIComponent(del[1])); return resp({}); }
    return resp({ error: { code: 'not_found', message: p } }, false, 404);
  };
}

async function boot(opts) {
  const store = makeStore();
  // The boot IIFE fetches capabilities immediately, so a test that wants
  // different caps has to hand them in before the page comes up.
  if (opts && 'caps' in opts) store.caps = opts.caps;
  const netlog = [];
  const dom = new JSDOM(loadHtml(), {
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    url: 'http://localhost/',
    virtualConsole: makeVirtualConsole(),
    beforeParse: installGlobals(window => mockFetch(store, netlog, window)),
  });
  await flush(60); // boot render() + device/capabilities/system fetches
  return { dom, window: dom.window, store, netlog };
}

// ---- live simulator backend -----------------------------------------------
// Same webui source, but fetch() forwards to a running simulator. Node's global
// fetch handles gzip/JSON and enforces no CORS, so the local index.html talks to
// the real device API end-to-end.
async function bootSim(base = 'http://localhost:8080/') {
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
  await flush(120); // boot + real HTTP roundtrips to the sim
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
// to have the upload land in its file view too, the way the device would store it.
function stubXhr(window, log, store) {
  window.XMLHttpRequest = class {
    constructor() { this.upload = {}; this.status = 200; this.responseText = '{"ok":true}'; }
    open(method, url) { this.method = method; this.url = url; }
    send(body) {
      const entry = { method: this.method, url: this.url, files: [] };
      if (body && typeof body.entries === 'function')
        for (const [field, v] of body.entries()) entry.files.push({ field, name: v && v.name });
      log.push(entry);
      if (store) {
        const dir = new URL(this.url, 'http://localhost').searchParams.get('dir');
        if (dir && store.files[dir])
          for (const f of entry.files) if (f.name) store.files[dir].set(f.name, 1);
      }
      setTimeout(() => this.onload && this.onload(), 0);
    }
  };
}

module.exports = { boot, bootSim, goto, flush, stubXhr };
