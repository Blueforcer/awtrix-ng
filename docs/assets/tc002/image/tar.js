const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });
export const MAX_TAR_BYTES = 128 * 1024 * 1024;
const MAX_ENTRIES = 20000;

function text(bytes) {
  const end = bytes.indexOf(0);
  return decoder.decode(end < 0 ? bytes : bytes.subarray(0, end));
}

function number(bytes) {
  if (bytes[0] & 0x80) {
    if (bytes[0] & 0x40) throw new Error('Negative TAR metadata is unsupported');
    let value = BigInt(bytes[0] & 0x7f);
    for (const byte of bytes.subarray(1)) value = (value << 8n) | BigInt(byte);
    if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error('TAR integer overflow');
    return Number(value);
  }
  const value = text(bytes).trim();
  if (!/^[0-7]*$/.test(value)) throw new Error('Invalid TAR integer');
  return value ? parseInt(value, 8) : 0;
}

export function safePath(value, root = false) {
  if (typeof value !== 'string' || !value || value.length > 4096 || /[\x00-\x1f\x7f\\]/.test(value)) throw new Error('Invalid archive path');
  if (root) {
    if (value === '.' || value === './') return '.';
    if (value.startsWith('./')) value = value.slice(2);
    if (value.endsWith('/')) value = value.slice(0, -1);
  }
  if (value.startsWith('/') || value.split('/').some(part => !part || part === '.' || part === '..')) throw new Error('Unsafe archive path');
  return value;
}

export function parseTar(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length > MAX_TAR_BYTES || bytes.length % 512) throw new Error('Invalid TAR length');
  const entries = new Map();
  let offset = 0, rawStart = 0, longName = null, longLink = null, ended = false;
  while (offset + 512 <= bytes.length) {
    const header = bytes.subarray(offset, offset + 512);
    if (header.every(byte => byte === 0)) {
      if (longName !== null || longLink !== null || bytes.length - offset < 1024 || !bytes.subarray(offset).every(byte => byte === 0)) throw new Error('Invalid TAR terminator');
      ended = true;
      break;
    }
    const sum = header.reduce((total, byte, index) => total + (index >= 148 && index < 156 ? 32 : byte), 0);
    if (number(header.subarray(148, 156)) !== sum) throw new Error('TAR checksum mismatch');
    if (!text(header.subarray(257, 263)).startsWith('ustar')) throw new Error('Unsupported TAR header');
    const size = number(header.subarray(124, 136));
    const next = offset + 512 + Math.ceil(size / 512) * 512;
    if (!Number.isSafeInteger(next) || next > bytes.length) throw new Error('Truncated TAR entry');
    const data = bytes.subarray(offset + 512, offset + 512 + size);
    const type = String.fromCharCode(header[156] || 48);
    if (type === 'L' || type === 'K') {
      if (size > 4096 || (type === 'L' ? longName !== null : longLink !== null)) throw new Error('Invalid TAR long name');
      if (type === 'L') longName = text(data); else longLink = text(data);
      offset = next;
      continue;
    }
    if (!['0', '1', '2', '5'].includes(type)) throw new Error(`Unsupported TAR entry type ${type}`);
    const prefix = header[262] === 0 ? text(header.subarray(345, 500)) : '';
    const path = safePath(longName ?? (prefix ? `${prefix}/` : '') + text(header.subarray(0, 100)), true);
    let link = longLink ?? text(header.subarray(157, 257));
    if (type === '1') link = safePath(link, true);
    const mode = number(header.subarray(100, 108));
    const uid = number(header.subarray(108, 116)), gid = number(header.subarray(116, 124));
    const mtime = number(header.subarray(136, 148));
    if (mode > 0o7777 || uid > 0xffffffff || gid > 0xffffffff || mtime > 0xffffffff || (type !== '0' && size !== 0)) throw new Error('Unsupported TAR metadata');
    if (type === '2' && (!link || /[\x00-\x1f\x7f]/.test(link))) throw new Error('Invalid symbolic link');
    if (!['1', '2'].includes(type) && link) throw new Error('Unexpected link target');
    if (entries.has(path)) throw new Error(`Duplicate archive path: ${path}`);
    if (entries.size >= MAX_ENTRIES) throw new Error('Too many archive entries');
    entries.set(path, { path, type, mode, uid, gid, mtime, link, data, raw: bytes.subarray(rawStart, next) });
    offset = next;
    rawStart = next;
    longName = longLink = null;
  }
  if (!ended || entries.get('.')?.type !== '5') throw new Error('Missing TAR root or terminator');
  for (const entry of entries.values()) {
    if (entry.type === '1') {
      const target = entries.get(entry.link);
      if (target?.type !== '0' || ['mode', 'uid', 'gid', 'mtime'].some(key => target[key] !== entry[key]))
        throw new Error('Invalid hard link target or metadata');
    }
    if (entry.path === '.') continue;
    const pieces = entry.path.split('/');
    pieces.pop();
    while (pieces.length) {
      if (entries.get(pieces.join('/'))?.type !== '5') throw new Error('Archive parent is not a directory');
      pieces.pop();
    }
  }
  return entries;
}

function putText(header, offset, size, value) {
  const bytes = encoder.encode(value);
  if (bytes.length > size) throw new Error('TAR field is too long');
  header.set(bytes, offset);
}

function putNumber(header, offset, size, value) {
  if (!Number.isSafeInteger(value) || value < 0) throw new Error('Invalid TAR number');
  const octal = value.toString(8);
  if (octal.length < size) putText(header, offset, size, octal.padStart(size - 1, '0'));
  else {
    let remainder = BigInt(value);
    for (let index = offset + size - 1; index >= offset; --index) {
      header[index] = Number(remainder & 255n);
      remainder >>= 8n;
    }
    if (remainder || header[offset] & 0x80) throw new Error('TAR number overflow');
    header[offset] |= 0x80;
  }
}

export function encodeEntry(entry) {
  const path = entry.path === '.' ? './' : entry.path;
  safePath(path, true);
  if (encoder.encode(path).length > 99 || encoder.encode(entry.link || '').length > 99) throw new Error('New TAR path is too long');
  const data = entry.type === '0' ? entry.data : new Uint8Array();
  const result = new Uint8Array(512 + Math.ceil(data.length / 512) * 512);
  putText(result, 0, 100, path);
  putNumber(result, 100, 8, entry.mode);
  putNumber(result, 108, 8, entry.uid);
  putNumber(result, 116, 8, entry.gid);
  putNumber(result, 124, 12, data.length);
  putNumber(result, 136, 12, entry.mtime);
  result.fill(32, 148, 156);
  result[156] = entry.type.charCodeAt(0);
  putText(result, 157, 100, entry.link || '');
  putText(result, 257, 6, 'ustar');
  putText(result, 263, 2, '00');
  putNumber(result, 148, 8, result.subarray(0, 512).reduce((a, b) => a + b, 0));
  result.set(data, 512);
  return result;
}

export function encodeTar(entries) {
  const chunks = [...entries.values()].map(entry => entry.raw ?? encodeEntry(entry));
  const length = chunks.reduce((sum, chunk) => sum + chunk.length, 1024);
  if (length > MAX_TAR_BYTES) throw new Error('Expanded image exceeds memory limit');
  const output = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { output.set(chunk, offset); offset += chunk.length; }
  return output;
}

export function compareTrees(expected, actual) {
  if (expected.size !== actual.size) throw new Error('Image readback entry count mismatch');
  for (const [path, entry] of expected) {
    const found = actual.get(path);
    const left = entry.type === '1' ? expected.get(entry.link) : entry;
    const right = found?.type === '1' ? actual.get(found.link) : found;
    if (!right || ['type', 'mode', 'uid', 'gid', 'mtime', 'link'].some(key => (left[key] ?? '') !== (right[key] ?? '')) || left.data.length !== right.data.length || left.data.some((byte, index) => byte !== right.data[index])) throw new Error(`Image readback differs at ${path}`);
  }
  const links = entries => {
    const groups = new Map();
    for (const entry of entries.values()) {
      if (!['0', '1'].includes(entry.type)) continue;
      const key = entry.type === '1' ? entry.link : entry.path;
      if (!groups.has(key)) groups.set(key, []);
      groups.get(key).push(entry.path);
    }
    return [...groups.values()].filter(group => group.length > 1).map(group => group.sort().join('\0')).sort();
  };
  if (JSON.stringify(links(expected)) !== JSON.stringify(links(actual))) throw new Error('Image readback hard links differ');
}
