import {connectionMessage, firmwareProgressMessage} from '../ui/controller.js';
import {MAX_ARCHIVE_BYTES} from '../shared/constants.js';

const imageStages = {reading: 'Reading the original application…', preparing: 'Preparing the installation…',
  compressing: 'Building the installation…', verifying: 'Checking every file in the installation…'};
const yes = value => typeof value === 'string' && /^(y|yes)$/i.test(value.trim());

export async function runInstaller(deps) {
  const {desktop, usb, connectGranted, parsePackage, Tc002Installer, buildResImage, validateWifi, provisionWifi,
    preload, prompt, message, signal} = deps;
  let installer, session, identity, credentials = null, bundle, lastStatus, lastProgress = -1, firmwareSource = null;
  const clearWifi = () => { if (credentials) { credentials.password = ''; credentials.ssid = ''; } credentials = null; };
  const status = text => {
    const value = imageStages[text] || String(text).replace('in your browser', 'on your computer');
    if (lastStatus !== value) { lastStatus = value; void message(value).catch(() => {}); }
  };
  const progress = value => {
    const percent = Math.floor(Math.max(0, Math.min(1, value)) * 10) * 10;
    if (Number.isFinite(percent) && percent !== lastProgress) { lastProgress = percent; status(`${percent}%`); }
  };
  const options = () => ({usb, identity, signal, onStatus: event => status(connectionMessage(event))});
  const askWifi = async () => {
    for (;;) {
      const ssid = await ask('Wi-Fi network name (optional; press Enter to use the hotspot later): ');
      if (!ssid) return null;
      let password = await ask('Wi-Fi password (hidden; Enter for an open network): ', true);
      try { validateWifi(ssid, password); const entered = {ssid, password}; password = ''; return entered; }
      catch (error) { password = ''; await message(error.message); }
    }
  };
  const ask = async (text, secret = false) => {
    const value = await prompt(text, secret);
    if (typeof value !== 'string') throw Object.assign(new Error('Terminal input closed.'), {code: 'cancelled'});
    return value;
  };
  try {
    await message('AWTRIX NG · TC002 USB installer');
    for (;;) {
      await desktop.phase('loading');
      firmwareSource = null;
      try {
        status('Looking for firmware…');
        const [bytes] = await Promise.all([desktop.firmware(event => {
          if (event.stage === 'local') firmwareSource = 'local';
          else if (event.stage === 'checking' || event.stage === 'downloading') firmwareSource = 'github';
          status(firmwareProgressMessage(event));
        }), preload()]);
        if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length > MAX_ARCHIVE_BYTES) throw new Error('The firmware package has an invalid size.');
        bundle = await parsePackage(bytes);
        break;
      } catch (error) {
        await desktop.phase('failed');
        await message(error?.code === 'no-compatible-release' ? 'No compatible TC002 firmware has been published yet.' : error.message || 'Firmware could not be loaded.');
        if (error?.code === 'local-firmware' || firmwareSource === 'local')
          await message('Check the local firmware ZIP, then retry. No firmware was downloaded.');
        if (!yes(await ask('Retry firmware? [y/N] '))) return {cancelled: true};
      }
    }
    await message(`${firmwareSource === 'local' ? 'Local firmware' : firmwareSource === 'github' ? 'Firmware from GitHub' : 'Firmware'} checked: ${bundle.version}. Connect one TC002 with a data cable, then switch it on.`);
    for (;;) {
      try {
        await desktop.phase('waiting');
        session = await connectGranted(options()); identity = session.identity;
        installer = new Tc002Installer(session, bundle, buildResImage, {onStatus: status, onProgress: progress});
        await desktop.phase('preparing');
        await installer.prepare();
        await desktop.phase('ready');
        break;
      } catch (error) {
        await installer?.dispose().catch(() => {}); await session?.close().catch(() => {});
        installer = session = identity = null;
        await desktop.phase('failed');
        await message(error.message || 'The clock could not be prepared.');
        if (!yes(await ask('Keep the installer open, check the cable and power-cycle the clock. Try USB again? [y/N] '))) return {cancelled: true};
      }
    }
    credentials = await askWifi();
    if (await ask('Type INSTALL to install AWTRIX NG on this TC002: ') !== 'INSTALL') {
      await message('Cancelled. Nothing was installed.');
      return {cancelled: true};
    }
    for (;;) {
      try {
        await desktop.phase('installing'); lastProgress = -1;
        await installer.install(session);
        await desktop.phase('verified');
        await message('Installation verified. Every file was checked.');
        break;
      } catch (error) {
        await desktop.phase('recovery');
        await message(error.message || 'Installation was interrupted.');
        await message('Keep the clock powered and this terminal open. Do not restart it. Reconnect the same USB cable to finish safely.');
        for (;;) {
          let answer;
          try { answer = await ask('Press Enter to reconnect and finish. Type QUIT only to leave this installation incomplete: '); }
          catch {
            await message('Terminal input is closed. Installation remains incomplete; keep the clock powered and do not restart it.');
            return {cancelled: true, recoveryNeeded: true};
          }
          if (answer === 'QUIT') {
            await message('Installation is incomplete. Do not restart the clock.');
            return {cancelled: true, recoveryNeeded: true};
          }
          if (answer !== '') continue;
          try {
            await desktop.phase('reconnecting');
            await session?.close().catch(() => {});
            session = await connectGranted(options());
            break;
          } catch (failure) {
            await desktop.phase('recovery');
            await message(failure.message || 'The same clock could not be reconnected.');
          }
        }
      }
    }
    await message('Restarting the clock…');
    await desktop.phase('restarting');
    await installer.dispose(); await installer.reboot(); session = null;
    while (credentials) {
      await desktop.phase('provisioning');
      const result = await provisionWifi({connect: () => connectGranted(options()), credentials, signal, status, sleep: deps.sleep});
      clearWifi();
      await message(result.message);
      if (result.ip) { await desktop.phase('done'); await message(`Open http://${result.ip} in your browser.`); return {installed: true, restarted: true, ip: result.ip}; }
      if (!yes(await ask('Keep the USB cable connected. Try Wi-Fi again? [y/N] '))) break;
      credentials = await askWifi();
    }
    await desktop.phase('done');
    await message('Join the Wi-Fi shown on the clock (awtrixng-XXXXXX), then open http://192.168.4.1 and enter your home Wi-Fi details.');
    return {installed: true, restarted: true, ip: null};
  } finally {
    clearWifi();
    if (!installer?.writeStarted || installer?.verified) {
      await installer?.dispose().catch(() => {});
      await session?.close().catch(() => {});
    }
  }
}
