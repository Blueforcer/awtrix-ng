/* Backup & restore.
   - Offline (default): drives the real zipStore() inside jsdom and checks it
     emits a well-formed store-only ZIP (manifest first, extractable entries).
   - Live (--live): a full round-trip against a running AWTRIX NG such as
     awtrix-linux on :8080 - the browser's zipStore builds an archive from it,
     we scramble the state, POST the archive back to
     the real /api/v1/restore, and confirm the state came back. The ZIP the
     browser writes is read by the actual firmware ZipReader, so this is the
     writer<->reader interop check the offline test can't be. */
const { boot, bootLive, goto, flush, stubXhr } = require('./harness');

const LIVE = process.argv.includes('--live');
const BASE = 'http://localhost:8080';
let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) { pass++; } else { fail++; console.error('  ✗ ' + msg); }
}

// ---- a tiny independent ZIP reader, just enough to validate the writer -------
function parseZip(buf) {
  const entries = [];
  let p = 0;
  while (p + 4 <= buf.length) {
    const sig = buf.readUInt32LE(p);
    if (sig !== 0x04034b50) break; // central directory / EOCD
    const crc = buf.readUInt32LE(p + 14);
    const size = buf.readUInt32LE(p + 18);
    const nameLen = buf.readUInt16LE(p + 26);
    const extraLen = buf.readUInt16LE(p + 28);
    const name = buf.slice(p + 30, p + 30 + nameLen).toString('utf8');
    const dataStart = p + 30 + nameLen + extraLen;
    const data = buf.slice(dataStart, dataStart + size);
    entries.push({
      name, crc, data,
      dosTime: buf.readUInt16LE(p + 10),
      dosDate: buf.readUInt16LE(p + 12),
    });
    p = dataStart + size;
  }
  return entries;
}
function parseCentralDirectory(buf) {
  const entries = [];
  let p = 0;
  while (p + 4 <= buf.length && buf.readUInt32LE(p) === 0x04034b50) {
    p += 30 + buf.readUInt16LE(p + 26) + buf.readUInt16LE(p + 28) + buf.readUInt32LE(p + 18);
  }
  while (p + 46 <= buf.length && buf.readUInt32LE(p) === 0x02014b50) {
    const nameLen = buf.readUInt16LE(p + 28);
    const extraLen = buf.readUInt16LE(p + 30);
    const commentLen = buf.readUInt16LE(p + 32);
    entries.push({
      name: buf.slice(p + 46, p + 46 + nameLen).toString('utf8'),
      dosTime: buf.readUInt16LE(p + 12),
      dosDate: buf.readUInt16LE(p + 14),
    });
    p += 46 + nameLen + extraLen + commentLen;
  }
  return entries;
}
function fromDosDateTime(date, time) {
  return new Date(1980 + (date >>> 9), ((date >>> 5) & 15) - 1, date & 31,
    time >>> 11, (time >>> 5) & 63, (time & 31) * 2);
}
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[i] = c >>> 0; }
  return t;
})();
function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

// jsdom's Blob has no arrayBuffer(), but it does implement FileReader.
function blobBytes(window, blob) {
  return new Promise((res, rej) => {
    const fr = new window.FileReader();
    fr.onload = () => res(Buffer.from(fr.result));
    fr.onerror = () => rej(fr.error || new Error('read failed'));
    fr.readAsArrayBuffer(blob);
  });
}

// ---- offline: the ZIP writer -----------------------------------------------
async function testZipStructure() {
  const { window } = await boot();
  const entries = [
    { name: 'manifest.json', data: '{"app":"awtrix-ng","backupFormat":1}' },
    { name: 'PALETTES/fire.txt', data: 'FF0000\nFFAA00\n' },
  ];
  const before = Date.now();
  const blob = window.zipStore(entries);
  const after = Date.now();
  const bytes = await blobBytes(window, blob);
  const got = parseZip(bytes);
  const central = parseCentralDirectory(bytes);

  assert(got.length === 2, 'writer emits both entries (got ' + got.length + ')');
  assert(got[0].name === 'manifest.json', 'manifest.json is written first');
  assert(got[1].name === 'PALETTES/fire.txt', 'second entry name preserved');
  assert(got[1].data.toString('utf8') === 'FF0000\nFFAA00\n', 'entry data preserved');
  // The firmware verifies this CRC; an independent recompute must match.
  assert(got[0].crc === crc32(got[0].data), 'manifest CRC is correct');
  assert(got[1].crc === crc32(got[1].data), 'palette CRC is correct');
  const timestamps = [...got, ...central].map(e => +fromDosDateTime(e.dosDate, e.dosTime));
  assert(timestamps.length === 4 && timestamps.every(ts => ts >= before - 2000 && ts <= after),
    'local and central entries carry the backup creation time');
  // End-of-central-directory record present.
  assert(bytes.readUInt32LE(bytes.length - 22) === 0x06054b50, 'EOCD signature present');
  window.close();
}

async function testSelectAllCategories() {
  const { window } = await boot();
  await goto(window, '#/system');
  const section = window.document.querySelector('#sec-backup');
  const labels = [...section.querySelectorAll('label')]
    .filter(label => label.querySelector('input[type=checkbox]'));
  const all = labels.find(label => label.textContent.trim() === 'All');
  assert(!!all, 'backup offers an All checkbox');
  if (all) {
    const master = all.querySelector('input');
    master.checked = true;
    master.dispatchEvent(new window.Event('change', { bubbles: true }));
    const categories = labels.filter(label => label !== all).map(label => label.querySelector('input'));
    assert(categories.length > 0 && categories.every(box => box.checked),
      'All selects every available backup category');
    categories[0].checked = false;
    categories[0].dispatchEvent(new window.Event('change', { bubbles: true }));
    assert(!master.checked && master.indeterminate,
      'All becomes indeterminate when only some categories are selected');
  }
  window.close();
}

async function testSetupOffersWifiAndRestore() {
  const { window, netlog } = await boot({ device: { ipAddress: '0.0.0.0' },
    system: { wifiSsid: '', hostname: 'awtrix', netStatic: true, ip: '10.0.0.2',
      gateway: '10.0.0.1', dns1: '10.0.0.1', wifiConnectTimeout: 30, mqttHost: 'broker' } });
  await goto(window, '#/system');
  const doc = window.document;
  const fields = [...doc.querySelectorAll('#sec-wifi .frow')];
  assert(fields.length === 3, 'setup offers exactly Wi-Fi name, password and hostname');
  const restore = doc.querySelector('#sec-backup');
  assert(!!restore, 'setup offers backup restore');
  const files = restore ? [...restore.querySelectorAll('input[type=file]')] : [];
  assert(files.length === 1 && doc.querySelectorAll('input[type=file]').length === 1,
    'restore is the only file upload in setup');
  assert(restore && !restore.querySelector('input[type=checkbox]') &&
    ![...restore.querySelectorAll('button')].some(b => b.textContent === 'Download backup'),
    'setup offers no backup download');
  const link = restore && restore.querySelector('.captive-hint a');
  assert(link && link.getAttribute('href') === window.location.origin + '/' &&
    link.textContent === window.location.host, 'captive hint links to this setup page');
  assert(!doc.querySelector('#sec-adv'), 'setup does not expose other system settings');
  assert(netlog.every(line => !/secrets|\/api\/v1\/(settings|files|apps)/.test(line)),
    'setup does not request private settings or files');
  const changes = ['SetupNetwork', 'test-password', 'new-host'];
  fields.forEach((field, i) => {
    const input = field.querySelector('input');
    input.value = changes[i];
    input.dispatchEvent(new window.Event('input', { bubbles: true }));
  });
  const fetch = window.fetch;
  let saved;
  window.fetch = async (url, opts) => {
    if (url === '/api/v1/system' && opts.method === 'PUT') saved = JSON.parse(opts.body);
    return fetch(url, opts);
  };
  doc.querySelector('#savebar button.pri').click();
  await flush();
  assert(JSON.stringify(saved) === JSON.stringify({ wifiSsid: changes[0], wifiPass: changes[1], hostname: changes[2] }),
    'setup saves only the three allowed string fields');
  const uploads = [];
  stubXhr(window, uploads);
  if (files.length) {
    Object.defineProperty(files[0], 'files', { value: [new window.File(['zip'], 'backup.zip')] });
    files[0].dispatchEvent(new window.Event('change'));
    await flush();
  }
  assert(uploads.length === 1 && uploads[0].method === 'POST' && uploads[0].url === '/api/v1/restore' &&
    uploads[0].files[0]?.name === 'backup.zip', 'setup restore uploads the chosen backup');
  assert(doc.querySelector('#toasts').textContent.includes('Backup restored'), 'setup restore reports success');
  window.close();
}

// ---- live backend: full round-trip -----------------------------------------
async function postArchive(bytes) {
  const fd = new FormData();
  fd.append('file', new Blob([bytes], { type: 'application/zip' }), 'backup.zip');
  const r = await fetch(BASE + '/api/v1/restore', { method: 'POST', body: fd });
  return { status: r.status, body: await r.json() };
}

async function testRoundTripAgainstLiveBackend() {
  // Seed a known state through the real API.
  await fetch(BASE + '/api/v1/settings', {
    method: 'PATCH', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ brightness: 42 }),
  });
  await fetch(BASE + '/api/v1/system', {
    method: 'PUT', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ wifiSsid: 'BackupNet', wifiPass: 'topsecret' }),
  });
  // A script with a sound of its own; the sound has to come back with the scripts category.
  await fetch(BASE + '/api/v1/apps/script/BkRacer', { method: 'PUT', body: 'def draw() end\n' });
  const sound = new FormData();
  sound.append('file', new Blob(['ID3-backup-boost']), 'boost.mp3');
  const upload = await fetch(BASE + '/api/v1/apps/script/BkRacer/sounds', { method: 'POST', body: sound });
  assert(upload.status === 200, 'the script sound is stored (got ' + upload.status + ')');

  // The browser builds the backup from the live device.
  const { window } = await bootLive(BASE + '/');
  const entries = await window.collectBackup({ wifi: true, settings: true, icons: true,
    melodies: true, palettes: true, scripts: true, apporder: true });
  const names = entries.map(e => e.name);
  assert(names[0] === 'manifest.json', 'collectBackup puts manifest first');
  assert(names.includes('config/wifi.json'), 'wifi captured');
  assert(names.includes('config/system.json'), 'system config captured');
  assert(names.includes('config/settings.json'), 'settings captured');
  assert(names.includes('SCRIPTS/BkRacer.ax') && names.includes('SCRIPTS/BkRacer/boost.mp3'),
    'a script and its sound are captured');
  const bytes = await blobBytes(window, window.zipStore(entries));
  window.close();

  // Scramble the state so a successful restore is unambiguous.
  await fetch(BASE + '/api/v1/settings', {
    method: 'PATCH', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ brightness: 199 }),
  });
  await fetch(BASE + '/api/v1/system', {
    method: 'PUT', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ wifiSsid: 'WrongNet', wifiPass: 'wrong' }),
  });
  await fetch(BASE + '/api/v1/apps/BkRacer', { method: 'DELETE' });
  assert((await fetch(BASE + '/SCRIPTS/BkRacer/boost.mp3')).status === 404, 'deleting the script took its sound');

  // The firmware reads the browser's archive and applies it.
  const res = await postArchive(bytes);
  assert(res.status === 200, 'restore returns 200 (got ' + res.status + ')');
  assert(res.body.ok === true, 'restore reports ok');
  assert(res.body.applied.settings >= 1, 'settings applied');
  assert(res.body.applied.wifi >= 1, 'wifi applied');
  const restored = await fetch(BASE + '/SCRIPTS/BkRacer/boost.mp3');
  assert(restored.status === 200 && await restored.text() === 'ID3-backup-boost', 'the script sound is restored byte for byte');
  await fetch(BASE + '/api/v1/apps/BkRacer', { method: 'DELETE' });

  // Confirm the scrambled state was overwritten.
  const set = await (await fetch(BASE + '/api/v1/settings')).json();
  assert(set.brightness === 42, 'brightness restored to 42 (got ' + set.brightness + ')');
  const sys = await (await fetch(BASE + '/api/v1/system?secrets=1')).json();
  assert(sys.wifiSsid === 'BackupNet', 'wifi ssid restored');
  assert(sys.wifiPass === 'topsecret', 'wifi password restored');

  // A foreign archive must be refused without touching anything.
  const bad = await postArchive(Buffer.from('not a zip at all'));
  assert(bad.status === 400, 'garbage upload rejected with 400 (got ' + bad.status + ')');
  assert(bad.body.ok === false, 'garbage upload reports not-ok');
}

async function main() {
  await testZipStructure();
  await testSelectAllCategories();
  await testSetupOffersWifiAndRestore();
  if(!LIVE){
    const{window,store}=await boot();
    store.files['/ICONS'].set('mail.gif',12);
    const origin={name:'mail.gif',hub:'https://awtrix.de/icons/',slug:'mail',sha256:'a'.repeat(64)};
    store.iconOrigins.set('mail.gif',origin);
    const entries=await window.collectBackup({icons:true});
    const metadata=entries.find(e=>e.name==='config/icon-origins.json');
    assert(entries.some(e=>e.name==='ICONS/mail.gif'),'icon backup includes actual file');
    assert(JSON.parse(metadata.data).icons[0].sha256===origin.sha256,'icon backup retains original content reference');
    store.originFailure=true;let failed=false;
    try{await window.collectBackup({icons:true});}catch(e){failed=true;}
    assert(failed,'metadata storage failure cannot silently produce incomplete backup');
    for(const[status,body,want]of[
      [401,'{"error":{"code":"unauthorized","message":"Login required"}}','Login required'],
      [507,'{"error":{"code":"insufficientStorage"}}','insufficientStorage'],
      [400,'{"error":"manifest missing"}','manifest missing'],
      [500,'oops','HTTP 500']]){
      window.xhrPost=async()=>({status,responseText:body});
      let msg='';
      try{await window.restoreBackup({name:'b.zip'});}catch(e){msg=e.message;}
      assert(msg===want,'restore error '+status+' reads "'+want+'" (got "'+msg+'")');
    }
    window.close();
  }
  if (LIVE) {
    await testRoundTripAgainstLiveBackend();
  } else {
    console.log('  (skipping live round-trip; pass --live with awtrix-linux running on :8080)');
  }
  await flush(20);
  console.log(`backup-restore: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main();
