import assert from 'node:assert/strict';
import { copyFile, mkdir, readFile, readdir, rename, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { verifyDigest } from './build.mjs';

export const manifestName = 'AWTRIX-NG-TC002-Installer-manifest.json';

export const platforms = {
  'x86_64-pc-windows-msvc': 'windows-x64',
  'x86_64-apple-darwin': 'macos-x64',
  'aarch64-apple-darwin': 'macos-arm64',
  'x86_64-unknown-linux-gnu': 'linux-x64',
  'aarch64-unknown-linux-gnu': 'linux-arm64',
};

export function stableName(name, target) {
  const platform = platforms[target];
  assert(platform, `Unknown target: ${target}`);
  if (/^AWTRIX-NG-TC002-Installer-.+-sources\.tar\.gz$/.test(name)) return `AWTRIX-NG-TC002-Installer-${platform}-sources.tar.gz`;
  const cli = /^AWTRIX-NG-TC002-CLI-.+\.(zip|tar\.gz)$/.exec(name);
  if (cli) return `AWTRIX-NG-TC002-CLI-${platform}.${cli[1]}`;
  const gui = /\.(exe|dmg|deb|rpm)$/.exec(name);
  if (gui) return `AWTRIX-NG-TC002-Installer-${platform}.${gui[1]}`;
  throw new Error(`Unexpected installer artifact: ${name}`);
}

export async function collect(source) {
  const manifests = (await readdir(source, { recursive: true }))
    .filter(name => path.basename(name) === 'artifact-manifest.json')
    .map(name => path.join(source, name));
  const files = [];
  let build;
  for (const filename of manifests) {
    const directory = path.dirname(filename);
    const artifacts = JSON.parse(await readFile(filename, 'utf8'));
    const { installer } = JSON.parse(await readFile(path.join(directory, 'build-manifest.json'), 'utf8'));
    assert.equal(installer.dirty, false, `${artifacts.target}: built from a dirty checkout`);
    build ??= installer;
    assert.equal(`${installer.version} ${installer.commit}`, `${build.version} ${build.commit}`,
      `${artifacts.target}: built from another version or commit`);
    for (const { path: name, size, sha256 } of artifacts.artifacts) {
      assert(typeof name === 'string' && !/[\/]/.test(name) && name !== '.' && name !== '..', `Invalid artifact path: ${name}`);
      files.push({ name: stableName(name, artifacts.target), target: artifacts.target, from: path.join(directory, name), size, sha256 });
    }
  }
  const targets = new Set(files.map(file => file.target));
  for (const target of Object.keys(platforms)) assert(targets.has(target), `Missing installer for ${target}`);
  const names = files.map(file => file.name);
  assert.equal(new Set(names).size, names.length, 'Two artifacts map to the same file name');
  return { version: build.version, commit: build.commit, files: files.sort((a, b) => a.name.localeCompare(b.name)) };
}

export async function publish(source, output) {
  const release = await collect(source);
  const staging = `${output}.staging`;
  await rm(staging, { recursive: true, force: true });
  await mkdir(staging, { recursive: true });
  try {
    for (const file of release.files) {
      const destination = path.join(staging, file.name);
      await copyFile(file.from, destination);
      const bytes = await readFile(destination);
      verifyDigest(bytes, file.sha256, path.basename(file.from));
      assert.equal(bytes.length, file.size, `${path.basename(file.from)}: size mismatch`);
    }
    const manifest = {
      version: release.version,
      commit: release.commit,
      files: release.files.map(({ name, target, size, sha256 }) => ({ name, target, size, sha256 })),
    };
    await writeFile(path.join(staging, manifestName), `${JSON.stringify(manifest, null, 2)}\n`);
    await rm(output, { recursive: true, force: true });
    await rename(staging, output);
    return manifest;
  } catch (error) {
    await rm(staging, { recursive: true, force: true });
    throw error;
  }
}

function parse(args) {
  if (args.length === 4 && args[0] === '--from' && args[2] === '--out') return { from: args[1], out: args[3] };
  throw new Error('Usage: node publish.mjs --from ARTIFACTS --out DIRECTORY');
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  Promise.resolve().then(() => {
    const options = parse(process.argv.slice(2));
    return publish(options.from, options.out).then(manifest => [manifest, options.out]);
  }).then(([manifest, out]) => {
    console.log(`Installer ${manifest.version} (${manifest.commit}): ${manifest.files.length} files in ${out}`);
  }).catch(error => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
