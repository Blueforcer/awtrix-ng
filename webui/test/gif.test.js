/* Checks the web UI's GIF writer (gifEncode in webui/index.html) by decoding what
   it produced with an independent reader written here. A live browser is the real
   authority on a GIF, and this file exists so a broken frame is caught without one.

   Run: node gif.test.js   (from webui/test) */
const { boot } = require('./harness');

let failed = 0;
function check(name, cond, detail) {
  if (cond) { console.log('  ok  ' + name); return; }
  failed++;
  console.log('  FAIL ' + name + (detail ? ' - ' + detail : ''));
}

// ---- minimal GIF89a reader ------------------------------------------------
function lzwDecode(bytes, minCode, want) {
  const clear = 1 << minCode, eoi = clear + 1;
  let dict, size, prev, cur = 0, bits = 0, pos = 0;
  const out = [];
  const reset = () => {
    dict = [];
    for (let i = 0; i < clear; i++) dict.push([i]);
    dict.push(null, null); // clear + end-of-information hold their code numbers
    size = minCode + 1;
    prev = null;
  };
  reset();
  while (out.length < want) {
    while (bits < size && pos < bytes.length) { cur |= bytes[pos++] << bits; bits += 8; }
    if (bits < size) break;
    const code = cur & ((1 << size) - 1);
    cur >>>= size; bits -= size;
    if (code === clear) { reset(); continue; }
    if (code === eoi) break;
    const entry = code < dict.length && dict[code] ? dict[code] : prev.concat(prev[0]);
    out.push(...entry);
    if (prev && dict.length < 4096) {
      dict.push(prev.concat(entry[0]));
      if (dict.length >= (1 << size) && size < 12) size++;
    }
    prev = entry;
  }
  return out;
}

function gifDecode(b) {
  let p = 0;
  const u8 = () => b[p++];
  const u16 = () => { const v = b[p] | (b[p + 1] << 8); p += 2; return v; };
  const magic = String.fromCharCode(...b.slice(0, 6)); p = 6;
  const width = u16(), height = u16(), packed = u8();
  p += 2; // background colour index + pixel aspect ratio
  if (packed & 0x80) p += 3 * (2 << (packed & 7)); // global colour table (unused here)
  const frames = [];
  let delay = 0, loops = null;
  for (;;) {
    const tag = u8();
    if (tag === 0x3B || tag === undefined) break;
    if (tag === 0x21) { // extension
      const label = u8();
      const blocks = [];
      for (let n = u8(); n; n = u8()) { blocks.push(...b.slice(p, p + n)); p += n; }
      if (label === 0xF9) delay = blocks[1] | (blocks[2] << 8);
      if (label === 0xFF && String.fromCharCode(...blocks.slice(0, 11)) === 'NETSCAPE2.0')
        loops = blocks[12] | (blocks[13] << 8);
      continue;
    }
    if (tag !== 0x2C) throw new Error('unexpected block 0x' + tag.toString(16) + ' at ' + (p - 1));
    const x = u16(), y = u16(), w = u16(), h = u16(), ip = u8();
    if (!(ip & 0x80)) throw new Error('frame without a local colour table');
    const palette = [];
    for (let i = 0, n = 2 << (ip & 7); i < n; i++) palette.push((b[p++] << 16) | (b[p++] << 8) | b[p++]);
    const minCode = u8();
    const data = [];
    for (let n = u8(); n; n = u8()) { data.push(...b.slice(p, p + n)); p += n; }
    frames.push({ x, y, w, h, delay, palette, px: lzwDecode(data, minCode, w * h).map(i => palette[i]) });
  }
  return { magic, width, height, loops, frames };
}

// ---- fixtures -------------------------------------------------------------
// distinct: how many different colours the frame uses. 255 is the most a frame
// can hold exactly - the 256th palette slot belongs to the dark grid line.
const pattern = (W, H, off, span, distinct = 200) => {
  const px = [];
  for (let i = 0; i < W * H; i++) px.push(((i % distinct) * span + off) & 0xFFFFFF);
  return px;
};

(async () => {
  const { window } = await boot();
  const { gifEncode } = window;
  if (typeof gifEncode !== 'function') { console.log('FAIL gifEncode is not exported'); process.exit(1); }

  // The gap is a tenth of the cell, the same rule the preview and the PNG use.
  const gap = SC => Math.max(1, Math.round(SC / 10));

  console.log('gif: panel-sized frames round-trip');
  {
    const W = 32, H = 8, SC = 20; // the scale the web UI actually exports at
    const src = [
      { t: 1000, px: pattern(W, H, 0, 0x010307) },
      { t: 1130, px: pattern(W, H, 77, 0x030905) },
      { t: 1260, px: pattern(W, H, 200, 0x070101) },
    ];
    const gif = gifDecode(gifEncode(src, W, H, SC));
    check('GIF89a header', gif.magic === 'GIF89a', gif.magic);
    check('scaled canvas size', gif.width === W * SC && gif.height === H * SC, gif.width + 'x' + gif.height);
    check('loops forever', gif.loops === 0, String(gif.loops));
    check('frame count', gif.frames.length === 3, String(gif.frames.length));
    check('delays follow capture gaps', gif.frames.map(f => f.delay).join(',') === '13,13,10',
      gif.frames.map(f => f.delay).join(','));

    let bad = 0, first = null;
    gif.frames.forEach((f, i) => {
      for (let yy = 0; yy < f.h; yy++) for (let xx = 0; xx < f.w; xx++) {
        // Each LED is an SC×SC cell: colour, with a dark gap right and below.
        const lit = SC - gap(SC);
        const edge = xx % SC >= lit || yy % SC >= lit;
        const want = edge ? 0 : src[i].px[((yy / SC) | 0) * W + ((xx / SC) | 0)];
        const got = f.px[yy * f.w + xx];
        if (got !== want) { bad++; if (!first) first = { i, xx, yy, want, got }; }
      }
    });
    check('every pixel decodes back, LED grid intact', bad === 0,
      bad + ' wrong, first ' + JSON.stringify(first));
  }

  console.log('gif: icon-sized frame at SC=1 (what a converted PNG/JPG becomes)');
  {
    const W = 8, H = 8, SC = 1;
    const R = 0xf50017; // icon 4103: one saturated red on black, hard edges
    const px = [];
    for (let i = 0; i < W * H; i++) px.push([11, 12, 18, 19, 20, 21, 25, 26, 27, 28, 29, 30].includes(i) ? R : 0);
    const gif = gifDecode(gifEncode([{ t: 0, px }], W, H, SC));
    const f = gif.frames[0];
    check('no LED grid at SC=1', f.w === W && f.h === H, f.w + 'x' + f.h);
    check('palette holds only the colours used', f.palette.length <= 2, String(f.palette.length));
    let bad = 0;
    for (let i = 0; i < W * H; i++) if (f.px[i] !== px[i]) bad++;
    check('every pixel survives exactly', bad === 0, bad + ' wrong');
    check('smaller than the JPEG it replaces', gifEncode([{ t: 0, px }], W, H, SC).length < 200,
      String(gifEncode([{ t: 0, px }], W, H, SC).length));
  }

  console.log('gif: a 32x8 icon at SC=1 keeps every colour');
  {
    const W = 32, H = 8, SC = 1;
    const px = [];
    for (let i = 0; i < W * H; i++) px.push((i * 0x010203) & 0xFFFFFF); // 256 distinct
    const gif = gifDecode(gifEncode([{ t: 0, px }], W, H, SC));
    const f = gif.frames[0];
    let bad = 0;
    for (let i = 0; i < W * H; i++) if (f.px[i] !== px[i]) bad++;
    check('lossless up to a full 256-colour panel', bad === 0, bad + ' wrong');
  }

  console.log('gif: a panel with more colours than GIF can hold');
  {
    const W = 128, H = 8, SC = 2;                    // 1024 pixels, all distinct
    const src = [{ t: 0, px: pattern(W, H, 0, 257) }];
    const gif = gifDecode(gifEncode(src, W, H, SC));
    const f = gif.frames[0];
    check('one frame', gif.frames.length === 1, String(gif.frames.length));
    check('palette fits GIF', f.palette.length <= 256, String(f.palette.length));
    let drift = 0;
    for (let yy = 0; yy < f.h; yy++) for (let xx = 0; xx < f.w; xx++) {
      const lit = SC - gap(SC);
      if (xx % SC >= lit || yy % SC >= lit) continue; // grid line, not a colour
      const want = src[0].px[((yy / SC) | 0) * W + ((xx / SC) | 0)];
      const got = f.px[yy * f.w + xx];
      for (const sh of [16, 8, 0]) drift = Math.max(drift, Math.abs(((want >> sh) & 255) - ((got >> sh) & 255)));
    }
    check('colours only lose low bits', drift <= 7, 'max channel drift ' + drift);
  }

  console.log(failed ? '\n' + failed + ' check(s) failed' : '\nall checks passed');
  process.exit(failed ? 1 : 0);
})();
