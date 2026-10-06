import {MAX_FILE_BYTES, MAX_ARCHIVE_BYTES, MAX_FILES, PRODUCTION_TARGET} from './constants.js';
const NAME = /^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$/;
const HEX = /^[0-9a-f]{64}$/;
const PUBLIC_FILES = [
  'bin/awtrix-linux', 'bin/awtrix-tc002-audio-pcm', 'bin/awtrix-tc002-flash',
  'bin/awtrix-tc002d', 'bin/dhcp-callback', 'bin/udhcpc', 'bin/wpa_supplicant',
  'lib/libawtrix-loader.so', 'lib/modules/aic8800_bsp.ko', 'lib/modules/aic8800_fdrv.ko',
  'lib/modules/awtrix_pcm.ko', 'lib/modules/loop.ko', 'share/boot.mp3', 'share/ca-certificates.crt',
  'share/index.html.gz', 'share/licenses.txt.gz',
].sort();
const HELPER = 'bin/awtrix-tc002-flash';
const HOST_FILES = ['lib/libawtrix-loader.so', 'lib/modules/loop.ko'];
const MCU_PATCH = ['share/mcu/LICENSE.txt', 'share/mcu/extension.bin', 'share/mcu/manifest.json'];
const SPEECH_VOICE = 'share/speech/voice.atts';
const NOTICES = ['notices/LICENSE.md', 'notices/SOURCE-NOTICE.txt', 'notices/THIRD-PARTY-NOTICES.md'];
const MANIFEST = 'bundle/manifest.json';
const IMAGE = 'bundle/release.img';
const ROLES = { ['bundle/' + HELPER]: 'helper', ['bundle/' + HOST_FILES[0]]: 'host', ['bundle/' + HOST_FILES[1]]: 'host',
  [MANIFEST]: 'manifest', [IMAGE]: 'image', ...Object.fromEntries(NOTICES.map(path => [path, 'notice'])) };
const ARCHIVE_FILES = Object.keys(ROLES).sort();
const OUTER = 'browser-manifest.json';
const decoder = new TextDecoder('utf-8', { fatal: true });
const crcTable = Uint32Array.from({ length: 256 }, (_, value) => {
  for (let bit = 0; bit < 8; bit++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  return value >>> 0;
});

function check(condition, message) {
  if (!condition) throw new Error(`TC002 package: ${message}`);
}

function crc32(data) {
  let crc = 0xffffffff;
  for (const byte of data) crc = crcTable[(crc ^ byte) & 255] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

async function sha256(data) {
  check(globalThis.crypto?.subtle, 'SHA-256 requires a secure browser context');
  const hash = new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', data));
  return Array.from(hash, value => value.toString(16).padStart(2, '0')).join('');
}

function safePath(path) {
  return typeof path === 'string' && path.length <= 256 && path.split('/').every(part => NAME.test(part));
}

function fields(value, required, optional = []) {
  check(value && typeof value === 'object' && !Array.isArray(value), 'expected a metadata object');
  check(required.every(key => Object.hasOwn(value, key)) &&
    Object.keys(value).every(key => required.includes(key) || optional.includes(key)), 'unexpected metadata members');
}

function same(left, right) {
  if (left === right) return true;
  if (!left || !right || typeof left !== 'object' || typeof right !== 'object' ||
      Array.isArray(left) !== Array.isArray(right)) return false;
  return Object.keys(left).length === Object.keys(right).length &&
    Object.keys(left).every(key => Object.hasOwn(right, key) && same(left[key], right[key]));
}

function json(data) {
  check(data.length <= 256 << 10, 'metadata exceeds 256 KiB');
  const text = decoder.decode(data);
  let offset = 0, nodes = 0;
  const space = () => { while (/^[\t\n\r ]$/.test(text[offset] || '')) offset++; };
  function string() {
    const match = /^"(?:[^"\\\x00-\x1f]|\\["\\/bfnrt]|\\u[0-9a-fA-F]{4})*"/.exec(text.slice(offset));
    check(match, 'invalid JSON string');
    offset += match[0].length;
    return JSON.parse(match[0]);
  }
  function value(depth) {
    check(depth <= 16 && ++nodes <= 8192, 'metadata is too complex');
    space();
    if (text[offset] === '"') return string();
    if (text[offset] === '{') {
      const result = Object.create(null);
      offset++; space();
      if (text[offset] === '}') { offset++; return result; }
      for (;;) {
        space();
        const key = string();
        check(!Object.hasOwn(result, key), 'duplicate JSON member');
        space(); check(text[offset++] === ':', 'invalid JSON object');
        result[key] = value(depth + 1);
        space();
        const end = text[offset++];
        if (end === '}') return result;
        check(end === ',', 'invalid JSON object');
      }
    }
    if (text[offset] === '[') {
      const result = [];
      offset++; space();
      if (text[offset] === ']') { offset++; return result; }
      for (;;) {
        result.push(value(depth + 1)); space();
        const end = text[offset++];
        if (end === ']') return result;
        check(end === ',', 'invalid JSON array');
      }
    }
    const match = /^(?:true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)/.exec(text.slice(offset));
    check(match, 'invalid JSON value');
    offset += match[0].length;
    const result = JSON.parse(match[0]);
    check(typeof result !== 'number' || Number.isSafeInteger(result), 'unsafe JSON number');
    return result;
  }
  const result = value(0);
  space(); check(offset === text.length, 'trailing JSON content');
  return result;
}

function readZip(bytes) {
  check(bytes.length >= 22 && bytes.length <= MAX_ARCHIVE_BYTES, 'archive size outside limits');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u16 = offset => view.getUint16(offset, true);
  const u32 = offset => view.getUint32(offset, true);
  const end = bytes.length - 22;
  check(u32(end) === 0x06054b50 && !u16(end + 4) && !u16(end + 6) && !u16(end + 20), 'unsupported ZIP end record');
  const count = u16(end + 10), centralStart = u32(end + 16);
  check(count > 0 && count <= MAX_FILES && count === u16(end + 8) &&
    centralStart + u32(end + 12) === end, 'invalid ZIP directory');
  let central = centralStart, local = 0;
  const entries = new Map();
  for (let index = 0; index < count; index++) {
    check(central + 46 <= end && u32(central) === 0x02014b50, 'invalid central ZIP header');
    const nameLength = u16(central + 28), size = u32(central + 24), crc = u32(central + 16);
    const attributes = u32(central + 38), mode = attributes >>> 16;
    check(u16(central + 4) === 0x0314 && u16(central + 6) === 20 &&
      !u16(central + 8) && !u16(central + 10) && !u16(central + 12) && u16(central + 14) === 33 &&
      size > 0 && size <= MAX_FILE_BYTES && u32(central + 20) === size &&
      !u16(central + 30) && !u16(central + 32) && !u16(central + 34) && !u16(central + 36) &&
      !(attributes & 0xffff) && [0o100644, 0o100755].includes(mode) && u32(central + 42) === local,
    'unsupported ZIP entry');
    check(nameLength > 0 && nameLength <= 256 && central + 46 + nameLength <= end, 'invalid ZIP name length');
    const path = decoder.decode(bytes.subarray(central + 46, central + 46 + nameLength));
    check(safePath(path) && !entries.has(path), 'unsafe or duplicate ZIP path');
    check(local + 30 + nameLength + size <= centralStart && u32(local) === 0x04034b50 &&
      u16(local + 4) === 20 && !u16(local + 6) && !u16(local + 8) && !u16(local + 10) &&
      u16(local + 12) === 33 && u32(local + 14) === crc && u32(local + 18) === size &&
      u32(local + 22) === size && u16(local + 26) === nameLength && !u16(local + 28) &&
      decoder.decode(bytes.subarray(local + 30, local + 30 + nameLength)) === path,
    'local and central ZIP headers disagree');
    const data = bytes.subarray(local + 30 + nameLength, local + 30 + nameLength + size);
    check(crc32(data) === crc, 'ZIP CRC mismatch');
    entries.set(path, { data, mode: (mode & 0o777).toString(8).padStart(4, '0') });
    local += 30 + nameLength + size;
    central += 46 + nameLength;
  }
  check(local === centralStart && central === end, 'hidden ZIP data');
  check(entries.keys().next().value === OUTER, 'browser manifest must be first');
  return entries;
}

function sourceReference(source) {
  fields(source, ['url', 'sha256']);
  check(typeof source.url === 'string' && source.url.length <= 2048 && /^[\x21-\x7e]+$/.test(source.url) &&
    !source.url.includes('\\') && typeof source.sha256 === 'string' && HEX.test(source.sha256), 'invalid source reference');
  const url = new URL(source.url);
  check(url.protocol === 'https:' && url.hostname && !url.username && !url.password && !url.search && !url.hash,
    'invalid source archive URL');
}

function publicFiles(paths) {
  const expected = [...PUBLIC_FILES];
  if (MCU_PATCH.some(path => paths.includes(path))) {
    check(MCU_PATCH.every(path => paths.includes(path)), 'incomplete MCU patch');
    expected.push(...MCU_PATCH);
  }
  if (paths.includes(SPEECH_VOICE)) expected.push(SPEECH_VOICE);
  return expected.sort();
}

function releaseImage(data) {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const used = data.length >= 96 && view.getUint32(0, true) === 0x73717368 ? view.getBigUint64(40, true) : 0n;
  return used >= 96n && used <= BigInt(data.length) && data.length <= Math.ceil(Number(used) / 4096) * 4096;
}

function bundleMetadata(manifest) {
  fields(manifest, ['schema_version', 'version', 'commit', 'dirty', 'release', 'counter', 'image', 'files', 'hostOnly'], ['compilers']);
  check(manifest.schema_version === 3, 'unsupported bundle manifest');
  check(typeof manifest.version === 'string' &&
    /^[0-9A-Za-z][0-9A-Za-z.+_-]{0,50}$/.test(manifest.version) &&
    typeof manifest.commit === 'string' && /^[0-9a-f]{7,40}$/.test(manifest.commit) &&
    typeof manifest.dirty === 'boolean' && typeof manifest.release === 'string' &&
    NAME.test(manifest.release) && !manifest.release.endsWith('.partial') &&
    Number.isSafeInteger(manifest.counter) && manifest.counter > 0, 'invalid release identity');
  if (Object.hasOwn(manifest, 'compilers')) {
    check(manifest.compilers && typeof manifest.compilers === 'object' && !Array.isArray(manifest.compilers), 'invalid compilers');
    for (const [name, compiler] of Object.entries(manifest.compilers)) {
      fields(compiler, ['version', 'sha256']);
      check(NAME.test(name) && typeof compiler.version === 'string' && compiler.version.length > 0 &&
        compiler.version.length <= 4096 && typeof compiler.sha256 === 'string' && HEX.test(compiler.sha256), 'invalid compiler identity');
    }
  }
  fields(manifest.image, ['size', 'sha256']);
  check(Number.isSafeInteger(manifest.image.size) && manifest.image.size > 0 && manifest.image.size <= MAX_FILE_BYTES &&
    typeof manifest.image.sha256 === 'string' && HEX.test(manifest.image.sha256), 'invalid release image metadata');
  for (const [listing, installed] of [[manifest.files, true], [manifest.hostOnly, false]]) {
    check(Array.isArray(listing), 'invalid bundle file list');
    const expected = publicFiles(listing.map(entry => entry?.path)).filter(path => installed !== HOST_FILES.includes(path));
    check(same(listing.map(entry => entry?.path), expected), 'bundle file list differs from public files');
    for (const entry of listing) {
      fields(entry, ['path', 'size', 'sha256', 'mode']);
      check(Number.isSafeInteger(entry.size) && entry.size >= 0 &&
        typeof entry.sha256 === 'string' && HEX.test(entry.sha256) &&
        entry.mode === (entry.path.startsWith('bin/') || entry.path.endsWith('.so') ? '0755' : '0644'), 'invalid bundle file metadata');
    }
  }
}

export async function parsePackage(input, { allowDirty = false } = {}) {
  check(input instanceof Uint8Array, 'expected Uint8Array');
  check(input.length <= MAX_ARCHIVE_BYTES, 'archive exceeds 32 MiB');
  const entries = readZip(new Uint8Array(input));
  check(entries.has(OUTER), 'missing browser manifest');
  const manifest = json(entries.get(OUTER).data);
  check(manifest?.schemaVersion !== 1, 'package format 1 is not supported; use a package from a current release');
  fields(manifest, ['schemaVersion', 'target', 'version', 'release', 'commit', 'counter', 'dirty', 'bundlePrefix',
    'bundleManifest', 'image', 'flashHelper', 'loader', 'loop', 'sources', 'files']);
  fields(manifest.image, ['path', 'size', 'sha256']);
  check(manifest.schemaVersion === 2 && manifest.target === PRODUCTION_TARGET &&
    manifest.bundlePrefix === 'bundle/' && manifest.bundleManifest === MANIFEST && manifest.image.path === IMAGE &&
    manifest.flashHelper === 'bundle/' + HELPER && manifest.loader === 'bundle/' + HOST_FILES[0] &&
    manifest.loop === 'bundle/' + HOST_FILES[1], 'unsupported package layout');
  check(typeof manifest.dirty === 'boolean' && (!manifest.dirty || allowDirty === true), 'dirty package is not a release');
  check(typeof manifest.counter === 'string' && /^[1-9][0-9]{0,18}$/.test(manifest.counter) &&
    BigInt(manifest.counter) <= 9223372036854775807n, 'invalid package counter');
  sourceReference(manifest.sources);
  check(Array.isArray(manifest.files) && same(manifest.files.map(entry => entry?.path), ARCHIVE_FILES) &&
    entries.size === ARCHIVE_FILES.length + 1 && ARCHIVE_FILES.every(path => entries.has(path)), 'archive file list differs from manifest');
  check(entries.get(OUTER).mode === '0644', 'invalid browser manifest mode');
  for (const entry of manifest.files) {
    fields(entry, ['path', 'size', 'sha256', 'mode', 'role']);
    const archived = entries.get(entry.path);
    check(Number.isSafeInteger(entry.size) && entry.size === archived.data.length && entry.mode === archived.mode &&
      entry.role === ROLES[entry.path] && typeof entry.sha256 === 'string' && HEX.test(entry.sha256), 'invalid archive file metadata');
    if (!['helper', 'host'].includes(entry.role)) check(entry.mode === '0644', 'invalid metadata mode');
    check(await sha256(archived.data) === entry.sha256, 'SHA-256 mismatch');
  }
  const original = json(entries.get(MANIFEST).data);
  bundleMetadata(original);
  for (const key of ['version', 'release', 'commit', 'dirty']) check(manifest[key] === original[key], 'package identity mismatch');
  check(manifest.counter === String(original.counter), 'package counter mismatch');
  const outer = path => manifest.files.find(file => file.path === path);
  check(['size', 'sha256'].every(key => manifest.image[key] === original.image[key] && outer(IMAGE)[key] === original.image[key]),
    'release image differs from the bundle manifest');
  check(releaseImage(entries.get(IMAGE).data), 'bundle/release.img is not a release image');
  for (const entry of [...original.files.filter(file => file.path === HELPER), ...original.hostOnly]) {
    check(['size', 'sha256', 'mode'].every(key => outer('bundle/' + entry.path)[key] === entry[key]), 'bundle and archive metadata disagree');
  }
  const identity = original.files.map(entry => `${entry.path} ${entry.mode} ${entry.sha256}\n`).join('');
  check(original.release === `${original.version}-${(await sha256(new TextEncoder().encode(identity))).slice(0, 12)}`,
    'release name does not match its files');
  const notice = decoder.decode(entries.get('notices/SOURCE-NOTICE.txt').data);
  check(notice.includes(`Archive: ${manifest.sources.url}\n`) && notice.includes(`SHA-256: ${manifest.sources.sha256}\n`),
    'source notice differs from manifest');
  return { manifest, helper: entries.get(manifest.flashHelper).data, loader: entries.get(manifest.loader).data,
    loop: entries.get(manifest.loop).data, image: entries.get(IMAGE).data,
    version: manifest.version, release: manifest.release, counter: manifest.counter };
}
