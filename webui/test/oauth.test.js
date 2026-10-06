/* Script sign-in: the settings of a script with an @oauth line, the Sign in button and the
   return from the provider through the Hub.
   Run: node oauth.test.js */
const { boot, goto, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  FAIL: ' + msg); }
}

const REDIRECT = 'https://awtrix.de/oauth/callback';
const status = (extra = {}) => ({ name: 'Spot', provider: 'accounts.spotify.com', scope: 'user-read-currently-playing',
  pkce: true, clientId: '', clientSecretSet: false, state: 'signedOut', ...extra });

function provider(current) {
  return (method, path, body) => {
    if (path === '/api/v1/oauth') return [{ redirectUri: REDIRECT, apps: [current] }, 200];
    if (path === '/api/v1/oauth/Spot' && method === 'GET') return [current, 200];
    if (path === '/api/v1/oauth/Spot' && method === 'POST') {
      if (body.clientId) current.clientId = body.clientId;
      if (body.clientSecret) current.clientSecretSet = true;
      return [{ ok: true }, 200];
    }
    if (path === '/api/v1/oauth/Spot/start') return [{ url: 'https://accounts.spotify.com/authorize?x=1' }, 200];
    if (path === '/api/v1/oauth/Spot/code') { current.state = 'signedIn'; return [{ ok: true }, 202]; }
    if (path === '/api/v1/oauth/Spot' && method === 'DELETE') { current.state = 'signedOut'; return [{ ok: true }, 200]; }
    return [{ error: { code: 'notFound', message: 'unknown route' } }, 404];
  };
}

const rowFor = (doc, name) => [...doc.querySelectorAll('.approw')]
  .find(r => r.querySelector('.nmt')?.textContent === name);

async function openSettings(caps, extra) {
  const current = status(extra);
  const { window, store } = await boot({ caps, oauth: provider(current) });
  store.apps = [{ name: 'Spot', origin: 'script', enabled: true, inLoop: true, present: true, config: false }];
  store.configs.Spot = { fields: [], warnings: [] };
  await goto(window, '#/apps');
  await flush(80);
  const gear = rowFor(window.document, 'Spot')?.querySelector('.cfgbtn');
  if (gear) { gear.click(); await flush(150); }
  return { window, store, current, gear, panel: rowFor(window.document, 'Spot')?.querySelector('.appcfg') };
}

async function testSettings() {
  const { window, store, current, gear, panel } = await openSettings({ transitions: [], oauth: true });
  const t = window.eval('t');
  assert(gear, 'a script with @oauth gets a settings button without @config');
  const text = panel ? panel.textContent : '';
  assert(!text.includes(t('cfgNone')), 'no "no settings" note next to the sign-in');
  const section = panel && panel.querySelector('details.cfgsection.oauth');
  assert(section && !section.open, 'the sign-in is a section of its own, folded like the others');
  assert(section && panel.firstElementChild === section, 'the sign-in comes first');
  const secret = panel && panel.querySelector('input[type=password]');
  assert(secret, 'the client secret field is masked');
  const id = panel && panel.querySelector('input[type=text]');
  id.value = 'cid';
  secret.value = 's3cret';
  const signIn = [...panel.querySelectorAll('button')].find(b => b.textContent === t('oaSignIn'));
  let opened = '';
  window.eval('location').assign = url => { opened = url; };
  signIn.click();
  await flush(80);
  const save = store.oauthCalls.find(c => c.method === 'POST' && c.path === '/api/v1/oauth/Spot');
  assert(save && JSON.parse(save.body).clientSecret === 's3cret' && current.clientId === 'cid',
    'Sign in saves what was typed first');
  assert(save && save.headers['X-Awtrix-OAuth'] === '1', 'changes carry the marker header');
  assert(store.oauthCalls.some(c => c.path === '/api/v1/oauth/Spot/start'), 'Sign in starts at the device');
}

async function testNewScript() {
  const current = status();
  const listed = [];
  const answer = provider(current);
  const { window, store } = await boot({ caps: { transitions: [], oauth: true },
    oauth: (method, path, body) => path === '/api/v1/oauth' ? [{ redirectUri: REDIRECT, apps: listed }, 200]
      : answer(method, path, body) });
  store.apps = [{ name: 'Spot', origin: 'script', enabled: true, inLoop: true, present: true, config: false }];
  store.configs.Spot = { fields: [], warnings: [] };
  await goto(window, '#/apps');
  await flush(80);
  assert(!rowFor(window.document, 'Spot')?.querySelector('.cfgbtn'), 'no sign-in before the script declares one');
  listed.push(current);
  await goto(window, '#/');
  await goto(window, '#/apps');
  await flush(150);
  assert(rowFor(window.document, 'Spot')?.querySelector('.cfgbtn'), 'a script that gains @oauth shows up on the next visit');
}

async function testSignedIn() {
  const { panel } = await openSettings({ transitions: [], oauth: true }, { clientId: 'cid', state: 'signedIn' });
  const section = panel && panel.querySelector('details.cfgsection.oauth');
  assert(section && !section.open, 'signed in: the sign-in section starts folded');
}

async function testStraightToApps() {
  const { store } = await boot({ caps: { transitions: [], oauth: true }, oauth: provider(status()),
    url: 'http://localhost/#/apps' });
  await flush(150);
  assert(store.oauthCalls.some(c => c.path === '/api/v1/oauth'),
    'opened straight on the Apps tab: the sign-ins are still listed');
}

async function testGating() {
  const { gear } = await openSettings({ transitions: [] });
  assert(!gear, 'nothing without the oauth capability');
}

async function testReturn() {
  const current = status({ clientId: 'cid', clientSecretSet: true });
  const state = Buffer.from(JSON.stringify({ r: 'http://localhost', a: 'Spot', n: 'x' })).toString('base64url');
  const { window, store } = await boot({ caps: { transitions: [], oauth: true }, oauth: provider(current),
    url: 'http://localhost/#oauth=C0DE&state=' + state });
  await flush(150);
  const call = store.oauthCalls.find(c => c.path === '/api/v1/oauth/Spot/code');
  assert(call && JSON.parse(call.body).code === 'C0DE' && JSON.parse(call.body).state === state,
    'the code goes to the device with its state');
  assert(call && call.headers['X-Awtrix-OAuth'] === '1', 'with the marker header');
  assert(!window.location.hash.includes('C0DE') && window.location.hash === '#/apps',
    'the code leaves the address bar and the Apps tab opens');
}

async function testReturnError() {
  const state = Buffer.from(JSON.stringify({ r: 'http://localhost', a: 'Spot', n: 'x' })).toString('base64url');
  const { window, store } = await boot({ caps: { transitions: [], oauth: true }, oauth: provider(status()),
    url: 'http://localhost/#oauth_error=access_denied&state=' + state });
  await flush(150);
  assert(!store.oauthCalls.some(c => c.path.endsWith('/code')), 'a refused sign-in sends no code');
  assert((window.document.querySelector('.toast')?.textContent || '').includes('access_denied'),
    'the reason is shown');
}

(async () => {
  await testSettings();
  await testNewScript();
  await testSignedIn();
  await testStraightToApps();
  await testGating();
  await testReturn();
  await testReturnError();
  console.log(`oauth: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
