const {test} = require('node:test');
const assert = require('node:assert/strict');
const {createRequire} = require('node:module');
const path = require('node:path');
let JSDOM;
try { ({JSDOM} = require('jsdom')); }
catch { ({JSDOM} = createRequire(path.resolve(__dirname, '../../../../webui/test/package.json'))('jsdom')); }
const modulePromise = import('../ui/controller.js');
const sharedPromise = import('../../../../docs/assets/tc002/install.js');
const settle = async () => { for (let i = 0; i < 4; i++) await new Promise(resolve => setImmediate(resolve)); };
const deferred = () => { let resolve, reject; const promise = new Promise((yes, no) => { resolve = yes; reject = no; }); return {promise, resolve, reject}; };
const identity = {vendorId: 0x18d1, productId: 0xd002, serialNumber: 'synthetic-clock'};
const bundle = {version: '1.1.2-test', manifest: {dirty: false}};

async function fixture(options = {}) {
  const dom = new JSDOM('<main id="installer"></main>', {url: 'https://installer.test/ui/index.html'});
  const root = dom.window.document.querySelector('#installer');
  const calls = [];
  let instance, connectCount = 0;
  const session = {identity, async close() { calls.push('close'); }, async controlRequest(command, payload) {
    calls.push({command, payload: payload ? {...payload} : undefined});
    if (options.control) return options.control(command, payload);
    if (command === 'wifi-set') return {ok: true};
    if (command === 'wifi-status') return {link: 'connected', ssid: 'Example network', store: 'ok', error: ''};
    if (command === 'status') return {network: {link: 'connected', ssid: 'Example network', ipv4: options.ip || '192.168.1.27'}};
  }};
  class Installer {
    constructor(...args) { this.args = args; this.writeStarted = false; this.verified = false; instance = this; calls.push('new'); }
    async prepare() { calls.push('prepare'); if (options.prepare) await options.prepare(); return {version: bundle.version}; }
    async install(connected) {
      assert.equal(connected, session); calls.push('install');
      this.writeStarted = true;
      if (options.interrupt && calls.filter(call => call === 'install').length === 1) {
        const failure = new Error('Synthetic interrupted write'); failure.recoveryNeeded = true; throw failure;
      }
      this.verified = true;
    }
    async dispose() { calls.push('dispose'); }
    async reboot() { calls.push('reboot'); }
  }
  const desktop = {
    chooseFirmware: async () => { calls.push('choose-firmware'); return options.chooseFirmware ? options.chooseFirmware() : false; },
    firmware: async onProgress => { calls.push('firmware'); return options.firmware ? options.firmware(onProgress) : new Uint8Array([1, 2]); },
    phase: async value => { calls.push({phase: value}); if (options.phase) await options.phase(value); },
    openSetup: async ip => { calls.push({open: ip}); },
  };
  const deps = {
    desktop, usb: {}, Tc002Installer: Installer,
    preload: async () => { calls.push('preload'); if (options.preload) await options.preload(); },
    parsePackage: async (bytes, config) => { calls.push({parse: config}); if (options.parseFailure) throw new Error('Invalid included firmware'); return options.parsePackage ? options.parsePackage(bytes) : bundle; },
    connectGranted: async settings => {
      calls.push({connect: settings}); connectCount++;
      if (options.connect) return options.connect(settings, connectCount, session);
      return session;
    },
    validateWifi: (ssid, password) => {
      calls.push('validate-wifi');
      if (!ssid || password && password.length < 8) throw new Error('Enter a network name and valid password.');
    },
    buildResImage: () => {}, sleep: async () => {}, provisionWifi: (await sharedPromise).provisionWifi,
  };
  const ui = (await modulePromise).mountInstaller(root, deps);
  const get = role => root.querySelector(`[data-role="${role}"]`);
  const click = action => root.querySelector(`[data-action="${action}"]`).click();
  const wifi = (ssid = 'Example network', password = 'synthetic-secret') => {
    get('wifi-form').elements.ssid.value = ssid;
    get('wifi-form').elements.password.value = password;
  };
  await settle();
  return {dom, root, ui, calls, get, click, wifi, session, get instance() { return instance; }};
}

test('desktop UI preloads and checks downloaded firmware before automatic USB discovery', async () => {
  const tools = deferred();
  const f = await fixture({preload: () => tools.promise});
  assert.equal(f.ui.phase, 'loading');
  assert.ok(!f.calls.some(call => call?.connect));
  tools.resolve(); await f.ui.ready; await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.calls.filter(call => call?.connect).length, 1);
  assert.ok(!f.calls.includes('install'));
  assert.ok(!f.calls.includes('reboot'));
  assert.equal(f.root.querySelector('input[type=file]'), null);
  assert.equal(f.root.querySelector('[data-action=connect]'), null);
  assert.match(f.get('release').textContent, /1.1.2-test/);
  assert.equal(f.calls.find(call => call && typeof call === 'object' && 'parse' in call).parse, undefined);
  await f.ui.destroy();
});

test('desktop UI installs on one click, restarts the verified clock and offers Wi-Fi or hotspot setup', async () => {
  const f = await fixture();
  f.click('install'); await settle();
  assert.equal(f.ui.phase, 'done');
  assert.ok(f.calls.indexOf('install') < f.calls.indexOf('reboot'));
  assert.ok(f.calls.findIndex(call => call?.phase === 'verified') < f.calls.indexOf('reboot'));
  assert.match(f.get('ap-steps').textContent, /awtrixng-XXXXXX/);
  assert.equal(f.get('ap-steps').hidden, false);
  assert.equal(f.get('wifi').hidden, false);
  assert.equal(f.root.querySelector('[data-action=send-wifi]').hidden, false);
  assert.ok(!f.calls.some(call => call?.command === 'wifi-set'));
  f.click('setup'); await settle();
  assert.deepEqual(f.calls.find(call => call?.open), {open: '192.168.4.1'});
  await f.ui.destroy();
});

test('desktop UI arms the native close guard before any install write and ignores double clicks', async () => {
  const guard = deferred();
  const f = await fixture({phase: value => value === 'installing' ? guard.promise : undefined});
  f.click('install'); f.click('install'); await settle();
  assert.equal(f.ui.phase, 'installing');
  assert.ok(!f.calls.includes('install'));
  assert.equal(await f.ui.destroy(), false);
  const unload = new f.dom.window.Event('beforeunload', {cancelable: true});
  f.dom.window.dispatchEvent(unload);
  assert.equal(unload.defaultPrevented, true);
  for (const keys of [{key: 'F5'}, {key: 'r', ctrlKey: true}, {key: 'r', metaKey: true}]) {
    const reload = new f.dom.window.KeyboardEvent('keydown', {cancelable: true, ...keys});
    f.dom.window.dispatchEvent(reload);
    assert.equal(reload.defaultPrevented, true);
  }
  guard.resolve(); await settle();
  assert.equal(f.calls.filter(call => call === 'install').length, 1);
  assert.equal(f.ui.phase, 'done');
  await f.ui.destroy();
});

test('desktop UI retains the same image and device identity after an interrupted flash', async () => {
  const f = await fixture({interrupt: true});
  const original = f.instance;
  f.wifi(); f.click('install'); await settle();
  assert.equal(f.ui.phase, 'recovery');
  assert.equal(f.get('recovery-help').hidden, false);
  assert.equal(await f.ui.destroy(), false);
  assert.ok(!f.calls.includes('dispose'));
  f.click('retry'); await settle();
  assert.equal(f.instance, original);
  assert.equal(f.calls.filter(call => call === 'prepare').length, 1);
  const connections = f.calls.filter(call => call?.connect);
  assert.deepEqual(connections[1].connect.identity, identity);
  assert.equal(f.ui.phase, 'done');
  assert.equal(f.calls.find(call => call?.command === 'wifi-set').payload.password, 'synthetic-secret');
  await f.ui.destroy();
});

test('desktop UI validates optional credentials before installing', async () => {
  const f = await fixture();
  f.wifi('', 'synthetic-secret'); f.click('install'); await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.ok(!f.calls.includes('install'));
  assert.equal(f.get('wifi').open, true);
  assert.match(f.get('error').textContent, /network name/);
  await f.ui.destroy();
});

test('desktop UI sends optional Wi-Fi only after restart and clears the form immediately', async () => {
  const f = await fixture();
  f.wifi(); f.click('install');
  assert.equal(f.get('wifi-form').elements.password.value, '');
  assert.equal(f.get('wifi-form').elements.ssid.value, '');
  await settle();
  assert.equal(f.ui.phase, 'done');
  const setAt = f.calls.findIndex(call => call?.command === 'wifi-set');
  assert.ok(setAt > f.calls.indexOf('reboot'));
  assert.deepEqual(f.calls[setAt].payload, {ssid: 'Example network', password: 'synthetic-secret'});
  assert.equal(f.get('ap-steps').hidden, true);
  assert.doesNotMatch(f.root.textContent, /synthetic-secret/);
  assert.ok(f.calls.filter(call => call?.phase).every(call => !JSON.stringify(call).includes('synthetic-secret')));
  f.click('open-clock'); await settle();
  assert.deepEqual(f.calls.find(call => call?.open), {open: '192.168.1.27'});
  await f.ui.destroy();
});

test('desktop UI keeps retrying unreachable Wi-Fi setup, hides credential-bearing errors and offers a retry', async () => {
  let received, attempts = 0;
  const f = await fixture({control: async (command, payload) => {
    if (command === 'wifi-set') { attempts++; received = payload; throw new Error(payload.password); }
  }});
  f.wifi(); f.click('install'); await settle(); for (let i = 0; i < 60; i++) await settle();
  assert.equal(f.ui.phase, 'done');
  assert.ok(attempts > 1);
  assert.deepEqual(received, {ssid: '', password: ''});
  assert.doesNotMatch(f.root.textContent, /synthetic-secret/);
  assert.match(f.get('wifi-result').textContent, /try again/i);
  assert.equal(f.get('ap-steps').hidden, false);
  assert.equal(f.root.querySelector('[data-action=send-wifi]').textContent, 'Try again');
  assert.equal(f.get('wifi-form').elements.ssid.value, 'Example network');
  assert.equal(f.get('wifi-form').elements.password.value, '');
  await f.ui.destroy();
});

test('desktop UI never connects USB after an invalid or oversized package', async () => {
  for (const options of [{parseFailure: true}, {firmware: () => new Uint8Array(32 * 1024 * 1024 + 1)}]) {
    const f = await fixture(options);
    assert.equal(f.ui.phase, 'failed');
    assert.ok(!f.calls.some(call => call?.connect));
    assert.equal(f.root.querySelector('[data-action=retry]').hidden, true);
    await f.ui.destroy();
  }
});

test('desktop UI offers a clear retry after USB timeout without retaining old credentials', async () => {
  const wait = deferred();
  const f = await fixture({connect: async (settings, count, session) => count === 1 ? wait.promise : session});
  f.wifi(); wait.reject(new Error('USB did not become stable.')); await settle();
  assert.equal(f.ui.phase, 'failed');
  assert.equal(f.get('wifi-form').elements.password.value, '');
  assert.equal(f.root.querySelector('[data-action=retry]').hidden, false);
  f.click('retry'); await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.calls.filter(call => call?.connect).length, 2);
  await f.ui.destroy();
});

test('desktop UI validates clock addresses before handing them to the native opener', async () => {
  const {privateIpv4} = await sharedPromise;
  for (const value of ['127.0.0.1', '0.0.0.0', '8.8.8.8', '192.168.001.1', '192.168.1.256', '192.168.1.2:8080', 'javascript:alert(1)', '172.32.0.2', undefined]) assert.equal(privateIpv4(value), null);
  for (const value of ['10.0.0.1', '172.16.0.2', '172.31.255.2', '192.168.1.27']) assert.equal(privateIpv4(value), value);
  const f = await fixture({ip: '127.0.0.1'});
  f.wifi(); f.click('install'); await settle(); for (let i = 0; i < 30; i++) await settle();
  assert.equal(f.ui.phase, 'done');
  assert.equal(f.get('ap-steps').hidden, false);
  f.click('open-clock'); await settle();
  assert.ok(!f.calls.some(call => call?.open));
  await f.ui.destroy();
});

test('desktop UI can skip Wi-Fi while reconnecting and closes a late connection', async () => {
  const pending = deferred();
  const f = await fixture({connect: async (settings, count, session) => count === 1 ? session : pending.promise});
  f.wifi(); f.click('install'); await settle();
  assert.equal(f.ui.phase, 'provisioning');
  f.click('skip-wifi'); await settle();
  assert.equal(f.ui.phase, 'done');
  const before = f.calls.filter(call => call === 'close').length;
  pending.resolve(f.session); await settle();
  assert.equal(f.calls.filter(call => call === 'close').length, before + 1);
  assert.ok(!f.calls.some(call => call?.command === 'wifi-set'));
  assert.equal(f.ui.phase, 'done');
  await f.ui.destroy();
});

test('desktop UI destruction cancels discovery without preparing a late device', async () => {
  const pending = deferred();
  const f = await fixture({connect: () => pending.promise});
  f.wifi();
  assert.equal(await f.ui.destroy(), true);
  assert.equal(f.get('wifi-form').elements.password.value, '');
  assert.equal(f.calls.find(call => call?.connect).connect.signal.aborted, true);
  pending.resolve(f.session); await settle();
  assert.ok(!f.calls.includes('prepare'));
  assert.ok(f.calls.includes('close'));
});

test('desktop UI retries a failed firmware download without accessing USB early', async () => {
  let attempts = 0;
  const f = await fixture({firmware: async onProgress => {
    onProgress({stage: 'downloading', received: 1, total: 2});
    if (++attempts === 1) throw new Error('Network temporarily unavailable.');
    return new Uint8Array([1, 2]);
  }});
  assert.equal(f.ui.phase, 'failed');
  assert.ok(!f.calls.some(call => call?.connect));
  assert.equal(f.root.querySelector('[data-action=download]').hidden, false);
  f.click('download'); await settle();
  assert.equal(attempts, 2);
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.calls.filter(call => call?.connect).length, 1);
  await f.ui.destroy();
});

test('desktop UI asks for a firmware file when no release is published, without an error or silent fallback', async () => {
  const f = await fixture({firmware: async () => { throw {code: 'no-compatible-release', message: 'Missing asset'}; }});
  assert.equal(f.ui.phase, 'firmware');
  assert.equal(f.get('error').hidden, true);
  assert.match(f.get('status').textContent, /usb-awtrix-ng-tc002\.zip/);
  const choose = f.root.querySelector('[data-action=choose-firmware]');
  assert.equal(choose.hidden, false);
  assert.equal(choose.classList.contains('secondary'), false);
  assert.ok(!f.calls.some(call => call?.connect));
  assert.equal(f.root.querySelector('input[type=file]'), null);
  await f.ui.destroy();
});

test('desktop UI shows local firmware progress and retains its source after the shared package check', async () => {
  const pending = deferred();
  const f = await fixture({firmware: onProgress => {
    onProgress({stage: 'local', received: 1, total: 2});
    return pending.promise;
  }});
  assert.equal(f.ui.phase, 'loading');
  assert.equal(f.get('progress').hidden, false);
  assert.equal(f.get('progress').value, 0.5);
  assert.match(f.get('status').textContent, /Loading local TC002 firmware/);
  assert.equal(f.get('release').textContent, 'Local firmware');
  assert.doesNotMatch(f.root.textContent, /GitHub|latest|download/i);
  assert.ok(!f.calls.some(value => value?.connect));
  pending.resolve(new Uint8Array([1, 2])); await f.ui.ready; await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.get('release').textContent, 'AWTRIX NG 1.1.2-test · Local firmware');
  assert.ok(f.calls.findIndex(value => value && typeof value === 'object' && 'parse' in value) < f.calls.findIndex(value => value?.connect));
  assert.ok(!f.calls.includes('install'));
  await f.ui.destroy();
});

test('desktop UI keeps unreadable and invalid local firmware failures offline and before USB', async () => {
  for (const options of [{firmware: async () => { throw {code: 'local-firmware', message: 'Local firmware: file is unreadable.'}; }},
    {parseFailure: true, firmware: async onProgress => { onProgress({stage: 'local', received: 2, total: 2}); return new Uint8Array([1, 2]); }}]) {
    const f = await fixture(options);
    assert.equal(f.ui.phase, 'failed');
    assert.ok(!f.calls.some(value => value?.connect));
    assert.equal(f.calls.filter(value => value === 'firmware').length, 1);
    assert.match(f.get('status').textContent, /Check the local firmware ZIP/);
    assert.doesNotMatch(f.get('status').textContent, /internet|Checking GitHub|Downloading/);
    assert.equal(f.root.querySelector('[data-action=download]').textContent, 'Retry firmware');
    await f.ui.destroy();
  }
});

test('desktop UI retry resets the source when a broken local file has been removed', async () => {
  let attempt = 0;
  const f = await fixture({firmware: async onProgress => {
    if (++attempt === 1) {
      onProgress({stage: 'local', received: 0, total: 2});
      throw {code: 'local-firmware', message: 'Local firmware: file is unreadable.'};
    }
    onProgress({stage: 'checking', received: 0});
    onProgress({stage: 'downloading', received: 2, total: 2});
    onProgress({stage: 'verified', received: 2, total: 2});
    return new Uint8Array([1, 2]);
  }});
  assert.equal(f.ui.phase, 'failed');
  assert.ok(!f.calls.some(value => value?.connect));
  f.click('download'); await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.get('release').textContent, 'AWTRIX NG 1.1.2-test · GitHub release');
  assert.equal(f.calls.filter(value => value?.connect).length, 1);
  await f.ui.destroy();
});

test('desktop firmware picker is explicit, serialized, and cancellation resumes the existing load', async () => {
  const firmware = deferred(), picker = deferred();
  const f = await fixture({firmware: () => firmware.promise, chooseFirmware: () => picker.promise});
  const button = f.root.querySelector('[data-action=choose-firmware]');
  assert.equal(button.hidden, false);
  assert.ok(!f.calls.includes('choose-firmware'));
  f.click('choose-firmware'); f.click('choose-firmware');
  assert.equal(button.disabled, true);
  assert.equal(f.calls.filter(value => value === 'choose-firmware').length, 1);
  firmware.resolve(new Uint8Array([1, 2])); await settle();
  assert.ok(!f.calls.some(value => value?.connect));
  picker.resolve(false); await f.ui.ready; await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.calls.filter(value => value === 'firmware').length, 1);
  assert.equal(f.calls.filter(value => value?.connect).length, 1);
  await f.ui.destroy();
});

test('selected firmware supersedes an old download and ignores its late progress, success, or failure', async () => {
  for (const outcome of ['success', 'failure']) {
    const oldLoad = deferred(); let loads = 0, oldProgress;
    const f = await fixture({chooseFirmware: async () => true, firmware: onProgress => {
      if (++loads === 1) { oldProgress = onProgress; return oldLoad.promise; }
      onProgress({stage: 'local', received: 1, total: 1});
      return new Uint8Array([2]);
    }, parsePackage: async bytes => ({version: `test-${bytes[0]}`})});
    f.click('choose-firmware'); await settle();
    assert.equal(f.ui.phase, 'ready');
    assert.equal(f.get('release').textContent, 'AWTRIX NG test-2 · Local firmware');
    oldProgress({stage: 'downloading', received: 1, total: 2});
    if (outcome === 'success') oldLoad.resolve(new Uint8Array([1]));
    else oldLoad.reject(new Error('Old download failed.'));
    await f.ui.ready; await settle();
    assert.equal(f.ui.phase, 'ready');
    assert.equal(f.get('release').textContent, 'AWTRIX NG test-2 · Local firmware');
    assert.equal(f.get('error').hidden, true);
    assert.equal(f.calls.filter(value => value && typeof value === 'object' && 'parse' in value).length, 1);
    assert.equal(f.calls.filter(value => value?.connect).length, 1);
    await f.ui.destroy();
  }
});

test('a superseded package parser cannot overwrite the selected bundle after it resolves late', async () => {
  const parsing = deferred(); let loads = 0;
  const f = await fixture({chooseFirmware: async () => true,
    firmware: async () => new Uint8Array([++loads]),
    parsePackage: async bytes => bytes[0] === 1 ? parsing.promise : {version: 'chosen'}});
  f.click('choose-firmware'); await settle();
  assert.equal(f.ui.phase, 'ready');
  parsing.resolve({version: 'obsolete'}); await f.ui.ready; await settle();
  assert.equal(f.instance.args[1].version, 'chosen');
  assert.match(f.get('release').textContent, /chosen/);
  assert.equal(f.calls.filter(value => value?.connect).length, 1);
  await f.ui.destroy();
});

test('choosing firmware aborts waiting USB and closes a late connection without using the old bundle', async () => {
  const connection = deferred(); let loads = 0, lateClosed = false;
  const f = await fixture({chooseFirmware: async () => true, connect: () => connection.promise,
    firmware: async () => {
      if (++loads === 1) return new Uint8Array([1]);
      throw {code: 'local-firmware', message: 'The selected firmware cannot be read.'};
    }});
  assert.equal(f.ui.phase, 'waiting');
  const original = f.calls.find(value => value?.connect).connect;
  f.click('choose-firmware'); await settle();
  assert.equal(original.signal.aborted, true);
  assert.equal(f.ui.phase, 'failed');
  const message = f.get('status').textContent;
  original.onStatus({message: 'Late USB status'});
  connection.resolve({identity, close: async () => { lateClosed = true; }}); await settle();
  assert.equal(lateClosed, true);
  assert.equal(f.get('status').textContent, message);
  assert.equal(f.root.querySelector('[data-action=retry]').hidden, true);
  assert.ok(!f.calls.includes('prepare'));
  assert.equal(f.calls.filter(value => value?.connect).length, 1);
  await f.ui.destroy();
});

test('cancelling a firmware choice while USB connects resumes that same prepared installation', async () => {
  const connection = deferred(), picker = deferred();
  const f = await fixture({connect: () => connection.promise, chooseFirmware: () => picker.promise});
  f.click('choose-firmware');
  connection.resolve(f.session); await settle();
  assert.equal(f.ui.phase, 'waiting');
  assert.ok(!f.calls.includes('prepare'));
  picker.resolve(false); await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.calls.filter(value => value === 'firmware').length, 1);
  assert.equal(f.calls.filter(value => value?.connect).length, 1);
  assert.ok(!f.calls.includes('close'));
  await f.ui.destroy();
});

test('an aborted USB discovery cannot fail the replacement installation when it rejects late', async () => {
  const oldConnection = deferred();
  const f = await fixture({chooseFirmware: async () => true,
    connect: async (options, count, session) => count === 1 ? oldConnection.promise : session});
  f.click('choose-firmware'); await settle();
  assert.equal(f.ui.phase, 'ready');
  oldConnection.reject(new Error('Old discovery was cancelled.')); await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(f.get('error').hidden, true);
  assert.ok(!f.calls.includes('dispose'));
  assert.equal(f.calls.filter(value => value === 'prepare').length, 1);
  await f.ui.destroy();
});

test('selected invalid firmware offers retry without falling back or starting USB', async () => {
  let loads = 0;
  const f = await fixture({chooseFirmware: async () => true, firmware: async onProgress => {
    if (++loads === 1) throw new Error('Network is unavailable.');
    onProgress({stage: 'local', received: 1, total: 1});
    return loads === 2 ? new Uint8Array() : new Uint8Array([1]);
  }});
  assert.equal(f.root.querySelector('[data-action=choose-firmware]').hidden, false);
  f.click('choose-firmware'); await settle();
  assert.equal(f.ui.phase, 'failed');
  assert.equal(loads, 2);
  assert.ok(!f.calls.some(value => value?.connect));
  f.click('download'); await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(loads, 3);
  assert.equal(f.calls.filter(value => value === 'choose-firmware').length, 1);
  await f.ui.destroy();
});

test('firmware selection is blocked once preparation begins and throughout verified or recovery states', async () => {
  const preparing = deferred();
  const f = await fixture({prepare: () => preparing.promise});
  const button = f.root.querySelector('[data-action=choose-firmware]');
  assert.equal(f.ui.phase, 'preparing');
  assert.equal(button.hidden, true); f.click('choose-firmware');
  preparing.resolve(); await settle();
  assert.equal(f.ui.phase, 'ready');
  assert.equal(button.hidden, true); f.click('choose-firmware');
  f.click('install');
  assert.equal(button.hidden, true); f.click('choose-firmware'); await settle();
  assert.equal(f.ui.phase, 'done');
  assert.equal(button.hidden, true); f.click('choose-firmware');
  assert.ok(!f.calls.includes('choose-firmware'));
  await f.ui.destroy();
  const recovery = await fixture({interrupt: true});
  recovery.click('install'); await settle();
  assert.equal(recovery.ui.phase, 'recovery');
  assert.equal(recovery.root.querySelector('[data-action=choose-firmware]').hidden, true);
  recovery.click('choose-firmware');
  assert.ok(!recovery.calls.includes('choose-firmware'));
  recovery.click('retry'); await settle(); await recovery.ui.destroy();
});

test('desktop UI waits for durable credentials and the requested network before declaring Wi-Fi success', async () => {
  let polls = 0;
  const f = await fixture({control: async command => {
    if (command === 'wifi-set') return {ok: true};
    if (command === 'wifi-status') {
      polls++;
      if (polls === 1) return {link: 'connected', ssid: 'Example network', store: 'saving', error: ''};
      if (polls === 2) return {link: 'connected', ssid: 'Old network', store: 'ok', error: ''};
      return {link: 'connected', ssid: 'Example network', store: 'ok', error: ''};
    }
    if (command === 'status') return {network: {link: 'connected', ssid: 'Example network', ipv4: '192.168.1.27'}};
  }});
  f.wifi(); f.click('install'); await settle(); await settle();
  assert.equal(f.ui.phase, 'done');
  assert.equal(polls, 3);
  assert.equal(f.calls.filter(call => call?.command === 'status').length, 1);
  assert.equal(f.get('ap-steps').hidden, true);
  await f.ui.destroy();
});

test('desktop UI reports a credential-store failure safely even when an old network is connected', async () => {
  const f = await fixture({control: async command => command === 'wifi-set' ? {ok: true} :
    {link: 'connected', ssid: 'Old network', store: 'ok', error: 'credential store write failed: synthetic-secret'}});
  f.wifi(); f.click('install'); await settle(); await settle();
  assert.equal(f.ui.phase, 'done');
  assert.equal(f.get('ap-steps').hidden, false);
  assert.match(f.get('wifi-result').textContent, /could not store/);
  assert.doesNotMatch(f.root.textContent, /synthetic-secret/);
  assert.ok(!f.calls.some(call => call?.command === 'status'));
  await f.ui.destroy();
});

test('desktop UI ignores a late network-status response after Wi-Fi setup was skipped', async () => {
  const reply = deferred();
  const f = await fixture({control: async command => {
    if (command === 'wifi-set') return {ok: true};
    if (command === 'wifi-status') return {link: 'connected', ssid: 'Example network', store: 'ok', error: ''};
    return reply.promise;
  }});
  f.wifi(); f.click('install'); await settle();
  assert.equal(f.ui.phase, 'provisioning');
  f.click('skip-wifi'); await settle();
  reply.resolve({network: {link: 'connected', ssid: 'Example network', ipv4: '192.168.1.27'}}); await settle();
  assert.equal(f.ui.phase, 'done');
  assert.equal(f.get('ap-steps').hidden, false);
  assert.match(f.get('wifi-result').textContent, /Continue Wi-Fi setup/);
  await f.ui.destroy();
});

test('desktop UI rides out a dropped session and a busy clock before saving Wi-Fi', async () => {
  let sets = 0;
  const f = await fixture({control: async (command, payload) => {
    if (command === 'wifi-set') {
      sets++;
      if (sets === 1) throw Object.assign(new Error('USB is disconnected.'), {code: 'disconnected'});
      if (sets === 2) return {ok: false, error: 'busy'};
      return {ok: true};
    }
    if (command === 'wifi-status') return {link: 'connected', ssid: 'Example network', store: 'ok', error: ''};
    return {network: {link: 'connected', ssid: 'Example network', ipv4: '192.168.1.27'}};
  }});
  f.wifi(); f.click('install'); for (let i = 0; i < 10; i++) await settle();
  assert.equal(f.ui.phase, 'done');
  assert.equal(sets, 3);
  assert.ok(f.calls.filter(call => call === 'close').length >= 1);
  assert.equal(f.root.querySelector('[data-action=open-clock]').hidden, false);
  assert.equal(f.get('wifi').hidden, true);
  await f.ui.destroy();
});

test('desktop UI names a wrong password and resends corrected Wi-Fi over USB without restarting again', async () => {
  let attempt = 0;
  const f = await fixture({control: async (command, payload) => {
    if (command === 'wifi-set') { attempt++; return {ok: true}; }
    if (command === 'wifi-status') return attempt === 1 ? {link: 'failed', ssid: 'Example network', store: 'ok', error: ''}
      : {link: 'connected', ssid: 'Example network', store: 'ok', error: ''};
    return {network: {link: 'connected', ssid: 'Example network', ipv4: '192.168.1.27'}};
  }});
  f.wifi(); f.click('install'); for (let i = 0; i < 10; i++) await settle();
  assert.equal(f.ui.phase, 'done');
  assert.match(f.get('wifi-result').textContent, /Check the password/);
  assert.equal(f.get('wifi-form').elements.ssid.value, 'Example network');
  f.get('wifi-form').elements.password.value = 'corrected-secret';
  f.click('send-wifi'); for (let i = 0; i < 10; i++) await settle();
  assert.equal(f.calls.filter(call => call === 'reboot').length, 1);
  assert.deepEqual(f.calls.filter(call => call?.command === 'wifi-set').at(-1).payload, {ssid: 'Example network', password: 'corrected-secret'});
  assert.equal(f.root.querySelector('[data-action=open-clock]').hidden, false);
  assert.doesNotMatch(f.root.textContent, /corrected-secret/);
  await f.ui.destroy();
});
