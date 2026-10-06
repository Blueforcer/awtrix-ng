import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createNativeBridge} from '../ui/native.js';
import {GUARDED_PHASES} from '../ui/controller.js';
import {createRpc} from './rpc.mjs';
import {runInstaller} from './runner.mjs';

export async function main() {
  if (Number(process.versions.node.split('.')[0]) < 24) throw new Error('Terminal installation requires Node.js 24 or newer.');
  const rpc = createRpc();
  const bridge = createNativeBridge({core: {invoke: rpc.invoke}, event: {listen: rpc.listen}});
  const [{connectGranted}, {parsePackage}, {Tc002Installer, validateWifi, provisionWifi}, {buildImage}, {loadImageTools}] = await Promise.all([
    import('../shared/adb.js'), import('../shared/package.js'), import('../shared/install.js'), import('../shared/image/core.js'), import('../shared/image/node.mjs'),
  ]);
  let tools, guarded = false;
  const setPhase = bridge.desktop.phase;
  bridge.desktop.phase = async value => { guarded = GUARDED_PHASES.has(value); await setPhase(value); };
  const interrupt = () => {
    if (guarded) process.stderr.write('Keep the clock powered and this terminal open. Finish or retry the installation before exiting.\n');
    else { rpc.close(); process.exitCode = 130; }
  };
  process.on('SIGINT', interrupt);
  try {
    const result = await runInstaller({desktop: bridge.desktop, usb: bridge.usb, connectGranted, parsePackage,
      Tc002Installer, validateWifi, provisionWifi, preload: async () => { tools = await loadImageTools(); },
      buildResImage: input => buildImage(input, input.onProgress, tools),
      prompt: (message, secret) => rpc.invoke('console_prompt', {message, secret}),
      message: message => rpc.invoke('console_message', {message}),
    });
    if (result.recoveryNeeded) process.exitCode = 2;
    return result;
  } finally {
    process.removeListener('SIGINT', interrupt);
    await bridge.destroy(); rpc.close();
  }
}

if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  main().catch(error => { process.stderr.write(`${error?.message || 'The installer could not finish.'}\n`); process.exitCode ||= 1; });
}
