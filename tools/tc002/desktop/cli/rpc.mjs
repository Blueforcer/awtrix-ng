import {createInterface} from 'node:readline';

const MAX_LINE = 48 * 1024 * 1024;
const COMMANDS = new Set(['firmware', 'phase', 'open_setup', 'console_prompt', 'console_message',
  'usb_list', 'usb_open', 'usb_close', 'usb_select_configuration', 'usb_claim_interface',
  'usb_select_alternate_interface', 'usb_transfer_in', 'usb_transfer_out']);

export function createRpc({input = process.stdin, output = process.stdout, commands = COMMANDS} = {}) {
  let next = 0, closed = false;
  const pending = new Map(), listeners = new Map();
  const lines = createInterface({input, crlfDelay: Infinity, terminal: false});
  const stop = () => {
    if (closed) return;
    closed = true;
    for (const entry of pending.values()) entry.reject(Object.assign(new Error('The native installer connection closed.'), {code: 'disconnected'}));
    pending.clear(); listeners.clear();
  };
  lines.on('close', stop);
  input.on('error', stop); output.on('error', stop);
  lines.on('line', line => {
    if (closed) return;
    let message;
    try {
      if (line.length > MAX_LINE) throw new Error();
      message = JSON.parse(line);
    } catch { stop(); lines.close(); return; }
    if (typeof message.event === 'string') {
      for (const listener of listeners.get(message.event) || []) listener({payload: message.payload});
      return;
    }
    const request = pending.get(message.requestId);
    if (!request) return;
    pending.delete(message.requestId);
    if (message.error) {
      request.reject(Object.assign(new Error(typeof message.error.message === 'string' ? message.error.message : 'The native request failed.'),
        {code: typeof message.error.code === 'string' ? message.error.code : 'transport'}));
    } else if (typeof message.bytes === 'string') {
      const padding = message.bytes.indexOf('=');
      if (message.bytes.length % 4 || /[^A-Za-z0-9+/=]/.test(message.bytes) || padding >= 0 &&
          (padding < message.bytes.length - 2 || !['=', '=='].includes(message.bytes.slice(padding)))) {
        request.reject(Object.assign(new Error('Invalid binary response.'), {code: 'invalid'})); return;
      }
      const bytes = Buffer.from(message.bytes, 'base64');
      request.resolve(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
    } else request.resolve(message.result);
  });
  return {
    invoke(command, args, options) {
      if (closed) return Promise.reject(Object.assign(new Error('The native installer connection closed.'), {code: 'disconnected'}));
      if (!commands.has(command)) return Promise.reject(new Error('Unsupported native command.'));
      const request = {requestId: ++next, command};
      if (args instanceof Uint8Array || args instanceof ArrayBuffer) {
        if (command !== 'usb_transfer_out') return Promise.reject(new Error('Unexpected binary request.'));
        const headers = options?.headers || {};
        request.args = {id: headers['x-device-id'], sessionId: headers['x-session-id'], endpoint: Number(headers['x-endpoint'])};
        request.bytes = Buffer.from(args instanceof Uint8Array ? args : new Uint8Array(args)).toString('base64');
      } else if (args !== undefined) request.args = args;
      return new Promise((resolve, reject) => {
        pending.set(request.requestId, {resolve, reject});
        output.write(JSON.stringify(request) + '\n', error => {
          if (error && pending.delete(request.requestId)) reject(error);
        });
      });
    },
    async listen(name, listener) {
      if (closed) throw new Error('The native installer connection closed.');
      if (!['usb-event', 'firmware-progress'].includes(name)) throw new Error('Unsupported native event.');
      if (!listeners.has(name)) listeners.set(name, new Set());
      listeners.get(name).add(listener);
      return () => listeners.get(name)?.delete(listener);
    },
    close() { stop(); lines.close(); input.removeListener('error', stop); output.removeListener('error', stop); },
  };
}
