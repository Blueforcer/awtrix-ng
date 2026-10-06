import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

export async function loadImageTools(base = new URL('./', import.meta.url)) {
  const metadata = JSON.parse(await readFile(new URL('build.json', base), 'utf8'));
  return Promise.all(['sqfs2tar', 'tar2sqfs'].map(async name => {
    const filenames = [`${name}.js`, `${name}.wasm`];
    const files = await Promise.all(filenames.map(file => readFile(new URL(file, base))));
    files.forEach((bytes, index) => {
      if (createHash('sha256').update(bytes).digest('hex') !== metadata.artifacts[filenames[index]])
        throw new Error(`Image tool failed verification: ${filenames[index]}`);
    });
    const module = await import(new URL(filenames[0], base));
    return {factory: module.default, bytes: new Uint8Array(files[1])};
  }));
}
