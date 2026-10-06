import {createRpc} from '../desktop/cli/rpc.mjs';
import {Tc002Installer, wifiCommand} from '../../../docs/assets/tc002/install.js';

const rpc = createRpc({commands: new Set(['engine_start', 'engine_finish', 'run', 'exists',
  'readFile', 'writeFile', 'sleep', 'backup', 'status', 'helper', 'image', 'journal', 'deploy', 'request', 'now'])});
const invoke = (command, args) => rpc.invoke(command, args);

class HostInstaller extends Tc002Installer {
  constructor() {
    super({
      readFile: async (file, options) => new Uint8Array(await invoke('readFile', {file, ...options})),
      writeFile: (file, bytes, options) => invoke('writeFile', {file, bytes: Buffer.from(bytes).toString('base64'), ...options}),
    }, null, null, {sleep: ms => invoke('sleep', {ms})});
    this.work = '/tmp/awtrix-install';
    this.helperPath = `${this.work}/awtrix-tc002-flash`;
  }
  run(command, timeoutMs = 30000) { return invoke('run', {command, timeoutMs}); }
  exists(file, kind = 'f') { return invoke('exists', {file, kind}); }
  helper(command, args = [], timeoutMs = 120000) { return invoke('helper', {command, args, timeoutMs}); }
}

try {
  const {operation, options} = await invoke('engine_start');
  const installer = new HostInstaller();
  const host = {
    backup: snapshot => invoke('backup', {...snapshot,
      files: snapshot.files.map(file => ({...file, bytes: Buffer.from(file.bytes).toString('base64')}))}),
    status: message => invoke('status', {message}),
    image: async target => new Uint8Array(await invoke('image', {target})),
    journal: (active, target) => invoke('journal', {active, target}),
    deploy: slotReady => invoke('deploy', {slotReady}),
  };
  let result;
  if (operation === 'secure-vendor') {
    result = await installer.secureVendor({apply: options.apply, backup: host.backup, status: host.status});
  } else if (operation === 'flash') {
    result = await installer.flashSaved(options, host);
  } else if (operation === 'wifi') {
    result = await wifiCommand(options, {...host, now: () => invoke('now'),
      sleep: ms => invoke('sleep', {ms}), request: (command, payload) => invoke('request', {command, payload})});
  } else throw new Error('Unsupported installation operation');
  await invoke('engine_finish', {result});
} catch (error) {
  await invoke('engine_finish', {error: error.message, code: error.code, cleanupFailures: error.cleanupFailures,
    recoveryNeeded: !!error.recoveryNeeded}).catch(() => {});
} finally { rpc.close(); }
