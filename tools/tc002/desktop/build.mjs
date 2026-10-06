import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { copyFile, lstat, mkdir, mkdtemp, readFile, readdir, realpath, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { deflateRawSync } from 'node:zlib';

const here = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(here, '../../..');
const native = path.join(here, 'native');
export const uiFiles = ['index.html', 'main.js', 'controller.js', 'styles.css', 'native.js'];
export const cliFiles = ['main.mjs', 'runner.mjs', 'rpc.mjs'];
const sharedFiles = ['adb.js', 'install.js', 'package.js', 'constants.js'];
const imageFiles = ['core.js', 'index.js', 'node.mjs', 'NOTICE.md', 'tar.js', 'worker.js', 'build.json',
  'sqfs2tar.js', 'sqfs2tar.wasm', 'tar2sqfs.js', 'tar2sqfs.wasm'];
const imageLicenses = ['0BSD.txt', 'compiler-rt.txt', 'emscripten-musl.txt', 'emscripten.txt',
  'GPLv3.txt', 'hash_table.txt', 'LGPLv3.txt', 'musl.txt', 'squashfs-tools-ng.md', 'xxhash.txt', 'xz.txt'];
const imageSources = ['build.py', 'clang-size-t.patch', 'emsdk-4.0.21.tar.gz',
  'squashfs-tools-ng-1.3.2.tar.xz', 'xz-5.8.4.tar.xz'];
const sourceFiles = ['build.mjs', 'package.json', 'package-lock.json', 'README.md',
  'native/Cargo.toml', 'native/Cargo.lock', 'native/build.rs', 'native/tauri.conf.json',
  'native/capabilities/default.json', 'native/src/lib.rs', 'native/src/main.rs',
  'native/src/cli.rs', 'native/src/download.rs', 'native/src/usb.rs', 'native/src/service.rs', 'native/src/runtime.rs',
  'native/src/sidecar.rs', 'native/src/constants.rs',
  'native/src/qualification.rs', 'test/tauri-smoke.js',
  'native/resources/linux/70-awtrix-tc002.rules', 'native/resources/linux/README-LINUX.md', 'native/resources/linux/udev-reload.sh',
  'native/icons/icon.svg', 'native/icons/icon.ico', 'native/icons/icon.icns',
  'native/icons/32x32.png', 'native/icons/128x128.png', 'native/icons/128x128@2x.png'];

export const digest = bytes => createHash('sha256').update(bytes).digest('hex');

export const frontendDistPath = (filename, platform = process.platform) => platform === 'win32'
  ? path.win32.toNamespacedPath(filename) : filename;

export function childPath(root, relative) {
  assert(typeof relative === 'string' && relative.length > 0 &&
    relative.split('/').every(part => /^[A-Za-z0-9][A-Za-z0-9._+@-]*$/.test(part) && part !== '.' && part !== '..'),
  'Invalid resource path');
  return path.join(root, ...relative.split('/'));
}

export function verifyDigest(bytes, expected, label) {
  assert(typeof expected === 'string' && /^[a-f0-9]{64}$/.test(expected), `${label}: invalid SHA-256`);
  assert.equal(digest(bytes), expected, `${label}: SHA-256 mismatch`);
}

async function regularFile(filename) {
  assert((await lstat(filename)).isFile(), `Expected a regular file: ${filename}`);
  return readFile(filename);
}

export async function createOutput(output, repo = repository) {
  if (!output) return mkdtemp(path.join(os.tmpdir(), 'awtrix-tc002-installer-'));
  const absolute = path.resolve(output);
  const parent = await realpath(path.dirname(absolute));
  const destination = path.join(parent, path.basename(absolute));
  const root = await realpath(repo);
  const relative = path.relative(root, destination);
  assert(relative && (relative === '..' || relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative)),
    'Build output must be outside the repository');
  await mkdir(destination);
  return destination;
}

export async function verifyImageBuild(imageRoot) {
  const metadata = JSON.parse(await regularFile(path.join(imageRoot, 'build.json')));
  const artifacts = ['sqfs2tar.js', 'sqfs2tar.wasm', 'tar2sqfs.js', 'tar2sqfs.wasm'];
  assert.deepEqual(Object.keys(metadata.artifacts).sort(), artifacts.sort(), 'Unexpected WASM artifact list');
  assert.deepEqual(Object.keys(metadata.sources).sort(), imageSources.filter(name => name.includes('.tar.')).sort(),
    'Unexpected WASM source list');
  for (const name of artifacts) verifyDigest(await regularFile(childPath(imageRoot, name)), metadata.artifacts[name], name);
  for (const [name, source] of Object.entries(metadata.sources)) {
    assert(Array.isArray(source) && source.length === 2 && new URL(source[0]).protocol === 'https:', 'Invalid source reference');
    verifyDigest(await regularFile(childPath(imageRoot, `sources/${name}`)), source[1], name);
  }
  return metadata;
}

async function writeResource(root, relative, bytes, inputs, origin) {
  const destination = childPath(root, relative);
  await mkdir(path.dirname(destination), { recursive: true });
  await writeFile(destination, bytes, { flag: 'wx' });
  inputs.push({ path: relative, size: bytes.length, sha256: digest(bytes), source: origin });
}

export function parseArguments(args) {
  const options = { stageOnly: false, cliOnly: false };
  for (let i = 0; i < args.length; i++) {
    const name = args[i];
    if (name === '--stage-only') options.stageOnly = true;
    else if (name === '--cli-only') options.cliOnly = true;
    else if (['--output', '--target', '--bundles'].includes(name)) {
      const value = args[++i];
      assert(value && !value.startsWith('--'), `Missing value for ${name}`);
      options[name.slice(2)] = value;
    } else throw new Error(`Unknown argument: ${name}`);
  }
  if (options.target) assert(/^(x86_64|aarch64)-(pc-windows-msvc|apple-darwin|unknown-linux-gnu)$/.test(options.target), 'Unsupported target');
  if (options.bundles) assert(/^(none|dmg|deb|rpm|deb,rpm)$/.test(options.bundles), 'Unsupported bundle selection');
  assert(!(options.cliOnly && options.bundles && options.bundles !== 'none'), '--cli-only cannot create GUI bundles');
  return options;
}

export async function stageApplication({ output } = {}) {
  const imageRoot = path.join(repository, 'docs/assets/tc002/image');
  await verifyImageBuild(imageRoot);
  const destination = await createOutput(output);
  const frontend = path.join(destination, 'frontend');
  await mkdir(frontend);
  const inputs = [];
  const copy = async (relative, source, origin) => writeResource(frontend, relative, await regularFile(source), inputs, origin);
  for (const name of uiFiles) await copy(`ui/${name}`, path.join(here, 'ui', name), `tools/tc002/desktop/ui/${name}`);
  for (const name of cliFiles) await copy(`cli/${name}`, path.join(here, 'cli', name), `tools/tc002/desktop/cli/${name}`);
  for (const name of sharedFiles) await copy(`shared/${name}`, path.join(repository, 'docs/assets/tc002', name), `docs/assets/tc002/${name}`);
  for (const name of [...imageFiles, ...imageLicenses.map(name => `licenses/${name}`), ...imageSources.map(name => `sources/${name}`)]) {
    await copy(`shared/image/${name}`, childPath(imageRoot, name), `docs/assets/tc002/image/${name}`);
  }
  for (const name of ['LICENSE.md', 'THIRD-PARTY-NOTICES.md']) await copy(`licenses/${name}`, path.join(repository, name), name);
  await copy('licenses/NOTICE.md', path.join(here, 'licenses/NOTICE.md'), 'tools/tc002/desktop/licenses/NOTICE.md');
  await writeResource(frontend, 'package.json', Buffer.from('{"private":true,"type":"module"}\n'), inputs, 'generated ES module marker');
  const appPackage = JSON.parse(await regularFile(path.join(here, 'package.json')));
  const revision = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: repository, encoding: 'utf8' }).trim();
  const dirty = execFileSync('git', ['status', '--porcelain', '--untracked-files=normal', '--',
    'tools/tc002/desktop', 'docs/assets/tc002', 'tools/tc002/browser-image', 'LICENSE.md', 'THIRD-PARTY-NOTICES.md'],
  { cwd: repository, encoding: 'utf8' }).trim().length > 0;
  const manifest = { schemaVersion: 2, installer: { version: appPackage.version, commit: revision, dirty },
    firmware: { embedded: false, source: 'Explicit file selection first, then adjacent usb-awtrix-ng-tc002.zip, otherwise GitHub release selected at installation time' },
    runtime: { tauriCli: appPackage.devDependencies['@tauri-apps/cli'], systemWebview: true }, inputs };
  await writeFile(path.join(destination, 'build-manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);
  await writeFile(path.join(destination, 'tauri-build.json'), `${JSON.stringify({ build: { frontendDist: frontendDistPath(frontend) } }, null, 2)}\n`);
  return { destination, frontend, manifest };
}

async function walk(root, relative = '') {
  const result = [];
  for (const entry of await readdir(path.join(root, relative), { withFileTypes: true })) {
    const name = relative ? `${relative}/${entry.name}` : entry.name;
    if (entry.isDirectory()) result.push(...await walk(root, name));
    else if (entry.isFile()) result.push(name);
  }
  return result.sort();
}

export async function verifyStagedAssets(frontend, inputs) {
  assert.deepEqual(await walk(frontend), inputs.map(item => item.path).sort(), 'Unexpected staged asset list');
  for (const input of inputs) {
    const bytes = await regularFile(childPath(frontend, input.path));
    assert.equal(bytes.length, input.size, `${input.path}: size mismatch`);
    verifyDigest(bytes, input.sha256, input.path);
  }
}

const upstreamLicenseFiles = {
  'dropbox/rust-alloc-no-stdlib': ['LICENSE'],
  'knurling-rs/defmt': ['LICENSE-MIT', 'LICENSE-APACHE'],
  'Michael-F-Bryan/include_dir': ['LICENSE'],
  'tauri-apps/tauri': ['LICENSE-MIT'],
  'wravery/webview2-rs': ['LICENSE'],
  'madsmtm/objc2': ['LICENSE.md'],
  'OpenByteDev/dlopen2': ['LICENSE'],
  'tauri-apps/libappindicator-rs': ['LICENSE-MIT', 'LICENSE-APACHE'],
  'servo/stylo': [],
};
const standardLicenses = {
  MIT: 'b05785f9f18e6716bab63424b11454513b9943a222595b70411009202fc592b5',
  'MPL-2.0': '66a3107d5ad6a058aab753eaac2047ccb2ed0e39465dd0fe5844da3e300d5172',
};
const noticeDownloads = new Map();

async function downloadNotice(url) {
  if (!noticeDownloads.has(url)) noticeDownloads.set(url, (async () => {
    const response = await fetch(url, { redirect: 'error', signal: AbortSignal.timeout(30000) });
    assert(response.ok, `Cannot retrieve required license: ${url} (HTTP ${response.status})`);
    const bytes = Buffer.from(await response.arrayBuffer());
    assert(bytes.length > 100 && bytes.length < 256 * 1024, `Invalid license size: ${url}`);
    return bytes;
  })());
  return noticeDownloads.get(url);
}

async function missingCrateNotices(item, directory) {
  const vcs = JSON.parse(await regularFile(path.join(directory, '.cargo_vcs_info.json')));
  assert(/^[0-9a-f]{40}$/.test(vcs.git?.sha1), `Missing immutable source reference for ${item.name}`);
  const repositoryUrl = item.repository || (item.name === 'libappindicator-sys' ? 'https://github.com/tauri-apps/libappindicator-rs' : '');
  const repo = /^https:\/\/github\.com\/([A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+)$/.exec(repositoryUrl)?.[1];
  assert(repo && Object.hasOwn(upstreamLicenseFiles, repo), `Review missing license texts for ${item.name}`);
  const notices = [];
  for (const filename of upstreamLicenseFiles[repo]) {
    const url = `https://raw.githubusercontent.com/${repo}/${vcs.git.sha1}/${filename}`;
    notices.push({ name: filename, bytes: await downloadNotice(url), source: url });
  }
  if (repo === 'madsmtm/objc2' || repo === 'servo/stylo') {
    const license = repo === 'madsmtm/objc2' ? 'MIT' : 'MPL-2.0';
    const url = `https://raw.githubusercontent.com/spdx/license-list-data/v3.28.0/text/${license}.txt`;
    const bytes = await downloadNotice(url);
    verifyDigest(bytes, standardLicenses[license], `${license} license text`);
    notices.push({ name: `SPDX-${license}.txt`, bytes, source: url });
  }
  const attribution = `${item.name} ${item.version}\nDeclared license: ${item.license}\nAuthors: ${item.authors.join(', ')}\nSource: ${repositoryUrl}/tree/${vcs.git.sha1}\n\nThis crate omits license files from its published package. The original package,\nincluding its copyright and SPDX notices, accompanies this installer in the\nmatching sources archive. Upstream license notices and, where required, the\nstandard license text are reproduced here without changing the original grant.\n`;
  notices.push({ name: 'SOURCE-NOTICE.txt', bytes: Buffer.from(attribution), source: `crate:${item.name}-${item.version}/Cargo.toml` });
  return notices;
}

export async function nativeNotices(stage, env, target, cliOnly = false) {
  const metadataArgs = ['metadata', '--locked', '--format-version', '1', '--filter-platform', target];
  if (cliOnly) metadataArgs.push('--no-default-features', '--features', 'cli');
  const metadata = JSON.parse(execFileSync('cargo', metadataArgs,
    { cwd: native, env, encoding: 'utf8', maxBuffer: 32 << 20 }));
  const nodes = new Map(metadata.resolve.nodes.map(node => [node.id, node]));
  const reachable = new Set();
  const visit = id => { if (reachable.has(id)) return; reachable.add(id); for (const dependency of nodes.get(id).dependencies) visit(dependency); };
  visit(metadata.resolve.root);
  const packages = metadata.packages.filter(item => item.source && reachable.has(item.id))
    .sort((a, b) => `${a.name}-${a.version}`.localeCompare(`${b.name}-${b.version}`));
  const inventory = [];
  const sources = path.join(stage.destination, 'sources');
  await mkdir(sources);
  for (const item of packages) {
    assert(item.source.startsWith('registry+https://github.com/rust-lang/crates.io-index'), `Unrecognized dependency source: ${item.name}`);
    assert(item.license, `Missing license expression for ${item.name}`);
    const directory = path.dirname(item.manifest_path);
    const prefix = `${item.name}-${item.version}`;
    const paths = (await walk(directory)).filter(name => /(^|\/)(licen[cs]e[^/]*|copying[^/]*|notice[^/]*|copyright[^/]*)$/i.test(name) || /(^|\/)licen[cs]es?\//i.test(name));
    if (item.license_file && !paths.includes(item.license_file)) paths.push(item.license_file);
    const notices = [];
    for (const name of paths.sort()) {
      const relative = `licenses/native/${prefix}/${name}`;
      await writeResource(stage.frontend, relative, await regularFile(childPath(directory, name)), stage.manifest.inputs, `crate:${prefix}/${name}`);
      notices.push(relative);
    }
    if (paths.length === 0) for (const notice of await missingCrateNotices(item, directory)) {
      const relative = `licenses/native/${prefix}/${notice.name}`;
      await writeResource(stage.frontend, relative, notice.bytes, stage.manifest.inputs, notice.source);
      notices.push(relative);
    }
    const record = { name: item.name, version: item.version, license: item.license, authors: item.authors, repository: item.repository, notices };
    if (paths.length === 0 || /\b(MPL|LGPL|GPL|AGPL|EPL|CDDL)-/.test(item.license)) {
      const source = path.join(path.dirname(path.dirname(path.dirname(directory))), 'cache', path.basename(path.dirname(directory)), `${prefix}.crate`);
      const bytes = await regularFile(source);
      await mkdir(path.join(sources, 'native'), { recursive: true });
      await writeFile(path.join(sources, 'native', `${prefix}.crate`), bytes, { flag: 'wx' });
      record.sources = { path: `native/${prefix}.crate`, sha256: digest(bytes), size: bytes.length };
    }
    inventory.push(record);
  }
  await writeResource(stage.frontend, 'licenses/native-dependencies.json', Buffer.from(`${JSON.stringify(inventory, null, 2)}\n`), stage.manifest.inputs, 'cargo metadata --locked');
  await writeFile(path.join(sources, 'native-dependencies.json'), `${JSON.stringify(inventory, null, 2)}\n`);
  for (const relative of sourceFiles) {
    const destination = childPath(sources, `desktop/${relative}`);
    await mkdir(path.dirname(destination), { recursive: true });
    const bytes = await regularFile(childPath(here, relative));
    await writeFile(destination, bytes, { flag: 'wx' });
    (stage.manifest.nativeInputs ??= []).push({ path: relative, size: bytes.length, sha256: digest(bytes) });
  }
  for (const input of stage.manifest.inputs) {
    const destination = childPath(sources, `frontend/${input.path}`);
    await mkdir(path.dirname(destination), { recursive: true });
    await copyFile(childPath(stage.frontend, input.path), destination);
  }
  await writeFile(path.join(sources, 'REBUILD.md'), `# Rebuilding this source snapshot\n\nThe desktop/ directory contains the native Rust project and pinned Tauri CLI.\nThe frontend/ directory contains its exact embedded assets, including notices.\nInstall the platform prerequisites listed in desktop/README.md, then install\nthe CLI with npm ci in desktop/. Set AWTRIX_INSTALLER_ASSETS to the absolute\nfrontend/ path. For the terminal binary, run in desktop/native/:\n\ncargo build --locked --release --no-default-features --features cli --bin awtrix-tc002-installer-cli\n\nFor the graphical program, create an override JSON containing\n{ "build": { "frontendDist": "ABSOLUTE_FRONTEND_PATH" } }, then run in desktop/native/:\n\nnode ../node_modules/@tauri-apps/cli/tauri.js build --config /absolute/override.json -- --locked --bin awtrix-tc002-installer\n\nOn Windows, use a namespaced path for frontendDist (the value produced by\nNode's path.win32.toNamespacedPath), so Tauri treats it as a filesystem path.\nThe full build.mjs entry point is intended for the original Git checkout;\nthe commands above rebuild this extracted snapshot directly.\n\nImage-tool source archives and their build recipe are in frontend/shared/image/sources/.\nNative dependency source archives, where required, are in native/. Cargo.lock\nfixes the remaining registry dependencies for reproducible resolution.\n`);
}

function hostTarget() {
  const target = /^host: (.+)$/m.exec(execFileSync('rustc', ['-vV'], { encoding: 'utf8' }))?.[1];
  assert(target, 'Cannot identify the Rust host target');
  return target;
}

export function zipFiles(files) {
  const crc32 = bytes => {
    let crc = 0xffffffff;
    for (const byte of bytes) { crc ^= byte; for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1)); }
    return (crc ^ 0xffffffff) >>> 0;
  };
  const local = [], central = [];
  let offset = 0, centralSize = 0;
  for (const { name, bytes } of files) {
    childPath('/archive', name);
    const encoded = Buffer.from(name), crc = crc32(bytes), compressed = deflateRawSync(bytes, { level: 9 });
    const header = Buffer.alloc(30), index = Buffer.alloc(46);
    header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4); header.writeUInt16LE(0x800, 6);
    header.writeUInt16LE(8, 8); header.writeUInt16LE(33, 12); header.writeUInt32LE(crc, 14); header.writeUInt32LE(compressed.length, 18);
    header.writeUInt32LE(bytes.length, 22); header.writeUInt16LE(encoded.length, 26);
    index.writeUInt32LE(0x02014b50); index.writeUInt16LE(20, 4); index.writeUInt16LE(20, 6); index.writeUInt16LE(0x800, 8);
    index.writeUInt16LE(8, 10); index.writeUInt16LE(33, 14); index.writeUInt32LE(crc, 16); index.writeUInt32LE(compressed.length, 20);
    index.writeUInt32LE(bytes.length, 24); index.writeUInt16LE(encoded.length, 28); index.writeUInt32LE(offset, 42);
    local.push(header, encoded, compressed); central.push(index, encoded);
    offset += header.length + encoded.length + compressed.length; centralSize += index.length + encoded.length;
  }
  const end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50); end.writeUInt16LE(files.length, 8);
  end.writeUInt16LE(files.length, 10); end.writeUInt32LE(centralSize, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...local, ...central, end]);
}

export function packageUnixCli(archive, executable, target) {
  const args = ['-czf', archive, '-C', path.dirname(executable), path.basename(executable), '-C', here, 'README.md'];
  if (target.includes('linux')) args.push('-C', path.join(native, 'resources/linux'), '70-awtrix-tc002.rules', 'README-LINUX.md');
  execFileSync('tar', args);
}

export async function build(options) {
  const stage = await stageApplication(options);
  console.log(`Staged installer assets: ${stage.frontend}`);
  if (options.stageOnly) { await verifyStagedAssets(stage.frontend, stage.manifest.inputs); return stage; }
  const target = options.target || hostTarget();
  const targetDirectory = process.env.CARGO_TARGET_DIR ? path.resolve(process.env.CARGO_TARGET_DIR) : path.join(stage.destination, 'cargo-target');
  const env = { ...process.env, AWTRIX_INSTALLER_ASSETS: stage.frontend, CARGO_TARGET_DIR: targetDirectory };
  await nativeNotices(stage, env, target, options.cliOnly);
  await verifyStagedAssets(stage.frontend, stage.manifest.inputs);
  stage.manifest.target = target;
  await writeFile(path.join(stage.destination, 'build-manifest.json'), `${JSON.stringify(stage.manifest, null, 2)}\n`);
  const artifacts = path.join(stage.destination, 'artifacts');
  await mkdir(artifacts);
  const extension = target.includes('windows') ? '.exe' : '';
  const version = stage.manifest.installer.version;
  const releaseDirectory = path.join(targetDirectory, target, 'release');
  execFileSync('cargo', ['build', '--locked', '--release', '--target', target, '--no-default-features', '--features', 'cli', '--bin', 'awtrix-tc002-installer-cli'],
    { cwd: native, env, stdio: 'inherit' });
  const cliName = `awtrix-tc002-installer-cli${extension}`;
  const cliBytes = await regularFile(path.join(releaseDirectory, cliName));
  if (extension) await writeFile(path.join(artifacts, `AWTRIX-NG-TC002-CLI-${version}-${target}.zip`),
    zipFiles([{ name: cliName, bytes: cliBytes }, { name: 'README.md', bytes: await regularFile(path.join(here, 'README.md')) }]));
  else packageUnixCli(path.join(artifacts, `AWTRIX-NG-TC002-CLI-${version}-${target}.tar.gz`), path.join(releaseDirectory, cliName), target);
  if (!options.cliOnly) {
    const bundles = options.bundles || (target.includes('windows') ? 'none' : target.includes('darwin') ? 'dmg' : 'deb,rpm');
    const args = [path.join(here, 'node_modules/@tauri-apps/cli/tauri.js'), 'build', '--ci', '--target', target,
      '--config', path.join(stage.destination, 'tauri-build.json')];
    if (bundles === 'none') args.push('--no-bundle'); else args.push('--bundles', bundles);
    args.push('--', '--locked', '--bin', 'awtrix-tc002-installer');
    execFileSync(process.execPath, args, { cwd: native, env, stdio: 'inherit' });
    if (extension) await copyFile(path.join(releaseDirectory, `awtrix-tc002-installer${extension}`),
      path.join(artifacts, `AWTRIX-NG-TC002-Installer-${version}-${target}.exe`));
    if (bundles !== 'none') {
      const bundleDirectory = path.join(releaseDirectory, 'bundle');
      const matches = (await walk(bundleDirectory)).filter(name => /\.(dmg|deb|rpm)$/.test(name));
      assert(matches.length > 0, 'Tauri produced no installer bundle');
      for (const name of matches) await copyFile(path.join(bundleDirectory, name), path.join(artifacts, path.basename(name)));
    }
  }
  execFileSync('tar', ['-czf', path.join(artifacts, `AWTRIX-NG-TC002-Installer-${version}-${target}-sources.tar.gz`),
    '-C', path.join(stage.destination, 'sources'), '.']);
  await verifyStagedAssets(stage.frontend, stage.manifest.inputs);
  for (const input of stage.manifest.nativeInputs) {
    const bytes = await regularFile(childPath(here, input.path));
    verifyDigest(bytes, input.sha256, `Source changed during build: ${input.path}`);
  }
  const files = [];
  for (const name of await walk(artifacts)) {
    const bytes = await regularFile(path.join(artifacts, name));
    files.push({ path: name, size: bytes.length, sha256: digest(bytes) });
  }
  assert(files.length >= 2, 'Missing packaged executable or corresponding sources');
  await writeFile(path.join(stage.destination, 'artifact-manifest.json'), `${JSON.stringify({ schemaVersion: 1, target, artifacts: files }, null, 2)}\n`);
  await copyFile(path.join(stage.destination, 'build-manifest.json'), path.join(artifacts, 'build-manifest.json'));
  await copyFile(path.join(stage.destination, 'artifact-manifest.json'), path.join(artifacts, 'artifact-manifest.json'));
  console.log(`Verified ${stage.manifest.inputs.length} embedded assets; release files: ${artifacts}`);
  return stage;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  Promise.resolve().then(() => build(parseArguments(process.argv.slice(2)))).catch(error => {
    console.error(error.stack || error.message);
    process.exitCode = 1;
    process.on('exit', () => { process.exitCode = 1; });
  });
}
