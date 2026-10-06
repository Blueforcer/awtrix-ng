(() => {
  const FONT = {
    '0': ['111', '101', '101', '101', '111'], '1': ['010', '110', '010', '010', '111'],
    '2': ['111', '001', '111', '100', '111'], '3': ['111', '001', '111', '001', '111'],
    '4': ['101', '101', '111', '001', '001'], '5': ['111', '100', '111', '001', '111'],
    '6': ['111', '100', '111', '101', '111'], '7': ['111', '001', '001', '001', '001'],
    '8': ['111', '101', '111', '101', '111'], '9': ['111', '101', '111', '001', '111'],
    A: ['010', '101', '111', '101', '101'], B: ['110', '101', '110', '101', '110'],
    C: ['011', '100', '100', '100', '011'], D: ['110', '101', '101', '101', '110'],
    E: ['111', '100', '110', '100', '111'], F: ['111', '100', '110', '100', '100'],
    G: ['011', '100', '101', '101', '011'], H: ['101', '101', '111', '101', '101'],
    I: ['111', '010', '010', '010', '111'], J: ['001', '001', '001', '101', '010'],
    K: ['101', '101', '110', '101', '101'], L: ['100', '100', '100', '100', '111'],
    M: ['101', '111', '111', '101', '101'], N: ['110', '101', '101', '101', '101'],
    O: ['010', '101', '101', '101', '010'], P: ['110', '101', '110', '100', '100'],
    Q: ['010', '101', '101', '110', '011'], R: ['110', '101', '110', '101', '101'],
    S: ['011', '100', '010', '001', '110'], T: ['111', '010', '010', '010', '010'],
    U: ['101', '101', '101', '101', '111'], V: ['101', '101', '101', '101', '010'],
    W: ['101', '101', '111', '111', '101'], X: ['101', '101', '010', '101', '101'],
    Y: ['101', '101', '010', '010', '010'], Z: ['111', '001', '010', '100', '111'],
    ' ': ['00', '00', '00', '00', '00'], ':': ['0', '1', '0', '1', '0'],
    '.': ['0', '0', '0', '0', '1'], ',': ['00', '00', '00', '01', '10'],
    '!': ['1', '1', '1', '0', '1'], '?': ['111', '001', '010', '000', '010'],
    '-': ['000', '000', '111', '000', '000'], '+': ['000', '010', '111', '010', '000'],
    '/': ['001', '001', '010', '100', '100'], '%': ['101', '001', '010', '100', '101'],
    "'": ['1', '1', '0', '0', '0'], '°': ['11', '11', '00', '00', '00'],
    '#': ['101', '111', '101', '111', '101'], '&': ['010', '101', '010', '101', '011'],
    '(': ['01', '10', '10', '10', '01'], ')': ['10', '01', '01', '01', '10'],
    '<': ['001', '010', '100', '010', '001'], '>': ['100', '010', '001', '010', '100'],
    '=': ['000', '111', '000', '111', '000'], '_': ['000', '000', '000', '000', '111'],
    '*': ['000', '101', '010', '101', '000'], '@': ['111', '101', '111', '100', '011'],
    '$': ['011', '110', '010', '011', '110'], ';': ['0', '1', '0', '1', '1'],
  };
  const map = { 'Ä': 'A', 'Ö': 'O', 'Ü': 'U', 'ß': 'S', 'É': 'E', 'È': 'E', 'À': 'A' };
  const glyph = ch => FONT[ch] || FONT[map[ch]] || FONT['?'];
  const upper = s => [...s.toUpperCase()];
  const widthOf = s => upper(s).reduce((w, ch) => w + glyph(ch)[0].length + 1, 0) - 1;

  const W = 32, H = 8;
  const SUN = ['00100100', '00011000', '10111101', '01111110', '01111110', '10111101', '00011000', '00100100'];
  const DAYS = ['#FF2D55', '#FF9500', '#FFD60A', '#34C759', '#32ADE6', '#5856D6', '#AF52DE'];

  function start(root) {
    if (root.dataset.ready) return;
    root.dataset.ready = '1';
    const canvas = root.querySelector('canvas');
    const ctx = canvas.getContext('2d');
    const form = root.querySelector('form');
    const input = root.querySelector('input[type=text]');
    const code = root.querySelector('[data-role=curl]');
    const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
    let fb = [];
    let app = 0, appStart = performance.now(), note = null;

    const clear = () => { fb = Array.from({ length: H }, () => Array(W).fill(null)); };
    const set = (x, y, c) => { if (x >= 0 && x < W && y >= 0 && y < H) fb[y][x] = c; };
    const text = (x, y, s, c) => {
      for (const ch of upper(s)) {
        const g = glyph(ch);
        g.forEach((row, ry) => [...row].forEach((b, rx) => { if (b === '1') set(x + rx, y + ry, c); }));
        x += g[0].length + 1;
      }
    };

    const color = () => (form.querySelector('input[name=color]:checked') || {}).value || '#FFFFFF';
    const payload = () => JSON.stringify({ text: input.value || 'Hello', textColor: color() });
    const updateCurl = () => {
      code.textContent = 'curl -X POST http://<awtrix-ip>/api/v1/notifications \\\n' +
        '  -H "Content-Type: application/json" \\\n' +
        `  -d '${payload().replace(/'/g, "'\\''")}'`;
    };

    function drawClock(now) {
      const d = new Date();
      const hh = String(d.getHours()).padStart(2, '0');
      const mm = String(d.getMinutes()).padStart(2, '0');
      const x = Math.floor((W - widthOf(hh + ':' + mm)) / 2);
      text(x, 1, hh, '#FFFFFF');
      if (still || Math.floor(now / 1000) % 2 === 0) text(x + widthOf(hh) + 1, 1, ':', '#FFFFFF');
      text(x + widthOf(hh + ':') + 1, 1, mm, '#FFFFFF');
      const today = (d.getDay() + 6) % 7;
      for (let i = 0; i < 7; i++)
        for (let k = 0; k < 3; k++) set(2 + i * 4 + k, 7, i === today ? DAYS[i] : '#3a3a3a');
    }

    function drawWeather() {
      SUN.forEach((row, y) => [...row].forEach((b, x) => { if (b === '1') set(x, y, '#FFB000'); }));
      const s = '21°C';
      text(9 + Math.floor((W - 9 - widthOf(s)) / 2), 1, s, '#FF8A1F');
    }

    function drawNote(now) {
      const w = widthOf(note.text);
      if (still || w <= W) { text(Math.max(0, Math.floor((W - w) / 2)), 1, note.text, note.color); return w <= W || still ? now - note.t0 > 4000 : false; }
      const x = W - Math.floor((now - note.t0) / 45);
      text(x, 1, note.text, note.color);
      return x < -w;
    }

    function paint() {
      const cw = canvas.clientWidth;
      const dpr = window.devicePixelRatio || 1;
      if (canvas.width !== Math.round(cw * dpr)) {
        canvas.width = Math.round(cw * dpr);
        canvas.height = Math.round(cw * dpr * H / W);
      }
      const p = canvas.width / W, r = p * 0.36;
      ctx.fillStyle = '#050505';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      for (let y = 0; y < H; y++)
        for (let x = 0; x < W; x++) {
          const c = fb[y][x];
          ctx.beginPath();
          ctx.arc(x * p + p / 2, y * p + p / 2, r, 0, Math.PI * 2);
          if (c) { ctx.shadowColor = c; ctx.shadowBlur = p * 0.9; ctx.fillStyle = c; }
          else { ctx.shadowBlur = 0; ctx.fillStyle = '#161616'; }
          ctx.fill();
        }
      ctx.shadowBlur = 0;
    }

    function frame(now) {
      if (!root.isConnected) return;
      clear();
      if (note) { if (drawNote(now)) { note = null; appStart = now; } }
      else {
        if (now - appStart > 6000) { app = (app + 1) % 2; appStart = now; }
        (app === 0 ? drawClock : drawWeather)(now);
      }
      paint();
      if (!still || note) requestAnimationFrame(frame);
    }

    form.addEventListener('submit', e => {
      e.preventDefault();
      note = { text: input.value.trim() || 'Hello', color: color(), t0: performance.now() };
      requestAnimationFrame(frame);
    });
    form.addEventListener('input', updateCurl);
    updateCurl();
    requestAnimationFrame(frame);
    if (still) setInterval(() => requestAnimationFrame(frame), 30000);
  }

  const boot = () => document.querySelectorAll('.awtrix-live').forEach(start);
  if (window.document$) window.document$.subscribe(boot);
  else document.addEventListener('DOMContentLoaded', boot);
})();
