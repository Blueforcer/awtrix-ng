const {test} = require('node:test');
const assert = require('node:assert/strict');
const {webcrypto} = require('node:crypto');
globalThis.crypto ||= webcrypto;
const modulePromise = import('../../docs/assets/tc002/install.js');
const encoder = new TextEncoder();
const decoder = new TextDecoder();
const RES = '/dev/mtd/mtd3';
const BOOT = '11111111-2222-3333-4444-555555555555';
const RELEASE = '1.2.0-abcdef123456';
const COUNTER = '1790000000';

class FakeClock {
  constructor() {
    this.res = new Uint8Array(8 * 1024 * 1024).fill(19);
    this.files = new Map([
      ['/data/setting.ini', encoder.encode('name=clock\r\nauthToken=PRIVATE\r\nwifiPwd=PRIVATE\r\nvolume=15\r\n')],
      ['/data/misc/wifi/wpa_supplicant.conf', encoder.encode('network={\npsk="PRIVATE"\n}\n')],
      ['/etc/wifi/wpa_supplicant.conf', encoder.encode('ctrl_interface=/tmp/wpa\n')],
    ]);
    this.properties = new Map([['ro.product.model', 'Zkswe_SSD21X_SPINOR'],
      ['init.svc.zkswe', 'running'], ['init.svc.hciattach', 'running'], ['init.svc.wpa_supplicant', 'running']]);
    this.writes = [];
    this.commands = [];
    this.flashWrites = 0;
    this.slotWrites = 0;
    this.slot = null;
    this.slotReply = {};
    this.slotInfo = {};
    this.tmpFree = 12 * 1024 * 1024;
    this.reboots = 0;
    this.boot = BOOT;
  }
  async writeFile(path, data, {mode}) { assert.ok(Number.isInteger(mode) && mode >= 0 && mode <= 0o777); this.writes.push(path); this.files.set(path, data.slice()); }
  async readFile(path, {maxBytes}) {
    const data = path === RES ? this.res : this.files.get(path);
    assert.ok(data, `fixture file missing: ${path}`);
    assert.ok(data.length <= maxBytes);
    return data.slice();
  }
  async close() {}
  async helper(op, args) {
    const {sha256} = await modulePromise;
    const result = {command: op, result: 'ok'};
    if (op === 'statfs') return {...result, free_bytes: this.tmpFree};
    if (op === 'hash') {
      const bytes = args[0] === RES ? this.res : this.files.get(args[0]);
      assert.ok(bytes, args[0]);
      const length = args[1] ? Number(args[1]) : bytes.length;
      const reply = {...result, size: bytes.length, length, sha256: await sha256(bytes.subarray(0, length))};
      return args[0] === RES ? {...reply, name: 'res', erase_size: 65536} : reply;
    }
    if (op === 'write') {
      this.flashWrites++;
      const bytes = this.files.get(args[1]);
      const offset = Number(args[3]);
      this.res.set(bytes, offset);
      if (this.failFlash) throw new Error('USB disconnected');
      return {...result, name: 'res', offset, length: bytes.length, sha256: await sha256(bytes)};
    }
    if (op === 'write-slot') {
      const options = Object.fromEntries(args.slice(1).map((value, index, list) => index % 2 ? null : [value, list[index + 1]]).filter(Boolean));
      const source = this.files.get(args[0]);
      const length = Number(options['--length']);
      assert.ok(source && source.length === length && await sha256(source) === options['--sha256']);
      this.slotWrites++;
      if (this.failSlot) throw new Error('USB disconnected');
      this.slot = {release: options['--release'], counter: Number(options['--counter']), imageBytes: length, sha256: options['--sha256']};
      return {...result, image: 1179648, blocks: 5, skipped: 0, written: 5, ...this.slot, ...this.slotReply};
    }
    assert.equal(op, 'slot-info');
    assert.deepEqual(args, ['--verify']);
    return {...result, header: 1114112, image: 1179648, capacity: 7208960, slot: this.slot ? 'verified' : 'empty', ...this.slot, ...this.slotInfo};
  }
  async shell(wrapped) {
    this.commands.push(wrapped);
    if (wrapped === 'sync; reboot') { this.reboots++; return ''; }
    const match = /^\( ([\s\S]+) \); r=\$\?; echo; echo (AWTRIX_\d+_RESULT):\$r$/.exec(wrapped);
    assert.ok(match, wrapped);
    const command = match[1];
    let out = '';
    if (command === 'cat /proc/mtd') out = 'dev: size erasesize name\nmtd3: 00800000 00010000 "res"';
    else if (command === 'cat /proc/sys/kernel/random/boot_id') out = this.boot;
    else if (command.startsWith('getprop ')) out = this.properties.get(command.slice(8)) || '';
    else if (command.startsWith('setprop ctl.stop ')) this.properties.set(`init.svc.${command.slice(17)}`, 'stopped');
    else if (command.startsWith('setprop persist.')) {
      const [, name, value] = command.split(' ');
      this.properties.set(name, value);
      this.files.set(`/data/property/${name}`, encoder.encode(value));
    } else if (command.startsWith('cat /data/property/')) out = decoder.decode(this.files.get(command.slice(4)));
    else if (command.startsWith('ls -ln ')) out = '-rw-rw---- 1 1000 1000 27 Sep 26 00:00 settings';
    else if (command.startsWith('if [ -')) {
      const file = /^if \[ -[fLd] (\S+) /.exec(command)[1];
      out = this.files.has(file) ? 'yes' : 'no';
    } else if (/\/flash (statfs|hash|write|write-slot|slot-info) /.test(command)) {
      const [, op, ...args] = command.split(' ');
      out = JSON.stringify(await this.helper(op, args));
    } else if (command.includes(' && cp ')) {
      const [, source, target] = / && cp (\S+) (\S+)/.exec(command);
      this.files.set(target, this.files.get(source).slice());
    } else if (command.includes(' && mv -f ')) {
      const [, source, target] = / && mv -f (\S+) (\S+)/.exec(command);
      this.files.set(target, this.files.get(source).slice());
      this.files.delete(source);
    } else if (command.startsWith('rm -f ')) {
      for (const name of command.split(';')[0].split(' ').slice(2)) this.files.delete(name);
    } else if (!/^(mkdir -p |chmod 700 |test ! -L |sync$)/.test(command)) {
      assert.fail(`unhandled fixture command: ${command}`);
    }
    return `${out}\n${match[2]}:0\n`;
  }
}

async function setup({capacity} = {}) {
  const {Tc002Installer, sha256} = await modulePromise;
  const device = new FakeClock();
  const release = new Uint8Array(300 * 1024).fill(42);
  const bundle = {helper: new Uint8Array([1, 2, 3]), loader: new Uint8Array([4, 5]), loop: new Uint8Array([6, 7]),
    image: release, version: '1.2.0', release: RELEASE, counter: COUNTER};
  const messages = [], progress = [], builds = [];
  const buildImage = async input => {
    builds.push(input);
    const image = new Uint8Array(1024 * 1024 + 8192).fill(73);
    return {image, stockSha256: await sha256(input.stockRes), imageSha256: await sha256(image),
      slot: {header: 1114112, image: 1179648, capacity: capacity ?? 7208960}};
  };
  const installer = new Tc002Installer(device, bundle, buildImage,
    {sleep: async () => {}, onStatus: msg => messages.push(msg), onProgress: value => progress.push(value), id: 'a'.repeat(24)});
  return {device, installer, messages, progress, builds, bundle, releaseSha256: await sha256(release)};
}

test('first installation reads only res, writes the res image and the release slot, and reboots only after success', async () => {
  const {device, installer, messages, progress, builds, bundle, releaseSha256} = await setup();
  await installer.prepare();
  assert.equal(builds[0].loader, bundle.loader);
  assert.equal(builds[0].loop, bundle.loop);
  assert.equal(device.flashWrites, 0);
  assert.equal(device.slotWrites, 0);
  assert.equal(device.reboots, 0);
  assert.ok(device.writes.every(name => name.startsWith('/tmp/')));
  progress.length = 0;
  await installer.install();
  assert.equal(device.flashWrites, 2);
  assert.equal(device.slotWrites, 1);
  assert.deepEqual(device.slot, {release: RELEASE, counter: Number(COUNTER), imageBytes: bundle.image.length, sha256: releaseSha256});
  assert.ok(device.writes.includes('/tmp/awtrix-browser-aaaaaaaaaaaaaaaaaaaaaaaa/release.img'));
  assert.ok(![...device.files.keys()].some(name => name.endsWith('/release.img')));
  assert.deepEqual(JSON.parse(decoder.decode(device.files.get('/data/awtrix-ng/state/res-image.json'))),
    {sha256: installer.prepared.imageSha256, bytes: installer.prepared.image.length});
  assert.deepEqual(progress, [...progress].sort((a, b) => a - b));
  assert.equal(progress.at(-1), 1);
  assert.equal(device.reboots, 0);
  assert.equal(installer.verified, true);
  await installer.reboot();
  assert.equal(device.reboots, 1);
  assert.equal(decoder.decode(device.files.get('/data/setting.ini')), 'name=clock\r\nvolume=15\r\n');
  assert.equal(decoder.decode(device.files.get('/data/misc/wifi/wpa_supplicant.conf')), 'ctrl_interface=/tmp/wpa\n');
  assert.ok(!messages.join(' ').includes('PRIVATE'));
  assert.ok(!device.commands.join(' ').includes('PRIVATE'));
  assert.ok(!device.commands.join(' ').includes('mtd0'));
});

test('wrong model and existing AWTRIX are rejected before uploading a helper', async () => {
  for (const existing of ['', '/res/lib/libawtrix-loader.so', '/data/awtrix-ng/current']) {
    const {device, installer} = await setup();
    if (existing) device.files.set(existing, new Uint8Array());
    else device.properties.set('ro.product.model', 'different-device');
    await assert.rejects(installer.prepare(), existing ? /already installed/ : /not a supported/);
    assert.equal(device.writes.length, 0);
    assert.equal(device.flashWrites, 0);
  }
});

test('a release image larger than the slot of the built res image is refused before anything is written', async () => {
  const {device, installer, bundle} = await setup({capacity: 300 * 1024 - 1});
  await assert.rejects(installer.prepare(), /does not fit/);
  assert.equal(installer.prepared, null);
  await assert.rejects(installer.install(), /No installation is ready/);
  assert.equal(device.flashWrites, 0);
  assert.equal(device.slotWrites, 0);
  assert.equal(device.properties.get('init.svc.zkswe'), 'running');
  assert.equal(bundle.image.length, 300 * 1024);
});

test('changed source partition prevents every flash write', async () => {
  const {device, installer} = await setup();
  await installer.prepare();
  device.res[999] ^= 1;
  await assert.rejects(installer.install(), /changed/);
  assert.equal(device.flashWrites, 0);
  assert.equal(device.slotWrites, 0);
  assert.equal(device.properties.get('init.svc.zkswe'), 'running');
});

test('interrupted write prohibits reboot and allows explicit retry in the same boot', async () => {
  const {device, installer} = await setup();
  await installer.prepare();
  device.failFlash = true;
  await assert.rejects(installer.install(), error => error.recoveryNeeded === true);
  await assert.rejects(installer.reboot(), /not been verified/);
  assert.equal(device.reboots, 0);
  device.failFlash = false;
  await installer.install(device);
  assert.equal(installer.verified, true);
});

test('a write-slot reply that names another release needs recovery; a retry writes the slot again', async () => {
  for (const reply of [{release: '1.2.0-000000000000'}, {counter: 1790000001}, {imageBytes: 300 * 1024 + 1}, {sha256: '0'.repeat(64)}, {release: undefined}]) {
    const {device, installer} = await setup();
    await installer.prepare();
    device.slotReply = reply;
    await assert.rejects(installer.install(), error => error.recoveryNeeded === true && /could not be verified/.test(error.message));
    await assert.rejects(installer.reboot(), /not been verified/);
    assert.equal(device.files.has('/data/awtrix-ng/state/res-image.json'), false);
    device.slotReply = {};
    await installer.install(device);
    assert.equal(installer.verified, true);
    assert.equal(device.slotWrites, 2);
  }
});

test('a release slot that does not verify after writing needs recovery', async () => {
  for (const info of [{slot: 'valid'}, {slot: 'corrupt'}, {release: '1.2.0-000000000000'}, {counter: 1}]) {
    const {device, installer} = await setup();
    await installer.prepare();
    device.slotInfo = info;
    await assert.rejects(installer.install(), error => error.recoveryNeeded === true && /final verification/.test(error.message));
    assert.equal(installer.verified, false);
  }
});

test('too little /tmp for the release image and a lost USB link during the slot write need recovery', async () => {
  const {device, installer} = await setup();
  await installer.prepare();
  device.tmpFree = 300 * 1024 + 2 * 1024 * 1024 - 1;
  await assert.rejects(installer.install(), error => error.recoveryNeeded === true && /temporary memory/.test(error.message));
  assert.ok(!device.writes.some(name => name.endsWith('/release.img')));
  assert.equal(device.slotWrites, 0);
  device.tmpFree = 12 * 1024 * 1024;
  device.failSlot = true;
  await assert.rejects(installer.install(), error => error.recoveryNeeded === true);
  device.failSlot = false;
  await installer.install();
  assert.equal(installer.verified, true);
  assert.ok(![...device.files.keys()].some(name => name.endsWith('/release.img')));
});

test('retry cannot silently act on another device startup', async () => {
  const {device, installer} = await setup();
  await installer.prepare();
  device.failFlash = true;
  await assert.rejects(installer.install());
  const before = device.flashWrites;
  device.boot = '99999999-2222-3333-4444-555555555555';
  device.failFlash = false;
  await assert.rejects(installer.install(), /restarted/);
  assert.equal(device.flashWrites, before);
});

test('partition geometry, settings preservation and Wi-Fi validation', async () => {
  const {parsePartition, stripVendorSettings, validateWifi} = await modulePromise;
  assert.throws(() => parsePartition('mtd3: 00400000 00010000 "res"'));
  assert.throws(() => parsePartition('mtd3: 00800000 00010000 "res"\nmtd3: 00800000 00010000 "res"'));
  const bytes = new Uint8Array([...encoder.encode('; wifiPwd=keep\nname='), 255, 254, ...encoder.encode('\n wifiPwd = remove\n')]);
  assert.deepEqual(stripVendorSettings(bytes), bytes.subarray(0, bytes.length - ' wifiPwd = remove\n'.length));
  assert.doesNotThrow(() => validateWifi('my-network', 'correct horse battery staple'));
  assert.doesNotThrow(() => validateWifi('open', ''));
  assert.throws(() => validateWifi('', 'abcdefgh'));
  assert.throws(() => validateWifi('wifi', 'short'));
  assert.throws(() => validateWifi('wifi', 'secret\n123'));
  assert.doesNotThrow(() => validateWifi('é'.repeat(16), '0123456789abcdef'.repeat(4)));
  for (const ssid of ['x'.repeat(33), 'é'.repeat(17)]) assert.throws(() => validateWifi(ssid, 'abcdefgh'));
  for (const password of ['x'.repeat(64), 'g'.repeat(64), 'tab\there!!']) assert.throws(() => validateWifi('Home', password));
  const {displaySsid} = await modulePromise;
  assert.equal(displaySsid('Café "5G"'), 'Café "5G"');
  assert.equal(displaySsid('é\\\x7f'), '\\xc3\\xa9\\\\\\x7f');
  assert.equal(displaySsid('a\u0085'), 'a\\xc2\\x85');
});

test('vendor inspection preserves non-secret bytes, comments and repeated settings', async () => {
  const {inspectVendorSettings, parseAttributes} = await modulePromise;
  const input = encoder.encode('\ufeffwifiPwd=x\r\n; wifiPwd=comment\r\n[main]\r\n  secretKey = y\r\nvolume=1\r\nwifiSsid=a\r\nwifiSsid=b');
  const result = inspectVendorSettings(input);
  assert.equal(decoder.decode(result.bytes), '; wifiPwd=comment\r\n[main]\r\nvolume=1\r\n');
  assert.deepEqual(result.removed, ['wifiPwd', 'secretKey', 'wifiSsid']);
  assert.deepEqual(result.kept, ['volume']);
  assert.deepEqual(inspectVendorSettings(result.bytes).bytes, result.bytes);
  assert.equal(decoder.decode(inspectVendorSettings(encoder.encode('name=clock\rwifiPwd=secret\rvolume=1')).bytes), 'name=clock\rvolume=1');
  assert.deepEqual(parseAttributes('-rwxr-x--- 1 1000 1010 7 Sep 25 00:11 /x'), {mode: '0750', uid: '1000', gid: '1010'});
  for (const value of ['lrwxrwxrwx 1 0 0 7 May 26 02:48 /x -> y', '-rw-r--r-- 1 root root 553 Sep 25 00:11 /x',
    '-rwsr-xr-x 1 0 0 553 Sep 25 00:11 /x', '']) assert.throws(() => parseAttributes(value));
});

test('vendor dry-run and failed private backup cannot write settings or properties', async () => {
  const {device, installer} = await setup();
  const before = new Map([...device.files].map(([path, bytes]) => [path, bytes.slice()]));
  const result = await installer.secureVendor({apply: false, backup: () => assert.fail('dry-run backup')});
  assert.deepEqual(result, {changed: true, backup: null});
  assert.deepEqual(device.files, before);
  await assert.rejects(installer.secureVendor({backup: () => { throw new Error('backup unavailable'); }}), /backup unavailable/);
  assert.deepEqual(device.files, before);
  assert.equal(device.writes.length, 0);
  assert.ok(!device.commands.some(command => command.includes('setprop persist.')));
});
