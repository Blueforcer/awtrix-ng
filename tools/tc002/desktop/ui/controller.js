import {MAX_ARCHIVE_BYTES as MAX_PACKAGE} from '../shared/constants.js';
export const GUARDED_PHASES = new Set(['installing', 'recovery', 'reconnecting', 'restarting']);
const imageStages = {
  reading: 'Reading the original application…',
  preparing: 'Preparing the installation…',
  compressing: 'Building the installation…',
  verifying: 'Checking every file in the installation…',
};

export function firmwareProgressMessage(event) {
  if (event.stage === 'local') return 'Loading local TC002 firmware…';
  if (event.stage === 'downloading') return 'Downloading the latest TC002 firmware…';
  if (event.stage === 'verified') return 'Checking firmware and preparing the image tools…';
  return 'Checking GitHub for the latest TC002 firmware…';
}

const connectionSteps = {connecting: 'Waiting for your TC002…', opening: 'Clock found. Connecting…',
  reconnecting: 'Connecting to the clock…', disconnected: 'Connecting to the clock…',
  keeper: 'Connected. Getting the clock ready…', ready: 'Connected.'};

export function connectionMessage(event) {
  return connectionSteps[event?.phase] ?? event?.message ?? '';
}

export function mountInstaller(root, dependencies) {
  const win = root.ownerDocument.defaultView;
  const deps = {sleep: ms => new Promise(resolve => setTimeout(resolve, ms)), ...dependencies};
  root.innerHTML = `
    <header class="masthead"><span class="wordmark">AWTRIX <b>NG</b></span><span class="edition">TC002 Installer</span></header>
    <ol class="steps" aria-label="Installation steps"><li data-step="connect">Connect</li><li data-step="install">Install</li><li data-step="enjoy">Done</li></ol>
    <section class="workbench" aria-label="Installation">
      <div class="release-line"><span class="signal" aria-hidden="true"></span><span data-role="release">Looking for firmware</span></div>
      <h2 data-role="title">Getting ready.</h2>
      <p class="status" data-role="status" role="status" aria-live="polite">Loading the installer…</p>
      <progress data-role="progress" max="1" hidden aria-label="Installation progress"></progress>
      <section class="success" data-role="success" hidden aria-labelledby="setup-title"><h3 id="setup-title" tabindex="-1">All set.</h3><p data-role="wifi-result"></p><button type="button" data-action="open-clock" hidden>Open your clock</button></section>
      <p class="help" data-role="provisioning-help" hidden>Your Wi-Fi details are sent automatically as soon as the clock has started. This can take a minute.</p>
      <p class="help" data-role="connection-help" hidden>Connect the TC002 with a USB data cable and switch it on. Already on? Switch it off and on once.</p>
      <div class="notice error" data-role="error" role="alert" hidden></div>
      <p class="notice" data-role="recovery-help" hidden>Keep the clock powered and this window open. Reconnect the USB cable and retry. Do not restart the clock yet.</p>
      <details class="wifi" data-role="wifi" open hidden><summary>Home Wi-Fi <span>optional</span></summary><form data-role="wifi-form" autocomplete="off"><div class="wifi-fields"><label for="ssid">Network name<input id="ssid" name="ssid" maxlength="32" autocomplete="off" spellcheck="false"></label><label for="password">Password<input id="password" name="password" type="password" maxlength="64" autocomplete="off"></label></div><p class="small" data-role="wifi-note">Sent to the clock after installation.</p></form></details>
      <div class="actions"><button type="button" data-action="install" hidden>Install AWTRIX NG</button><button type="button" data-action="restart" hidden>Restart clock</button><button type="button" data-action="retry" hidden>Try USB again</button><button type="button" data-action="download" hidden>Retry firmware</button><button type="button" class="secondary" data-action="choose-firmware">Choose firmware ZIP…</button><button type="button" data-action="send-wifi" hidden>Send Wi-Fi to the clock</button><button type="button" class="link" data-action="skip-wifi" hidden>Skip and set up Wi-Fi later</button></div>
      <p class="small" data-role="keep-connected" hidden>Keep the clock powered and connected.</p>
      <div class="hotspot" data-role="ap-steps" hidden><p class="small">Or use the clock’s hotspot:</p><ol><li>Join the Wi-Fi shown on the clock: <strong>awtrixng-XXXXXX</strong>.</li><li>Open <strong>192.168.4.1</strong> and enter your Wi-Fi details.</li></ol><button class="secondary" type="button" data-action="setup">Open hotspot setup</button></div>
    </section>`;
  const get = role => root.querySelector(`[data-role="${role}"]`);
  const button = action => root.querySelector(`[data-action="${action}"]`);
  let phase = 'loading', bundle, installer, session, identity, abort, picker;
  let credentials = null, clockIp = null, destroyed = false, operation = 0, downloading = false, firmwareSource = null;
  let lastSsid = '', wifiTried = false;
  const titles = {
    loading: 'Getting ready.', firmware: 'Choose the firmware.', waiting: 'Ready when you are.', preparing: 'Found your clock.',
    ready: 'Let’s make it yours.', installing: 'Installing AWTRIX NG.', verified: 'Installed. Checked. Ready.',
    restarting: 'Starting fresh.', recovery: 'Let’s finish this safely.', reconnecting: 'Reconnecting your clock.',
    provisioning: 'Connecting to your Wi-Fi.', done: 'Hello, AWTRIX.', failed: 'Let’s try that again.',
  };
  const holdsPage = () => GUARDED_PHASES.has(phase) || Boolean(installer?.writeStarted && !installer?.verified);
  const canChooseFirmware = () => !destroyed && ['loading', 'firmware', 'waiting', 'failed'].includes(phase);
  const status = text => {
    if (!destroyed) get('status').textContent = imageStages[text] || String(text).replace('in your browser', 'on your computer');
  };
  const error = text => {
    if (destroyed) return;
    get('error').textContent = text || '';
    get('error').hidden = !text;
  };
  const clearFields = () => {
    get('wifi-form').elements.ssid.value = '';
    get('wifi-form').elements.password.value = '';
  };
  const forgetWifi = () => {
    if (credentials) { credentials.password = ''; credentials.ssid = ''; }
    credentials = null; clearFields();
  };
  const progress = value => {
    if (!destroyed && Number.isFinite(value)) get('progress').value = Math.max(0, Math.min(1, value));
  };
  const render = () => {
    if (destroyed) return;
    root.dataset.phase = phase;
    const busy = ['loading', 'waiting', 'preparing', 'installing', 'reconnecting', 'restarting', 'provisioning'].includes(phase);
    root.setAttribute('aria-busy', String(busy));
    get('title').textContent = titles[phase];
    get('connection-help').hidden = !['waiting', 'failed'].includes(phase) || !bundle;
    get('recovery-help').hidden = !['recovery', 'reconnecting'].includes(phase);
    get('progress').hidden = !['preparing', 'installing', 'restarting', 'provisioning'].includes(phase) && !downloading;
    if (['restarting', 'provisioning'].includes(phase)) get('progress').removeAttribute('value');
    get('provisioning-help').hidden = !['restarting', 'provisioning'].includes(phase) || !credentials;
    get('wifi').hidden = !bundle || !(['waiting', 'preparing', 'ready'].includes(phase) || phase === 'done' && !clockIp);
    get('wifi-note').textContent = phase === 'done' ? 'Sent over USB to the running clock.' : 'Sent to the clock after installation.';
    button('send-wifi').hidden = phase !== 'done' || Boolean(clockIp);
    button('send-wifi').textContent = wifiTried ? 'Try again' : 'Send Wi-Fi to the clock';
    button('install').hidden = phase !== 'ready';
    button('restart').hidden = phase !== 'verified';
    button('retry').hidden = !['failed', 'recovery'].includes(phase) || !bundle;
    button('retry').textContent = phase === 'recovery' ? 'Reconnect & finish installation' : 'Try USB again';
    button('download').hidden = phase !== 'failed' || Boolean(bundle);
    button('download').disabled = Boolean(picker);
    button('retry').disabled = Boolean(picker);
    button('choose-firmware').hidden = !canChooseFirmware();
    button('choose-firmware').classList.toggle('secondary', phase !== 'firmware');
    button('choose-firmware').disabled = Boolean(picker);
    button('skip-wifi').hidden = phase !== 'provisioning';
    get('keep-connected').hidden = !['preparing', 'ready', 'installing', 'verified', 'restarting', 'reconnecting', 'provisioning'].includes(phase);
    get('success').hidden = phase !== 'done';
    get('ap-steps').hidden = phase !== 'done' || Boolean(clockIp);
    button('open-clock').hidden = !clockIp;
    const step = ['done', 'provisioning'].includes(phase) ? 2 : ['ready', 'installing', 'verified', 'recovery', 'reconnecting', 'restarting'].includes(phase) ? 1 : 0;
    root.querySelectorAll('[data-step]').forEach((item, index) => {
      item.classList.toggle('complete', index < step);
      if (index === step) item.setAttribute('aria-current', 'step'); else item.removeAttribute('aria-current');
    });
  };
  const setPhase = async value => {
    if (destroyed) return;
    phase = value;
    render();
    await deps.desktop.phase(value);
  };
  const options = (token = operation) => ({usb: deps.usb, identity, signal: abort?.signal,
    onStatus: event => {
      if (!destroyed && token === operation && phase !== 'provisioning') status(connectionMessage(event));
    }});
  const fail = async (failure, token = operation) => {
    if (destroyed || token !== operation) return;
    if (!bundle && !installer && failure?.code === 'no-compatible-release') {
      forgetWifi();
      await setPhase('firmware');
      if (destroyed || token !== operation) return;
      get('release').textContent = 'No firmware selected';
      status('Select the firmware file usb-awtrix-ng-tc002.zip.');
      return;
    }
    error(failure?.code === 'no-compatible-release' ? 'No compatible TC002 firmware has been published yet.' : failure?.message || 'The installation could not finish.');
    if (failure?.recoveryNeeded || installer?.writeStarted && !installer?.verified) {
      await setPhase('recovery');
      status('Installation is incomplete. Retry with the same clock.');
      return;
    }
    const failedInstaller = installer, failedSession = session;
    await failedInstaller?.dispose().catch(() => {});
    await failedSession?.close().catch(() => {});
    if (destroyed || token !== operation) return;
    installer = session = null;
    identity = null;
    forgetWifi();
    await setPhase('failed');
    if (destroyed || token !== operation) return;
    status(bundle ? 'Check the USB connection, then try again.' : failure?.code === 'local-firmware' || firmwareSource === 'local' ?
      'Check the local firmware ZIP, then retry. No firmware was downloaded.' : failure?.code === 'no-compatible-release' ?
      'Choose a firmware ZIP, or check again when a TC002 release is available.' : 'Firmware could not be loaded. Check the error above, then retry.');
  };
  const connect = async () => {
    const token = ++operation;
    abort = new AbortController();
    error('');
    try {
      await setPhase('waiting');
      if (destroyed || token !== operation) return;
      status('Waiting for your TC002…');
      const connected = await deps.connectGranted(options(token));
      if (picker) await picker;
      if (destroyed || token !== operation) { await connected.close(); return; }
      session = connected;
      identity = connected.identity;
      installer = new deps.Tc002Installer(session, bundle, deps.buildResImage, {onStatus: status, onProgress: progress});
      await setPhase('preparing');
      progress(0);
      await installer.prepare();
      if (destroyed || token !== operation) return;
      await setPhase('ready');
      status('Your clock is ready. Install AWTRIX NG when you are.');
    } catch (failure) { await fail(failure, token); }
  };
  const install = async () => {
    try {
      await setPhase('installing');
      error(''); progress(0);
      await installer.install(session);
      await setPhase('verified');
      status('Every file is verified. Restarting the clock…');
    } catch (failure) { await fail(failure); return; }
    await restartClock();
  };
  const finish = async message => {
    forgetWifi();
    await session?.close().catch(() => {});
    session = null;
    if (destroyed) return;
    get('wifi-result').textContent = message;
    get('wifi-result').classList.toggle('notice', wifiTried && !clockIp);
    root.querySelector('#setup-title').textContent = clockIp ? 'All set.' : wifiTried ? 'Wi-Fi needs another try.' : 'One more step: Wi-Fi.';
    await setPhase('done');
    status(clockIp ? 'Installed and connected to your Wi-Fi.' : wifiTried ? 'Installed. Wi-Fi is not set up yet.' :
      'Installed. Set up Wi-Fi below or on the clock’s hotspot.');
    if (!clockIp && lastSsid) {
      get('wifi-form').elements.ssid.value = lastSsid;
      get('wifi-form').elements.password.focus();
    } else root.querySelector('#setup-title').focus();
  };
  const provision = async () => {
    const token = ++operation;
    abort = new AbortController();
    wifiTried = true;
    error('');
    try {
      await setPhase('provisioning');
      status('Waiting for the clock to start…');
      const result = await deps.provisionWifi({connect: () => deps.connectGranted(options(token)), credentials,
        signal: abort.signal, status: text => { if (!destroyed && token === operation) status(text); }, sleep: deps.sleep});
      if (destroyed || token !== operation) return;
      clockIp = result.ip;
      await finish(result.message);
    } catch {
      if (!destroyed && token === operation) await finish('USB Wi-Fi setup could not finish. Check the cable and try again.');
    }
  };
  const readWifi = () => {
    const form = get('wifi-form');
    const ssid = form.elements.ssid.value;
    const password = form.elements.password.value;
    if (!ssid && !password) return null;
    deps.validateWifi(ssid, password);
    lastSsid = ssid;
    return {ssid, password};
  };
  button('install').addEventListener('click', () => {
    if (phase !== 'ready') return;
    try { credentials = readWifi(); }
    catch (failure) { error(failure.message); get('wifi').open = true; return; }
    clearFields();
    void install();
  });
  button('send-wifi').addEventListener('click', () => {
    if (phase !== 'done' || clockIp) return;
    let entered;
    try { entered = readWifi(); }
    catch (failure) { error(failure.message); return; }
    if (!entered) { error('Enter the network name and password.'); return; }
    credentials = entered;
    clearFields();
    void provision();
  });
  get('wifi-form').addEventListener('submit', event => { event.preventDefault(); });
  button('retry').addEventListener('click', async () => {
    if (picker) return;
    if (phase === 'failed' && bundle) { void connect(); return; }
    if (phase !== 'recovery') return;
    abort = new AbortController();
    error('');
    try {
      await setPhase('reconnecting');
      await session?.close().catch(() => {});
      session = await deps.connectGranted(options());
      await install();
    } catch (failure) { await fail(failure); }
  });
  const restartClock = async () => {
    try {
      await setPhase('restarting'); error('');
      await installer.dispose();
      await installer.reboot();
      session = null;
      if (credentials) await provision();
      else await finish('Your clock is starting. Set up Wi-Fi below, or join its hotspot.');
    } catch {
      await setPhase('verified');
      error('The clock could not be restarted over USB. Installation is verified; try restarting again.');
    }
  };
  button('restart').addEventListener('click', () => { if (phase === 'verified') void restartClock(); });
  button('skip-wifi').addEventListener('click', () => {
    if (phase !== 'provisioning') return;
    ++operation;
    abort?.abort();
    void finish('Continue Wi-Fi setup using the clock’s hotspot.');
  });
  button('setup').addEventListener('click', () => {
    if (phase === 'done') void deps.desktop.openSetup('192.168.4.1').catch(() => error('Join the clock’s Wi-Fi, then open http://192.168.4.1 in your browser.'));
  });
  button('open-clock').addEventListener('click', () => {
    if (phase === 'done' && clockIp) void deps.desktop.openSetup(clockIp).catch(() => error(`Open http://${clockIp} in your browser.`));
  });
  const beforeUnload = event => {
    if (holdsPage()) { event.preventDefault(); event.returnValue = ''; }
    else forgetWifi();
  };
  win.addEventListener('beforeunload', beforeUnload);
  const preventReload = event => {
    if (holdsPage() && (event.key === 'F5' || (event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'r')) {
      event.preventDefault(); event.stopImmediatePropagation();
    }
  };
  win.addEventListener('keydown', preventReload, true);
  const loadFirmware = async () => {
    const token = ++operation;
    bundle = null; downloading = false; firmwareSource = null; error('');
    try {
      await setPhase('loading');
      if (destroyed || token !== operation) return;
      get('release').textContent = 'Looking for firmware';
      status('Looking for firmware…');
      const [bytes] = await Promise.all([deps.desktop.firmware(event => {
        if (destroyed || token !== operation) return;
        if (event.stage === 'local') firmwareSource = 'local';
        else if (event.stage === 'checking' || event.stage === 'downloading') firmwareSource = 'github';
        get('release').textContent = firmwareSource === 'local' ? 'Local firmware' : 'Latest GitHub release';
        downloading = event.stage === 'downloading' || event.stage === 'local';
        render();
        status(firmwareProgressMessage(event));
        if (downloading) {
          if (Number.isFinite(event.total) && event.total > 0 && Number.isFinite(event.received)) progress(event.received / event.total);
          else get('progress').removeAttribute('value');
        }
      }), deps.preload()]);
      if (destroyed || token !== operation) return;
      downloading = false;
      if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length > MAX_PACKAGE) throw new Error('The firmware package has an invalid size.');
      const parsed = await deps.parsePackage(bytes);
      if (picker) await picker;
      if (destroyed || token !== operation) return;
      bundle = parsed;
      get('release').textContent = `AWTRIX NG ${bundle.version}${firmwareSource === 'local' ? ' · Local firmware' : firmwareSource === 'github' ? ' · GitHub release' : ''}`;
      void connect();
    } catch (failure) {
      if (destroyed || token !== operation) return;
      downloading = false;
      await fail(failure, token);
    }
  };
  button('choose-firmware').addEventListener('click', async () => {
    if (!canChooseFirmware() || picker) return;
    let releasePicker;
    picker = new Promise(resolve => { releasePicker = resolve; });
    render();
    try {
      if (!await deps.desktop.chooseFirmware() || destroyed) return;
      ++operation;
      abort?.abort();
      const previousSession = session;
      session = installer = identity = bundle = null;
      await previousSession?.close().catch(() => {});
      if (!destroyed) void loadFirmware();
    } catch (failure) {
      error(failure?.message || 'The firmware picker could not open. Try again.');
    } finally {
      picker = null; releasePicker(); render();
    }
  });
  button('download').addEventListener('click', () => { if (!picker && phase === 'failed' && !bundle) void loadFirmware(); });
  const ready = loadFirmware();
  render();
  return {ready, get phase() { return phase; }, get holdsPage() { return holdsPage(); }, async destroy() {
    if (holdsPage()) return false;
    destroyed = true; ++operation;
    abort?.abort(); forgetWifi();
    win.removeEventListener('beforeunload', beforeUnload);
    win.removeEventListener('keydown', preventReload, true);
    await installer?.dispose().catch(() => {});
    await session?.close().catch(() => {});
    return true;
  }};
}
