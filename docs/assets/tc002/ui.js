import { connect, connectGranted } from './adb.js';
import { parsePackage } from './package.js';
import { Tc002Installer, provisionWifi, validateWifi, sha256 } from './install.js';
import { buildResImage, preload } from './image/index.js';

import {MAX_ARCHIVE_BYTES as MAX_PACKAGE} from './constants.js';
const imageStages = { reading: 'Reading the original application…', preparing: 'Preparing the installation…', compressing: 'Building the installation in your browser…', verifying: 'Checking every file in the installation…' };

async function responseBytes(response, limit) {
  if (!response.ok) throw new Error(`Download failed (${response.status}).`);
  if (Number(response.headers.get('content-length')) > limit) throw new Error('The download is too large.');
  const reader = response.body.getReader();
  const chunks = [];
  let length = 0;
  try {
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      length += value.length;
      if (length > limit) throw new Error('The download is too large.');
      chunks.push(value);
    }
  } finally { await reader.cancel().catch(() => {}); }
  const bytes = new Uint8Array(length);
  let offset = 0;
  for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
  return bytes;
}

export function mountInstaller(root, overrides = {}) {
  const win = root.ownerDocument.defaultView;
  const deps = { connect, connectGranted, parsePackage, Tc002Installer, provisionWifi, validateWifi, sha256, buildResImage, preload,
    fetch: (...args) => fetch(...args), secure: win.isSecureContext, usb: win.navigator.usb,
    sleep: milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds)), ...overrides };
  root.classList.add('tc002-install');
  root.innerHTML = `
    <div class="tc002-heading"><div><p class="tc002-eyebrow">AWTRIX NG / TC002</p><h2>Make it your clock.</h2><p>Install directly from your browser with a USB data cable.</p></div><div class="tc002-matrix" aria-hidden="true"><span>HELLO</span></div></div>
    <ol class="tc002-steps" aria-label="Installation steps"><li data-step="firmware">Firmware</li><li data-step="usb">USB connection</li><li data-step="install">Install</li><li data-step="setup">Wi-Fi setup</li></ol>
    <div class="tc002-body">
      <p class="tc002-release" data-role="release">Checking for firmware…</p>
      <p class="tc002-status" data-role="status" role="status" aria-live="polite">Loading the installer…</p>
      <progress data-role="progress" max="1" hidden aria-label="Installation progress"></progress>
      <div class="tc002-error" data-role="error" role="alert" hidden></div>
      <p data-role="connection-help" hidden>Connect one TC002 with a USB data cable. Switch it off and on, then select it in the browser’s USB picker. Keep this page open while the connection settles.</p>
      <p data-role="recovery-help" class="tc002-recovery" hidden>The installation was interrupted. Keep the clock powered and this page open. Reconnect the same clock and retry to finish writing and checking the image.</p>
      <div class="tc002-actions"><button type="button" data-action="connect" disabled>Connect TC002</button><button type="button" data-action="install" hidden>Install AWTRIX NG</button><button type="button" data-action="restart" hidden>Restart clock</button><button type="button" data-action="retry" hidden>Reconnect &amp; retry</button><button type="button" class="tc002-secondary" data-action="cancel" hidden>Cancel connection</button></div>
      <p class="tc002-caption" data-role="compatibility">Use desktop Chrome or Edge over HTTPS. Your clock’s original application data stays in this browser.</p>
      <details class="tc002-local" data-role="local"><summary>Developer: use a local firmware package</summary><label class="tc002-file-label">Browser installer ZIP<input type="file" accept=".zip,application/zip" data-role="file"></label><label class="tc002-check"><input type="checkbox" data-role="allow-dirty"> Allow a development package</label><p class="tc002-caption">Choose a TC002 browser installer package built from this project.</p></details>
      <section class="tc002-success" data-role="success" hidden aria-labelledby="tc002-ready-title"><h3 id="tc002-ready-title" tabindex="-1">Your clock is ready for Wi-Fi.</h3><ol><li>Wait for the clock to start, then connect to its hotspot: <strong>awtrixng-XXXXXX</strong> (or the clock’s hostname).</li><li>Open <a href="http://192.168.4.1/" target="_blank" rel="noopener">192.168.4.1</a> and enter your Wi-Fi network name and password.</li></ol><p class="tc002-caption">You can finish setup from a phone or this computer.</p><details data-role="wifi-details"><summary>Alternatively, set Wi-Fi over USB</summary><form data-role="wifi-form"><label>Network name<input name="ssid" autocomplete="off" maxlength="32" required></label><label>Password<input name="password" type="password" autocomplete="new-password" maxlength="64"></label><button type="submit">Save Wi-Fi over USB</button></form><p data-role="wifi-status" role="status" aria-live="polite"></p><a data-role="clock-link" target="_blank" rel="noopener" hidden>Open your clock</a></details></section>
      <p class="tc002-caption tc002-notice"><a data-role="licenses" target="_blank" rel="noopener">Image tools: licenses and source</a></p>
    </div>`;
  const get = role => root.querySelector(`[data-role="${role}"]`);
  const button = action => root.querySelector(`[data-action="${action}"]`);
  get('licenses').href = new URL('./image/NOTICE/', import.meta.url).href;
  let phase = 'loading', bundle = null, installer = null, session = null, identity = null;
  let toolsReady = false, loadToken = 0, abort = null, wifiBusy = false, destroyed = false;
  const supported = Boolean(deps.secure && deps.usb);
  const status = text => { if (!destroyed) get('status').textContent = imageStages[text] || text; };
  const error = text => { get('error').textContent = text || ''; get('error').hidden = !text; };
  const progress = value => {
    if (!destroyed && Number.isFinite(value)) { get('progress').hidden = false; get('progress').value = Math.max(0, Math.min(1, value)); }
  };
  const render = () => {
    const active = ['connecting', 'preparing', 'installing', 'reconnecting', 'restarting'].includes(phase);
    root.dataset.phase = phase;
    root.setAttribute('aria-busy', String(active));
    button('connect').hidden = !['loading', 'idle'].includes(phase);
    button('connect').disabled = !supported || !bundle || !toolsReady || phase !== 'idle';
    button('install').hidden = phase !== 'ready';
    button('restart').hidden = phase !== 'verified';
    button('retry').hidden = phase !== 'recovery';
    button('cancel').hidden = phase !== 'connecting';
    get('connection-help').hidden = !['idle', 'connecting'].includes(phase) || !bundle;
    get('recovery-help').hidden = !['recovery', 'reconnecting'].includes(phase);
    get('success').hidden = phase !== 'done';
    get('local').hidden = !['loading', 'idle'].includes(phase);
    get('file').disabled = active;
    get('allow-dirty').disabled = active;
    get('progress').hidden = !['preparing', 'installing'].includes(phase);
    const step = phase === 'done' ? 3 : ['ready', 'installing', 'verified', 'recovery', 'reconnecting', 'restarting'].includes(phase) ? 2 : ['connecting', 'preparing'].includes(phase) ? 1 : 0;
    root.querySelectorAll('[data-step]').forEach((item, index) => {
      item.classList.toggle('is-complete', index < step);
      if (index === step) item.setAttribute('aria-current', 'step'); else item.removeAttribute('aria-current');
    });
  };
  const setPhase = value => { phase = value; render(); };
  const idleStatus = () => status(!supported ? 'USB installation requires desktop Chrome or Edge on a secure HTTPS page.' : bundle ? 'Firmware checked. Connect your TC002 to continue.' : 'Choose a local package to continue.');
  const setBundle = value => {
    bundle = value;
    get('release').textContent = `${bundle.version}${bundle.manifest?.dirty ? ' · development package' : ''} · ready to install`;
    setPhase('idle'); idleStatus();
  };
  const showFailure = async failure => {
    error(failure?.message || 'The installation could not finish.');
    if (failure?.recoveryNeeded || installer?.writeStarted && !installer?.verified) {
      setPhase('recovery'); status('Installation incomplete. Retry with the same clock.');
    } else {
      await installer?.dispose().catch(() => {});
      await session?.close().catch(() => {});
      installer = null; session = null;
      setPhase('idle'); idleStatus();
    }
  };
  const usbOptions = () => ({usb: deps.usb, identity, signal: abort?.signal, onStatus: event => status(event.message)});
  const installPrepared = async () => {
    setPhase('installing'); error(''); progress(0);
    try { await installer.install(session); setPhase('verified'); status('Installation verified. Restart the clock to set up Wi-Fi.'); }
    catch (failure) { await showFailure(failure); }
  };
  button('connect').addEventListener('click', () => {
    if (phase !== 'idle' || !bundle || !toolsReady || !supported) return;
    abort = new AbortController(); error(''); setPhase('connecting');
    const connecting = deps.connect(usbOptions());
    connecting.then(async connected => {
      session = connected; identity = connected.identity;
      setPhase('preparing');
      installer = new deps.Tc002Installer(session, bundle, deps.buildResImage, {onStatus: status, onProgress: progress});
      const prepared = await installer.prepare();
      setPhase('ready'); status(`${prepared.version} is ready. Install it while keeping the USB cable connected.`);
    }).catch(showFailure);
  });
  button('cancel').addEventListener('click', () => { if (phase === 'connecting') abort?.abort(); });
  button('install').addEventListener('click', () => { if (phase === 'ready') void installPrepared(); });
  button('retry').addEventListener('click', () => {
    if (phase !== 'recovery') return;
    abort = new AbortController(); error(''); setPhase('reconnecting');
    Promise.resolve(session?.close()).catch(() => {}).then(() => deps.connectGranted(usbOptions())).then(connected => { session = connected; return installPrepared(); }).catch(showFailure);
  });
  button('restart').addEventListener('click', async () => {
    if (phase !== 'verified') return;
    setPhase('restarting'); error('');
    try {
      await installer.dispose();
      await installer.reboot();
      session = null;
      setPhase('done'); status('Installed. Continue on the clock’s Wi-Fi hotspot.');
      root.querySelector('#tc002-ready-title').focus();
    } catch (failure) { setPhase('verified'); error(failure.message); status('Installation is verified. Restart the clock to continue.'); }
  });
  get('file').addEventListener('change', async event => {
    if (!['idle', 'loading'].includes(phase)) return;
    const file = event.target.files?.[0];
    if (!file) return;
    const token = ++loadToken;
    bundle = null; error(''); setPhase('loading'); status('Checking the local firmware package…');
    try {
      if (file.size > MAX_PACKAGE) throw new Error('The firmware package exceeds 32 MiB.');
      const parsed = await deps.parsePackage(new Uint8Array(await file.arrayBuffer()), {allowDirty: get('allow-dirty').checked});
      if (token === loadToken) setBundle(parsed);
    } catch (failure) {
      if (token === loadToken) { error(failure.message); setPhase('idle'); idleStatus(); }
    } finally { event.target.value = ''; }
  });
  get('allow-dirty').addEventListener('change', () => {
    if (bundle?.manifest?.dirty && !get('allow-dirty').checked) { bundle = null; get('release').textContent = 'Select a release package.'; render(); idleStatus(); }
  });
  get('wifi-form').addEventListener('submit', async event => {
    event.preventDefault();
    if (phase !== 'done' || wifiBusy) return;
    const form = event.currentTarget;
    const ssid = form.elements.ssid.value;
    let password = form.elements.password.value;
    try { deps.validateWifi(ssid, password); }
    catch (failure) { get('wifi-status').textContent = failure.message; return; }
    form.elements.password.value = '';
    wifiBusy = true;
    const submit = form.querySelector('button'); submit.disabled = true;
    get('clock-link').hidden = true;
    get('wifi-status').textContent = 'Reconnecting to the clock over USB…';
    try {
      const result = await deps.provisionWifi({connect: () => deps.connectGranted({usb: deps.usb, identity}),
        credentials: {ssid, password}, status: text => { get('wifi-status').textContent = text; }, sleep: deps.sleep});
      password = '';
      get('wifi-status').textContent = result.message;
      if (result.ip) {
        get('clock-link').href = `http://${result.ip}/`;
        get('clock-link').textContent = `Open your clock at ${result.ip}`;
        get('clock-link').hidden = false;
      }
    } finally {
      password = ''; form.elements.password.value = ''; wifiBusy = false; submit.disabled = false;
    }
  });
  const holdsPage = () => ['installing', 'reconnecting', 'restarting'].includes(phase) || Boolean(installer?.writeStarted && !installer?.verified);
  const beforeUnload = event => {
    if (holdsPage()) { event.preventDefault(); event.returnValue = ''; }
  };
  win.addEventListener('beforeunload', beforeUnload);
  const ready = (async () => {
    const token = ++loadToken;
    const preloadPromise = deps.preload().then(() => { toolsReady = true; render(); }).catch(failure => { error(`Image tools could not be loaded: ${failure.message}`); });
    try {
      const indexUrl = new URL(root.dataset.index || '../../../firmware/tc002/index.json', root.dataset.index ? win.location.href : import.meta.url);
      if (indexUrl.origin !== win.location.origin) throw new Error('Firmware index must be hosted on this site.');
      const response = await deps.fetch(indexUrl, {cache: 'no-cache', signal: AbortSignal.timeout(60000)});
      if (response.status === 404) {
        if (token === loadToken) { get('release').textContent = 'No public TC002 firmware is available yet.'; setPhase('idle'); idleStatus(); }
      } else {
        const index = JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(await responseBytes(response, 8192)));
        if (!index || typeof index.version !== 'string' || !/^[0-9A-Za-z][0-9A-Za-z.+_-]{0,50}$/.test(index.version) || !/^usb-awtrix-ng-tc002(?:-[A-Za-z0-9._-]+)?\.zip$/.test(index.asset) || !Number.isSafeInteger(index.size) || index.size < 1 || index.size > MAX_PACKAGE || !/^[a-f0-9]{64}$/.test(index.sha256)) throw new Error('The published firmware index is invalid.');
        status('Downloading and checking the published firmware…');
        const bytes = await responseBytes(await deps.fetch(new URL(index.asset, indexUrl), {signal: AbortSignal.timeout(120000)}), index.size);
        if (bytes.length !== index.size || await deps.sha256(bytes) !== index.sha256) throw new Error('The firmware download failed verification.');
        const parsed = await deps.parsePackage(bytes);
        if (parsed.version !== index.version) throw new Error('The firmware version differs from the published index.');
        if (token === loadToken) setBundle(parsed);
      }
    } catch (failure) {
      if (token === loadToken) { get('release').textContent = 'Published firmware could not be loaded.'; error(failure.message); setPhase('idle'); idleStatus(); }
    }
    await preloadPromise;
    render();
  })();
  render();
  return {ready, get phase() { return phase; }, get holdsPage() { return holdsPage(); }, async destroy() {
    if (holdsPage()) return false;
    destroyed = true; ++loadToken; abort?.abort();
    win.removeEventListener('beforeunload', beforeUnload);
    await installer?.dispose().catch(() => {});
    await session?.close().catch(() => {});
    return true;
  }};
}

export function watchInstaller(doc, pages, mount = mountInstaller) {
  let activeRoot = null, controller = null;
  const update = () => {
    const next = doc.querySelector('#tc002-installer');
    if (next === activeRoot) return;
    if (controller?.holdsPage) {
      if (next && next !== activeRoot) next.remove();
      (doc.querySelector('.md-content__inner') || doc.querySelector('main') || doc.body).prepend(activeRoot);
      return;
    }
    void controller?.destroy();
    activeRoot = next;
    controller = next ? mount(next) : null;
  };
  const guard = event => {
    if (!controller?.holdsPage || event.button !== 0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    const anchor = event.target.closest?.('a[href]');
    if (!anchor || anchor.target === '_blank' || anchor.hasAttribute('download')) return;
    const target = new URL(anchor.href, doc.location.href);
    if (target.origin !== doc.location.origin || target.pathname === doc.location.pathname && target.search === doc.location.search) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    const message = activeRoot.querySelector('[data-role="error"]');
    if (message) { message.textContent = 'Finish or retry the installation before leaving this page.'; message.hidden = false; }
    activeRoot.scrollIntoView?.({block: 'nearest'});
  };
  doc.addEventListener('click', guard, true);
  const subscription = pages?.subscribe(update);
  update();
  return () => {
    if (controller?.holdsPage) return false;
    subscription?.unsubscribe();
    doc.removeEventListener('click', guard, true);
    void controller?.destroy();
    return true;
  };
}

if (globalThis.document) watchInstaller(document, globalThis.document$);
