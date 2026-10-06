import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { digest } from '../build.mjs';
import { manifestName, platforms, publish, stableName } from '../publish.mjs';

const outputs = {
  'x86_64-pc-windows-msvc': ['AWTRIX-NG-TC002-Installer-0.1.0-x86_64-pc-windows-msvc.exe', 'AWTRIX-NG-TC002-CLI-0.1.0-x86_64-pc-windows-msvc.zip'],
  'x86_64-apple-darwin': ['AWTRIX NG TC002 Installer_0.1.0_x64.dmg', 'AWTRIX-NG-TC002-CLI-0.1.0-x86_64-apple-darwin.tar.gz'],
  'aarch64-apple-darwin': ['AWTRIX NG TC002 Installer_0.1.0_aarch64.dmg', 'AWTRIX-NG-TC002-CLI-0.1.0-aarch64-apple-darwin.tar.gz'],
  'x86_64-unknown-linux-gnu': ['AWTRIX NG TC002 Installer_0.1.0_amd64.deb', 'AWTRIX NG TC002 Installer-0.1.0-1.x86_64.rpm', 'AWTRIX-NG-TC002-CLI-0.1.0-x86_64-unknown-linux-gnu.tar.gz'],
  'aarch64-unknown-linux-gnu': ['AWTRIX NG TC002 Installer_0.1.0_arm64.deb', 'AWTRIX NG TC002 Installer-0.1.0-1.aarch64.rpm', 'AWTRIX-NG-TC002-CLI-0.1.0-aarch64-unknown-linux-gnu.tar.gz'],
};

async function temporary(t) {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'awtrix-desktop-publish-'));
  t.after(() => rm(directory, { recursive: true }));
  return directory;
}

async function runArtifacts(root, { dirty = false, commit = 'a'.repeat(40), skip } = {}) {
  for (const [target, names] of Object.entries(outputs)) {
    if (target === skip) continue;
    const directory = path.join(root, `tc002-installer-${target}`);
    await mkdir(directory);
    const artifacts = [];
    for (const name of [...names, `AWTRIX-NG-TC002-Installer-0.1.0-${target}-sources.tar.gz`]) {
      const bytes = Buffer.from(`${target} ${name}`);
      await writeFile(path.join(directory, name), bytes);
      artifacts.push({ path: name, size: bytes.length, sha256: digest(bytes) });
    }
    await writeFile(path.join(directory, 'artifact-manifest.json'), JSON.stringify({ schemaVersion: 1, target, artifacts }));
    await writeFile(path.join(directory, 'build-manifest.json'), JSON.stringify({ installer: { version: '0.1.0', commit, dirty } }));
  }
}

test('installer artifacts get version-free names per platform', () => {
  assert.equal(stableName('AWTRIX-NG-TC002-Installer-0.1.0-x86_64-pc-windows-msvc.exe', 'x86_64-pc-windows-msvc'), 'AWTRIX-NG-TC002-Installer-windows-x64.exe');
  assert.equal(stableName('AWTRIX-NG-TC002-Installer-0.1.0-x86_64-pc-windows-msvc-sources.tar.gz', 'x86_64-pc-windows-msvc'), 'AWTRIX-NG-TC002-Installer-windows-x64-sources.tar.gz');
  assert.equal(stableName('AWTRIX-NG-TC002-CLI-0.1.0-aarch64-apple-darwin.tar.gz', 'aarch64-apple-darwin'), 'AWTRIX-NG-TC002-CLI-macos-arm64.tar.gz');
  assert.equal(stableName('AWTRIX NG TC002 Installer-0.1.0-1.aarch64.rpm', 'aarch64-unknown-linux-gnu'), 'AWTRIX-NG-TC002-Installer-linux-arm64.rpm');
  assert.throws(() => stableName('notes.txt', 'x86_64-apple-darwin'), /Unexpected installer artifact/);
  assert.throws(() => stableName('a.exe', 'i686-pc-windows-msvc'), /Unknown target/);
});

test('publishing replaces the folder with every platform and a manifest', async t => {
  const root = await temporary(t);
  const source = path.join(root, 'run');
  const output = path.join(root, 'installer');
  await mkdir(source);
  await runArtifacts(source);
  await mkdir(output);
  await writeFile(path.join(output, 'AWTRIX-NG-TC002-Installer-old.exe'), 'old');
  const manifest = await publish(source, output);
  assert.equal(manifest.version, '0.1.0');
  assert.equal(manifest.files.length, 17);
  assert.deepEqual(new Set(manifest.files.map(file => file.target)), new Set(Object.keys(platforms)));
  const names = await readdir(output);
  assert.deepEqual(names.sort(), [...manifest.files.map(file => file.name), manifestName].sort());
  for (const file of manifest.files) assert.equal(digest(await readFile(path.join(output, file.name))), file.sha256);
});

test('publishing refuses incomplete, dirty, mixed or altered builds', async t => {
  const cases = [
    [{ skip: 'aarch64-apple-darwin' }, /Missing installer for aarch64-apple-darwin/],
    [{ dirty: true }, /dirty checkout/],
  ];
  for (const [options, error] of cases) {
    const source = await temporary(t);
    await runArtifacts(source, options);
    await assert.rejects(publish(source, path.join(source, 'out')), error);
  }
  const mixed = await temporary(t);
  await runArtifacts(mixed);
  await writeFile(path.join(mixed, 'tc002-installer-x86_64-apple-darwin', 'build-manifest.json'),
    JSON.stringify({ installer: { version: '0.1.0', commit: 'b'.repeat(40), dirty: false } }));
  await assert.rejects(publish(mixed, path.join(mixed, 'out')), /another version or commit/);
  const escaping = await temporary(t);
  await runArtifacts(escaping);
  const manifestPath = path.join(escaping, 'tc002-installer-x86_64-apple-darwin', 'artifact-manifest.json');
  const manifest = JSON.parse(await readFile(manifestPath, 'utf8'));
  manifest.artifacts[0].path = '../secret.dmg';
  await writeFile(manifestPath, JSON.stringify(manifest));
  await assert.rejects(publish(escaping, path.join(escaping, 'out')), /Invalid artifact path/);
});

test('a failed publish keeps the previous folder untouched', async t => {
  const root = await temporary(t);
  const source = path.join(root, 'run');
  const output = path.join(root, 'installer');
  await mkdir(source);
  await runArtifacts(source);
  await mkdir(output);
  await writeFile(path.join(output, manifestName), 'previous');
  await writeFile(path.join(source, 'tc002-installer-x86_64-unknown-linux-gnu', 'AWTRIX NG TC002 Installer_0.1.0_amd64.deb'), 'changed');
  await assert.rejects(publish(source, output), /SHA-256 mismatch/);
  assert.deepEqual(await readdir(output), [manifestName]);
  assert.equal(await readFile(path.join(output, manifestName), 'utf8'), 'previous');
  assert.deepEqual((await readdir(root)).sort(), ['installer', 'run']);
});
