const { boot, goto, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  FAIL: ' + msg); }
}

const keys = () => ({ keys: [
  { key: 'key', label: 'API key', help: 'Your OpenAI key', set: false },
  { key: 'org', label: '', help: '', set: true }] });

function device(current) {
  return (method, path, body) => {
    if (path === '/api/v1/secrets') return [{ apps: ['Assistant'] }, 200];
    if (path === '/api/v1/secrets/Assistant' && method === 'GET') return [current, 200];
    if (path === '/api/v1/secrets/Assistant' && method === 'PUT') {
      for (const [k, v] of Object.entries(body)) current.keys.find(x => x.key === k).set = v !== '';
      return [{ ok: true }, 200];
    }
    const one = path.match(/^\/api\/v1\/secrets\/Assistant\/(\w+)$/);
    if (one && method === 'DELETE') {
      current.keys.find(x => x.key === one[1]).set = false;
      return [{ ok: true }, 200];
    }
    return [{ error: { code: 'notFound', message: 'unknown route' } }, 404];
  };
}

const oauthDevice = (method, path) => path === '/api/v1/oauth'
  ? [{ redirectUri: 'https://awtrix.de/oauth/callback', apps: [{ name: 'Assistant' }] }, 200]
  : [{ name: 'Assistant', provider: 'a.test', state: 'signedOut', clientId: '', clientSecretSet: false }, 200];

const rowFor = (doc, name) => [...doc.querySelectorAll('.approw')]
  .find(r => r.querySelector('.nmt')?.textContent === name);

async function openSettings(caps, current, extra = {}) {
  const { window, store } = await boot({ caps, secrets: device(current), ...extra.boot });
  store.apps = [{ name: 'Assistant', origin: 'script', enabled: true, inLoop: true, present: true,
    config: !!extra.config }];
  store.configs.Assistant = extra.config || { fields: [], warnings: [] };
  await goto(window, '#/apps');
  await flush(80);
  const gear = rowFor(window.document, 'Assistant')?.querySelector('.cfgbtn');
  if (gear) { gear.click(); await flush(150); }
  const panel = rowFor(window.document, 'Assistant')?.querySelector('.appcfg');
  return { window, store, gear, panel, t: window.eval('t'),
    section: () => panel && panel.querySelector('details.cfgsection.secret') };
}

const button = (root, text) => [...root.querySelectorAll('button')].find(b => b.textContent === text);

async function testSection() {
  const { gear, panel, section, t } = await openSettings({ transitions: [], secret: true }, keys());
  assert(gear, 'a script with @secret gets a settings button without @config');
  assert(panel && !panel.textContent.includes(t('cfgNone')), 'no "no settings" note next to the keys');
  const box = section();
  assert(box && !box.open, 'the keys are a section of their own, folded like the others');
  assert(box && box.querySelector('summary').textContent === t('seSection'), 'the section is named');
  const inputs = box ? [...box.querySelectorAll('input')] : [];
  assert(inputs.length === 2 && inputs.every(i => i.type === 'password' && i.autocomplete === 'new-password'),
    'one password field per key, never filled by the browser');
  assert(inputs[0]?.placeholder === t('seEmpty') && inputs[1]?.placeholder === t('seSet'),
    'the placeholder says whether a key is set');
  assert(inputs[0]?.getAttribute('aria-label') === 'API key' && inputs[1]?.getAttribute('aria-label') === 'org',
    'the label names the field, the key stands in for a missing label');
  assert(box && box.textContent.includes('Your OpenAI key'), 'the help text is shown');
  assert(box && [...box.querySelectorAll('button')].filter(b => b.textContent === t('seRemove')).length === 1,
    'only a set key can be removed');
}

async function testSaveSendsOnlyFilledFields() {
  const current = keys();
  const { window, store, section, t } = await openSettings({ transitions: [], secret: true }, current);
  const box = section();
  button(box, t('save')).click();
  await flush(80);
  assert(!store.secretsCalls.some(c => c.method === 'PUT'), 'Save with empty fields sends nothing');
  box.querySelectorAll('input')[0].value = 'sk-typed';
  button(box, t('save')).click();
  await flush(120);
  const put = store.secretsCalls.find(c => c.method === 'PUT');
  assert(put && put.path === '/api/v1/secrets/Assistant' && put.body === JSON.stringify({ key: 'sk-typed' }),
    'Save sends only the filled field: ' + (put && put.body));
  assert(put && put.headers['X-Awtrix-Secret'] === '1', 'changes carry the marker header');
  const fresh = section();
  assert(fresh && fresh.querySelectorAll('input')[0].placeholder === t('seSet') &&
    fresh.querySelectorAll('input')[0].value === '', 'after saving the field is empty again and shows the key as set');
  assert((window.document.querySelector('.toast')?.textContent || '').includes(t('saved')), 'saving is confirmed');
}

async function testRemove() {
  const current = keys();
  const { store, section, t } = await openSettings({ transitions: [], secret: true }, current);
  const remove = button(section(), t('seRemove'));
  remove.click();
  await flush(40);
  assert(!store.secretsCalls.some(c => c.method === 'DELETE'), 'Remove asks first');
  remove.click();
  await flush(120);
  const del = store.secretsCalls.find(c => c.method === 'DELETE');
  assert(del && del.path === '/api/v1/secrets/Assistant/org' && del.headers['X-Awtrix-Secret'] === '1',
    'a second press removes that one key');
  assert(!button(section(), t('seRemove')), 'nothing is left to remove');
}

async function testInvalid() {
  const { section, t } = await openSettings({ transitions: [], secret: true }, { invalid: '@secret: bad key' });
  const box = section();
  assert(box && box.textContent.includes(t('seInvalid')) && box.textContent.includes('@secret: bad key'),
    'an invalid line is named');
  assert(box && !box.querySelector('input') && !button(box, t('save')), 'and offers nothing to save');
}

async function testOrderWithConfigAndSignIn() {
  const config = { fields: [{ key: 'city', type: 'text', label: 'City', default: '', value: '' }], warnings: [] };
  const { panel, section } = await openSettings({ transitions: [], secret: true, oauth: true }, keys(),
    { config, boot: { oauth: oauthDevice } });
  assert(panel && panel.firstElementChild?.matches('details.cfgsection.oauth'), 'the sign-in still comes first');
  assert(panel && panel.lastElementChild === section(), 'the keys come after the settings and their Save');
  assert(panel && panel.textContent.includes('City'), 'the settings stay');
}

async function testGating() {
  const { store, gear } = await openSettings({ transitions: [] }, keys());
  assert(!gear, 'nothing without the secret capability');
  assert(!store.secretsCalls.length, 'and no request for it');
}

(async () => {
  await testSection();
  await testSaveSendsOnlyFilledFields();
  await testRemove();
  await testInvalid();
  await testOrderWithConfigAndSignIn();
  await testGating();
  console.log(`secrets: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
