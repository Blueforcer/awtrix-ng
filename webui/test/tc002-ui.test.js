const {test} = require('node:test');
const assert = require('node:assert/strict');
const {JSDOM} = require('jsdom');
const modulePromise = import('../../docs/assets/tc002/ui.js');
const encoder = new TextEncoder();
const settle = () => new Promise(resolve => setImmediate(resolve));
const bundle = {version: 'v1.0.0', manifest: {dirty: false}};

async function fixture(options = {}) {
  const dom = new JSDOM('<div id="tc002-installer" data-index="/firmware/tc002/index.json"></div>', {url:'https://example.test/install/tc002/'});
  const root = dom.window.document.querySelector('#tc002-installer');
  const calls = [];
  const identity = {vendorId: 0x18d1, productId: 0xd002, serialNumber: 'synthetic-clock'};
  const session = {identity, async close() { calls.push('close'); }, async controlRequest(command, payload) {
    calls.push({command, payload: payload && {...payload}});
    if (command === 'wifi-set') { if (options.wifiFailure) throw new Error(payload.password); return {ok:true}; }
    if (command === 'wifi-status') return {link:'connected', ssid:'Example network', store:'ok', error:''};
    if (command === 'status') return {network:{link:'connected', ssid:'Example network', ipv4:'192.168.1.27'}};
  }};
  let instance;
  class Installer {
    constructor(...args) { this.args = args; this.writeStarted = false; this.verified = false; instance = this; }
    async prepare() { calls.push('prepare'); return {version:bundle.version}; }
    async install(connected) {
      assert.equal(connected, session); calls.push('install');
      this.writeStarted = true;
      if (options.interrupt && calls.filter(call => call === 'install').length === 1) {
        const error = new Error('Synthetic interrupted write'); error.recoveryNeeded = true; throw error;
      }
      this.verified = true;
    }
    async dispose() { calls.push('dispose'); }
    async reboot() { calls.push('reboot'); }
  }
  const bytes = encoder.encode('synthetic firmware');
  const index = {version:bundle.version, asset:'usb-awtrix-ng-tc002.zip', size:bytes.length, sha256:'a'.repeat(64)};
  const deps = {
    secure: options.secure ?? true, usb: {}, Tc002Installer: Installer,
    preload: async () => { calls.push('preload'); },
    parsePackage: async (input, settings) => { calls.push({parse:settings}); return bundle; },
    sha256: async () => options.badHash ? 'b'.repeat(64) : index.sha256,
    fetch: async url => {
      calls.push(String(url));
      if (String(url).endsWith('index.json')) return options.public ? new Response(JSON.stringify(index)) : new Response('', {status:404});
      return new Response(bytes);
    },
    connect: settings => { calls.push('connect'); if (options.connect) return options.connect(settings); return Promise.resolve(session); },
    connectGranted: async settings => { calls.push({reconnect:settings.identity}); return session; },
    sleep: async () => {},
  };
  const ui = (await modulePromise).mountInstaller(root, deps);
  await ui.ready;
  const get = role => root.querySelector(`[data-role="${role}"]`);
  const click = action => root.querySelector(`[data-action="${action}"]`).click();
  async function local() {
    const file = {size:bytes.length, arrayBuffer: async () => bytes.buffer};
    Object.defineProperty(get('file'), 'files', {value:[file], configurable:true});
    get('file').dispatchEvent(new dom.window.Event('change', {bubbles:true}));
    await settle();
  }
  return {dom, root, ui, calls, get, click, local, get instance() { return instance; }};
}

test('TC002 UI: missing public index is honest and local package remains available', async () => {
  const f = await fixture();
  assert.match(f.get('release').textContent, /No public TC002 firmware/);
  assert.equal(f.root.querySelector('[data-action="connect"]').disabled, true);
  assert.equal(f.get('local').hidden, false);
  assert.doesNotMatch(f.root.textContent, /download.*backup/i);
  await f.local();
  assert.equal(f.root.querySelector('[data-action="connect"]').disabled, false);
  assert.equal(f.ui.phase, 'idle');
  await f.ui.destroy();
});

test('TC002 UI: USB chooser starts inside click, then explicit install and reboot lead to hotspot', async () => {
  const f = await fixture({public:true});
  f.click('connect');
  assert.ok(f.calls.includes('connect'));
  assert.equal(f.ui.phase, 'connecting');
  await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.ok(!f.calls.includes('install'));
  f.click('install'); await settle();
  assert.equal(f.ui.phase, 'verified');
  assert.ok(!f.calls.includes('reboot'));
  f.click('restart'); await settle();
  assert.equal(f.ui.phase, 'done');
  assert.match(f.get('success').textContent, /awtrixng-XXXXXX/);
  assert.equal(f.get('success').querySelector('a').href, 'http://192.168.4.1/');
  assert.ok(!f.calls.some(call => call?.command === 'wifi-set'));
  await f.ui.destroy();
});

test('TC002 UI: partial flash preserves prepared context and retry uses same identity', async () => {
  const f = await fixture({public:true, interrupt:true});
  f.click('connect'); await settle();
  const original = f.instance;
  f.click('install'); await settle();
  assert.equal(f.ui.phase, 'recovery');
  assert.equal(f.get('local').hidden, true);
  assert.equal(await f.ui.destroy(), false);
  assert.ok(!f.calls.includes('dispose'));
  const before = new f.dom.window.Event('beforeunload', {cancelable:true});
  f.dom.window.dispatchEvent(before);
  assert.equal(before.defaultPrevented, true);
  f.click('retry'); await settle(); await settle();
  assert.equal(f.instance, original);
  assert.equal(f.ui.phase, 'verified');
  assert.equal(f.calls.filter(call => call === 'prepare').length, 1);
  assert.equal(f.calls.find(call => call?.reconnect).reconnect.serialNumber, 'synthetic-clock');
  await f.ui.destroy();
});

test('TC002 UI: checksum and secure-context checks prevent connecting', async () => {
  const bad = await fixture({public:true, badHash:true});
  assert.match(bad.get('error').textContent, /verification/);
  assert.equal(bad.root.querySelector('[data-action="connect"]').disabled, true);
  assert.ok(!bad.calls.some(call => call?.parse));
  const insecure = await fixture({public:true, secure:false});
  assert.equal(insecure.root.querySelector('[data-action="connect"]').disabled, true);
  assert.match(insecure.get('status').textContent, /HTTPS/);
  await bad.ui.destroy(); await insecure.ui.destroy();
});

test('TC002 UI: optional USB Wi-Fi clears password and exposes only validated IP', async () => {
  const f = await fixture({public:true});
  f.click('connect'); await settle(); f.click('install'); await settle(); f.click('restart'); await settle();
  const form = f.get('wifi-form');
  form.elements.ssid.value = 'Example network'; form.elements.password.value = 'synthetic-secret';
  form.dispatchEvent(new f.dom.window.Event('submit', {cancelable:true}));
  assert.equal(form.elements.password.value, '');
  for (let i = 0; i < 10; i++) await settle();
  assert.equal(f.get('clock-link').href, 'http://192.168.1.27/');
  assert.equal(f.get('clock-link').hidden, false);
  assert.doesNotMatch(f.root.innerHTML, /synthetic-secret/);
  assert.equal(f.calls.find(call => call?.command === 'wifi-set').payload.password, 'synthetic-secret');
  await f.ui.destroy();
});

test('TC002 UI: USB Wi-Fi error never displays credential-bearing transport errors', async () => {
  const f = await fixture({public:true, wifiFailure:true});
  f.click('connect'); await settle(); f.click('install'); await settle(); f.click('restart'); await settle();
  const form = f.get('wifi-form');
  form.elements.ssid.value = 'Example network'; form.elements.password.value = 'synthetic-secret';
  form.dispatchEvent(new f.dom.window.Event('submit', {cancelable:true})); for (let i = 0; i < 60; i++) await settle();
  assert.doesNotMatch(f.root.textContent, /synthetic-secret/);
  assert.match(f.get('wifi-status').textContent, /try again/);
  assert.equal(form.elements.password.value, '');
  await f.ui.destroy();
});


test('TC002 UI: instant navigation remounts safely and retains an interrupted installer', async () => {
  const {watchInstaller} = await modulePromise;
  const dom = new JSDOM('<main><div id="tc002-installer"><p data-role="error" hidden></p></div><a href="/another/">Other documentation</a></main>', {url:'https://example.test/install/'});
  const doc = dom.window.document;
  let update;
  const pages = {subscribe(callback) { update = callback; return {unsubscribe() {}}; }};
  const controllers = [];
  const stop = watchInstaller(doc, pages, () => {
    const controller = {holdsPage:false, destroyed:false, destroy() { this.destroyed = true; }};
    controllers.push(controller); return controller;
  });
  const original = doc.querySelector('#tc002-installer');
  controllers[0].holdsPage = true;
  const click = new dom.window.MouseEvent('click', {bubbles:true,cancelable:true,button:0});
  doc.querySelector('a').dispatchEvent(click);
  assert.equal(click.defaultPrevented, true);
  assert.match(original.textContent, /Finish or retry/);
  doc.querySelector('main').innerHTML = '<div id="tc002-installer"></div>';
  update();
  assert.equal(doc.querySelector('#tc002-installer'), original);
  assert.equal(controllers.length, 1);
  assert.equal(stop(), false);
  controllers[0].holdsPage = false;
  doc.querySelector('main').innerHTML = '<div id="tc002-installer"></div>';
  update();
  assert.equal(controllers[0].destroyed, true);
  assert.equal(controllers.length, 2);
  assert.equal(stop(), true);
});

test('TC002 UI: source text is valid UTF-8 and source license link is accessible', async () => {
  const fs = require('node:fs/promises');
  const source = await fs.readFile(require('node:path').resolve(__dirname, '../../docs/assets/tc002/ui.js'));
  assert.doesNotThrow(() => new TextDecoder('utf-8', {fatal: true}).decode(source));
  const f = await fixture();
  const link = f.get('licenses');
  assert.ok(link.textContent.trim(), 'the license link has an accessible name');
  assert.match(link.href, /image\/NOTICE\/$/);
  assert.equal(link.target, '_blank');
  await f.ui.destroy();
});
