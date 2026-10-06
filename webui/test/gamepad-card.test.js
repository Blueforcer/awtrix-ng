/* Two-controller registry on System: device/player identities, targeted actions
   and polling lifecycle. Run: node gamepad-card.test.js */
const { boot, goto, flush, useFakeTimers } = require('./harness');
const restoreTimers = useFakeTimers();
const assert = require('node:assert/strict');
const audio = { rtttl: true, track: false, mp3: true, radio: true };
const PAD = { name: '8BitDo Ultimate', address: 'E4:17:D8:BC:EC:1D' };
const empty = id => ({ id, state: 'unpaired', name: '', address: '', player: null });
const registry = (...devices) => ({ devices: [empty(1), empty(2)].map(d => devices.find(p => p.id === d.id) || d) });

function gamepadMock(window, gp, options = {}) {
  const device = window.fetch, log = [];
  window.fetch = async (url, o = {}) => {
    const path = String(url), method = (o.method || 'GET').toUpperCase();
    if (!path.startsWith('/api/v1/gamepad')) return device(url, o);
    log.push(method + ' ' + path);
    if (path === '/api/v1/gamepad' && method === 'GET') return new Response(JSON.stringify(gp));
    if (path === '/api/v1/gamepad/pair' && method === 'POST') {
      if (options.wait) await options.wait;
      if (options.fail) return new Response('{"error":"unavailable"}', { status: 503 });
      const free = gp.devices.find(d => !d.address);
      if (!free) return new Response('{"error":"gamepadsFull"}', { status: 409 });
      free.state = 'pairing';
      return new Response(JSON.stringify({ ok: true, id: free.id }));
    }
    const remove = /^\/api\/v1\/gamepad\/([12])$/.exec(path);
    if (remove && method === 'DELETE') {
      const id = +remove[1];
      Object.assign(gp.devices.find(d => d.id === id), empty(id));
      return new Response('{"ok":true}');
    }
    return new Response('{}', { status: 405 });
  };
  return log;
}

async function open(gamepad, gp, options = {}) {
  const { window } = await boot({ caps: { transitions: [], audio, gamepad,
    ble: 'ble' in options ? options.ble : gamepad } });
  const log = gamepadMock(window, gp, options);
  await goto(window, '#/system'); await flush(80);
  const sec = window.document.querySelector('#sec-gamepad'), tr = window.eval('t');
  const badges = sec ? [...sec.querySelectorAll('[data-gamepad]')] : [];
  return { window, log, sec, tr, badges,
    pair: sec && [...sec.querySelectorAll('button')].find(b => b.textContent === tr('gpPairBtn')),
    forget: badges.map(b => b.parentElement.querySelector('button')) };
}

async function gating() {
  for (const gamepad of [undefined, false]) {
    const c = await open(gamepad, registry());
    try {
      assert.equal(c.sec, null);
      assert.equal(c.window.document.querySelector('.subnav a[data-sec="sec-gamepad"]'), null);
      assert.equal(c.log.length, 0);
      assert.ok(c.window.document.querySelector('#sec-hub'));
    } finally { c.window.close(); }
  }
  const c = await open(true, registry(), { ble: undefined });
  try {
    assert.equal(c.sec, null, 'Bluetooth capability gates the controller registry');
    assert.equal(c.log.length, 0);
  } finally { c.window.close(); }
}

async function states() {
  let c = await open(true, registry());
  try {
    assert.equal(c.badges.length, 2);
    assert.ok(c.badges.every(b => b.textContent === c.tr('gpNone')));
    assert.equal(c.pair.disabled, false);
    assert.ok(c.forget.every(b => b.style.display === 'none'));
    assert.ok(c.sec.textContent.includes(c.tr('gpPairH')));
    assert.ok(c.log.length && c.log.every(l => l === 'GET /api/v1/gamepad'));
  } finally { c.window.close(); }
  c = await open(true, registry({ ...empty(2), state: 'pairing' }));
  try {
    assert.equal(c.badges[1].textContent, c.tr('scanning'));
    assert.equal(c.pair.disabled, true, 'one pairing operation at a time');
    assert.ok(c.sec.textContent.includes(c.tr('gpPairH')));
  } finally { c.window.close(); }
  c = await open(true, registry({ id: 1, state: 'ready', ...PAD, player: 2 },
    { id: 2, state: 'ready', name: 'Second', address: 'E4:17:D8:BC:EC:2E', player: 1 }));
  try {
    assert.ok(c.badges[0].textContent.includes(PAD.name));
    assert.ok(c.badges[0].textContent.endsWith(c.tr('gpPlayer') + ' 2'));
    assert.ok(c.badges[1].textContent.endsWith(c.tr('gpPlayer') + ' 1'));
    assert.ok(c.badges.every(b => b.classList.contains('good')));
    assert.ok(c.forget.every(b => b.style.display === ''));
    assert.equal(c.pair.disabled, true, 'two stored devices fill the registry');
    assert.ok(c.sec.textContent.includes(c.tr('gpFull')));
  } finally { c.window.close(); }
  const gp = registry({ id: 2, state: 'waiting', ...PAD, player: null });
  c = await open(true, gp);
  try {
    assert.ok(c.badges[1].textContent.startsWith(c.tr('mqdown')));
    assert.ok(!c.badges[1].classList.contains('good'));
    assert.ok(!c.badges[1].textContent.includes(c.tr('gpPlayer')));
    assert.equal(c.pair.disabled, false);
  } finally { c.window.close(); }
}

async function pairAndForget() {
  const gp = registry({ id: 1, state: 'ready', ...PAD, player: 2 });
  let resolve;
  const wait = new Promise(r => { resolve = r; });
  let c = await open(true, gp, { wait });
  try {
    c.pair.click(); c.pair.click(); await flush();
    assert.equal(c.log.filter(l => l === 'POST /api/v1/gamepad/pair').length, 1);
    assert.equal(c.pair.disabled, true);
    resolve(); await flush();
    assert.equal(c.badges[1].textContent, c.tr('scanning'));
    assert.ok(c.badges[0].textContent.includes(PAD.name));
  } finally { resolve(); c.window.close(); }
  c = await open(true, registry({ id: 1, state: 'ready', ...PAD, player: 2 },
    { id: 2, state: 'ready', name: 'Other', address: 'E4:17:D8:BC:EC:2E', player: 1 }));
  try {
    c.forget[0].click(); await flush();
    assert.ok(!c.log.some(l => l.startsWith('DELETE')));
    assert.equal(c.forget[0].textContent, c.tr('sure'));
    c.forget[0].click(); await flush();
    assert.ok(c.log.includes('DELETE /api/v1/gamepad/1'), 'device id is independent of its player');
    assert.equal(c.badges[0].textContent, c.tr('gpNone'));
    assert.ok(c.badges[1].textContent.includes('Other'));
    assert.ok(c.badges[1].textContent.endsWith(c.tr('gpPlayer') + ' 1'));
    assert.equal(c.pair.disabled, false);
  } finally { c.window.close(); }
  c = await open(true, registry(), { fail: true });
  try {
    c.pair.click(); await flush();
    assert.equal(c.pair.disabled, false, 'a failed request can be retried');
    assert.ok(c.badges.every(b => b.textContent === c.tr('gpNone')));
  } finally { c.window.close(); }
}

async function phones() {
  let c = await open(true, registry());
  try {
    const rows = [...c.sec.querySelectorAll('[data-phone]')].map(b => b.parentElement.parentElement);
    assert.equal(rows.length, 2);
    assert.ok(rows.every(r => r.style.display === 'none'), 'no phone, no phone rows');
  } finally { c.window.close(); }
  c = await open(true, { ...registry({ id: 1, state: 'ready', ...PAD, player: 1 }),
    remotes: [{ session: 7, name: 'Pixel 8', player: 2 }] });
  try {
    const phone = c.sec.querySelector('[data-phone="2"]');
    assert.equal(phone.textContent, 'Pixel 8 · ' + c.tr('gpPlayer') + ' 2');
    assert.notEqual(phone.closest('[style]')?.style.display, 'none', 'a playing phone shows with its player');
    assert.equal(c.sec.querySelector('[data-phone="1"]').textContent, '');
    assert.ok(c.badges[0].textContent.includes(PAD.name), 'next to the Bluetooth pad');
  } finally { c.window.close(); }
}

async function polling() {
  const c = await open(true, registry({ ...empty(2), state: 'pairing' }));
  try {
    const gets = () => c.log.filter(l => l === 'GET /api/v1/gamepad').length;
    const first = gets(); await flush(4400);
    assert.ok(gets() >= first + 2);
    await goto(c.window, '#/');
    const left = gets(); await flush(2300);
    assert.equal(gets(), left, 'polling stops on leaving System');
  } finally { c.window.close(); }
}

(async () => {
  await gating(); await states(); await pairAndForget(); await phones(); await polling();
  console.log('gamepad-card: two devices, phones, player identities, targeted actions and polling passed');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(restoreTimers);
