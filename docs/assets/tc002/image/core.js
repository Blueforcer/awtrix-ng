import { parseTar, encodeTar, compareTrees, MAX_TAR_BYTES } from './tar.js';
import {RES_PARTITION_BYTES as RES_SIZE, RES_ERASE_BYTES as ERASE_SIZE} from '../constants.js';

const decoder = new TextDecoder('utf-8', { fatal: true });
const encoder = new TextEncoder();
const BT_FILES = ['bin/gattserverbin', 'bin/hciattach', 'bin/hciconfig', 'bin/hcitool'];
const VERMAGIC = encoder.encode('vermagic=');
let binaries;

export async function preloadTools() {
  if (!binaries) {
    binaries = Promise.all(['sqfs2tar', 'tar2sqfs'].map(async name => {
      const [factory, response] = await Promise.all([import(`./${name}.js`), fetch(new URL(`./${name}.wasm`, import.meta.url))]);
      if (!response.ok) throw new Error(`Cannot load image tool: ${name}`);
      return { factory: factory.default, bytes: new Uint8Array(await response.arrayBuffer()) };
    })).catch(error => { binaries = undefined; throw error; });
  }
  return binaries;
}

export function superblock(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 96 || bytes.length > RES_SIZE) throw new Error('Invalid res image size');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0, true) !== 0x73717368 || view.getUint16(28, true) !== 4 || view.getUint16(30, true) !== 0) throw new Error('Expected SquashFS 4.0 res image');
  const used = view.getBigUint64(40, true);
  const flags = view.getUint16(24, true);
  if (used < 96n || used > BigInt(bytes.length)) throw new Error('Truncated SquashFS image');
  if (view.getUint16(20, true) !== 4 || view.getUint32(12, true) !== 131072 || view.getUint16(22, true) !== 17) throw new Error('Expected stock XZ/128 KiB SquashFS');
  if ((flags & ~0x2c0) !== 0 || !(flags & 0x40) || view.getBigUint64(56, true) !== 0xffffffffffffffffn) throw new Error('Unsupported stock compression flags or extended attributes');
  return { flags, time: view.getUint32(8, true), used: Number(used), inodes: view.getUint32(4, true),
    compression: 'xz', blockSize: view.getUint32(12, true) };
}

function requireBytes(bytes, max, label) {
  if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length > max) throw new Error(`Invalid ${label} bytes`);
}

function newEntry(path, data, mode, time, uid = 0, gid = 0, type = '0') {
  return { path, data, mode, uid, gid, mtime: time, type, link: '' };
}

function requireElf(bytes, type, label, kind) {
  if (bytes.length < 52 || bytes[0] !== 127 || bytes[1] !== 69 || bytes[2] !== 76 || bytes[3] !== 70 || bytes[4] !== 1 || bytes[5] !== 1) throw new Error(`${label} must be a 32-bit little-endian ELF`);
  const elf = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (elf.getUint16(16, true) !== type || elf.getUint16(18, true) !== 40) throw new Error(`${label} must be an ARM ${kind}`);
}

function contains(bytes, needle) {
  for (let at = bytes.indexOf(needle[0]); at >= 0 && at + needle.length <= bytes.length; at = bytes.indexOf(needle[0], at + 1)) {
    if (needle.every((byte, index) => bytes[at + index] === byte)) return true;
  }
  return false;
}

export function slotLayout(used) {
  const header = Math.ceil(used / ERASE_SIZE) * ERASE_SIZE;
  return { header, image: header + ERASE_SIZE, capacity: Math.max(0, RES_SIZE - header - ERASE_SIZE) };
}

export function patchTree(stock, { loader, loop }, time) {
  requireBytes(loader, RES_SIZE, 'loader');
  requireBytes(loop, RES_SIZE, 'loop.ko');
  requireElf(loader, 3, 'Loader', 'shared object');
  requireElf(loop, 1, 'loop.ko', 'kernel module');
  if (!contains(loop, VERMAGIC)) throw new Error('loop.ko carries no vermagic: not a kernel module');
  const result = new Map(stock);
  const config = stock.get('etc/EasyUI.cfg');
  if (config?.type !== '0' || stock.get('etc')?.type !== '5' || stock.get('lib')?.type !== '5' || stock.has('lib/libawtrix-loader.so') || stock.has('awtrix-ng')) throw new Error('Input is not a stock res partition');
  for (const entry of stock.values()) {
    if (entry.type === '1' && (entry.link === config.path || BT_FILES.includes(entry.link)))
      throw new Error(`${entry.link} also exists under other names`);
  }
  const original = decoder.decode(config.data);
  const pattern = /("startupLibPath"\s*:\s*")([^"]*)(")/g;
  const matches = [...original.matchAll(pattern)];
  if (matches.length !== 1 || matches[0][2] !== '/res/lib/libzkgui.so') throw new Error('EasyUI.cfg must start the original stock library exactly once');
  const replaced = original.replace(pattern, '$1/res/lib/libawtrix-loader.so$3');
  const before = JSON.parse(original), after = JSON.parse(replaced);
  if (!before || typeof before !== 'object' || Array.isArray(before)) throw new Error('Invalid stock configuration');
  before.startupLibPath = '/res/lib/libawtrix-loader.so';
  if (JSON.stringify(before) !== JSON.stringify(after)) throw new Error('Configuration rewrite changed other properties');
  result.set(config.path, { ...config, data: encoder.encode(replaced), raw: undefined });
  for (const path of BT_FILES) {
    if (stock.has(path) && stock.get(path).type !== '0') throw new Error('Stock Bluetooth entry is not a regular file');
    result.delete(path);
  }
  const owner = stock.get('.');
  result.set('lib/libawtrix-loader.so', newEntry('lib/libawtrix-loader.so', loader, 0o755, time, owner.uid, owner.gid));
  result.set('awtrix-ng', newEntry('awtrix-ng', new Uint8Array(), 0o755, time, 0, 0, '5'));
  result.set('awtrix-ng/loop.ko', newEntry('awtrix-ng/loop.ko', loop, 0o644, time));
  return result;
}

async function runTool(tool, args, input, readTar) {
  const errors = [];
  let status = 0;
  const module = await tool.factory({ wasmBinary: tool.bytes, noInitialRun: true, print: () => {}, printErr: text => { if (errors.length < 20) errors.push(text); }, onExit: code => { status = code; } });
  const fs = module.FS;
  fs.writeFile('/input', input);
  if (readTar) {
    fs.close(fs.getStream(1));
    const stream = fs.open('/output', 'w');
    const operations = stream.stream_ops;
    stream.stream_ops = { ...operations, write(stream, buffer, offset, length, position, canOwn) {
      if (position + length > MAX_TAR_BYTES) throw new Error('Expanded image exceeds memory limit');
      return operations.write(stream, buffer, offset, length, position, canOwn);
    } };
  } else {
    fs.close(fs.getStream(0));
    fs.open('/input', 'r');
  }
  const code = module.callMain(args);
  if (code || status) throw new Error(`Image tool failed: ${errors.join('; ') || code || status}`);
  const output = fs.readFile('/output');
  fs.unlink('/input');
  fs.unlink('/output');
  return output;
}

export async function sha256(bytes) {
  const hash = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return [...hash].map(byte => byte.toString(16).padStart(2, '0')).join('');
}

export async function buildImage(input, progress = () => {}, tools) {
  const meta = superblock(input.stockRes);
  const stockSha256 = await sha256(input.stockRes);
  const [reader, writer] = tools ?? await preloadTools();
  progress('reading');
  const stock = parseTar(await runTool(reader, ['-s', '-r', '.', '/input'], input.stockRes, true));
  const inodeCount = entries => [...entries.values()].filter(entry => entry.type !== '1').length;
  if (inodeCount(stock) !== meta.inodes) throw new Error('Stock image inode count mismatch');
  progress('preparing');
  const expected = patchTree(stock, input, meta.time);
  const tar = encodeTar(expected);
  progress('compressing');
  const args = ['-s', '-q', '-T', '-c', 'xz', '-b', '131072', '-j', '1', '-d', `mtime=${meta.time}`, ...(meta.flags & 0x80 ? ['-e'] : []), '/output'];
  const image = await runTool(writer, args, tar, false);
  if (image.length > RES_SIZE) throw new Error('Installation image does not fit the res partition');
  const header = new DataView(image.buffer, image.byteOffset, image.byteLength);
  if ((header.getUint16(24, true) ^ meta.flags) & ~0x228) throw new Error(`Output compression flags changed unexpectedly: ${header.getUint16(24, true).toString(16)} vs ${meta.flags.toString(16)}`);
  header.setUint16(24, meta.flags, true);
  const built = superblock(image);
  if (built.time !== meta.time || built.inodes !== inodeCount(expected)) throw new Error('Output filesystem metadata mismatch');
  progress('verifying');
  compareTrees(expected, parseTar(await runTool(reader, ['-s', '-r', '.', '/input'], image, true)));
  return { image, stockSha256, imageSha256: await sha256(image), slot: slotLayout(built.used),
    stock: {...meta, owner: `${stock.get('.').uid}/${stock.get('.').gid}`},
    built: {...built, entries: expected.size}, removed: BT_FILES.filter(path => stock.has(path)) };
}
