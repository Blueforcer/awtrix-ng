let cachedTools;

export async function preload() {
  if (!cachedTools) cachedTools = import('./core.js').then(module => module.preloadTools()).catch(error => { cachedTools = undefined; throw error; });
  await cachedTools;
}

export function buildResImage({ stockRes, loader, loop, onProgress } = {}) {
  return new Promise((resolve, reject) => {
    const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
    const finish = (error, result) => {
      clearTimeout(timer);
      worker.terminate();
      if (error) reject(error); else resolve(result);
    };
    const timer = setTimeout(() => finish(new Error('Image creation timed out')), 10 * 60 * 1000);
    worker.onerror = event => finish(new Error(event.message || 'Image worker failed'));
    worker.onmessage = ({ data }) => {
      if (data.type === 'progress') {
        try { onProgress?.(data.stage); } catch (error) { finish(error); }
      } else if (data.type === 'error') finish(new Error(data.message));
      else if (data.type === 'result') finish(null, { image: data.image, stockSha256: data.stockSha256, imageSha256: data.imageSha256, slot: data.slot });
    };
    try { worker.postMessage({ type: 'build', input: { stockRes, loader, loop } }); }
    catch (error) { finish(error); }
  });
}
