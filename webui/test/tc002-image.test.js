const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {webcrypto} = require('node:crypto');
globalThis.crypto ||= webcrypto;
const encoder = new TextEncoder();
const base = path.resolve(__dirname, '../../docs/assets/tc002/image');
const core = import('../../docs/assets/tc002/image/core.js');
const tar = import('../../docs/assets/tc002/image/tar.js');
const tools = Promise.all(['sqfs2tar', 'tar2sqfs'].map(async name => ({
  factory: (await import(`../../docs/assets/tc002/image/${name}.js`)).default,
  bytes: new Uint8Array(await fs.readFile(path.join(base, `${name}.wasm`))),
})));

function elf(type, extra = '') {
  const data = new Uint8Array(64 + extra.length);
  data.set([127, 69, 76, 70, 1, 1, 1]);
  data[16] = type;
  data[18] = 40;
  data.set(encoder.encode(extra), 64);
  return data;
}

const loader = () => elf(3);
const loop = () => elf(1, 'vermagic=3.18.30 mod_unload ARMv7 ');

async function input() {
  return {
    stockRes: new Uint8Array(await fs.readFile(path.resolve(__dirname, '../../tools/tc002/browser-image/fixtures/stock.sqfs'))),
    loader: loader(),
    loop: loop(),
  };
}

function entry(name, type = '0', data = '') {
  return {path: name, type, mode: type === '5' ? 0o755 : 0o644, uid: 1000, gid: 1000, mtime: 1500000000, link: '', data: encoder.encode(data)};
}

function tree(config = '{"startupLibPath":"/res/lib/libzkgui.so"}') {
  return new Map([
    ['.', entry('.', '5')], ['etc', entry('etc', '5')], ['lib', entry('lib', '5')],
    ['etc/EasyUI.cfg', entry('etc/EasyUI.cfg', '0', config)],
  ]);
}

test('TC002 image: real WASM XZ roundtrip is deterministic and keeps input bytes', async () => {
  const {buildImage, superblock} = await core;
  const source = await input();
  const before = source.stockRes.slice();
  const stages = [];
  const first = await buildImage(source, stage => stages.push(stage), await tools);
  const second = await buildImage(source, undefined, await tools);
  assert.deepEqual(first.image, second.image);
  assert.equal(first.imageSha256, second.imageSha256);
  assert.deepEqual(source.stockRes, before);
  assert.deepEqual(stages, ['reading', 'preparing', 'compressing', 'verifying']);
  assert.equal(superblock(first.image).flags, superblock(before).flags);
  assert.equal(superblock(first.image).time, 1600000000);
  assert.equal(first.stockSha256, await (await core).sha256(before));
  const header = Math.ceil(superblock(first.image).used / 65536) * 65536;
  assert.deepEqual(first.slot, {header, image: header + 65536, capacity: 8 * 1024 * 1024 - header - 65536});
});

test('TC002 image: the release slot starts at the erase block behind the squashfs', async () => {
  const {slotLayout} = await core;
  assert.deepEqual(slotLayout(96), {header: 65536, image: 131072, capacity: 8 * 1024 * 1024 - 131072});
  assert.deepEqual(slotLayout(65536), {header: 65536, image: 131072, capacity: 8 * 1024 * 1024 - 131072});
  assert.deepEqual(slotLayout(65537), {header: 131072, image: 196608, capacity: 8 * 1024 * 1024 - 196608});
  assert.equal(slotLayout(8 * 1024 * 1024 - 65536).capacity, 0);
  assert.equal(slotLayout(8 * 1024 * 1024).capacity, 0);
});

test('TC002 image: loop.ko ownership, modes, timestamps and exact bytes', async () => {
  const {patchTree} = await core;
  const source = await input();
  const output = patchTree(tree(), source, 1600000000);
  assert.deepEqual([...output.keys()].filter(name => name.startsWith('awtrix-ng')), ['awtrix-ng', 'awtrix-ng/loop.ko']);
  const directory = output.get('awtrix-ng'), module = output.get('awtrix-ng/loop.ko');
  assert.deepEqual([directory.type, directory.mode, directory.uid, directory.gid, directory.mtime], ['5', 0o755, 0, 0, 1600000000]);
  assert.deepEqual([module.type, module.mode, module.uid, module.gid, module.mtime], ['0', 0o644, 0, 0, 1600000000]);
  assert.deepEqual(module.data, source.loop);
  assert.equal(output.get('lib/libawtrix-loader.so').uid, 1000);
  assert.equal(output.get('lib/libawtrix-loader.so').mode, 0o755);
  assert.equal(output.get('etc/EasyUI.cfg').mtime, 1500000000);
});

test('TC002 image: reject nonstock, ambiguous or nested startup configuration', async () => {
  const {patchTree} = await core;
  const source = await input();
  for (const config of ['{}', '{"startupLibPath":"evil"}', '{"startupLibPath":"/res/lib/libzkgui.so","startupLibPath":"/res/lib/libzkgui.so"}', '{"nested":{"startupLibPath":"/res/lib/libzkgui.so"}}']) {
    assert.throws(() => patchTree(tree(config), source, 1));
  }
  const installed = tree();
  installed.set('awtrix-ng', entry('awtrix-ng', '5'));
  assert.throws(() => patchTree(installed, source, 1), /not a stock/);
});

test('TC002 image: reject a loop.ko that is no ARM kernel module', async () => {
  const {patchTree} = await core;
  const source = await input();
  for (const module of [undefined, new Uint8Array(), elf(3, 'vermagic=3.18.30 '), elf(1), loop().subarray(1)]) {
    assert.throws(() => patchTree(tree(), {...source, loop: module}, 1), /loop\.ko/);
  }
  const foreign = loop();
  foreign[18] = 62;
  assert.throws(() => patchTree(tree(), {...source, loop: foreign}, 1), /ARM kernel module/);
});

test('TC002 image: reject invalid loader and Bluetooth symlink', async () => {
  const {patchTree} = await core;
  const source = await input();
  source.loader[18] = 62;
  assert.throws(() => patchTree(tree(), source, 1), /ARM/);
  source.loader = loader();
  const stock = tree();
  stock.set('bin/hciattach', {...entry('bin/hciattach', '2'), link: '/elsewhere'});
  assert.throws(() => patchTree(stock, source, 1), /regular file/);
});

test('TC002 image: TAR parser validates checksum, duplicate and parent types', async () => {
  const {encodeTar, encodeEntry, parseTar} = await tar;
  const original = encodeTar(tree());
  assert.equal(parseTar(original).size, 4);
  const corrupt = original.slice(); corrupt[0] ^= 1;
  assert.throws(() => parseTar(corrupt), /checksum/);
  const duplicate = new Uint8Array(original.length + 512);
  duplicate.set(encodeEntry(entry('.', '5')));
  duplicate.set(original, 512);
  assert.throws(() => parseTar(duplicate), /Duplicate/);
  const badParent = tree(); badParent.set('etc', {...entry('etc', '2'), link: 'lib'});
  assert.throws(() => parseTar(encodeTar(badParent)), /parent/);
  const hardlink = tree(); hardlink.set('link', {...entry('link', '1'), link: 'lib'});
  assert.throws(() => parseTar(encodeTar(hardlink)), /hard link/);
  assert.throws(() => parseTar(original.subarray(0, original.length - 512)), /terminator/);
});

test('TC002 image: regular hard links survive readback and protected files cannot have aliases', async () => {
  const {encodeTar, parseTar, compareTrees} = await tar;
  const {patchTree} = await core;
  const stock = tree();
  stock.set('lib/value', entry('lib/value', '0', 'same bytes'));
  stock.set('alias', {...entry('alias', '1'), link: 'lib/value'});
  const parsed = parseTar(encodeTar(stock));
  compareTrees(parsed, parseTar(encodeTar(parsed)));
  const split = new Map(parsed);
  split.set('alias', {...parsed.get('lib/value'), path: 'alias'});
  assert.throws(() => compareTrees(parsed, split), /hard links differ/);
  const configAlias = tree();
  configAlias.set('alias', {...entry('alias', '1'), link: 'etc/EasyUI.cfg'});
  assert.throws(() => patchTree(configAlias, {loader: loader(), loop: loop()}, 1), /other names/);
});

test('TC002 image: corrupt or incompatible SquashFS never produces output', async () => {
  const {buildImage} = await core;
  for (const kind of ['magic', 'compression', 'flags', 'truncated', 'data']) {
    const source = await input();
    if (kind === 'magic') source.stockRes[0] ^= 1;
    if (kind === 'compression') source.stockRes[20] = 1;
    if (kind === 'flags') source.stockRes[25] |= 4;
    if (kind === 'truncated') source.stockRes = source.stockRes.subarray(0, 100);
    if (kind === 'data') source.stockRes.fill(0, 96);
    const exitCode = process.exitCode;
    try { await assert.rejects(buildImage(source, undefined, await tools)); }
    finally { process.exitCode = exitCode; }
  }
});
