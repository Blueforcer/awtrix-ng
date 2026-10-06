const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { execFileSync } = require('node:child_process');
const { createHash, webcrypto } = require('node:crypto');

globalThis.crypto ||= webcrypto;
const repo = path.resolve(__dirname, '../..');
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'awtrix-browser-package-'));
test.after(() => fs.rmSync(root, { recursive: true, force: true }));
execFileSync(process.platform === 'win32' ? 'python' : 'python3', ['-c', `
import hashlib, json, pathlib, sys
root, repo = map(pathlib.Path, sys.argv[1:])
sys.path.insert(0, str(repo / 'tools/tc002/install'))
import browser_package, bundle
tree = root / 'bundle'
for name in browser_package.PUBLIC_FILES - {bundle.IMAGE}:
    target = tree / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes((name + '\\n').encode() * 3)
manifest = bundle.write_manifest(tree, '1.2.0-gabcdef123456', 'abcdef123456', False, counter=1790000000)
image = bytearray(bytes(range(256)) * 40)
image[:4] = b'hsqs'
image[40:48] = len(image).to_bytes(8, 'little')
(tree / bundle.IMAGE).write_bytes(image)
files, host = manifest.pop('files'), manifest.pop('hostOnly')
manifest.update(image={'size': len(image), 'sha256': hashlib.sha256(image).hexdigest()}, files=files, hostOnly=host)
(tree / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\\n', encoding='utf-8')
(root / 'LICENSE.md').write_text('Fixture licence\\n', encoding='utf-8')
(root / 'NOTICES.md').write_text('Fixture notices\\n', encoding='utf-8')
browser_package.create(tree, root / 'package.zip', license_path=root / 'LICENSE.md',
    notices_path=root / 'NOTICES.md', source_url='https://example.invalid/sources.tar.gz',
    source_sha256='a' * 64)
`, root, repo], { env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' } });
const original = fs.readFileSync(path.join(root, 'package.zip'));
const parser = import(pathToFileURL(path.join(repo, 'docs/assets/tc002/package.js')).href);
const sha = data => createHash('sha256').update(data).digest('hex');
const jsonBytes = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');
const outerName = 'browser-manifest.json';
const imageName = 'bundle/release.img';

function crc32(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function files() {
  const entries = [];
  let offset = 0;
  while (original.readUInt32LE(offset) === 0x04034b50) {
    const size = original.readUInt32LE(offset + 18), length = original.readUInt16LE(offset + 26);
    const name = original.subarray(offset + 30, offset + 30 + length).toString();
    entries.push({ name, data: Buffer.from(original.subarray(offset + 30 + length, offset + 30 + length + size)) });
    offset += 30 + length + size;
  }
  const manifest = JSON.parse(entries[0].data);
  for (const entry of entries) entry.mode = entry.name === outerName ? '0644' :
    manifest.files.find(file => file.path === entry.name).mode;
  return entries;
}

function pack(entries) {
  const local = [], central = [];
  let offset = 0;
  for (const entry of entries) {
    const name = Buffer.from(entry.name), crc = crc32(entry.data), header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4); header.writeUInt16LE(33, 12);
    header.writeUInt32LE(crc, 14); header.writeUInt32LE(entry.data.length, 18);
    header.writeUInt32LE(entry.data.length, 22); header.writeUInt16LE(name.length, 26);
    local.push(header, name, entry.data);
    const record = Buffer.alloc(46);
    record.writeUInt32LE(0x02014b50); record.writeUInt16LE(0x0314, 4); record.writeUInt16LE(20, 6);
    record.writeUInt16LE(33, 14); record.writeUInt32LE(crc, 16);
    record.writeUInt32LE(entry.data.length, 20); record.writeUInt32LE(entry.data.length, 24);
    record.writeUInt16LE(name.length, 28);
    record.writeUInt32LE(((0o100000 | parseInt(entry.mode, 8)) << 16) >>> 0, 38);
    record.writeUInt32LE(offset, 42);
    central.push(record, name);
    offset += header.length + name.length + entry.data.length;
  }
  const directory = Buffer.concat(central), end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...local, directory, end]);
}

function change(entries, name, transform) {
  const entry = entries.find(file => file.name === name);
  entry.data = transform(entry.data);
  if (name === outerName) return;
  const outer = entries.find(file => file.name === outerName), manifest = JSON.parse(outer.data);
  const record = manifest.files.find(file => file.path === name);
  record.size = entry.data.length; record.sha256 = sha(entry.data);
  outer.data = jsonBytes(manifest);
}

function metadata(entries, name, transform) {
  change(entries, name, data => {
    const value = JSON.parse(data);
    transform(value);
    return jsonBytes(value);
  });
}

function replaceImage(entries, transform) {
  change(entries, imageName, transform);
  const image = entries.find(file => file.name === imageName).data;
  const record = { size: image.length, sha256: sha(image) };
  metadata(entries, 'bundle/manifest.json', value => { value.image = record; });
  metadata(entries, outerName, value => { value.image = { path: imageName, ...record }; });
}

async function reject(data, reason) {
  const { parsePackage } = await parser;
  await assert.rejects(parsePackage(data), reason);
}

test('Python archive is consumed with the release image, helper, loader and loop.ko', async () => {
  const { parsePackage } = await parser;
  const result = await parsePackage(original);
  assert.equal(result.version, '1.2.0-gabcdef123456');
  assert.equal(result.counter, '1790000000');
  assert.match(result.release, /^1\.2\.0-gabcdef123456-[0-9a-f]{12}$/);
  assert.equal(Buffer.from(result.helper).toString(), 'bin/awtrix-tc002-flash\n'.repeat(3));
  assert.equal(Buffer.from(result.loader).toString(), 'lib/libawtrix-loader.so\n'.repeat(3));
  assert.equal(Buffer.from(result.loop).toString(), 'lib/modules/loop.ko\n'.repeat(3));
  assert.deepEqual(Buffer.from(result.image), fs.readFileSync(path.join(root, 'bundle', 'release.img')));
  assert.deepEqual(files().map(entry => entry.name).sort(), [outerName, 'bundle/bin/awtrix-tc002-flash',
    'bundle/lib/libawtrix-loader.so', 'bundle/lib/modules/loop.ko', 'bundle/manifest.json', imageName,
    'notices/LICENSE.md', 'notices/SOURCE-NOTICE.txt', 'notices/THIRD-PARTY-NOTICES.md'].sort());
  assert.deepEqual(pack(files()), original);
});

test('a format 1 package is refused with a clear message', async () => {
  const entries = files();
  metadata(entries, outerName, value => {
    value.schemaVersion = 1;
    delete value.image; delete value.loop;
    value.releaseManifest = 'release-manifest.json';
    value.installedFiles = [];
  });
  await reject(pack(entries), /format 1 is not supported/);
});

test('ZIP local and central headers, CRC, storage and trailing bytes are strict', async () => {
  const central = original.readUInt32LE(original.length - 6);
  for (const [offset, byte, reason] of [[30, 0x78, /headers disagree/], [14, 0, /headers disagree/],
    [central + 10, 8, /unsupported ZIP entry/], [central + 38, 1, /unsupported ZIP entry/]]) {
    const broken = Buffer.from(original); broken[offset] = byte;
    await reject(broken, reason);
  }
  const broken = Buffer.from(original);
  broken[30 + original.readUInt16LE(26)] ^= 1;
  await reject(broken, /CRC mismatch/);
  await reject(Buffer.concat([original, Buffer.from([0])]), /ZIP end record/);
  const hidden = Buffer.concat([original.subarray(0, central), Buffer.from([0]), original.subarray(central)]);
  hidden.writeUInt32LE(central + 1, hidden.length - 6);
  await reject(hidden, /hidden ZIP data/);
});

test('duplicate, unsafe, missing and unlisted archive files are rejected', async () => {
  const duplicate = files(); duplicate.push({ ...duplicate[1] });
  await reject(pack(duplicate), /duplicate ZIP path/);
  for (const name of ['../private', '/absolute', 'bundle\\secret', 'bundle/a:b', 'bundle/.secret']) {
    const entries = files(); entries[1].name = name;
    await reject(pack(entries), /unsafe or duplicate/);
  }
  const missing = files(); missing.pop();
  await reject(pack(missing), /file list differs/);
  const extra = files(); extra.push({ name: 'bundle/share/private', data: Buffer.from('x'), mode: '0644' });
  await reject(pack(extra), /file list differs/);
});

test('file SHA-256 is checked even when ZIP CRC matches', async () => {
  for (const name of ['bundle/lib/modules/loop.ko', imageName]) {
    const entries = files(); entries.find(entry => entry.name === name).data[0] ^= 1;
    await reject(pack(entries), /SHA-256 mismatch/);
  }
});

test('standalone MCU bench helper is excluded even when listed in the archive manifest', async () => {
  const entries = files();
  const name = 'bundle/bin/awtrix-tc002-mcu-prepare';
  const data = Buffer.from('bench helper');
  entries.push({ name, data, mode: '0755' });
  metadata(entries, outerName, manifest => {
    manifest.files.push({ path: name, size: data.length, sha256: sha(data), mode: '0755', role: 'helper' });
    manifest.files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
  });
  await reject(pack(entries), /file list differs/);
});

test('identity, manifests, file modes and roles must agree', async () => {
  const helper = value => value.files.find(file => file.path === 'bin/awtrix-tc002-flash');
  const cases = [
    ['bundle/manifest.json', value => { value.version = '1.3.0'; }, /identity mismatch/],
    ['bundle/manifest.json', value => { value.schema_version = 2; }, /unsupported bundle manifest/],
    ['bundle/manifest.json', value => { value.hostOnly = []; }, /bundle file list differs/],
    ['bundle/manifest.json', value => { value.files[0].jffs2 = 0; }, /metadata members/],
    ['bundle/manifest.json', value => { helper(value).sha256 = 'c'.repeat(64); }, /metadata disagree/],
    ['bundle/manifest.json', value => { value.hostOnly[1].size += 1; }, /metadata disagree/],
    ['bundle/manifest.json', value => { value.image.sha256 = 'b'.repeat(64); }, /release image differs/],
    [outerName, value => { value.image.size += 1; }, /release image differs/],
    [outerName, value => { value.loop = value.loader; }, /unsupported package layout/],
    [outerName, value => { value.counter = '1790000001'; }, /counter mismatch/],
    [outerName, value => { value.files[0].mode = '0644'; }, /file metadata/],
    [outerName, value => { value.files[0].role = 'notice'; }, /file metadata/],
    [outerName, value => { value.sources.url = 'https://example.invalid/other.tar.gz'; }, /source notice differs/],
    [outerName, value => { value.secret = 'unexpected'; }, /metadata members/],
  ];
  for (const [name, transform, reason] of cases) {
    const entries = files(); metadata(entries, name, transform);
    await reject(pack(entries), reason);
  }
});

test('the release image must match both manifests and be a squashfs release image', async () => {
  const inconsistent = files();
  change(inconsistent, imageName, data => Buffer.concat([data, Buffer.alloc(1)]));
  await reject(pack(inconsistent), /release image differs/);
  const corrupt = [data => { const copy = Buffer.from(data); copy[0] ^= 1; return copy; },
    data => Buffer.concat([data, Buffer.alloc(4096)]), data => data.subarray(0, 4096)];
  for (const transform of corrupt) {
    const entries = files(); replaceImage(entries, transform);
    await reject(pack(entries), /not a release image/);
  }
  const padded = files();
  replaceImage(padded, data => Buffer.concat([data, Buffer.alloc(2048)]));
  const { parsePackage } = await parser;
  assert.equal((await parsePackage(pack(padded))).image.length, 12288);
});

test('dirty input needs an explicit private preview option', async () => {
  const entries = files();
  for (const name of ['bundle/manifest.json', outerName]) {
    metadata(entries, name, value => { value.dirty = true; });
  }
  const data = pack(entries), { parsePackage } = await parser;
  await reject(data, /dirty package/);
  assert.equal((await parsePackage(data, { allowDirty: true })).manifest.dirty, true);
});

test('JSON duplicate members and unsafe counters are refused', async () => {
  const entries = files();
  change(entries, outerName, data => Buffer.from(data.toString().replace('"schemaVersion": 2', '"schemaVersion": 2, "schemaVersion": 2')));
  await reject(pack(entries), /duplicate JSON/);
  const unsafe = files();
  change(unsafe, 'bundle/manifest.json', data => Buffer.from(data.toString().replace('1790000000', '9007199254740993')));
  metadata(unsafe, outerName, value => { value.counter = '9007199254740993'; });
  await reject(pack(unsafe), /unsafe JSON number/);
});

test('archive, entry and count bounds are enforced before reading payloads', async () => {
  await reject(new Uint8Array((32 << 20) + 1), /32 MiB/);
  const huge = Buffer.from(original), central = huge.readUInt32LE(huge.length - 6);
  huge.writeUInt32LE((16 << 20) + 1, central + 20); huge.writeUInt32LE((16 << 20) + 1, central + 24);
  await reject(huge, /unsupported ZIP entry/);
  const count = Buffer.from(original); count.writeUInt16LE(129, count.length - 12); count.writeUInt16LE(129, count.length - 14);
  await reject(count, /invalid ZIP directory/);
});

test('input mutation cannot change verified output', async () => {
  const { parsePackage } = await parser;
  const input = Buffer.from(original);
  const pending = parsePackage(input);
  input.fill(0);
  const result = await pending;
  assert.equal(Buffer.from(result.helper).toString(), 'bin/awtrix-tc002-flash\n'.repeat(3));
});
