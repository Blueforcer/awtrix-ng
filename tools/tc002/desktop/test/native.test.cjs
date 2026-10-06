const {test} = require('node:test');
const assert = require('node:assert/strict');
const api = import('../ui/native.js');
const defer = () => { let resolve, reject; const promise = new Promise((yes, no) => { resolve = yes; reject = no; }); return {promise, resolve, reject}; };
const settle = () => new Promise(resolve => setImmediate(resolve));
const metadata = (id = 'device-1', hydrate = false, sessionId = 'session-1') => {
  const alternate = {alternateSetting: 0, interfaceClass: 255, interfaceSubclass: 66, interfaceProtocol: 1,
    endpoints: [{endpointNumber: 1, direction: 'in', type: 'bulk', packetSize: 64}, {endpointNumber: 2, direction: 'out', type: 'bulk', packetSize: 64}]};
  return {id, vendorId: 0x18d1, productId: 0xd002, serialNumber: 'synthetic', configuration: null,
    configurations: hydrate ? [{configurationValue: 1, interfaces: [{interfaceNumber: 0, alternate: null, alternates: [alternate]}]}] : [],
    ...(hydrate ? {sessionId} : {})};
};
async function fixture(handler = () => undefined) {
  const calls = [], listeners = new Map();
  const invoke = async (command, args, options) => {
    calls.push({command, args, options});
    const custom = handler(command, args, options);
    if (custom !== undefined) return custom;
    if (command === 'usb_list') return [metadata()];
    if (command === 'usb_open') return metadata(args.id, true);
    if (command === 'usb_transfer_in') return new Uint8Array([1, 2, 3]).buffer;
    if (command === 'usb_transfer_out') return args.length;
  };
  const listen = async (name, callback) => { calls.push({listen: name}); listeners.set(name, callback); return () => { calls.push({unlisten: name}); listeners.delete(name); }; };
  const usb = (await api).createNativeUsb({invoke, listen});
  return {usb, calls, invoke, listen, emit: payload => listeners.get('usb-event')({payload}), async device() { return (await usb.getDevices())[0]; }};
}

test('native adapter is lazy and subscribes before enumeration', async () => {
  const f = await fixture();
  assert.deepEqual(f.calls, []);
  const device = await f.device();
  assert.equal(f.calls[0].listen, 'usb-event');
  assert.equal(f.calls[1].command, 'usb_list');
  assert.equal(device.configurations.length, 0);
  assert.equal(device.opened, false);
  await f.usb.destroy();
});

test('native adapter preserves stable wrappers and hydrated descriptors across polling', async () => {
  const f = await fixture(), device = await f.device();
  await device.open();
  assert.equal(device.configurations.length, 1);
  assert.equal(device.configuration, null);
  assert.equal(await f.device(), device);
  assert.equal(device.configurations.length, 1);
  await device.selectConfiguration(1);
  await device.claimInterface(0);
  assert.equal(device.configuration.interfaces[0].alternate, null);
  await device.selectAlternateInterface(0, 0);
  assert.equal(device.configuration.interfaces[0].alternate.alternateSetting, 0);
  assert.ok(f.calls.filter(call => call.command && !['usb_list', 'usb_open'].includes(call.command)).every(call => call.args.sessionId === 'session-1'));
  await f.usb.destroy();
});

test('native binary transfers retain their session and exact bytes including zero-length packets', async () => {
  const f = await fixture(), device = await f.device(); await device.open();
  const result = await device.transferIn(1, 24);
  assert.deepEqual([...new Uint8Array(result.data.buffer, result.data.byteOffset, result.data.byteLength)], [1, 2, 3]);
  const source = new Uint8Array([9, 8, 7, 6]);
  assert.deepEqual(await device.transferOut(2, source.subarray(1, 3)), {status: 'ok', bytesWritten: 2});
  assert.deepEqual(await device.transferOut(2, new Uint8Array()), {status: 'ok', bytesWritten: 0});
  const sent = f.calls.find(call => call.command === 'usb_transfer_out');
  assert.deepEqual([...sent.args], [8, 7]);
  assert.deepEqual(sent.options.headers, {'x-device-id': 'device-1', 'x-session-id': 'session-1', 'x-endpoint': '2'});
  assert.notEqual(sent.args.buffer, source.buffer);
  await f.usb.destroy();
});

test('native disconnect emits the cached wrapper and rejects a late transfer result', async () => {
  const pending = defer();
  const f = await fixture(command => command === 'usb_transfer_in' ? pending.promise : undefined);
  const device = await f.device(); await device.open();
  const events = []; f.usb.addEventListener('disconnect', event => events.push(event.device));
  const transfer = device.transferIn(1, 24);
  const rejected = assert.rejects(transfer, {code: 'disconnected'});
  f.emit({kind: 'disconnect', device: metadata()});
  pending.resolve(new Uint8Array([1]).buffer); await rejected;
  assert.equal(events[0], device);
  assert.equal(device.opened, false);
  assert.deepEqual(await f.usb.getDevices(), []);
  await f.usb.destroy();
});

test('native close during a pending open cleans up that exact late session', async () => {
  const pending = defer();
  const f = await fixture(command => command === 'usb_open' ? pending.promise : undefined);
  const device = await f.device();
  const opening = device.open();
  const rejected = assert.rejects(opening, {code: 'disconnected'});
  const closing = device.close();
  pending.resolve(metadata('device-1', true, 'session-late'));
  await rejected; await closing;
  assert.deepEqual(f.calls.find(call => call.command === 'usb_close').args, {id: 'device-1', sessionId: 'session-late'});
  assert.equal(device.opened, false);
  await f.usb.destroy();
});

test('a stale close cannot clear a newly opened native session', async () => {
  const closing = defer(); let opens = 0, closes = 0;
  const f = await fixture((command, args) => {
    if (command === 'usb_open') return metadata(args.id, true, `session-${++opens}`);
    if (command === 'usb_close' && ++closes === 1) return closing.promise;
  });
  const device = await f.device(); await device.open();
  const oldClose = device.close();
  await device.open();
  closing.resolve(); await oldClose;
  assert.equal(device.opened, true);
  await device.transferOut(2, new Uint8Array([1]));
  const sent = f.calls.find(call => call.command === 'usb_transfer_out');
  assert.equal(sent.options.headers['x-session-id'], 'session-2');
  await f.usb.destroy();
});

test('hotplug during enumeration cannot resurrect removed devices or lose a newly connected generation', async () => {
  const pending = defer(); let lists = 0;
  const f = await fixture(command => command === 'usb_list' && ++lists === 2 ? pending.promise : undefined);
  await f.device();
  const listing = f.usb.getDevices(); await settle();
  f.emit({kind: 'disconnect', device: metadata()});
  f.emit({kind: 'connect', device: metadata('device-2')});
  pending.resolve([metadata()]);
  const result = await listing;
  assert.deepEqual(result.map(device => device.id), ['device-2']);
  await f.usb.destroy();
});

test('native access failures retain actionable error code and message', async () => {
  const f = await fixture(command => command === 'usb_open' ? Promise.reject({code: 'denied', message: 'USB permission denied. Check your Linux device rules.'}) : undefined);
  const device = await f.device();
  await assert.rejects(device.open(), {name: 'NativeUsbError', code: 'denied', message: 'USB permission denied. Check your Linux device rules.'});
  assert.equal(device.opened, false);
  await f.usb.destroy();
});

test('firmware download registers progress before invocation and always removes its listener', async () => {
  const calls = [], progress = [];
  let callback;
  const bridge = (await api).createNativeBridge({core: {invoke: async (command, args) => {
    calls.push(command); callback({payload: {requestId: args.requestId, stage: 'downloading', received: 4, total: 8}});
    return new Uint8Array([1, 2]).buffer;
  }}, event: {listen: async (name, listener) => { calls.push(name); callback = listener; return () => calls.push('unlisten'); }}});
  assert.deepEqual([...await bridge.desktop.firmware(value => progress.push(value))], [1, 2]);
  assert.deepEqual(calls, ['firmware-progress', 'firmware', 'unlisten']);
  assert.equal(progress[0].received, 4);
  await bridge.destroy();
  const failure = (await api).createNativeBridge({core: {invoke: async () => { throw {code: 'no-compatible-release', message: 'No TC002 firmware available.'}; }}, event: {listen: async () => () => calls.push('failure-unlisten')}});
  await assert.rejects(failure.desktop.firmware(), {code: 'no-compatible-release'});
  assert.equal(calls.at(-1), 'failure-unlisten');
  await failure.destroy();
});

test('native bridge forwards both local firmware progress events without changing their source', async () => {
  const progress = []; let callback, removed = false;
  const bridge = (await api).createNativeBridge({core: {invoke: async (command, args) => {
    assert.equal(command, 'firmware');
    callback({payload: {requestId: args.requestId, stage: 'local', received: 0, total: 2}});
    callback({payload: {requestId: args.requestId, stage: 'local', received: 2, total: 2}});
    return new Uint8Array([1, 2]).buffer;
  }}, event: {listen: async (name, listener) => {
    assert.equal(name, 'firmware-progress'); callback = listener; return () => { removed = true; };
  }}});
  assert.deepEqual([...await bridge.desktop.firmware(value => progress.push(value))], [1, 2]);
  assert.deepEqual(progress, [{requestId: '1', stage: 'local', received: 0, total: 2}, {requestId: '1', stage: 'local', received: 2, total: 2}]);
  assert.equal(removed, true);
  await bridge.destroy();
});

test('native firmware picker is opened only by request and preserves cancellation', async () => {
  const calls = []; let chosen = false;
  const bridge = (await api).createNativeBridge({core: {invoke: async (command, args) => {
    calls.push({command, args}); return chosen;
  }}, event: {listen: async () => () => {}}});
  assert.deepEqual(calls, []);
  assert.equal(await bridge.desktop.chooseFirmware(), false);
  chosen = true;
  assert.equal(await bridge.desktop.chooseFirmware(), true);
  assert.deepEqual(calls, [{command: 'choose_firmware', args: undefined}, {command: 'choose_firmware', args: undefined}]);
  await bridge.destroy();
});

test('native bridge isolates queued firmware progress by request ID across overlapping loads', async () => {
  const listeners = new Set(), requests = [], oldProgress = [], newProgress = [];
  const bridge = (await api).createNativeBridge({core: {invoke: (command, args) => {
    assert.equal(command, 'firmware'); const pending = defer(); requests.push({id: args.requestId, pending}); return pending.promise;
  }}, event: {listen: async (name, callback) => { listeners.add(callback); return () => listeners.delete(callback); }}});
  const oldLoad = bridge.desktop.firmware(value => oldProgress.push(value));
  const newLoad = bridge.desktop.firmware(value => newProgress.push(value));
  await settle();
  assert.deepEqual(requests.map(value => value.id), ['1', '2']);
  for (const callback of listeners) {
    callback({payload: {requestId: '1', stage: 'downloading', received: 1, total: 4}});
    callback({payload: {requestId: '2', stage: 'local', received: 2, total: 2}});
    callback({payload: {requestId: 'unrelated', stage: 'verified', received: 4, total: 4}});
  }
  assert.deepEqual(oldProgress.map(value => value.stage), ['downloading']);
  assert.deepEqual(newProgress.map(value => value.stage), ['local']);
  requests[1].pending.resolve(new Uint8Array([2]).buffer); await newLoad;
  requests[0].pending.resolve(new Uint8Array([1]).buffer); await oldLoad;
  assert.equal(listeners.size, 0);
  await bridge.destroy();
});

test('destroy during listener startup unregisters the late subscription and prevents enumeration', async () => {
  const pending = defer(); let unlistened = 0, enumerated = 0;
  const usb = (await api).createNativeUsb({listen: () => pending.promise, invoke: async () => { enumerated++; return []; }});
  const listing = usb.getDevices();
  const rejected = assert.rejects(listing, {code: 'disconnected'});
  await usb.destroy(); pending.resolve(() => { unlistened++; });
  await rejected;
  assert.equal(unlistened, 1); assert.equal(enumerated, 0);
});
