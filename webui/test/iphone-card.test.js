/* iPhone card: capability gating, link state, enable switch and confirmed unpairing.
   Run: node iphone-card.test.js */
const { boot, goto, flush } = require(process.env.HARNESS || './harness');
const assert = require('node:assert/strict');

const audio = { rtttl: true, track: false, mp3: true, radio: true };
const base = () => ({
  enabled: true, state: 'ready', phone: { addr: 'AC:E4:B5:D6:D0:57', name: 'Test iPhone' },
});

function mock(window, ip) {
  const device = window.fetch, log = [], faults = { rejectNext: false };
  window.fetch = async (url, o = {}) => {
    const path = String(url), method = (o.method || 'GET').toUpperCase();
    if (!path.startsWith('/api/v1/iphone')) return device(url, o);
    log.push({ method, path, body: o.body ? JSON.parse(o.body) : null });
    if (path === '/api/v1/iphone' && method === 'PUT') {
      if (faults.rejectNext) {
        faults.rejectNext = false;
        return new Response('{"error":{"code":"serviceBusy","message":"busy"}}', { status: 503 });
      }
      ip.enabled = JSON.parse(o.body).enabled;
      ip.state = ip.enabled ? 'waiting' : 'off';
    } else if (path === '/api/v1/iphone/phone' && method === 'DELETE') {
      ip.phone = null; ip.state = ip.enabled ? 'waiting' : 'off';
    } else if (path !== '/api/v1/iphone' || method !== 'GET') return new Response('{}', { status: 405 });
    return new Response(JSON.stringify(ip));
  };
  return { log, faults };
}

async function open(iphone, ip) {
  const { window } = await boot({ caps: { transitions: [], audio, ble: iphone } });
  const network = mock(window, ip);
  await goto(window, '#/system');
  await flush(80);
  const sec = window.document.querySelector('#sec-iphone');
  const tr = window.eval('t');
  const btn = label => sec && [...sec.querySelectorAll('button')].find(b => b.textContent === label);
  const sw = sec && sec.querySelector('input[type=checkbox]');
  return { window, ...network, sec, tr, btn, sw, badge: sec && sec.querySelector('.badge') };
}

(async () => {
  for (const iphone of [undefined, false]) {
    const c = await open(iphone, base());
    try {
      assert.equal(c.sec, null, 'no card without the capability');
      assert.equal(c.log.length, 0);
    } finally { c.window.close(); }
  }

  let c = await open(true, base());
  try {
    assert.ok(c.badge.textContent.includes('Test iPhone'));
    assert.ok(c.badge.classList.contains('good'));
    assert.equal(c.sw.checked, true);
    const forget = c.btn(c.tr('ipForget'));
    assert.equal(forget.style.display, '');
    forget.click(); await flush();
    assert.ok(!c.log.some(l => l.method === 'DELETE'), 'the first click only asks for confirmation');
    forget.click(); await flush();
    assert.deepEqual(c.log.filter(l => l.method === 'DELETE'),
      [{ method: 'DELETE', path: '/api/v1/iphone/phone', body: null }]);
    assert.equal(forget.style.display, 'none');
    assert.equal(c.badge.textContent, c.tr('ipWait'));
    assert.ok(!c.badge.classList.contains('good'));

    c.sw.click(); await flush();
    assert.deepEqual(c.log.filter(l => l.method === 'PUT').pop().body, { enabled: false });
    assert.equal(c.sw.checked, false);
    assert.equal(c.badge.textContent, c.tr('mqoff'));
    c.sw.click(); await flush();
    assert.deepEqual(c.log.filter(l => l.method === 'PUT').pop().body, { enabled: true });
    assert.equal(c.sw.checked, true);
    assert.equal(c.badge.textContent, c.tr('ipWait'));

    c.faults.rejectNext = true;
    c.sw.click(); await flush(80);
    assert.equal(c.sw.checked, true, 'a rejected change reloads the saved setting');
    assert.equal(c.badge.textContent, c.tr('ipWait'));
  } finally { c.window.close(); }

  const unnamed = base();
  unnamed.phone.name = '';
  c = await open(true, unnamed);
  try { assert.ok(c.badge.textContent.includes(unnamed.phone.addr), 'an unnamed phone shows its address'); }
  finally { c.window.close(); }
  console.log('iphone-card: gating, state, enable switch, failed save and confirmed forget passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
