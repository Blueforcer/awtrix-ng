import { buildImage, preloadTools } from './core.js';

self.onmessage = async ({ data }) => {
  try {
    if (data.type === 'preload') {
      await preloadTools();
      self.postMessage({ type: 'ready' });
      return;
    }
    if (data.type !== 'build') throw new Error('Unknown image worker request');
    const result = await buildImage(data.input, stage => self.postMessage({ type: 'progress', stage }));
    self.postMessage({ type: 'result', ...result }, [result.image.buffer]);
  } catch (error) {
    self.postMessage({ type: 'error', message: error instanceof Error ? error.message : String(error) });
  }
};
