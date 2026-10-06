window.addEventListener('DOMContentLoaded', async () => {
  const invoke = (...args) => window.__TAURI__.core.invoke(...args);
  const report = {ok: false, origin: location.origin, userAgent: navigator.userAgent};
  const done = () => invoke('qualification', {action: 'report', report});
  const timer = setTimeout(() => { report.error = 'WebView qualification timed out'; void done(); }, 90000);
  try {
    for (let attempt = 0; attempt < 1000; attempt++) {
      const phase = document.querySelector('#installer')?.dataset.phase;
      if (phase === 'waiting') break;
      if (phase === 'failed') throw new Error(document.querySelector('[data-role="error"]')?.textContent || 'Installer preload failed');
      await new Promise(resolve => setTimeout(resolve, 20));
    }
    report.phase = document.querySelector('#installer')?.dataset.phase;
    report.firmwareLabel = document.querySelector('[data-role="release"]')?.textContent;
    if (report.phase !== 'waiting') throw new Error('Installer did not become ready');
    const {parsePackage} = await import('../shared/package.js');
    const {buildResImage} = await import('../shared/image/index.js');
    const bytes = await invoke('firmware');
    if (!(bytes instanceof ArrayBuffer)) throw new Error('Firmware IPC was not binary');
    report.firmwareBytes = bytes.byteLength;
    const bundle = await parsePackage(new Uint8Array(bytes));
    const stockRes = new Uint8Array(await invoke('qualification', {action: 'stock'}));
    const result = await buildResImage({stockRes, loader: bundle.loader, loop: bundle.loop});
    report.release = bundle.release;
    report.imageBytes = result.image.length;
    report.imageSha256 = result.imageSha256;
    report.stockSha256 = result.stockSha256;
    report.slot = result.slot;
    if (report.imageBytes > 8 * 1024 * 1024) throw new Error('Image exceeds partition size');
    if (!(result.slot?.capacity >= bundle.image.length)) throw new Error('Release image exceeds the release slot');
    if (report.imageSha256 !== 'cd84aba1314e4d3ca50eb006f1a5fd108e8a81487e4b96757bd196e277175b00') throw new Error('Synthetic image differs from independently qualified native result');
    const devices = await invoke('usb_list');
    if (!Array.isArray(devices) || devices.length) throw new Error('Qualification USB boundary is not closed');
    try { await invoke('open_setup', {ip: '8.8.8.8'}); throw new Error('Premature external navigation succeeded'); }
    catch (error) { if (error?.message === 'Premature external navigation succeeded') throw error; }
    report.ok = true;
  } catch (error) { report.error = error?.message || String(error); }
  clearTimeout(timer);
  await done();
}, {once: true});
