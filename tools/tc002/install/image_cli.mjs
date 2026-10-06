import {readFile, writeFile} from 'node:fs/promises';
import {buildImage} from '../../../docs/assets/tc002/image/core.js';
import {loadImageTools} from '../../../docs/assets/tc002/image/node.mjs';
import {RES_PARTITION_BYTES, RES_ERASE_BYTES} from '../../../docs/assets/tc002/constants.js';

async function main() {
  let request = '';
  for await (const chunk of process.stdin) {
    request += chunk;
    if (request.length > 65536) throw new Error('Image request is too large');
  }
  const options = JSON.parse(request);
  if (options.partitionSize !== RES_PARTITION_BYTES || options.eraseSize !== RES_ERASE_BYTES)
    throw new Error('Unsupported res partition geometry');
  const [stockRes, loader, loop] = await Promise.all([options.stock, options.loader, options.loop].map(file => readFile(file)));
  if (stockRes.length !== RES_PARTITION_BYTES) throw new Error('Stock image does not cover the res partition');
  const result = await buildImage({stockRes, loader, loop}, undefined, await loadImageTools());
  if (result.stockSha256 !== options.sha256) throw new Error('Stock image SHA-256 does not match');
  if (!Number.isSafeInteger(options.releaseBytes) || options.releaseBytes <= 0 || result.slot.capacity < options.releaseBytes)
    throw new Error(`Release image needs ${options.releaseBytes} bytes; res leaves a release slot of ${result.slot.capacity} bytes`);
  await writeFile(options.output, result.image, {mode: 0o600, flag: 'wx'});
  const {image, ...metadata} = result;
  process.stdout.write(JSON.stringify({...metadata, imageBytes: image.length}) + '\n');
}

main().catch(error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
