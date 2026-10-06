const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const loaded = import('../../docs/assets/tc002/adb.js');
const encoder = new TextEncoder();
const decoder = new TextDecoder();
const commands = {CNXN: 0x4e584e43, OPEN: 0x4e45504f, OKAY: 0x59414b4f,
  CLSE: 0x45534c43, WRTE: 0x45545257, AUTH: 0x48545541};
const empty = new Uint8Array();
const bytes = value => typeof value === 'string' ? encoder.encode(value) : value;
const concat = parts => new Uint8Array(Buffer.concat(parts.map(p => Buffer.from(p))));
const sum = data => data.reduce((a, b) => (a + b) >>> 0, 0);

function record(id, data = empty, length = data.length) {
  const header = new Uint8Array(8); header.set(bytes(id));
  new DataView(header.buffer).setUint32(4, length, true);
  return concat([header, data]);
}

class Clock {
  constructor() { this.time = 0; this.advance = null; }
  now() { return this.time; }
  setTimeout(fn, ms) {
    return setTimeout(() => { this.time += ms; this.advance?.(ms); fn(); }, 0);
  }
  clearTimeout(id) { clearTimeout(id); }
}

class USB extends EventTarget {
  constructor() { super(); this.devices = []; this.requests = []; }
  requestDevice(options) { this.requests.push(options); return Promise.resolve(this.devices[0]); }
  async getDevices() { return this.devices; }
  disconnect(device) {
    this.devices = this.devices.filter(item => item !== device);
    const event = new Event('disconnect'); event.device = device; this.dispatchEvent(event);
  }
}

class Device {
  constructor(clock, backend = {}) {
    this.clock = clock; this.backend = backend; this.vendorId = 0x18d1; this.productId = 0xd002;
    this.serialNumber = '0123456789ABCDEF'; this.opened = false; this.configuration = null;
    const alternate = {interfaceClass: 255, interfaceSubclass: 66, interfaceProtocol: 1, alternateSetting: 0,
      endpoints: [{type: 'bulk', direction: 'in', endpointNumber: 1, packetSize: 64},
        {type: 'bulk', direction: 'out', endpointNumber: 2, packetSize: 64}]};
    this.configurations = [{configurationValue: 1,
      interfaces: [{interfaceNumber: 0, alternates: [alternate], alternate}]}];
    this.incoming = []; this.waiters = []; this.streams = new Map(); this.outgoing = [];
    this.calls = []; this.services = []; this.zeroWrites = 0; this.fragment = 4096; this.nextRemote = 70;
    backend.shells ??= []; backend.files ??= new Map(); backend.writes ??= []; backend.controls ??= [];
  }
  async open() { this.calls.push('open'); this.opened = true; }
  async close() {
    this.calls.push('close'); this.opened = false;
    for (const pending of this.waiters.splice(0)) pending.reject(new Error('Disconnected'));
  }
  async selectConfiguration(value) { assert.equal(value, 1); this.configuration = this.configurations[0]; }
  async claimInterface(value) { assert.equal(value, 0); if (this.claimError) throw new Error('Busy'); }
  async selectAlternateInterface() { throw new Error('Unexpected alternate change'); }
  async reset() { throw new Error('USB reset is forbidden'); }
  response(command, arg0, arg1, data = empty) {
    const header = new Uint8Array(24), view = new DataView(header.buffer), id = commands[command];
    [id, arg0, arg1, data.length, sum(data), (id ^ 0xffffffff) >>> 0]
      .forEach((value, i) => view.setUint32(i * 4, value, true));
    if (this.corrupt && command === 'WRTE') {
      this.corrupt(view, data); this.corrupt = null;
    }
    this.incoming.push(header); if (data.length) this.incoming.push(data.slice()); this.flush();
  }
  flush() {
    while (this.waiters.length && this.incoming.length) {
      const {length, resolve} = this.waiters.shift(); const next = this.incoming[0];
      const take = Math.min(length, next.length, this.fragment), out = next.slice(0, take);
      if (take === next.length) this.incoming.shift(); else this.incoming[0] = next.subarray(take);
      resolve({status: 'ok', data: new DataView(out.buffer)});
    }
  }
  transferIn(endpoint, length) {
    assert.equal(endpoint, 1);
    if (this.stalled) return Promise.reject(new Error('Stall'));
    return new Promise((resolve, reject) => { this.waiters.push({length, resolve, reject}); this.flush(); });
  }
  async transferOut(endpoint, input) {
    assert.equal(endpoint, 2); assert.ok(this.opened); const data = new Uint8Array(input);
    if (this.stalled) throw new Error('Stall');
    if (this.failWrite) { this.failWrite = false; return {status: 'ok', bytesWritten: data.length - 1}; }
    if (!data.length) { ++this.zeroWrites; return {status: 'ok', bytesWritten: 0}; }
    if (!this.pending) {
      assert.equal(data.length, 24);
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      const id = view.getUint32(0, true);
      assert.equal(view.getUint32(20, true), (id ^ 0xffffffff) >>> 0);
      const command = Object.keys(commands).find(key => commands[key] === id);
      assert.ok(command);
      this.pending = {command, arg0: view.getUint32(4, true), arg1: view.getUint32(8, true),
        length: view.getUint32(12, true), checksum: view.getUint32(16, true)};
      assert.ok(this.pending.length <= 4096);
      if (!this.pending.length) { const packet = this.pending; this.pending = null; this.handle(packet, empty); }
    } else {
      const packet = this.pending; this.pending = null;
      assert.equal(packet.length, data.length); assert.equal(packet.checksum, sum(data));
      this.handle(packet, data);
    }
    return {status: 'ok', bytesWritten: data.length};
  }
  deliver(stream, data, finish = false) {
    for (let offset = 0; offset < data.length; offset += 997) stream.responses.push(data.slice(offset, offset + 997));
    stream.finish = finish; this.next(stream);
  }
  next(stream) {
    if (stream.awaiting) return;
    if (stream.responses.length) {
      stream.awaiting = true; this.response('WRTE', stream.remote, stream.local, stream.responses.shift());
    } else if (stream.finish) {
      stream.finish = false; this.response('CLSE', 0, stream.local);
    }
  }
  handle(packet, data) {
    this.outgoing.push({...packet, data: data.slice()});
    if (packet.command === 'CNXN') {
      assert.equal(packet.arg0, 0x01000000); assert.equal(decoder.decode(data), 'host::\0');
      for (let i = 0; i < (this.staleBeforeConnect ?? 0); i++) this.response(i % 2 ? 'CLSE' : 'OKAY', 7, 3);
      this.response(this.authenticate ? 'AUTH' : 'CNXN', this.authenticate ? 1 : 0x01000000,
        this.authenticate ? 0 : 4096, bytes(this.authenticate ? 'token' : 'device::ro.product.name=TC002;\0'));
      return;
    }
    if (packet.command === 'OPEN') {
      const service = decoder.decode(data).slice(0, -1); this.services.push(service);
      const stream = {local: packet.arg0, remote: this.nextRemote++, service, responses: [], incoming: empty};
      this.streams.set(stream.local, stream);
      if (this.rejectService) { this.response('CLSE', 0, stream.local); return; }
      this.response('OKAY', stream.remote, stream.local);
      if (service.startsWith('shell:')) {
        const command = service.slice(6); this.backend.shells.push(command);
        if (command.includes('awtrix-usb-keeper.pid')) {
          const state = this.backend.keeperRunning ? 'running' : 'started';
          this.backend.keeperRunning = true;
          if (!this.loseKeeperReply) this.deliver(stream, bytes(`awtrix-usb-keeper ${state} 321\n`), true);
        }
        else if (command === 'cat /proc/uptime') this.deliver(stream, bytes(`${this.clock.now() / 1000 + 2} 0\n`), true);
        else if (!this.hangShell) this.deliver(stream, bytes(this.shellOutput ?? 'hello\r\n__AWTRIX_RC=0\r\n'), true);
      }
      return;
    }
    const stream = this.streams.get(packet.arg0); assert.ok(stream);
    assert.equal(packet.arg1, stream.remote);
    if (packet.command === 'OKAY') {
      assert.ok(stream.awaiting); stream.awaiting = false; this.next(stream); return;
    }
    if (packet.command === 'CLSE') {
      if (stream.service === 'sync:' && !stream.quit) this.stalled = true;
      this.streams.delete(stream.local); return;
    }
    assert.equal(packet.command, 'WRTE');
    this.response('OKAY', stream.remote, stream.local);
    stream.incoming = concat([stream.incoming, data]);
    if (stream.service === 'sync:') this.sync(stream);
    else this.control(stream);
  }
  sync(stream) {
    while (stream.incoming.length >= 8) {
      const id = decoder.decode(stream.incoming.subarray(0, 4));
      const length = new DataView(stream.incoming.buffer).getUint32(4, true);
      if (id !== 'DONE' && id !== 'QUIT' && stream.incoming.length < length + 8) return;
      const bare = id === 'DONE' || id === 'QUIT';
      const data = stream.incoming.slice(8, bare ? 8 : 8 + length);
      stream.incoming = stream.incoming.slice(bare ? 8 : 8 + length);
      if (id === 'RECV') {
        const file = this.backend.files.get(decoder.decode(data));
        if (this.recvResponse) this.deliver(stream, this.recvResponse, this.recvFinish ?? false);
        else if (!file) this.deliver(stream, record('FAIL', bytes('File missing')));
        else {
          const records = [];
          for (let offset = 0; offset < file.length; offset += 65536) records.push(record('DATA', file.slice(offset, offset + 65536)));
          records.push(record('DONE')); this.deliver(stream, concat(records));
        }
      } else if (id === 'SEND') {
        stream.destination = decoder.decode(data); stream.fileParts = [];
      } else if (id === 'DATA') {
        assert.ok(length > 0 && length <= 65536); stream.fileParts.push(data);
      } else if (id === 'QUIT') {
        assert.equal(length, 0); stream.quit = true; this.deliver(stream, empty, true);
      } else if (id === 'DONE') {
        assert.equal(length, 0);
        this.backend.writes.push({destination: stream.destination, data: concat(stream.fileParts)});
        this.deliver(stream, record(this.writeFail ? 'FAIL' : 'OKAY'));
      } else assert.fail(`Unexpected sync record ${id}`);
    }
  }
  control(stream) {
    assert.equal(stream.service, 'localfilesystem:/tmp/awtrix-tc002d/control.sock');
    if (stream.incoming.length < 4) return;
    const size = new DataView(stream.incoming.buffer).getUint32(0, false);
    assert.ok(size <= 4096);
    if (stream.incoming.length < size + 4) return;
    const body = decoder.decode(stream.incoming.subarray(4)), split = body.indexOf('\n');
    this.backend.controls.push({command: body.slice(0, split), payload: body.slice(split + 1)});
    const response = bytes(this.controlBody ?? JSON.stringify({ok: true, link: 'connected'}));
    const header = new Uint8Array(4); new DataView(header.buffer).setUint32(0, this.controlSize ?? response.length, false);
    this.deliver(stream, concat([header, response]));
  }
}

async function fixture(options = {}) {
  const api = await loaded, clock = new Clock(), usb = new USB(), backend = {};
  const device = new Device(clock, backend); usb.devices.push(device);
  Object.assign(device, options.device);
  const session = await api.connect({usb, clock, ...options.connect});
  return {api, clock, usb, device, backend, session};
}

test('USB chooser is called in the click stack and keeper stabilizes without USB resets', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(); const device = new Device(clock);
  usb.devices.push(device); const phases = [];
  const pending = api.connect({usb, clock, onStatus: status => phases.push(status.phase)});
  assert.equal(usb.requests.length, 1); assert.deepEqual(usb.requests[0], {filters: api.USB_FILTERS});
  const session = await pending;
  assert.ok(clock.time >= 8000); assert.deepEqual(phases, ['connecting', 'opening', 'keeper', 'ready']);
  assert.equal(device.backend.shells.filter(s => s.includes('awtrix-usb-keeper.pid')).length, 1);
  assert.equal(device.calls.filter(c => c === 'open').length, 1);
  assert.equal(session.connected, true); await session.close(); assert.equal(session.connected, false);
});

test('packets left over from an abandoned connection are skipped before the handshake', async () => {
  const {session} = await fixture({device: {staleBeforeConnect: 3}});
  assert.equal(await session.shell('printf hello'), 'hello\n');
  await session.close();
});

test('a stream packet from another device stream is rejected', async () => {
  const {session, device} = await fixture();
  device.corrupt = view => view.setUint32(4, 99, true);
  await assert.rejects(session.rawShell('echo hello'), error => error.code === 'protocol' && /identifier changed/.test(error.message));
});

test('RAM keeper exactly matches the maintained Python installer', async () => {
  const api = await loaded;
  const script = "import sys;sys.path.insert(0,'tools/tc002/lib');from tc002_device import USB_KEEPER;print(USB_KEEPER)";
  const result = spawnSync(process.platform === 'win32' ? 'python' : 'python3', ['-c', script],
    {cwd: path.join(__dirname, '../..'), encoding: 'utf8'});
  assert.equal(result.status, 0, result.stderr); assert.equal(api.USB_KEEPER, result.stdout.trim());
});

test('fragmented legacy packets retain checksums, stream IDs and shell exit status', async () => {
  const {session, device} = await fixture({device: {fragment: 7}});
  assert.equal(await session.shell('printf hello'), 'hello\n');
  device.shellOutput = 'do not expose password\n__AWTRIX_RC=7\n';
  await assert.rejects(session.shell('secret-command'), error => error.code === 'command' &&
    !error.message.includes('password') && !error.message.includes('secret-command'));
  assert.equal(session.connected, true);
  const count = device.services.length;
  await assert.rejects(session.shell(undefined), {code: 'argument'});
  await assert.rejects(session.shell('x'.repeat(3072)), {code: 'argument'});
  await assert.rejects(session.shell('must-not-run', {maxBytes: 0}), {code: 'argument'});
  assert.equal(device.services.length, count);
  await session.close();
});

test('shell output cap fails closed without including command or output', async () => {
  const {session, device} = await fixture(); device.shellOutput = 'secret'.repeat(100);
  await assert.rejects(session.shell('secret', {maxBytes: 30}), {code: 'limit'});
  assert.equal(session.connected, false);
});

test('SYNC reads binary records across ADB and USB packet boundaries', async () => {
  const {session, backend, device} = await fixture(); device.fragment = 31;
  const expected = Uint8Array.from({length: 140123}, (_, i) => i % 251);
  backend.files.set('/tmp/backup.bin', expected); const progress = [];
  assert.deepEqual(await session.readFile('/tmp/backup.bin', {onProgress: p => progress.push(p.transferred)}), expected);
  assert.deepEqual(progress, [65536, 131072, expected.length]);
  backend.files.set('/tmp/empty', empty); assert.deepEqual(await session.readFile('/tmp/empty'), empty);
  await session.close();
});

test('SYNC sends bounded chunks, decimal mode and legacy zero length USB packets', async () => {
  const {session, backend, device} = await fixture();
  const expected = Uint8Array.from({length: 140123}, (_, i) => (i * 13) % 256), progress = [];
  await session.writeFile('/tmp/loader', expected, {mode: 0o755, onProgress: p => progress.push(p)});
  assert.equal(backend.writes[0].destination, '/tmp/loader,33261');
  assert.deepEqual(backend.writes[0].data, expected); assert.ok(device.zeroWrites > 0);
  assert.deepEqual(progress.map(p => p.transferred), [65536, 131072, expected.length]);
  assert.ok(progress.every(p => p.total === expected.length));
  await session.close();
});

test('SYNC rejects overflow, oversized records, truncation and device errors', async t => {
  for (const [name, response, maxBytes, code] of [
    ['overflow', record('DATA', bytes('123456')), 5, 'limit'],
    ['oversized-record', record('DATA', empty, 65537), 100000, 'limit'],
    ['truncated', record('DATA', bytes('123'), 5), 5, 'protocol'],
    ['read-failed', record('FAIL', bytes('sensitive-device-error')), 5, 'file'],
  ]) await t.test(name, async () => {
    const {session, device} = await fixture(); device.recvResponse = response; device.recvFinish = true;
    await assert.rejects(session.readFile('/tmp/file', {maxBytes}), error => error.code === code &&
      !error.message.includes('sensitive-device-error'));
    assert.equal(session.connected, false);
  });
});

test('invalid local arguments never start a file transfer', async () => {
  const {session, device} = await fixture(); const before = device.services.length;
  for (const name of ['relative', '/tmp/../secret', '/tmp/./file', '/tmp/a\nfile'])
    assert.throws(() => session.readFile(name), {code: 'argument'});
  await assert.rejects(session.writeFile('/tmp/file', empty, {mode: 0o7777}), {code: 'argument'});
  assert.equal(device.services.length, before); await session.close();
});

test('WiFi control uses the daemon big endian framing and never a shell password', async () => {
  const {session, backend, device} = await fixture(); device.fragment = 5;
  const payload = {ssid: 'Kitchen °', password: 'private $() secret'};
  assert.deepEqual(await session.controlRequest('wifi-set', payload), {ok: true, link: 'connected'});
  assert.deepEqual(backend.controls, [{command: 'wifi-set', payload: JSON.stringify(payload)}]);
  assert.ok(backend.shells.every(command => !command.includes(payload.password)));
  await session.controlRequest('wifi-status'); assert.equal(backend.controls[1].payload, '');
  await assert.rejects(session.controlRequest('shell'), {code: 'argument'});
  await assert.rejects(session.controlRequest('wifi-set', {password: 'x'.repeat(4096)}), {code: 'limit'});
  await session.close();
});

test('WiFi response limits and invalid JSON fail closed', async t => {
  for (const options of [{controlSize: 4097}, {controlSize: 0}, {controlBody: '{invalid'}])
    await t.test(JSON.stringify(options), async () => {
      const {session, device} = await fixture(); Object.assign(device, options);
      await assert.rejects(session.controlRequest('status'), {code: options.controlBody ? 'protocol' : 'limit'});
      assert.equal(session.connected, false);
    });
});

test('corrupt ADB checksums, magic, packet lengths and stream IDs fail closed', async t => {
  for (const [name, corrupt] of [
    ['checksum', view => view.setUint32(16, 0, true)],
    ['magic', view => view.setUint32(20, 0, true)],
    ['length', view => view.setUint32(12, 4097, true)],
    ['remote-id', view => view.setUint32(4, 12345, true)],
    ['command', view => view.setUint32(0, 12345, true)],
  ]) await t.test(name, async () => {
    const {session, device} = await fixture(); device.corrupt = corrupt;
    await assert.rejects(session.shell('echo test'), {code: 'protocol'}); assert.equal(session.connected, false);
  });
});

test('a partial USB write closes and does not retry the file operation', async () => {
  const {session, device, backend} = await fixture(); device.failWrite = true;
  await assert.rejects(session.writeFile('/tmp/file', bytes('123')), {code: 'transport'});
  assert.equal(backend.writes.length, 0); assert.equal(session.connected, false);
  assert.equal(device.calls.filter(c => c === 'open').length, 1);
});

test('timeout, disconnect and abort stop pending work, while concurrent calls are rejected', async t => {
  await t.test('timeout and concurrent operation', async () => {
    const {session, device} = await fixture(); device.hangShell = true;
    const first = session.shell('sleep forever', {timeoutMs: 50});
    await assert.rejects(session.shell('echo concurrent'), {code: 'busy'});
    await assert.rejects(first, {code: 'timeout'}); assert.equal(session.connected, false);
  });
  await t.test('disconnect', async () => {
    const {session, device, usb} = await fixture(); device.hangShell = true;
    const pending = session.shell('sleep forever'); usb.disconnect(device);
    await assert.rejects(pending, {code: 'disconnected'}); assert.equal(session.connected, false);
  });
  await t.test('abort', async () => {
    const controller = new AbortController();
    const {session, device} = await fixture({connect: {signal: controller.signal}}); device.hangShell = true;
    const pending = session.shell('sleep forever'); controller.abort();
    await assert.rejects(pending, {code: 'aborted'}); assert.equal(session.connected, false);
  });
});

test('unsupported authentication, claimed USB interface and ambiguous devices are rejected', async t => {
  await t.test('authentication', async () => {
    await assert.rejects(fixture({device: {authenticate: true}}), {code: 'authentication'});
  });
  await t.test('claimed', async () => {
    await assert.rejects(fixture({device: {claimError: true}}), {code: 'claim'});
  });
  await t.test('multiple', async () => {
    const api = await loaded, clock = new Clock(), usb = new USB(); usb.devices = [new Device(clock), new Device(clock)];
    await assert.rejects(api.connect({usb, clock}), {code: 'multiple'});
    assert.ok(usb.devices.every(d => !d.opened));
  });
});

test('USB re-enumeration confirms the existing RAM keeper and waits for a stable replacement', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(), backend = {};
  const first = new Device(clock, backend), replacement = new Device(clock, backend); usb.devices = [first];
  let detached = false, attached = false;
  clock.advance = () => {
    if (!detached && clock.time >= 1500) { detached = true; usb.disconnect(first); }
    if (detached && !attached && clock.time >= 4500) { attached = true; usb.devices = [replacement]; }
  };
  const session = await api.connect({usb, clock});
  assert.ok(attached); assert.equal(first.opened, false); assert.equal(replacement.opened, true);
  assert.equal(backend.shells.filter(s => s.includes('awtrix-usb-keeper.pid')).length, 2);
  assert.ok(clock.time >= 8500); await session.close();
});

test('automatic USB discovery catches a cold boot without requesting a chooser', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(); const device = new Device(clock);
  clock.advance = () => {
    if (clock.time >= 1200 && (clock.time < 3900 || device.backend.keeperRunning)) usb.devices = [device];
    else usb.devices = [];
  };
  const session = await api.connectGranted({usb, clock});
  assert.equal(usb.requests.length, 0); assert.equal(session.connected, true);
  assert.ok(device.backend.keeperRunning); assert.ok(clock.time > 3900);
  assert.equal(device.calls.filter(call => call === 'open').length, 1);
  await session.close();
});

test('equivalent USBDevice wrappers do not restart an established connection', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(); const device = new Device(clock);
  let count = 0;
  usb.getDevices = async () => [count++ ? {...device} : device];
  const session = await api.connectGranted({usb, clock});
  assert.equal(session.connected, true); assert.ok(count > 10);
  assert.equal(device.calls.filter(call => call === 'open').length, 1);
  assert.equal(device.backend.shells.filter(command => command.includes('awtrix-usb-keeper.pid')).length, 1);
  await session.close();
});

test('a lost keeper acknowledgement is recovered by confirming the keeper after re-enumeration', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(), backend = {};
  const first = new Device(clock, backend), replacement = new Device(clock, backend);
  first.loseKeeperReply = true; usb.devices = [first]; let replaced = false;
  clock.advance = () => {
    if (!replaced && backend.keeperRunning) {
      replaced = true; usb.disconnect(first); usb.devices = [replacement];
    }
  };
  const session = await api.connectGranted({usb, clock});
  assert.equal(session.connected, true); assert.equal(replaced, true); assert.equal(first.opened, false);
  assert.equal(replacement.opened, true);
  assert.equal(backend.shells.filter(command => command.includes('awtrix-usb-keeper.pid')).length, 2);
  await session.close();
});

test('a power cycle starts a fresh RAM keeper before automatic discovery becomes ready', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB();
  const first = new Device(clock), replacement = new Device(clock); usb.devices = [first]; let replaced = false;
  clock.advance = () => {
    if (!replaced && clock.time >= 1500) {
      replaced = true; usb.disconnect(first); usb.devices = [replacement];
    }
  };
  const session = await api.connectGranted({usb, clock});
  assert.equal(replaced, true); assert.equal(session.connected, true);
  assert.equal(first.backend.keeperRunning, true); assert.equal(replacement.backend.keeperRunning, true);
  await session.close();
});

test('automatic discovery pins its first device and does not switch to another serial', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(), controller = new AbortController();
  const first = new Device(clock), other = new Device(clock); other.serialNumber = 'OTHER';
  usb.devices = [first]; let replaced = false;
  clock.advance = () => {
    if (!replaced && clock.time >= 1500) {
      replaced = true; usb.disconnect(first); usb.devices = [other];
    }
    if (clock.time >= 12000) controller.abort();
  };
  await assert.rejects(api.connectGranted({usb, clock, signal: controller.signal}), {code: 'aborted'});
  assert.equal(other.calls.length, 0); assert.equal(usb.requests.length, 0);
});

test('automatic discovery refuses multiple identical TC002 devices', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB();
  usb.devices = [new Device(clock), new Device(clock)];
  await assert.rejects(api.connectGranted({usb, clock}), {code: 'multiple'});
  assert.equal(usb.requests.length, 0); assert.ok(usb.devices.every(device => !device.opened));
});

test('already granted reconnect does not request another chooser', async () => {
  const {api, session, usb, clock} = await fixture(); const identity = session.identity; await session.close();
  const requests = usb.requests.length; const replacement = new Device(clock); usb.devices = [replacement];
  const reconnected = await api.connectGranted({usb, clock, identity});
  assert.equal(usb.requests.length, requests); assert.equal(reconnected.connected, true);
  assert.equal(replacement.backend.shells.filter(s => s.includes('awtrix-usb-keeper.pid')).length, 1);
  await reconnected.close();
});

test('late first discovery still gets its separate stabilization window', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(); usb.devices = [new Device(clock)];
  let first = true;
  usb.getDevices = async () => { if (first) { clock.time += 119000; first = false; } return usb.devices; };
  const session = await api.connect({usb, clock});
  assert.ok(clock.time >= 123000); assert.equal(session.connected, true); await session.close();
});

test('cancelled connection never opens the browser chooser', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(), controller = new AbortController();
  controller.abort();
  await assert.rejects(api.connect({usb, clock, signal: controller.signal}), {code: 'aborted'});
  assert.equal(usb.requests.length, 0);
});

test('native USB descriptors may be hydrated by opening the device', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(), device = new Device(clock);
  const configurations = device.configurations;
  device.configurations = [];
  const open = device.open.bind(device);
  device.open = async () => { await open(); device.configurations = configurations; };
  usb.devices = [device];
  const session = await api.connectGranted({usb, clock});
  assert.equal(session.connected, true);
  assert.equal(device.calls.filter(call => call === 'open').length, 1);
  await session.close();
});

test('native permission and driver failures stop discovery instead of being hidden by a timeout', async t => {
  for (const failure of [new DOMException('Denied', 'SecurityError'),
    {code: 'denied', message: 'Linux USB access denied; check device permissions.'},
    {code: 'unsupported', message: 'Windows USB interface requires WinUSB.'}]) {
    await t.test(failure.name || failure.code, async () => {
      const api = await loaded, clock = new Clock(), usb = new USB(), device = new Device(clock);
      let opens = 0;
      device.open = async () => { opens++; throw failure; };
      usb.devices = [device];
      await assert.rejects(api.connectGranted({usb, clock}), error => ['denied', 'unsupported'].includes(error.code));
      assert.equal(opens, 1);
      assert.ok(clock.time < 120000);
    });
  }
});

test('a transient native open failure remains retryable and reports reconnecting', async () => {
  const api = await loaded, clock = new Clock(), usb = new USB(), device = new Device(clock), phases = [];
  const open = device.open.bind(device); let attempts = 0;
  device.open = async () => { if (++attempts === 1) throw {code: 'transport', message: 'USB interface is starting.'}; await open(); };
  usb.devices = [device];
  const session = await api.connectGranted({usb, clock, onStatus: event => phases.push(event.phase)});
  assert.equal(attempts, 2); assert.ok(phases.includes('reconnecting'));
  await session.close();
});
