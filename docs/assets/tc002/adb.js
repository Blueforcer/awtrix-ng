import {USB_CONNECT_WAIT_MS, USB_CONNECT_POLL_MS, USB_CONNECT_SETTLE_MS,
  USB_CONNECT_STABLE_MS, USB_SCAN_END_MS} from './constants.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', {fatal: true});
const EMPTY = new Uint8Array();
const VERSION = 0x01000000;
const MAX_PACKET = 4096;
const MAX_FILE = 64 * 1024 * 1024;
const MAX_SHELL = 1024 * 1024;
const COMMANDS = {CNXN: 0x4e584e43, OPEN: 0x4e45504f, OKAY: 0x59414b4f,
  CLSE: 0x45534c43, WRTE: 0x45545257, AUTH: 0x48545541};
const clockDefault = {now: () => performance.now(), setTimeout: (fn, ms) => setTimeout(fn, ms),
  clearTimeout: id => clearTimeout(id)};
export const USB_FILTERS = [{vendorId: 0x18d1, productId: 0xd002}];
export const USB_KEEPER = "k=; [ -f /tmp/awtrix-usb-keeper.pid ] && read -r k </tmp/awtrix-usb-keeper.pid; case $k in ''|*[!0-9]*) k=0;; esac; if [ $k -gt 0 ] && [ -d /proc/$k ]; then echo awtrix-usb-keeper running $k; else trap '' HUP; ( read -r s x </proc/uptime; s=${s%%.*}; end=$((s + 1200)); next=0; last=-1; seen=-1; while [ $s -lt $end ]; do if [ $s -ne $last ]; then last=$s; if [ -f /tmp/awtrix-loader.log ] && [ ! -f /tmp/awtrix-loader.vendor ]; then [ $seen -lt 0 ] && seen=$s; [ $s -ge $((seen + 2)) ] && break; else seen=-1; r=; read -r r </sys/bus/platform/devices/soc:usbotg/otg_role; case $r in usb_host|usb_null) if [ $s -ge $next ]; then echo -n usb_device >/sys/bus/platform/devices/soc:usbotg/otg_role; next=$((s + 10)); fi;; esac; fi; fi; sleep 1; read -r s x </proc/uptime; s=${s%%.*}; done; rm -f /tmp/awtrix-usb-keeper.pid ) </dev/null >/dev/null 2>&1 & echo $! >/tmp/awtrix-usb-keeper.pid; echo awtrix-usb-keeper started $!; fi";

export class AdbError extends Error {
  constructor(code, message) { super(message); this.name = 'AdbError'; this.code = code; }
}

function fail(code, message) { throw new AdbError(code, message); }
function aborted() { return new AdbError('aborted', 'USB operation cancelled.'); }
function usbFailure(error) {
  if (error instanceof AdbError) return error;
  if (['denied', 'claim', 'unsupported', 'invalid', 'disconnected', 'transport', 'timeout'].includes(error?.code))
    return new AdbError(error.code, typeof error.message === 'string' ? error.message : 'The USB operation failed.');
  if (['SecurityError', 'NotAllowedError'].includes(error?.name))
    return new AdbError('denied', 'USB access was denied. Check the device permissions and USB driver.');
  if (error?.name === 'NotSupportedError') return new AdbError('unsupported', 'The USB interface is not supported by its current driver.');
  if (error?.name === 'AbortError') return aborted();
  return new AdbError('transport', 'The USB connection changed. Waiting for the clock to reconnect…');
}
function limit(value, maximum, name) {
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) fail('argument', `Invalid ${name}.`);
  return value;
}
function timeout(value, fallback) { return limit(value ?? fallback, 600000, 'timeout'); }
function checksum(bytes) { let sum = 0; for (const b of bytes) sum = (sum + b) >>> 0; return sum; }
function join(parts, size = parts.reduce((n, p) => n + p.length, 0)) {
  const out = new Uint8Array(size); let offset = 0;
  for (const part of parts) { out.set(part, offset); offset += part.length; }
  return out;
}
function text(bytes) {
  try { return decoder.decode(bytes); } catch { fail('protocol', 'The device returned invalid UTF-8.'); }
}
function remotePath(path) {
  if (typeof path !== 'string' || !path.startsWith('/') || /[\x00-\x1f\x7f]/.test(path) ||
      path.split('/').some(part => part === '..' || part === '.') || encoder.encode(path).length > 1000)
    fail('argument', 'Invalid device file path.');
  return path;
}
function syncRecord(id, bytes = EMPTY, value = bytes.length) {
  const header = new Uint8Array(8); header.set(encoder.encode(id));
  new DataView(header.buffer).setUint32(4, value, true);
  return join([header, bytes]);
}

class Deadline {
  constructor(clock, milliseconds, signals = []) {
    this.clock = clock; this.end = clock.now() + milliseconds; this.signals = signals.filter(Boolean);
  }
  wait(start) {
    if (this.signals.some(s => s.aborted)) return Promise.reject(aborted());
    const remaining = this.end - this.clock.now();
    if (remaining <= 0) return Promise.reject(new AdbError('timeout', 'The USB operation timed out.'));
    return new Promise((resolve, reject) => {
      let done = false;
      const finish = (fn, value) => {
        if (done) return; done = true;
        this.clock.clearTimeout(timer);
        for (const signal of this.signals) signal.removeEventListener('abort', cancel);
        fn(value);
      };
      const cancel = () => finish(reject, aborted());
      const timer = this.clock.setTimeout(() => finish(reject,
        new AdbError('timeout', 'The USB operation timed out.')), remaining);
      for (const signal of this.signals) signal.addEventListener('abort', cancel, {once: true});
      try { Promise.resolve(start()).then(v => finish(resolve, v), e => finish(reject, e)); }
      catch (error) { finish(reject, error); }
    });
  }
}

function sleep(clock, milliseconds, signal) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) { reject(aborted()); return; }
    const cancel = () => { clock.clearTimeout(timer); reject(aborted()); };
    const timer = clock.setTimeout(() => { signal?.removeEventListener('abort', cancel); resolve(); }, milliseconds);
    signal?.addEventListener('abort', cancel, {once: true});
  });
}

function interfaceFor(device) {
  for (const configuration of device.configurations || [])
    for (const iface of configuration.interfaces)
      for (const alternate of iface.alternates) {
        if (alternate.interfaceClass !== 255 || alternate.interfaceSubclass !== 66 || alternate.interfaceProtocol !== 1)
          continue;
        const input = alternate.endpoints.find(e => e.type === 'bulk' && e.direction === 'in');
        const output = alternate.endpoints.find(e => e.type === 'bulk' && e.direction === 'out');
        if (input && output && output.packetSize > 0)
          return {configuration: configuration.configurationValue, interface: iface.interfaceNumber,
            alternate: alternate.alternateSetting, input: input.endpointNumber,
            output: output.endpointNumber, packetSize: output.packetSize};
      }
  fail('interface', 'This USB device has no supported ADB interface.');
}

class Wire {
  constructor(device, usb, clock, signal, onStatus) {
    this.device = device; this.usb = usb; this.clock = clock; this.signal = signal; this.onStatus = onStatus;
    this.stopped = new AbortController(); this.closed = false; this.claimed = false; this.maxData = MAX_PACKET;
    this.disconnect = event => {
      if (event.device === this.device) {
        this.onStatus({phase: 'disconnected', message: 'USB disconnected.'});
        void this.close();
      }
    };
    this.cancel = () => { void this.close(); };
    usb.addEventListener('disconnect', this.disconnect);
    signal?.addEventListener('abort', this.cancel, {once: true});
  }
  deadline(milliseconds) { return new Deadline(this.clock, milliseconds, [this.signal, this.stopped.signal]); }
  async open() {
    const deadline = this.deadline(10000);
    this.onStatus({phase: 'opening', message: 'TC002 found. Opening its USB connection…'});
    await deadline.wait(() => this.device.open().then(async () => {
      if (this.closed) { await this.device.close(); throw aborted(); }
    }));
    this.endpoints = interfaceFor(this.device);
    if (this.device.configuration?.configurationValue !== this.endpoints.configuration)
      await deadline.wait(() => this.device.selectConfiguration(this.endpoints.configuration));
    try { await deadline.wait(() => this.device.claimInterface(this.endpoints.interface)); }
    catch (error) {
      if (error instanceof AdbError) throw error;
      if (typeof error?.code === 'string') throw usbFailure(error);
      fail('claim', 'Cannot claim USB ADB. Close other USB tools and check the USB driver and device permissions.');
    }
    this.claimed = true;
    const selected = this.device.configuration.interfaces.find(i => i.interfaceNumber === this.endpoints.interface);
    if (selected?.alternate?.alternateSetting !== this.endpoints.alternate)
      await deadline.wait(() => this.device.selectAlternateInterface(this.endpoints.interface, this.endpoints.alternate));
    await this.send('CNXN', VERSION, MAX_PACKET, encoder.encode('host::\0'), deadline);
    let reply;
    for (let stale = 0; ; ++stale) {
      reply = await this.receive(deadline);
      if (reply.command === 'CNXN' || reply.command === 'AUTH') break;
      if (stale >= 64) fail('protocol', 'Unsupported ADB connection response.');
    }
    if (reply.command === 'AUTH') fail('authentication', 'This device requires ADB authentication and is not supported.');
    if (reply.command !== 'CNXN' || reply.arg0 !== VERSION || reply.arg1 < MAX_PACKET ||
        !text(reply.data).startsWith('device:')) fail('protocol', 'Unsupported ADB connection response.');
    this.maxData = Math.min(MAX_PACKET, reply.arg1);
  }
  async write(bytes, deadline) {
    const send = async data => {
      const result = await deadline.wait(() => this.device.transferOut(this.endpoints.output, data));
      if (result.status !== 'ok' || result.bytesWritten !== data.length) fail('transport', 'USB write failed.');
    };
    await send(bytes);
    if (bytes.length && bytes.length % this.endpoints.packetSize === 0) await send(EMPTY);
  }
  async read(size, deadline) {
    const bytes = new Uint8Array(size); let offset = 0, empty = 0;
    while (offset < size) {
      const result = await deadline.wait(() => this.device.transferIn(this.endpoints.input, size - offset));
      if (result.status !== 'ok' || !result.data) fail('transport', 'USB read failed.');
      const part = new Uint8Array(result.data.buffer, result.data.byteOffset, result.data.byteLength);
      if (part.length > size - offset) fail('protocol', 'Oversized USB response.');
      if (!part.length) { if (++empty > 8) fail('protocol', 'Empty USB response loop.'); continue; }
      empty = 0; bytes.set(part, offset); offset += part.length;
    }
    return bytes;
  }
  async send(command, arg0, arg1, data, deadline) {
    if (data.length > this.maxData) fail('protocol', 'ADB packet exceeds the transfer limit.');
    const header = new Uint8Array(24), view = new DataView(header.buffer), id = COMMANDS[command];
    [id, arg0, arg1, data.length, checksum(data), (id ^ 0xffffffff) >>> 0]
      .forEach((word, i) => view.setUint32(i * 4, word, true));
    await this.write(header, deadline);
    if (data.length) await this.write(data, deadline);
  }
  async receive(deadline) {
    const header = await this.read(24, deadline), view = new DataView(header.buffer);
    const id = view.getUint32(0, true), length = view.getUint32(12, true);
    const command = Object.keys(COMMANDS).find(name => COMMANDS[name] === id);
    if (!command || view.getUint32(20, true) !== ((id ^ 0xffffffff) >>> 0) || length > MAX_PACKET)
      fail('protocol', 'Invalid ADB packet header.');
    const data = length ? await this.read(length, deadline) : EMPTY;
    if (checksum(data) !== view.getUint32(16, true)) fail('protocol', 'ADB packet checksum mismatch.');
    return {command, arg0: view.getUint32(4, true), arg1: view.getUint32(8, true), data};
  }
  async close() {
    if (this.closed) return; this.closed = true; this.stopped.abort();
    this.usb.removeEventListener('disconnect', this.disconnect);
    this.signal?.removeEventListener('abort', this.cancel);
    const deadline = new Deadline(this.clock, 1000);
    try { await deadline.wait(() => this.device.close()); } catch {}
  }
}

class Channel {
  constructor(wire, local, deadline) {
    this.wire = wire; this.local = local; this.remote = 0; this.deadline = deadline;
    this.parts = []; this.buffered = 0; this.ended = false;
  }
  async packet() {
    for (let ignored = 0; ignored < 32; ++ignored) {
      const p = await this.wire.receive(this.deadline);
      if (!['OKAY', 'WRTE', 'CLSE'].includes(p.command)) fail('protocol', 'Unexpected ADB stream packet.');
      if (p.arg1 !== this.local) continue;
      if ((p.command !== 'CLSE' && !p.arg0) || (this.remote && p.arg0 && p.arg0 !== this.remote))
        fail('protocol', 'ADB stream identifier changed.');
      if (p.command !== 'WRTE' && p.data.length) fail('protocol', 'Unexpected ADB control payload.');
      if (p.command === 'CLSE') this.ended = true;
      return p;
    }
    fail('protocol', 'Too many unrelated ADB packets.');
  }
  async open(service) {
    await this.wire.send('OPEN', this.local, 0, encoder.encode(service + '\0'), this.deadline);
    const p = await this.packet();
    if (p.command === 'CLSE') fail('service', 'The requested device service is unavailable.');
    if (p.command !== 'OKAY') fail('protocol', 'ADB service did not acknowledge opening.');
    this.remote = p.arg0;
  }
  async accept(p) {
    if (p.command !== 'WRTE') fail('protocol', 'Expected ADB stream data.');
    if (this.buffered + p.data.length > 128 * 1024) fail('limit', 'ADB response buffer limit exceeded.');
    this.parts.push(p.data); this.buffered += p.data.length;
    await this.wire.send('OKAY', this.local, this.remote, EMPTY, this.deadline);
  }
  async write(data) {
    for (let offset = 0; offset < data.length; offset += this.wire.maxData) {
      if (this.ended) fail('protocol', 'The device closed the transfer early.');
      await this.wire.send('WRTE', this.local, this.remote, data.subarray(offset, offset + this.wire.maxData), this.deadline);
      for (;;) {
        const p = await this.packet();
        if (p.command === 'OKAY') break;
        if (p.command === 'CLSE') fail('protocol', 'The device closed the transfer early.');
        await this.accept(p);
      }
    }
  }
  async read(size) {
    while (this.buffered < size) {
      if (this.ended) fail('protocol', 'The device closed before its response was complete.');
      const p = await this.packet();
      if (p.command === 'CLSE') continue;
      await this.accept(p);
    }
    const out = new Uint8Array(size); let offset = 0;
    while (offset < size) {
      const first = this.parts[0], take = Math.min(first.length, size - offset);
      out.set(first.subarray(0, take), offset); offset += take; this.buffered -= take;
      if (take === first.length) this.parts.shift(); else this.parts[0] = first.subarray(take);
    }
    return out;
  }
  async all(maxBytes) {
    const chunks = []; let size = 0;
    while (!this.ended || this.buffered) {
      if (this.buffered) {
        if (size + this.buffered > maxBytes) fail('limit', 'Device output exceeds the size limit.');
        const bytes = await this.read(this.buffered); chunks.push(bytes); size += bytes.length;
      } else {
        const p = await this.packet();
        if (p.command !== 'CLSE') await this.accept(p);
      }
    }
    return join(chunks, size);
  }
  async quit() {
    if (this.ended) fail('protocol', 'The device closed the transfer early.');
    await this.wire.send('WRTE', this.local, this.remote, syncRecord('QUIT', EMPTY, 0), this.deadline);
    while (!this.ended) {
      const p = await this.packet();
      if (p.command === 'WRTE') fail('protocol', 'Unexpected data after the file transfer.');
    }
  }
  async close() {
    if (this.ended) return; this.ended = true;
    await this.wire.send('CLSE', this.local, this.remote, EMPTY, this.deadline);
  }
}

class Session {
  constructor(wire) {
    this.wire = wire; this.busy = false; this.nextId = 1;
    this.identity = Object.freeze({vendorId: wire.device.vendorId, productId: wire.device.productId,
      serialNumber: wire.device.serialNumber || ''});
  }
  get connected() { return !this.wire.closed; }
  close() { return this.wire.close(); }
  async run(service, timeoutMs, operation) {
    if (!this.connected) fail('disconnected', 'USB is disconnected. Reconnect before continuing.');
    if (this.busy) fail('busy', 'Another USB operation is still running.');
    this.busy = true;
    const channel = new Channel(this.wire, this.nextId++, this.wire.deadline(timeoutMs));
    try { await channel.open(service); const value = await operation(channel); await channel.close(); return value; }
    catch (error) {
      const wasDisconnected = this.wire.closed && !this.wire.signal?.aborted;
      await this.close();
      if (wasDisconnected) fail('disconnected', 'USB is disconnected. Reconnect before continuing.');
      throw usbFailure(error);
    }
    finally { this.busy = false; }
  }
  rawShell(command, {timeoutMs = 10000, maxBytes = MAX_SHELL} = {}) {
    if (typeof command !== 'string' || command.includes('\0') || encoder.encode(command).length > 3072)
      return Promise.reject(new AdbError('argument', 'Invalid or oversized shell command.'));
    const outputLimit = limit(maxBytes, MAX_SHELL, 'output limit');
    return this.run('shell:' + command, timeout(timeoutMs, 10000), channel => channel.all(outputLimit))
      .then(bytes => text(bytes).replace(/\r/g, ''));
  }
  async shell(command, {timeoutMs = 120000, maxBytes = MAX_SHELL} = {}) {
    if (typeof command !== 'string') fail('argument', 'Invalid shell command.');
    const output = await this.rawShell(command + '; echo __AWTRIX_RC=$?', {timeoutMs, maxBytes});
    const marker = output.lastIndexOf('__AWTRIX_RC=');
    if (marker < 0 || !/^\d+\s*$/.test(output.slice(marker + 12))) fail('protocol', 'The shell returned no exit status.');
    const code = Number(output.slice(marker + 12).trim());
    if (code !== 0) fail('command', `The device command failed (exit ${code}).`);
    return output.slice(0, marker);
  }
  readFile(path, {maxBytes = 16 * 1024 * 1024, timeoutMs = 120000, onProgress} = {}) {
    const sizeLimit = limit(maxBytes, MAX_FILE, 'file limit'); remotePath(path);
    return this.run('sync:', timeout(timeoutMs, 120000), async channel => {
      await channel.write(syncRecord('RECV', encoder.encode(path)));
      const parts = []; let total = 0;
      for (;;) {
        const header = await channel.read(8), id = text(header.subarray(0, 4));
        const size = new DataView(header.buffer).getUint32(4, true);
        if (id === 'DONE') { await channel.quit(); return join(parts, total); }
        if (id === 'FAIL') fail('file', 'The device could not read the requested file.');
        if (id !== 'DATA' || size === 0 || size > 65536 || size > sizeLimit - total)
          fail('limit', 'Invalid or oversized file response.');
        parts.push(await channel.read(size)); total += size; onProgress?.({transferred: total});
      }
    });
  }
  writeFile(path, bytes, {mode = 0o644, timeoutMs = 300000, onProgress} = {}) {
    remotePath(path);
    if (!(bytes instanceof Uint8Array) || bytes.length > MAX_FILE || !Number.isInteger(mode) || mode < 0 || mode > 0o777)
      return Promise.reject(new AdbError('argument', 'Invalid file data or permissions.'));
    return this.run('sync:', timeout(timeoutMs, 300000), async channel => {
      await channel.write(syncRecord('SEND', encoder.encode(path + ',' + (0o100000 | mode))));
      for (let offset = 0; offset < bytes.length; offset += 65536) {
        const chunk = bytes.subarray(offset, offset + 65536);
        await channel.write(syncRecord('DATA', chunk)); onProgress?.({transferred: offset + chunk.length, total: bytes.length});
      }
      await channel.write(syncRecord('DONE', EMPTY, 0));
      const header = await channel.read(8), id = text(header.subarray(0, 4));
      if (id !== 'OKAY') fail('file', 'The device could not finish writing the file.');
      await channel.quit();
    });
  }
  controlRequest(command, payload, {timeoutMs = 15000} = {}) {
    if (!['status', 'wifi-status', 'wifi-scan', 'wifi-scan-results', 'wifi-set'].includes(command))
      return Promise.reject(new AdbError('argument', 'Unsupported device control command.'));
    const body = encoder.encode(command + '\n' + (payload === undefined ? '' : JSON.stringify(payload)));
    if (body.length > 4096) return Promise.reject(new AdbError('limit', 'The control request is too large.'));
    const prefix = new Uint8Array(4); new DataView(prefix.buffer).setUint32(0, body.length, false);
    return this.run('localfilesystem:/tmp/awtrix-tc002d/control.sock', timeout(timeoutMs, 15000), async channel => {
      await channel.write(join([prefix, body]));
      const header = await channel.read(4), size = new DataView(header.buffer).getUint32(0, false);
      if (!size || size > 4096) fail('limit', 'Invalid device control response size.');
      const response = await channel.read(size);
      try { return JSON.parse(text(response)); }
      catch { fail('protocol', 'The device returned an invalid control response.'); }
    });
  }
}

function supported(device) { return USB_FILTERS.some(f => device.vendorId === f.vendorId && device.productId === f.productId); }
function sameDevice(device, identity) {
  return supported(device) && (!identity || device.vendorId === identity.vendorId && device.productId === identity.productId &&
    (device.serialNumber || '') === identity.serialNumber);
}
function environment(options) {
  const usb = options.usb ?? globalThis.navigator?.usb;
  if (!usb) fail('unsupported', 'This browser does not support USB access. Use a supported browser over HTTPS.');
  if (options.signal?.aborted) throw aborted();
  return {usb, clock: options.clock ?? clockDefault, onStatus: options.onStatus ?? (() => {}), signal: options.signal};
}
async function granted(usb, identity, deadline) {
  const all = (await deadline.wait(() => usb.getDevices())).filter(supported);
  if (all.length > 1) fail('multiple', 'Connect only one TC002 at a time.');
  return all.find(device => sameDevice(device, identity));
}

async function establish(selected, options, env) {
  const {usb, clock, signal, onStatus} = env;
  if (selected && !supported(selected)) fail('device', 'Select the TC002 USB device.');
  let identity = selected ? {vendorId: selected.vendorId, productId: selected.productId,
    serialNumber: selected.serialNumber || ''} : options.identity;
  let session, stableSince, readyAt, settleEnd;
  const deadline = new Deadline(clock, USB_CONNECT_WAIT_MS, [signal]);
  onStatus({phase: 'connecting', message: 'Waiting for USB. Connect one TC002 and switch it off and on if needed.'});
  try {
    while (clock.now() < (settleEnd ?? deadline.end)) {
      const device = await granted(usb, identity, deadline);
      if (!device || !session?.connected) {
        if (session) await session.close();
        session = null; stableSince = readyAt = undefined;
        if (device) {
          identity ??= {vendorId: device.vendorId, productId: device.productId,
            serialNumber: device.serialNumber || ''};
          const wire = new Wire(device, usb, clock, signal, onStatus);
          try {
            await wire.open(); session = new Session(wire);
            const reply = await session.rawShell(USB_KEEPER, {maxBytes: 1024});
            if (!/awtrix-usb-keeper (?:started|running) \d+/.test(reply)) fail('keeper', 'The USB keeper did not start.');
            settleEnd ??= clock.now() + USB_CONNECT_SETTLE_MS; deadline.end = settleEnd;
            onStatus({phase: 'keeper', message: 'USB keeper is running. Waiting for a stable connection.'});
            stableSince = clock.now();
          } catch (error) {
            await wire.close(); session = null;
            if (signal?.aborted) throw aborted();
            const failure = usbFailure(error);
            if (!['transport', 'timeout', 'aborted', 'disconnected'].includes(failure.code)) throw failure;
            onStatus({phase: 'reconnecting', message: failure.message});
          }
        }
      }
      if (session?.connected && stableSince !== undefined && clock.now() - stableSince >= USB_CONNECT_STABLE_MS) {
        if (readyAt === undefined) {
          try {
            const uptime = Number((await session.rawShell('cat /proc/uptime', {maxBytes: 256})).trim().split(/\s+/)[0]);
            if (!Number.isFinite(uptime) || uptime < 0) fail('protocol', 'The device returned an invalid uptime.');
            readyAt = clock.now() + Math.max(0, USB_SCAN_END_MS - uptime * 1000);
          } catch (error) {
            await session.close(); session = null; stableSince = readyAt = undefined;
            if (signal?.aborted) throw aborted();
            const failure = usbFailure(error);
            if (!['transport', 'timeout', 'aborted', 'disconnected'].includes(failure.code)) throw failure;
            onStatus({phase: 'reconnecting', message: failure.message});
          }
        }
        if (session?.connected && readyAt !== undefined && clock.now() >= readyAt) {
          onStatus({phase: 'ready', message: 'USB connection is ready.'}); return session;
        }
      }
      await sleep(clock, USB_CONNECT_POLL_MS, signal);
    }
    fail('timeout', 'USB did not become stable. Check the data cable and switch the TC002 off and on.');
  } catch (error) { if (session) await session.close(); throw error; }
}

export function connect(options = {}) {
  try {
    const env = environment(options);
    const selected = env.usb.requestDevice({filters: USB_FILTERS});
    return selected.then(device => establish(device, options, env));
  } catch (error) { return Promise.reject(error); }
}

export function connectGranted(options = {}) {
  try { return establish(null, options, environment(options)); }
  catch (error) { return Promise.reject(error); }
}
