import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateRawSync } from 'node:zlib';
import test from 'node:test';
import { childPath, createOutput, digest, frontendDistPath, packageUnixCli, parseArguments, tar, verifyDigest, verifyImageBuild, verifyStagedAssets, zipFiles } from '../build.mjs';

async function temporary(t) {
  const base = path.resolve(os.tmpdir());
  const directory = await mkdtemp(path.join(base, 'awtrix-desktop-package-'));
  t.after(async () => {
    assert.equal(path.dirname(path.resolve(directory)), base);
    assert(path.basename(directory).startsWith('awtrix-desktop-package-'));
    await rm(directory, { recursive: true });
  });
  return directory;
}

async function imageFixture(t) {
  const root = await temporary(t);
  await mkdir(path.join(root, 'sources'));
  const metadata = { artifacts: {}, sources: {} };
  for (const name of ['sqfs2tar.js', 'sqfs2tar.wasm', 'tar2sqfs.js', 'tar2sqfs.wasm']) {
    const bytes = Buffer.from(`generated ${name}`);
    await writeFile(path.join(root, name), bytes);
    metadata.artifacts[name] = digest(bytes);
  }
  for (const name of ['emsdk-4.0.21.tar.gz', 'squashfs-tools-ng-1.3.2.tar.xz', 'xz-5.8.4.tar.xz']) {
    const bytes = Buffer.from(`source ${name}`);
    await writeFile(path.join(root, 'sources', name), bytes);
    metadata.sources[name] = [`https://example.com/${name}`, digest(bytes)];
  }
  await writeFile(path.join(root, 'build.json'), JSON.stringify(metadata));
  return { root, metadata };
}

test('resource destinations reject absolute paths and traversal', () => {
  for (const name of ['../secret', 'shared/../../secret', '/absolute', 'C:/private', 'shared\\secret', './file', 'shared//file']) {
    assert.throws(() => childPath('/safe', name), /Invalid resource path/);
  }
  assert.equal(childPath('/safe', 'shared/image/tar2sqfs.wasm'), path.join('/safe', 'shared/image/tar2sqfs.wasm'));
});

test('Windows frontend paths cannot be interpreted as a c: URL by Tauri', () => {
  const result = frontendDistPath('C:\\Build Files\\frontend', 'win32');
  assert.equal(result, '\\\\?\\C:\\Build Files\\frontend');
  assert.throws(() => new URL(result));
  assert.equal(frontendDistPath('/tmp/frontend', 'linux'), '/tmp/frontend');
});

test('builds can only create new output directories outside the repository', async t => {
  const root = await temporary(t);
  const repo = path.join(root, 'repository');
  await mkdir(repo);
  await assert.rejects(createOutput(path.join(repo, 'output'), repo), /outside the repository/);
  await assert.rejects(createOutput(repo, repo), /outside the repository/);
  const output = path.join(root, 'package');
  assert.equal(await createOutput(output, repo), output);
  await writeFile(path.join(output, 'existing.txt'), 'must remain');
  await assert.rejects(createOutput(output, repo), /EEXIST/);
  assert.equal(await readFile(path.join(output, 'existing.txt'), 'utf8'), 'must remain');
});

test('native builds need no firmware input and constrain supported platform bundles', () => {
  assert.deepEqual(parseArguments([]), { stageOnly: false, cliOnly: false });
  assert.deepEqual(parseArguments(['--stage-only', '--output', '/new/output']),
    { stageOnly: true, cliOnly: false, output: '/new/output' });
  assert.equal(parseArguments(['--target', 'aarch64-apple-darwin', '--bundles', 'dmg']).bundles, 'dmg');
  assert.equal(parseArguments(['--cli-only']).cliOnly, true);
  assert.throws(() => parseArguments(['--firmware', 'old.zip']), /Unknown argument/);
  assert.throws(() => parseArguments(['--target', 'arbitrary']), /Unsupported target/);
  assert.throws(() => parseArguments(['--bundles', 'appimage']), /Unsupported bundle/);
  assert.throws(() => parseArguments(['--bundles', 'nsis']), /Unsupported bundle/);
  assert.throws(() => parseArguments(['--output']), /Missing value/);
  assert.throws(() => parseArguments(['--cli-only', '--bundles', 'dmg']), /cannot create GUI/);
});

test('staged inputs reject added private files and modified assets', async t => {
  const root = await temporary(t), bytes = Buffer.from('public installer asset');
  await writeFile(path.join(root, 'asset.js'), bytes);
  const inputs = [{ path: 'asset.js', size: bytes.length, sha256: digest(bytes) }];
  await verifyStagedAssets(root, inputs);
  await writeFile(path.join(root, 'asset.js'), Buffer.from('changed installer asset'));
  await assert.rejects(verifyStagedAssets(root, inputs), /mismatch/);
  await writeFile(path.join(root, 'asset.js'), bytes);
  await writeFile(path.join(root, 'private-backup.bin'), 'not a release input');
  await assert.rejects(verifyStagedAssets(root, inputs), /Unexpected staged asset list/);
});

test('portable CLI ZIP has interoperable names, CRC and central directory', () => {
  const bytes = zipFiles([{ name: 'installer.exe', bytes: Buffer.from('123456789') }]);
  assert.equal(bytes.readUInt32LE(0), 0x04034b50);
  assert.equal(bytes.readUInt32LE(14), 0xcbf43926);
  assert.equal(bytes.readUInt16LE(8), 8);
  assert.equal(bytes.readUInt32LE(22), 9);
  assert.equal(bytes.subarray(30, 43).toString(), 'installer.exe');
  const end = bytes.length - 22, centralOffset = bytes.readUInt32LE(end + 16);
  assert.equal(bytes.readUInt32LE(end), 0x06054b50);
  assert.equal(bytes.readUInt16LE(end + 10), 1);
  assert.equal(bytes.readUInt32LE(centralOffset), 0x02014b50);
  assert.equal(inflateRawSync(bytes.subarray(43, 43 + bytes.readUInt32LE(18))).toString(), '123456789');
  assert.throws(() => zipFiles([{ name: '../escape.exe', bytes: Buffer.alloc(0) }]), /Invalid resource path/);
});

test('Linux CLI archive includes restricted USB rule and usable setup instructions', async t => {
  const directory = await temporary(t), executable = path.join(directory, 'awtrix-tc002-installer-cli');
  await writeFile(executable, 'fixture binary');
  const archive = path.join(directory, 'terminal.tar.gz');
  packageUnixCli(archive, executable, 'x86_64-unknown-linux-gnu');
  const list = spawnSync(tar, ['-tzf', archive], { encoding: 'utf8' });
  assert.equal(list.status, 0, list.stderr);
  assert.deepEqual(list.stdout.trim().split(/\r?\n/).sort(),
    ['70-awtrix-tc002.rules', 'README-LINUX.md', 'README.md', 'awtrix-tc002-installer-cli'].sort());
  const extracted = spawnSync(tar, ['-xOf', archive, '70-awtrix-tc002.rules'], { encoding: 'utf8' });
  assert.equal(extracted.status, 0, extracted.stderr);
  assert.equal(extracted.stdout.trim(), 'SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="18d1", ATTR{idProduct}=="d002", MODE="0660", GROUP="plugdev", TAG+="uaccess"');
  const instructions = spawnSync(tar, ['-xOf', archive, 'README-LINUX.md'], { encoding: 'utf8' });
  assert.equal(instructions.status, 0, instructions.stderr);
  assert.match(instructions.stdout, /udevadm control --reload-rules/);
  assert.match(instructions.stdout, /groupadd --system --force plugdev/);
  const config = JSON.parse(await readFile(new URL('../native/tauri.conf.json', import.meta.url)));
  for (const format of ['deb', 'rpm']) {
    assert.equal(config.bundle.linux[format].files['/usr/lib/udev/rules.d/70-awtrix-tc002.rules'], 'resources/linux/70-awtrix-tc002.rules');
    assert.equal(config.bundle.linux[format].postInstallScript, 'resources/linux/udev-reload.sh');
  }
});

test('payload hashes reject invalid values and modified bytes', () => {
  const bytes = Buffer.from('immutable payload');
  verifyDigest(bytes, digest(bytes), 'payload');
  assert.throws(() => verifyDigest(bytes, 'not a digest', 'payload'), /invalid SHA-256/);
  assert.throws(() => verifyDigest(Buffer.from('modified'), digest(bytes), 'payload'), /SHA-256 mismatch/);
});

test('a build failure retains a nonzero exit code when a dependency changes it at exit', async t => {
  const root = await temporary(t);
  const hook = path.join(root, 'exit-hook.cjs');
  await writeFile(hook, "process.on('exit', () => { process.exitCode = 0; });\n");
  const result = spawnSync(process.execPath, ['--require', hook,
    fileURLToPath(new URL('../build.mjs', import.meta.url)), '--invalid-option'], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Unknown argument: --invalid-option/);
});

test('WASM preparation validates every executable and source archive', async t => {
  const { root, metadata } = await imageFixture(t);
  assert.deepEqual(await verifyImageBuild(root), metadata);
  await writeFile(path.join(root, 'tar2sqfs.wasm'), 'modified executable');
  await assert.rejects(verifyImageBuild(root), /tar2sqfs.wasm: SHA-256 mismatch/);
});

test('WASM preparation rejects altered corresponding sources', async t => {
  const { root } = await imageFixture(t);
  await writeFile(path.join(root, 'sources', 'xz-5.8.4.tar.xz'), 'modified source');
  await assert.rejects(verifyImageBuild(root), /xz-5.8.4.tar.xz: SHA-256 mismatch/);
});

test('WASM preparation rejects unexpected executable and source names', async t => {
  const { root, metadata } = await imageFixture(t);
  metadata.artifacts['private-backup.bin'] = 'a'.repeat(64);
  await writeFile(path.join(root, 'build.json'), JSON.stringify(metadata));
  await assert.rejects(verifyImageBuild(root), /Unexpected WASM artifact list/);
  delete metadata.artifacts['private-backup.bin'];
  metadata.sources['other.tar.gz'] = ['https://example.com/other.tar.gz', 'a'.repeat(64)];
  await writeFile(path.join(root, 'build.json'), JSON.stringify(metadata));
  await assert.rejects(verifyImageBuild(root), /Unexpected WASM source list/);
});
