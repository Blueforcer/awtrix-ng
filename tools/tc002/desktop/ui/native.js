const USB_VENDOR = 0x18d1;
const USB_PRODUCT = 0xd002;
const MAX_TRANSFER = 1024 * 1024;
const CODES = new Set(['denied', 'claim', 'disconnected', 'unsupported', 'invalid', 'transport', 'timeout']);

export class NativeUsbError extends Error {
  constructor(code, message) { super(message); this.name = 'NativeUsbError'; this.code = code; }
}

function failure(value) {
  if (value instanceof NativeUsbError) return value;
  const code = CODES.has(value?.code) ? value.code : 'transport';
  return new NativeUsbError(code, typeof value?.message === 'string' ? value.message : 'The USB operation could not finish.');
}

function disconnected() { return new NativeUsbError('disconnected', 'USB disconnected. Reconnect the same clock to continue.'); }
function identifier(value) { return typeof value === 'string' && /^[A-Za-z0-9_.:-]{1,128}$/.test(value); }
function validDevice(value) {
  return value && identifier(value.id) && value.vendorId === USB_VENDOR && value.productId === USB_PRODUCT &&
    typeof value.serialNumber === 'string' && Array.isArray(value.configurations);
}
function bytes(value) {
  if (value instanceof Uint8Array) return value;
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  if (Array.isArray(value) && value.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255)) return Uint8Array.from(value);
  throw new NativeUsbError('invalid', 'The native USB reply is invalid.');
}
function endpoint(value) {
  if (!Number.isInteger(value) || value < 1 || value > 15) throw new NativeUsbError('invalid', 'Invalid USB endpoint.');
}

class NativeDevice {
  constructor(metadata, invoke) {
    this.id = metadata.id; this.invoke = invoke; this.epoch = 0; this.opened = false;
    this.connected = true; this.sessionId = null; this.opening = null; this.closing = null;
    this.configurations = []; this.configuration = null;
    this.update(metadata);
  }
  update(metadata, hydrated = false) {
    if (!validDevice(metadata) || metadata.id !== this.id) throw new NativeUsbError('invalid', 'The native USB descriptor is invalid.');
    this.vendorId = metadata.vendorId; this.productId = metadata.productId; this.serialNumber = metadata.serialNumber;
    if (hydrated || metadata.configurations.length) this.configurations = metadata.configurations;
    if (hydrated) this.configuration = metadata.configuration || null;
  }
  async call(command, payload, options) {
    try { return await this.invoke(command, payload, options); }
    catch (error) { throw failure(error); }
  }
  open() {
    if (!this.connected) return Promise.reject(disconnected());
    if (this.opened) return Promise.resolve();
    if (this.opening) return this.opening;
    const epoch = ++this.epoch;
    const opening = (async () => {
      const metadata = await this.call('usb_open', {id: this.id});
      if (!identifier(metadata?.sessionId)) throw new NativeUsbError('invalid', 'The native USB session is invalid.');
      if (!this.connected || this.epoch !== epoch) {
        await this.call('usb_close', {id: this.id, sessionId: metadata.sessionId}).catch(() => {});
        throw disconnected();
      }
      try { this.update(metadata, true); }
      catch (error) {
        await this.call('usb_close', {id: this.id, sessionId: metadata.sessionId}).catch(() => {});
        throw error;
      }
      this.sessionId = metadata.sessionId; this.opened = true;
    })().finally(() => { if (this.opening === opening) this.opening = null; });
    this.opening = opening;
    return opening;
  }
  close() {
    ++this.epoch;
    const sessionId = this.sessionId, opening = this.opening;
    this.sessionId = null; this.opened = false;
    if (!sessionId) return this.closing || (opening ? opening.catch(() => {}) : Promise.resolve());
    const closing = this.call('usb_close', {id: this.id, sessionId}).finally(() => {
      if (this.closing === closing) this.closing = null;
    });
    this.closing = closing;
    return closing;
  }
  detach() { this.connected = false; void this.close().catch(() => {}); }
  async operation(command, args) {
    if (!this.connected || !this.opened || !this.sessionId) throw disconnected();
    const epoch = this.epoch, sessionId = this.sessionId;
    const result = await this.call(command, {id: this.id, sessionId, ...args});
    if (!this.connected || !this.opened || epoch !== this.epoch || sessionId !== this.sessionId) throw disconnected();
    return result;
  }
  async selectConfiguration(value) {
    const configuration = this.configurations.find(item => item.configurationValue === value);
    if (!configuration) throw new NativeUsbError('invalid', 'Invalid USB configuration.');
    await this.operation('usb_select_configuration', {value});
    this.configuration = configuration;
  }
  async claimInterface(number) {
    if (!this.configuration?.interfaces.some(item => item.interfaceNumber === number)) throw new NativeUsbError('invalid', 'Invalid USB interface.');
    const result = await this.operation('usb_claim_interface', {number});
    if (validDevice(result)) this.update(result, true);
  }
  async selectAlternateInterface(number, alternate) {
    const iface = this.configuration?.interfaces.find(item => item.interfaceNumber === number);
    const selected = iface?.alternates.find(item => item.alternateSetting === alternate);
    if (!selected) throw new NativeUsbError('invalid', 'Invalid USB alternate interface.');
    await this.operation('usb_select_alternate_interface', {number, alternate});
    iface.alternate = selected;
  }
  async transferIn(number, length) {
    endpoint(number);
    if (!Number.isInteger(length) || length < 1 || length > MAX_TRANSFER) throw new NativeUsbError('invalid', 'Invalid USB transfer size.');
    const result = bytes(await this.operation('usb_transfer_in', {endpoint: number, length}));
    if (result.length > length) throw new NativeUsbError('invalid', 'The USB reply exceeds the requested size.');
    return {status: 'ok', data: new DataView(result.buffer, result.byteOffset, result.byteLength)};
  }
  async transferOut(number, value) {
    endpoint(number);
    const data = bytes(value);
    if (data.length > MAX_TRANSFER) throw new NativeUsbError('invalid', 'Invalid USB transfer size.');
    if (!this.connected || !this.opened || !this.sessionId) throw disconnected();
    const epoch = this.epoch, sessionId = this.sessionId;
    const result = await this.call('usb_transfer_out', data.slice(), {headers: {
      'x-device-id': this.id, 'x-session-id': sessionId, 'x-endpoint': String(number),
    }});
    if (!this.connected || !this.opened || epoch !== this.epoch || sessionId !== this.sessionId) throw disconnected();
    if (!Number.isInteger(result) || result < 0 || result > data.length) throw new NativeUsbError('invalid', 'The USB write reply is invalid.');
    return {status: 'ok', bytesWritten: result};
  }
}

export function createNativeUsb({invoke, listen}) {
  const events = new EventTarget(), devices = new Map(), removed = new Set();
  let listening, unlisten, stopped = false, revision = 0;
  const dispatch = (type, device) => {
    const event = new Event(type);
    Object.defineProperty(event, 'device', {value: device});
    events.dispatchEvent(event);
  };
  const remove = device => {
    removed.add(device.id); devices.delete(device.id); device.detach(); dispatch('disconnect', device);
  };
  const update = metadata => {
    if (!validDevice(metadata) || removed.has(metadata.id)) return null;
    const existing = devices.get(metadata.id);
    if (existing) { existing.update(metadata); return existing; }
    const device = new NativeDevice(metadata, invoke); devices.set(device.id, device); return device;
  };
  const start = () => {
    if (stopped) return Promise.reject(disconnected());
    if (!listening) listening = Promise.resolve().then(() => listen('usb-event', event => {
      const value = event.payload;
      if (stopped || !validDevice(value?.device)) return;
      ++revision;
      if (value.kind === 'disconnect') {
        removed.add(value.device.id);
        const device = devices.get(value.device.id);
        if (device) remove(device);
      } else if (value.kind === 'connect') {
        const device = update(value.device);
        if (device) { device.revision = revision; dispatch('connect', device); }
      }
    })).then(callback => {
      if (stopped) { callback(); throw disconnected(); }
      unlisten = callback;
    }).catch(error => { listening = null; throw failure(error); });
    return listening;
  };
  return {
    addEventListener: (...args) => events.addEventListener(...args),
    removeEventListener: (...args) => events.removeEventListener(...args),
    async getDevices() {
      await start();
      const before = revision;
      let result;
      try { result = await invoke('usb_list'); }
      catch (error) { throw failure(error); }
      if (stopped) throw disconnected();
      if (!Array.isArray(result) || result.some(value => !validDevice(value))) throw new NativeUsbError('invalid', 'The native USB device list is invalid.');
      const listed = new Set();
      for (const metadata of result) { const device = update(metadata); if (device) listed.add(device.id); }
      for (const device of devices.values()) if (!listed.has(device.id) && (device.revision || 0) <= before) remove(device);
      return [...devices.values()];
    },
    async destroy() {
      stopped = true;
      unlisten?.(); unlisten = null;
      await Promise.all([...devices.values()].map(device => { device.connected = false; return device.close().catch(() => {}); }));
      devices.clear();
    },
  };
}

export function createNativeBridge(tauri = globalThis.__TAURI__) {
  if (typeof tauri?.core?.invoke !== 'function' || typeof tauri?.event?.listen !== 'function') throw new Error('The native installer could not start. Reopen the application.');
  const invoke = (...args) => tauri.core.invoke(...args);
  const listen = (...args) => tauri.event.listen(...args);
  const usb = createNativeUsb({invoke, listen});
  let firmwareRequest = 0;
  const desktop = {
    chooseFirmware: () => invoke('choose_firmware'),
    async firmware(onProgress = () => {}) {
      const requestId = String(++firmwareRequest);
      const unlisten = await listen('firmware-progress', event => {
        const value = event.payload;
        if (value?.requestId === requestId && ['local', 'checking', 'downloading', 'verified'].includes(value.stage)) onProgress(value);
      });
      try { return bytes(await invoke('firmware', {requestId})); }
      finally { unlisten(); }
    },
    phase: value => invoke('phase', {value}),
    openSetup: ip => invoke('open_setup', {ip}),
  };
  return {desktop, usb, destroy: () => usb.destroy()};
}
