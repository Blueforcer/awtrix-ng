import {test} from 'node:test';
import assert from 'node:assert/strict';
import {PassThrough} from 'node:stream';
import {readFile} from 'node:fs/promises';
import {runInstaller} from '../cli/runner.mjs';
import {createRpc} from '../cli/rpc.mjs';
import {loadImageTools} from '../../../../docs/assets/tc002/image/node.mjs';
import {provisionWifi} from '../../../../docs/assets/tc002/install.js';

const identity = {vendorId: 0x18d1, productId: 0xd002, serialNumber: 'synthetic'};
async function fixture({answers = ['', 'INSTALL'], interrupt = false, downloadFailure = false, firmware, parseFailure = false, control} = {}) {
  const calls = [], messages = [], prompts = [], instances = [];
  let downloads = 0;
  const session = {identity, async close() { calls.push('close'); }, async controlRequest(command, payload) {
    calls.push({command, payload: payload ? {...payload} : undefined});
    if (control) return control(command, payload);
    if (command === 'wifi-set') return {ok: true};
    if (command === 'wifi-status') return {link: 'connected', ssid: 'Home', store: 'ok', error: ''};
    return {network: {link: 'connected', ssid: 'Home', ipv4: '192.168.1.25'}};
  }};
  class Installer {
    constructor() { instances.push(this); this.prepared = {image: new Uint8Array([1])}; }
    async prepare() { calls.push('prepare'); }
    async install() {
      this.writeStarted = true; calls.push('install');
      if (interrupt && calls.filter(call => call === 'install').length === 1) throw new Error('Synthetic interrupted write');
      this.verified = true;
    }
    async dispose() { calls.push('dispose'); }
    async reboot() { calls.push('reboot'); }
  }
  const deps = {
    desktop: {async phase(value) { calls.push({phase: value}); }, async firmware(onProgress) {
      downloads++; calls.push('firmware');
      if (downloadFailure && downloads === 1) throw {code: 'no-compatible-release', message: 'Missing asset'};
      return firmware ? firmware(onProgress) : new Uint8Array([1]);
    }},
    usb: {}, Tc002Installer: Installer, async preload() { calls.push('preload'); },
    async parsePackage() { calls.push('parse'); if (parseFailure) throw new Error('Invalid firmware package.'); return {version: '1.2.3'}; },
    async connectGranted(options) { calls.push({connect: options.identity}); return session; },
    buildResImage() {}, validateWifi(ssid, password) { if (password && password.length < 8) throw new Error('Password is too short.'); },
    async prompt(text, secret) { prompts.push({text, secret}); if (!answers.length) throw new Error('Unexpected prompt'); return answers.shift(); },
    async message(text) { messages.push(text); }, async sleep() {}, provisionWifi,
  };
  const result = await runInstaller(deps);
  return {result, calls, messages, prompts, instances};
}

test('terminal install reuses the shared engine, requires INSTALL and restarts the verified clock', async () => {
  const f = await fixture();
  assert.equal(f.result.installed, true); assert.equal(f.result.restarted, true);
  assert.equal(f.instances.length, 1);
  assert.equal(f.calls.filter(call => call === 'install').length, 1);
  assert.ok(f.calls.findIndex(call => call?.phase === 'installing') < f.calls.indexOf('install'));
  assert.ok(f.calls.findIndex(call => call?.phase === 'verified') < f.calls.indexOf('reboot'));
  assert.match(f.messages.at(-1), /192\.168\.4\.1/);
  assert.equal(f.prompts[1].text.includes('Type INSTALL'), true);
});

test('terminal install never flashes on a missing or differently cased confirmation', async () => {
  const f = await fixture({answers: ['', 'install']});
  assert.equal(f.result.cancelled, true);
  assert.ok(!f.calls.includes('install')); assert.ok(!f.calls.includes('reboot'));
  assert.ok(f.calls.includes('dispose'));
});

test('terminal optional Wi-Fi uses a hidden password prompt and the same post-reboot provisioning contract', async () => {
  const f = await fixture({answers: ['Home', 'synthetic-secret', 'INSTALL']});
  assert.equal(f.result.ip, '192.168.1.25');
  assert.equal(f.prompts[1].secret, true);
  assert.ok(f.calls.findIndex(call => call?.command === 'wifi-set') > f.calls.indexOf('reboot'));
  assert.deepEqual(f.calls.find(call => call?.command === 'wifi-set').payload, {ssid: 'Home', password: 'synthetic-secret'});
  assert.ok(f.messages.every(text => !text.includes('synthetic-secret')));
});

test('terminal recovery preserves the exact prepared instance and reconnects only its identity', async () => {
  const f = await fixture({interrupt: true, answers: ['', 'INSTALL', '']});
  assert.equal(f.result.installed, true);
  assert.equal(f.instances.length, 1);
  assert.equal(f.calls.filter(call => call === 'prepare').length, 1);
  assert.equal(f.calls.filter(call => call === 'install').length, 2);
  assert.deepEqual(f.calls.filter(call => call && typeof call === 'object' && 'connect' in call)[1].connect, identity);
  assert.ok(f.messages.some(text => text.includes('Do not restart')));
});

test('terminal retries downloading explicitly and never connects before a valid package exists', async () => {
  const f = await fixture({downloadFailure: true, answers: ['y', '', 'INSTALL']});
  assert.equal(f.calls.filter(call => call === 'firmware').length, 2);
  assert.ok(f.calls.lastIndexOf('firmware') < f.calls.findIndex(call => call && typeof call === 'object' && 'connect' in call));
  assert.ok(f.messages.some(value => /No compatible TC002 firmware has been published/.test(value)));
  assert.equal(f.result.restarted, true);
});

test('terminal identifies local firmware while using the same checked package and explicit confirmation', async () => {
  const f = await fixture({firmware: async onProgress => {
    onProgress({stage: 'local', received: 0, total: 1});
    onProgress({stage: 'local', received: 1, total: 1});
    return new Uint8Array([1]);
  }});
  assert.equal(f.result.installed, true);
  assert.ok(f.messages.some(value => /Loading local TC002 firmware/.test(value)));
  assert.ok(f.messages.some(value => /Local firmware checked: 1\.2\.3/.test(value)));
  assert.ok(f.messages.every(value => !/GitHub|latest|download/i.test(value)));
  assert.ok(f.calls.indexOf('parse') < f.calls.findIndex(value => value && typeof value === 'object' && 'connect' in value));
  assert.ok(f.prompts.some(value => value.text.includes('Type INSTALL')));
});

test('terminal stops before USB and gives local-file guidance for unreadable or invalid sidecars', async () => {
  for (const options of [{firmware: async () => { throw {code: 'local-firmware', message: 'Local firmware: file is unreadable.'}; }},
    {parseFailure: true, firmware: async onProgress => { onProgress({stage: 'local', received: 1, total: 1}); return new Uint8Array([1]); }}]) {
    const f = await fixture({...options, answers: ['n']});
    assert.equal(f.result.cancelled, true);
    assert.ok(!f.calls.some(value => value && typeof value === 'object' && 'connect' in value));
    assert.equal(f.calls.filter(value => value === 'firmware').length, 1);
    assert.ok(f.messages.some(value => /Check the local firmware ZIP/.test(value)));
    assert.ok(f.messages.every(value => !/internet connection|Checking GitHub|Downloading/.test(value)));
    assert.equal(f.prompts.at(-1).text, 'Retry firmware? [y/N] ');
  }
});

test('terminal explicit QUIT during recovery reports incompleteness and never disposes its image or reboots', async () => {
  const f = await fixture({interrupt: true, answers: ['', 'INSTALL', 'QUIT']});
  assert.equal(f.result.recoveryNeeded, true);
  assert.ok(!f.calls.includes('reboot')); assert.ok(!f.calls.includes('dispose'));
  assert.equal(f.instances[0].prepared.image[0], 1);
});

test('terminal EOF during recovery returns an incomplete result without an input retry loop', async () => {
  const f = await fixture({interrupt: true, answers: ['', 'INSTALL']});
  assert.equal(f.result.recoveryNeeded, true);
  assert.equal(f.prompts.length, 3);
  assert.match(f.messages.at(-1), /Terminal input is closed/);
  assert.ok(!f.calls.includes('dispose')); assert.ok(!f.calls.includes('reboot'));
});

test('terminal names a failed Wi-Fi join and resends corrected details without another restart', async () => {
  let sets = 0;
  const f = await fixture({answers: ['Home', 'wrong-secret', 'INSTALL', 'y', 'Home', 'right-secret'], control: async command => {
    if (command === 'wifi-set') { sets++; return {ok: true}; }
    if (command === 'wifi-status') return sets === 1 ? {link: 'failed', ssid: 'Home', store: 'ok', error: ''} : {link: 'connected', ssid: 'Home', store: 'ok', error: ''};
    return {network: {link: 'connected', ssid: 'Home', ipv4: '192.168.1.25'}};
  }});
  assert.equal(f.result.ip, '192.168.1.25');
  assert.equal(f.calls.filter(call => call === 'reboot').length, 1);
  assert.ok(f.messages.some(text => /Check the password/.test(text)));
  assert.ok(f.prompts.some(value => value.text.includes('Try Wi-Fi again')));
  assert.ok(f.messages.every(text => !/wrong-secret|right-secret/.test(text)));
});

test('terminal RPC keeps stdout as JSON, routes out-of-order results and preserves binary session headers', async () => {
  const input = new PassThrough(), output = new PassThrough(); let written = '';
  output.on('data', bytes => { written += bytes; });
  const rpc = createRpc({input, output});
  const first = rpc.invoke('usb_list');
  const second = rpc.invoke('usb_transfer_out', new Uint8Array([0, 255, 7]), {headers: {'x-device-id': 'device-1', 'x-session-id': 'session-2', 'x-endpoint': '2'}});
  const sent = written.trim().split('\n').map(JSON.parse);
  assert.equal(sent.length, 2);
  assert.deepEqual(sent[1].args, {id: 'device-1', sessionId: 'session-2', endpoint: 2});
  assert.equal(sent[1].bytes, 'AP8H');
  input.write(JSON.stringify({requestId: sent[1].requestId, result: 3}) + '\n');
  input.write(JSON.stringify({requestId: sent[0].requestId, result: []}) + '\n');
  assert.equal(await second, 3); assert.deepEqual(await first, []);
  const binary = rpc.invoke('firmware');
  const last = JSON.parse(written.trim().split('\n').at(-1));
  input.write(JSON.stringify({requestId: last.requestId, bytes: 'AQID'}) + '\n');
  assert.deepEqual([...new Uint8Array(await binary)], [1, 2, 3]);
  rpc.close();
});

test('terminal RPC routes hotplug events and rejects pending requests on native EOF', async () => {
  const input = new PassThrough(), output = new PassThrough(), rpc = createRpc({input, output});
  const events = [];
  const unlisten = await rpc.listen('usb-event', value => events.push(value.payload));
  input.write(JSON.stringify({event: 'usb-event', payload: {kind: 'connect'}}) + '\n');
  assert.deepEqual(events, [{kind: 'connect'}]);
  unlisten();
  const pending = rpc.invoke('usb_list');
  const rejected = assert.rejects(pending, {code: 'disconnected'});
  input.end(); await rejected; rpc.close();
});

test('terminal RPC accepts a real firmware-sized binary response without regular-expression stack exhaustion', async () => {
  const input = new PassThrough(), output = new PassThrough(), rpc = createRpc({input, output});
  let written = ''; output.on('data', data => { written += data; });
  const pending = rpc.invoke('firmware');
  const request = JSON.parse(written);
  const bytes = Buffer.alloc(6 * 1024 * 1024, 0x7b);
  input.write(JSON.stringify({requestId: request.requestId, bytes: bytes.toString('base64')}) + '\n');
  const result = new Uint8Array(await pending);
  assert.equal(result.length, bytes.length); assert.equal(result.at(-1), 0x7b);
  rpc.close();
});

test('terminal image preflight and builder run the real shared WASM tools without a browser', async () => {
  const base = new URL('../../../../docs/assets/tc002/image/', import.meta.url);
  const tools = await loadImageTools(base);
  const {buildImage, superblock, sha256} = await import(new URL('core.js', base));
  const stockRes = new Uint8Array(await readFile(new URL('../../browser-image/fixtures/stock.sqfs', import.meta.url)));
  const loader = new Uint8Array(64); loader.set([127, 69, 76, 70, 1, 1, 1]); loader[16] = 3; loader[18] = 40;
  const loop = new Uint8Array(80); loop.set(loader); loop[16] = 1; loop.set(new TextEncoder().encode('vermagic=3.18.30'), 64);
  const stages = [];
  const image = await buildImage({stockRes, loader, loop}, stage => stages.push(stage), tools);
  assert.deepEqual(stages, ['reading', 'preparing', 'compressing', 'verifying']);
  assert.equal(image.imageSha256, await sha256(image.image));
  assert.equal(superblock(image.image).time, superblock(stockRes).time);
  assert.ok(image.image.length < 8 * 1024 * 1024);
  assert.equal(image.slot.image + image.slot.capacity, 8 * 1024 * 1024);
  assert.ok(image.slot.header >= superblock(image.image).used && image.slot.header % 65536 === 0);
});
