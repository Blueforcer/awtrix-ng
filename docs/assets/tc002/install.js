import {RES_PARTITION_BYTES as PARTITION_SIZE, RES_ERASE_BYTES as ERASE_SIZE,
  RES_CHUNK_BYTES as CHUNK, TMP_RESERVE_BYTES as TMP_RESERVE, MAX_CONFIG_BYTES as MAX_CONFIG} from './constants.js';

const MODEL = 'Zkswe_SSD21X_SPINOR';
const RES = '/dev/mtd/mtd3';
const LOADER = '/res/lib/libawtrix-loader.so';
const LEGACY_CURRENT = '/data/awtrix-ng/current';
const encoder = new TextEncoder();
const decoder = new TextDecoder();
const secrets = new Set(['secretKey', 'authToken', 'authRefreshToken', 'tokenExpireTime',
  'refreshTokenExpireTime', 'wifiSsid', 'wifiPwd']);

export async function sha256(bytes) {
  const result = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(result)].map(value => value.toString(16).padStart(2, '0')).join('');
}

function randomId() {
  return [...crypto.getRandomValues(new Uint8Array(12))].map(n => n.toString(16).padStart(2, '0')).join('');
}

function path(value) {
  if (!/^\/[A-Za-z0-9_./-]+$/.test(value) || value.split('/').includes('..'))
    throw new Error('Invalid installation path.');
  return value;
}

export function parsePartition(text) {
  const entries = text.split(/\r?\n/).filter(line => /^mtd3:/.test(line));
  if (entries.length !== 1) throw new Error('The clock has an unsupported flash layout.');
  const match = /^mtd3:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"res"$/.exec(entries[0].trim());
  if (!match || parseInt(match[1], 16) !== PARTITION_SIZE || parseInt(match[2], 16) !== ERASE_SIZE)
    throw new Error('The clock has an unsupported application partition.');
  return {size: PARTITION_SIZE, eraseSize: ERASE_SIZE};
}

export function inspectVendorSettings(bytes) {
  const lines = [], removed = [], kept = [];
  let start = 0;
  for (let at = 0; at <= bytes.length; at++) {
    if (at < bytes.length && bytes[at] !== 10 && bytes[at] !== 13) continue;
    if (bytes[at] === 13 && bytes[at + 1] === 10) at++;
    const end = at < bytes.length ? at + 1 : at;
    const line = bytes.subarray(start, end);
    const text = decoder.decode(line).replace(/^\uFEFF/, '').trim();
    const separator = text.indexOf('=');
    const key = separator < 0 || /^[;#\[]/.test(text) ? '' : text.slice(0, separator).trim();
    if (secrets.has(key)) removed.push(key);
    else { lines.push(line); if (key) kept.push(key); }
    start = end;
  }
  const result = new Uint8Array(lines.reduce((size, line) => size + line.length, 0));
  let offset = 0;
  for (const line of lines) { result.set(line, offset); offset += line.length; }
  return {bytes: result, removed: [...new Set(removed)], kept};
}

export function stripVendorSettings(bytes) { return inspectVendorSettings(bytes).bytes; }

export function parseAttributes(text) {
  const match = /^-([r-][w-][x-][r-][w-][x-][r-][w-][x-])\s+\d+\s+(\d+)\s+(\d+)\s/.exec(text.trim());
  if (!match) throw new Error('The clock has unsupported settings-file permissions.');
  const mode = [...match[1]].reduce((value, char, i) => value | (char === '-' ? 0 : 1 << (8 - i)), 0);
  return {mode: mode.toString(8).padStart(4, '0'), uid: match[2], gid: match[3]};
}

export class Tc002Installer {
  constructor(session, bundle, buildImage, {onStatus = () => {}, onProgress = () => {},
    sleep = ms => new Promise(resolve => setTimeout(resolve, ms)), id = randomId()} = {}) {
    if (!/^[0-9a-f]{24}$/.test(id)) throw new Error('Invalid installation identifier.');
    this.session = session;
    this.bundle = bundle;
    this.buildImage = buildImage;
    this.status = onStatus;
    this.progress = onProgress;
    this.sleep = sleep;
    this.work = `/tmp/awtrix-browser-${id}`;
    this.helperPath = `${this.work}/flash`;
    this.commandNumber = 0;
    this.prepared = null;
    this.writeStarted = false;
    this.verified = false;
    this.busy = false;
  }

  async run(command, timeoutMs = 30000) {
    if (command.length > 2800) throw new Error('Installation command is too long.');
    const marker = `AWTRIX_${++this.commandNumber}_RESULT`;
    const output = await this.session.shell(`( ${command} ); r=$?; echo; echo ${marker}:$r`,
      {timeoutMs, maxBytes: MAX_CONFIG});
    const match = new RegExp(`(?:\r?\n)${marker}:(\\d+)\\s*$`).exec(output);
    if (!match || match[1] !== '0') throw new Error('The clock could not complete an installation step.');
    return output.slice(0, match.index).trim();
  }

  async exists(name, kind = 'f') {
    return (await this.run(`if [ -${kind} ${path(name)} ]; then echo yes; else echo no; fi`)) === 'yes';
  }

  async checkClock() {
    if ((await this.run('getprop ro.product.model')) !== MODEL)
      throw new Error('The selected device is not a supported Ulanzi TC002.');
    parsePartition(await this.run('cat /proc/mtd'));
    const boot = await this.run('cat /proc/sys/kernel/random/boot_id');
    if (!/^[0-9a-f-]{36}$/.test(boot)) throw new Error('Could not identify this clock startup.');
    if (this.boot && this.boot !== boot)
      throw new Error('The clock restarted. Start a new installation after restoring the original firmware with its reset button.');
    this.boot = boot;
  }

  async helper(command, args = [], timeoutMs = 120000) {
    if (!/^[a-z0-9-]+$/.test(command) || args.some(arg => !/^[A-Za-z0-9_.+/-]+$/.test(String(arg))))
      throw new Error('Invalid flash-helper arguments.');
    const output = await this.run([this.helperPath, command, ...args].join(' '), timeoutMs);
    let result;
    try { result = JSON.parse(output); } catch { throw new Error('The flash helper returned an invalid reply.'); }
    if (result.command !== command || result.result !== 'ok') throw new Error('The flash helper refused the operation.');
    return result;
  }

  async stageHelper() {
    await this.run(`mkdir -p ${this.work} && chmod 700 ${this.work}`);
    await this.session.writeFile(this.helperPath, this.bundle.helper, {mode: 0o700});
    const back = await this.session.readFile(this.helperPath, {maxBytes: this.bundle.helper.length});
    if (await sha256(back) !== await sha256(this.bundle.helper)) throw new Error('USB transfer verification failed.');
    await this.run(`chmod 700 ${this.helperPath}`);
    await this.requireTmp(CHUNK * 2);
  }

  async requireTmp(bytes) {
    const free = await this.helper('statfs', ['/tmp']);
    if (!Number.isSafeInteger(free.free_bytes) || free.free_bytes < bytes)
      throw new Error('The clock does not have enough temporary memory.');
  }

  async resHash(length = PARTITION_SIZE) {
    const result = await this.helper('hash', [RES, length]);
    if (result.name !== 'res' || result.size !== PARTITION_SIZE || result.erase_size !== ERASE_SIZE ||
        result.length !== length || !/^[0-9a-f]{64}$/.test(result.sha256))
      throw new Error('The clock reported an unexpected flash partition.');
    return result.sha256;
  }

  async prepare() {
    if (this.busy || this.prepared || this.writeStarted) throw new Error('Installation is already in progress.');
    this.busy = true;
    try {
      this.status('Checking the TC002…');
      await this.checkClock();
      if (await this.exists(LOADER) || await this.exists(LEGACY_CURRENT, 'L'))
        throw new Error('AWTRIX NG is already installed on this clock. Update it from its web interface.');
      await this.stageHelper();
      const expected = await this.resHash();
      this.status('Reading the clock’s application data…');
      const stockRes = await this.session.readFile(RES, {maxBytes: PARTITION_SIZE, timeoutMs: 300000,
        onProgress: ({transferred}) => this.progress(transferred / PARTITION_SIZE)});
      if (stockRes.length !== PARTITION_SIZE) throw new Error('The clock’s application data was truncated.');
      if (await sha256(stockRes) !== expected || await this.resHash() !== expected)
        throw new Error('The clock’s application data changed or was transferred incorrectly.');
      this.status('Preparing the installation in your browser…');
      const built = await this.buildImage({stockRes, loader: this.bundle.loader, loop: this.bundle.loop,
        onProgress: this.status});
      if (!(built.image instanceof Uint8Array) || built.image.length < 96 || built.image.length > PARTITION_SIZE)
        throw new Error('The installation image does not fit this clock.');
      const imageSha256 = await sha256(built.image);
      if (built.stockSha256 !== expected || built.imageSha256 !== imageSha256)
        throw new Error('The installation image failed verification.');
      if (!Number.isSafeInteger(built.slot?.capacity) || built.slot.capacity < this.bundle.image.length)
        throw new Error('This firmware does not fit the clock’s flash memory. Nothing has been written.');
      this.prepared = {image: built.image, imageSha256, stockSha256: expected, releaseSha256: await sha256(this.bundle.image)};
      stockRes.fill(0);
      this.status('Ready to install.');
      return {version: this.bundle.version, bytes: built.image.length};
    } finally { this.busy = false; }
  }

  async stopVendor() {
    this.status('Stopping the original application…');
    return this.stopServices(['zkswe', 'hciattach', 'wpa_supplicant']);
  }

  async stopServices(names) {
    const changed = [];
    for (const name of names) {
      if ((await this.run(`getprop init.svc.${name}`)) !== 'running') continue;
      await this.run(`setprop ctl.stop ${name}`);
      let stopped = false;
      for (let i = 0; i < 20; i++) {
        if ((await this.run(`getprop init.svc.${name}`)) !== 'running') { stopped = true; break; }
        await this.sleep(250);
      }
      if (!stopped) throw new Error('The original application could not be stopped.');
      changed.push(name);
    }
    return changed;
  }

  async replaceFile(target, bytes, attributes) {
    const staged = `${this.work}/settings.tmp`;
    const slash = target.lastIndexOf('/');
    const temporary = `${target.slice(0, slash)}/.${target.slice(slash + 1)}.awtrix-new`;
    try {
      await this.session.writeFile(staged, bytes, {mode: 0o600});
      await this.run(`rm -f ${path(temporary)} && cp ${staged} ${temporary} && rm -f ${staged}`);
      if (await sha256(await this.session.readFile(temporary, {maxBytes: MAX_CONFIG})) !== await sha256(bytes))
        throw new Error(`${target} arrived damaged; the original is unchanged`);
      await this.run(`chown ${attributes.uid}:${attributes.gid} ${temporary} && chmod ${attributes.mode} ${temporary} && sync && mv -f ${temporary} ${path(target)} && sync`);
      if (await sha256(await this.session.readFile(target, {maxBytes: MAX_CONFIG})) !== await sha256(bytes))
        throw new Error('The clock’s settings could not be verified.');
    } catch (error) {
      try { await this.run(`rm -f ${temporary} ${staged}`); }
      catch (cleanup) { error.cleanupFailures = [`removing staged settings failed as well: ${cleanup.message}`]; }
      throw error;
    }
  }

  async secureVendor({apply = true, backup, status = async () => {}} = {}) {
    const settings = '/data/setting.ini', supplicant = '/data/misc/wifi/wpa_supplicant.conf';
    const files = [], changes = [], buffers = [];
    const attributes = async file => parseAttributes(await this.run(`ls -ln ${path(file)}`));
    const read = async file => {
      const bytes = await this.session.readFile(file, {maxBytes: MAX_CONFIG});
      buffers.push(bytes); return bytes;
    };
    try {
      for (const file of [settings, supplicant, '/data/misc/wifi/hostapd.conf']) {
        if (await this.exists(file)) files.push({file, bytes: await read(file), attributes: await attributes(file)});
      }
      for (const original of files) {
        if (original.file === settings) {
          const cleaned = inspectVendorSettings(original.bytes);
          buffers.push(cleaned.bytes);
          if (cleaned.removed.length) {
            changes.push({...original, bytes: cleaned.bytes});
            await status(`${settings}: remove ${cleaned.removed.join(', ')}; keep ${cleaned.kept.join(', ') || 'no other key'}`);
          }
        } else if (original.file === supplicant) {
          const template = '/etc/wifi/wpa_supplicant.conf';
          if (!await this.exists(template)) throw new Error(`stock template ${template} is missing`);
          const bytes = await read(template);
          if (/^[\t ]*network[\t ]*=[\t ]*\{/m.test(decoder.decode(bytes)))
            throw new Error(`stock template ${template} has a network block`);
          if (await sha256(original.bytes) !== await sha256(bytes) || original.attributes.mode !== '0660')
            changes.push({...original, bytes, attributes: {...original.attributes, mode: '0660'}});
        }
      }
      const properties = {}, pending = [];
      const store = await this.exists('/data/property', 'd');
      for (const name of ['persist.wifi.on', 'persist.softap.on']) {
        properties[name] = await this.run(`getprop ${name}`);
        const stored = store ? (await this.exists(`/data/property/${name}`) ? await this.run(`cat /data/property/${name}`) : '') : properties[name];
        if (properties[name] !== '0' || stored !== '0') pending.push(name);
      }
      if (!changes.length && !pending.length) return {changed: false, backup: null};
      if (!apply) { await status('dry run: vendor files and properties stay unchanged'); return {changed: true, backup: null}; }
      const saved = backup ? await backup({files, properties}) : null;
      for (const change of changes) await this.replaceFile(change.file, change.bytes, change.attributes);
      for (const name of pending) await this.run(`setprop ${name} 0`);
      for (const change of changes) {
        const actual = await attributes(change.file);
        if (await sha256(await read(change.file)) !== await sha256(change.bytes) ||
            ['mode', 'uid', 'gid'].some(key => actual[key] !== change.attributes[key]))
          throw new Error(`${change.file} does not read back as written`);
      }
      if (files.some(file => file.file === settings)) {
        const checked = inspectVendorSettings(await read(settings));
        buffers.push(checked.bytes);
        if (checked.removed.length) throw new Error(`${settings} still holds vendor credentials`);
      }
      for (const name of ['persist.wifi.on', 'persist.softap.on']) {
        let persisted = false;
        for (let attempt = 0; attempt < 10; attempt++) {
          if (await this.run(`getprop ${name}`) === '0' && (!store || await this.exists(`/data/property/${name}`) && await this.run(`cat /data/property/${name}`) === '0')) {
            persisted = true; break;
          }
          await this.sleep(200);
        }
        if (!persisted) throw new Error(`${name}=0 would be lost at the next reboot`);
      }
      return {changed: true, backup: saved};
    } finally { for (const bytes of buffers) bytes.fill(0); }
  }

  holdsRelease(result) {
    return result.release === this.bundle.release && String(result.counter) === this.bundle.counter &&
      result.imageBytes === this.bundle.image.length && result.sha256 === this.prepared.releaseSha256;
  }

  async writeRelease() {
    const release = this.bundle.image;
    const staged = `${this.work}/release.img`;
    this.status('Copying the firmware to the clock…');
    await this.run(`rm -f ${staged}`);
    await this.requireTmp(release.length + TMP_RESERVE);
    await this.session.writeFile(staged, release, {mode: 0o600, timeoutMs: Math.min(600000, 60000 * Math.ceil(release.length / CHUNK))});
    const uploaded = await this.helper('hash', [staged]);
    if (uploaded.size !== release.length || uploaded.sha256 !== this.prepared.releaseSha256)
      throw new Error('The firmware transfer failed verification.');
    this.status('Writing the firmware. Keep the cable connected…');
    const written = await this.helper('write-slot', [staged, '--length', release.length, '--sha256', this.prepared.releaseSha256,
      '--release', this.bundle.release, '--counter', this.bundle.counter], 600000);
    if (!this.holdsRelease(written)) throw new Error('The written firmware could not be verified.');
    await this.run(`rm -f ${staged}`);
    this.status('Verifying the firmware…');
    const slot = await this.helper('slot-info', ['--verify']);
    if (slot.slot !== 'verified' || !this.holdsRelease(slot)) throw new Error('The firmware failed its final verification.');
  }

  async writeRes(image, expected, progress = () => {}) {
    if (!(image instanceof Uint8Array) || !image.length || image.length > PARTITION_SIZE || await sha256(image) !== expected)
      throw new Error('Invalid installation image');
    const staged = `${this.work}/write.bin`;
    await this.requireTmp(Math.min(CHUNK, image.length));
    for (let offset = 0; offset < image.length; offset += CHUNK) {
      const bytes = image.subarray(offset, Math.min(image.length, offset + CHUNK));
      await this.session.writeFile(staged, bytes, {mode: 0o600, timeoutMs: 60000});
      const digest = await sha256(bytes);
      const uploaded = await this.helper('hash', [staged]);
      if (uploaded.sha256 !== digest || uploaded.size !== bytes.length)
        throw new Error('The installation transfer failed verification.');
      this.writeStarted = true;
      const result = await this.helper('write', [RES, staged, '--offset', offset, '--name', 'res']);
      if (result.name !== 'res' || result.offset !== offset || result.length !== bytes.length || result.sha256 !== digest)
        throw new Error('The written installation could not be verified.');
      await this.run(`rm -f ${staged}`);
      progress(offset + bytes.length);
    }
    if (await this.resHash(image.length) !== expected)
      throw new Error('The written application failed its final verification.');
  }

  async flashSaved(options, host) {
    const {record, target, yes, reboot, removeData, acceptUnknown, resume, previous, release} = options;
    if (!['stock', 'awtrix'].includes(target) || record.partition.size !== PARTITION_SIZE || record.partition.erase_size !== ERASE_SIZE)
      throw new Error('Unsupported installation target or partition');
    if (await this.run('getprop ro.product.model') !== MODEL) throw new Error('Unsupported device model');
    parsePartition(await this.run('cat /proc/mtd'));
    const wanted = record[target];
    let state = 'unknown', entry;
    if (await this.resHash() === record.stock.sha256) state = 'stock';
    else for (const image of [record.awtrix, previous]) {
      if (image && Number.isSafeInteger(image.bytes) && image.bytes > 0 && image.bytes <= PARTITION_SIZE &&
          /^[0-9a-f]{64}$/.test(image.sha256) && await this.resHash(image.bytes) === image.sha256) {
        state = 'image'; entry = image; break;
      }
    }
    if (state === 'unknown' && !resume && !(target === 'stock' && acceptUnknown))
      throw new Error('res holds neither stock nor a known image; refusing to write');
    const resReady = target === 'stock' ? state === 'stock' : state === 'image' && entry.sha256 === wanted.sha256;
    let slotReady = target === 'stock';
    if (target === 'awtrix' && resReady) {
      const slot = await this.helper('slot-info', ['--verify']);
      slotReady = slot.slot === 'verified' && slot.release === release.release && String(slot.counter) === release.counter &&
        slot.sha256 === release.sha256 && slot.imageBytes === release.size;
    }
    const already = resReady && slotReady;
    let vendorStopped = false;
    if (target === 'awtrix') {
      if (yes) {
        const vendor = !already || await this.exists('/tmp/awtrix-loader.vendor');
        vendorStopped = (await this.stopServices(vendor ? ['zkswe', 'hciattach', 'wpa_supplicant'] : ['wpa_supplicant'])).includes('zkswe');
      }
      await this.secureVendor({apply: yes, backup: host.backup, status: host.status});
    }
    if (already && !removeData) {
      await host.status('res already holds the target image' + (target === 'awtrix' ? ' and the release slot this release' : '') + '; nothing to flash');
      if (!vendorStopped) return {reboot: false};
    }
    if (!yes) { await host.status('dry run: pass --yes to flash'); return {reboot: false}; }
    await this.stopServices(['zkswe', 'hciattach']);
    try {
      if (!resReady) {
        const bytes = await host.image(target);
        await host.journal(true, wanted.sha256);
        await this.writeRes(bytes, wanted.sha256);
        await host.journal(false);
      }
      if (target === 'awtrix') {
        if ((await this.secureVendor({apply: false})).changed)
          throw new Error('vendor credentials or Wi-Fi settings came back after secure-vendor');
        await host.deploy(slotReady);
        const bytes = encoder.encode(JSON.stringify({sha256: wanted.sha256, bytes: wanted.bytes}));
        await this.replaceFile('/data/awtrix-ng/state/res-image.json', bytes, {mode: '0600', uid: '0', gid: '0'});
      } else {
        await this.run(removeData ? 'rm -rf /data/awtrix-ng; sync' : 'rm -f /data/awtrix-ng/state/res-image.json; sync');
      }
      this.verified = true;
      if (!reboot) await host.status(`not rebooting (--no-reboot); ${already ? 'the vendor app stays stopped' : '/res keeps serving the old content'} until a reboot`);
      return {reboot};
    } catch (error) {
      error.recoveryNeeded = this.writeStarted;
      if (this.writeStarted) error.message += '; do NOT reboot; repeat the command or restore-stock';
      throw error;
    }
  }

  async install(session = this.session) {
    if (this.busy || !this.prepared || this.verified) throw new Error('No installation is ready.');
    this.busy = true;
    this.session = session;
    try {
      await this.checkClock();
      await this.stageHelper();
      if (!this.writeStarted && await this.resHash() !== this.prepared.stockSha256)
        throw new Error('The clock’s application data changed. Nothing has been flashed.');
      await this.stopVendor();
      await this.secureVendor();
      this.status('Installing AWTRIX NG. Keep the cable connected…');
      const image = this.prepared.image;
      const total = image.length + this.bundle.image.length;
      await this.writeRes(image, this.prepared.imageSha256, bytes => this.progress(bytes / total));
      await this.writeRelease();
      this.progress(1);
      await this.secureVendor();
      const record = encoder.encode(JSON.stringify({sha256: this.prepared.imageSha256, bytes: image.length}));
      await this.run('test ! -L /data/awtrix-ng && test ! -L /data/awtrix-ng/state && mkdir -p /data/awtrix-ng/state && chmod 700 /data/awtrix-ng/state');
      await this.replaceFile('/data/awtrix-ng/state/res-image.json', record, {mode: '0600', uid: '0', gid: '0'});
      await this.run('sync');
      this.verified = true;
      this.status('Installation verified. You can restart the clock.');
    } catch (error) {
      error.recoveryNeeded = this.writeStarted && !this.verified;
      throw error;
    } finally { this.busy = false; }
  }

  async reboot() {
    if (!this.verified || this.busy) throw new Error('The installation has not been verified.');
    try { await this.session.shell('sync; reboot', {timeoutMs: 5000, maxBytes: 1024}); } catch {}
    await this.session.close();
  }

  async dispose() {
    if (this.busy || (this.writeStarted && !this.verified)) return;
    try { await this.run(`rm -f ${this.helperPath} ${this.work}/write.bin ${this.work}/release.img ${this.work}/settings.tmp; rmdir ${this.work}`); } catch {}
    this.prepared?.image.fill(0);
    this.prepared = null;
  }
}

export function validateWifi(ssid, password) {
  if (encoder.encode(ssid).length < 1 || encoder.encode(ssid).length > 32)
    throw new Error('The network name must contain 1 to 32 bytes.');
  if (password === '' || /^[0-9a-fA-F]{64}$/.test(password)) return;
  const bytes = encoder.encode(password);
  if (bytes.length < 8 || bytes.length > 63 || bytes.some(byte => byte < 32 || byte === 127))
    throw new Error('The Wi-Fi password must contain 8 to 63 characters, or 64 hexadecimal digits.');
}

export function privateIpv4(value) {
  if (typeof value !== 'string' || !/^\d{1,3}(\.\d{1,3}){3}$/.test(value)) return null;
  const parts = value.split('.');
  if (parts.some(part => String(Number(part)) !== part || Number(part) > 255)) return null;
  const [a, b] = parts.map(Number);
  return a === 10 || a === 192 && b === 168 || a === 172 && b >= 16 && b <= 31 ? value : null;
}

export function displaySsid(value) {
  if (!/[\x00-\x1f\x7f-\x9f]/.test(value)) return value;
  return [...encoder.encode(value)].map(byte => byte === 92 ? '\\\\' :
    byte >= 32 && byte < 127 ? String.fromCharCode(byte) : `\\x${byte.toString(16).padStart(2, '0')}`).join('');
}

// Both interactive hosts wait for the daemon to finish saving before interpreting a link.
export function wifiLink(wifi, ssid) {
  if (wifi.store === 'saving') return null;
  if (String(wifi.error || '').startsWith('credential')) return 'store-failed';
  return wifi.ssid === displaySsid(ssid) ? wifi.link || null : null;
}

export async function saveWifi(request, credentials) {
  validateWifi(credentials.ssid, credentials.password);
  return request('wifi-set', credentials);
}

// The CLI keeps its one-shot save and optional link wait; browser/desktop can reconnect
// while the newly installed daemon starts. Device decisions and credentials stay here.
export async function wifiCommand(options, host) {
  const {command, credentials, waitSeconds = 40} = options;
  const fail = message => { throw Object.assign(new Error(message), {code: 'wifi-device'}); };
  if (command === 'validate') {
    try { validateWifi(credentials.ssid, credentials.password); }
    catch (error) { error.code = 'wifi-usage'; throw error; }
    return null;
  }
  if (command === 'scan') {
    const reply = await host.request('wifi-scan');
    if (!reply.ok) fail(`device refused the scan: ${reply.error || 'unknown error'}`);
    const deadline = await host.now() + 25000;
    for (;;) {
      const result = await host.request('wifi-scan-results');
      if (!result.scanning) return result.networks || [];
      if (await host.now() >= deadline) fail('the scan did not finish in time');
      await host.sleep(1000);
    }
  }
  if (command === 'status' || command === 'wifi-status') return host.request(command);
  if (command !== 'set') throw new Error('Unsupported Wi-Fi operation');
  try {
    const reply = await saveWifi(host.request, credentials);
    credentials.password = '';
    if (!reply.ok) fail(`device refused the credentials: ${reply.error || 'unknown error'}`);
    await host.status('stored; connecting');
    const deadline = await host.now() + waitSeconds * 1000;
    let last = null;
    while (await host.now() < deadline) {
      const link = wifiLink(await host.request('wifi-status'), credentials.ssid);
      if (link === 'store-failed') fail('the device could not store the new network; nothing was changed');
      if (link) {
        if (last !== link) { await host.status(`link: ${link}`); last = link; }
        if (link === 'connected') return null;
        if (link === 'failed') fail('the network rejected the password');
      }
      await host.sleep(1000);
    }
    if (waitSeconds > 0) await host.status(last === null
      ? `the device has not applied the new network after ${waitSeconds.toFixed(0)} s; see wifi-status`
      : `not connected after ${waitSeconds.toFixed(0)} s (link: ${last}); check the SSID and range`);
    return null;
  } finally { credentials.password = ''; credentials.ssid = ''; }
}

const WIFI_REJECTED = {
  'invalid-password': 'The clock rejected the password. It needs 8 to 63 characters.',
  'invalid-ssid': 'The clock rejected the network name.',
};

export async function provisionWifi({connect, credentials, status = () => {}, signal,
  sleep = ms => new Promise(resolve => setTimeout(resolve, ms)), saveAttempts = 90, joinPolls = 45}) {
  let session = null;
  const expectedSsid = displaySsid(credentials.ssid);
  const check = () => {
    if (signal?.aborted) throw Object.assign(new Error('Wi-Fi setup cancelled.'), {code: 'aborted'});
  };
  const drop = async () => { const old = session; session = null; await old?.close().catch(() => {}); };
  const request = async (command, payload) => {
    if (!session) { session = await connect(); check(); }
    return session.controlRequest(command, payload);
  };
  try {
    check();
    status('Waiting for the clock to start…');
    for (let attempt = 1; ; attempt++) {
      check();
      let reply = null;
      try { reply = await saveWifi(request, credentials); check(); }
      catch (error) { if (error?.code === 'aborted' || signal?.aborted) throw error; await drop(); }
      if (reply?.ok === true) break;
      if (reply && WIFI_REJECTED[reply.error])
        return {outcome: 'rejected', ip: null, message: WIFI_REJECTED[reply.error]};
      if (attempt >= saveAttempts)
        return {outcome: 'unreachable', ip: null, message: 'The clock did not accept the Wi-Fi details over USB. Check the cable and try again.'};
      status(reply ? 'The clock is still starting…' : 'Waiting for the clock to start…');
      await sleep(2000);
    }
    credentials.password = '';
    status(`Wi-Fi details saved. Connecting to ${expectedSsid}…`);
    let lastLink = '';
    for (let poll = 0; poll < joinPolls; poll++) {
      check(); await sleep(2000); check();
      let wifi;
      try { wifi = await request('wifi-status'); check(); }
      catch (error) { if (error?.code === 'aborted' || signal?.aborted) throw error; await drop(); continue; }
      const link = wifiLink(wifi, credentials.ssid);
      if (link === 'store-failed')
        return {outcome: 'rejected', ip: null, message: 'The clock could not store the Wi-Fi details. Try again.'};
      if (wifi.store === 'saving') continue;
      lastLink = wifi.link;
      if (link === 'failed')
        return {outcome: 'join-failed', ip: null, message: `The clock could not join ${expectedSsid}. Check the password.`};
      if (link === 'connected') {
        let state;
        try { state = await request('status'); check(); }
        catch (error) { if (error?.code === 'aborted' || signal?.aborted) throw error; await drop(); continue; }
        const ip = state?.network?.link === 'connected' && state.network.ssid === expectedSsid ? privateIpv4(state.network.ipv4) : null;
        if (ip) return {outcome: 'connected', ip, message: `Connected to ${expectedSsid} at ${ip}.`};
      }
    }
    return lastLink === 'access-point'
      ? {outcome: 'not-found', ip: null, message: `The clock could not reach ${expectedSsid}. Check the network name and that the network is in range.`}
      : {outcome: 'saved', ip: null, message: `Wi-Fi details were saved, but ${expectedSsid} is not connected yet. The clock keeps trying.`};
  } catch (error) {
    if (signal?.aborted || error?.code === 'aborted') throw Object.assign(new Error('Wi-Fi setup cancelled.'), {code: 'aborted'});
    return {outcome: 'unreachable', ip: null, message: 'USB Wi-Fi setup could not finish. Check the cable and try again.'};
  } finally {
    credentials.password = ''; credentials.ssid = '';
    await drop();
  }
}
