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
    list() {
      if (this.apps) return this.apps;
      return [...scripts.keys()].map(name => ({ name, origin: 'script', error: null }));
    },
  };
}

function mockFetch(store, netlog) {
  const resp = (body, ok = true, status = 200) => ({
    ok, status,
    text: async () => (typeof body === 'string' ? body : JSON.stringify(body)),
  });
  return async function fetch(input, opts = {}) {
    const url = typeof input === 'string' ? input : input.url;
    const method = (opts.method || 'GET').toUpperCase();
    const p = new URL(url, 'http://localhost').pathname;
    netlog.push(method + ' ' + p);

    if (p === '/api/v1/device') return resp({ ipAddress: '192.168.1.5', firmware: 'test' });
    if (p === '/api/v1/capabilities') return resp({ transitions: [] });
    if (p === '/api/v1/system') return resp({ hostname: 'awtrix-ng' });
    if (p === '/api/v1/scripts/shared') return resp([]);
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

async function boot() {
  const store = makeStore();
  const netlog = [];
  const dom = new JSDOM(loadHtml(), {
    runScripts: 'dangerously',
    pretendToBeVisual: true,
    url: 'http://localhost/',
    virtualConsole: makeVirtualConsole(),
    beforeParse: installGlobals(() => mockFetch(store, netlog)),
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

module.exports = { boot, bootSim, goto, flush };
