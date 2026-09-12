const assert = require('node:assert/strict');
const { boot, goto, flush, stubXhr } = require('./harness');

(async () => {
  const { window, store } = await boot();
  store.iconDb = { v: 1, icons: [['sun', 'Sun', 8, 8, 1, 12]] };
  store.iconBytes.sun = 'GIF89a-sun';
  const uploads = [];
  stubXhr(window, uploads, store);
  await goto(window, '#/icons');
  await flush(80);
  assert.equal(window.document.querySelector('.idb .tile img').src, 'https://awtrix.de/icons/sun/preview.webp');
  await assert.rejects(window.idbFetch('sun'), {code: 'hubAuthentication'});
  assert.equal(store.iconDownloadRequests?.length || 0, 0);
  assert.equal(await window.installScriptIcons(['sun']), 0);
  assert.equal(uploads.length, 0);

  window.localStorage.awtrixHubToken = 'valid-test-key';
  store.requiredIconToken = 'valid-test-key';
  await window.idbInstall('sun');
  assert.equal(uploads.length, 1);
  let request = store.iconDownloadRequests.at(-1);
  assert.equal(request.options.headers.Authorization, 'Bearer valid-test-key');
  assert.equal(request.options.credentials, 'omit');
  assert.equal(request.options.redirect, 'error');
  assert.equal(request.options.cache, 'no-store');
  assert.ok(!request.url.includes('valid-test-key'));

  const file = (await window.iconInventory()).find(item => item.name === 'sun.gif');
  store.iconBytes.sun = 'GIF89a-sun-updated';
  await window.reloadHubIcon(file);
  assert.equal(uploads.length, 2);
  assert.equal(store.iconDownloadRequests.at(-1).options.headers.Authorization, 'Bearer valid-test-key');

  store.requiredIconToken = 'replacement-key';
  await assert.rejects(window.idbFetch('sun'), {code: 'hubAuthentication'});
  assert.equal(uploads.length, 2);
  window.localStorage.removeItem('awtrixHubToken');
  const before = store.iconDownloadRequests.length;
  await assert.rejects(window.reloadHubIcon(file), {code: 'hubAuthentication'});
  assert.equal(store.iconDownloadRequests.length, before);
  window.localStorage.awtrixHubToken = 'replacement-key';
  await assert.rejects(window.hubDownloadFile('https://other.example/icons/sun.gif'), /different Hub/);
  await assert.rejects(window.hubDownloadFile('http://awtrix.de/icons/sun.gif'), /different Hub/);
  await assert.rejects(window.hubDownloadFile('https://user:pass@awtrix.de/icons/sun.gif'), /different Hub/);
  assert.equal(store.iconDownloadRequests.length, before);
  store.requiredIconToken = 'replacement-key';
  await assert.rejects(window.idbFetch('missing'), /no longer available/);
  const originalFetch = window.fetch;
  window.fetch = async () => { throw new TypeError('Failed to fetch'); };
  await assert.rejects(window.idbFetch('sun'), /Check your internet connection/);
  window.fetch = originalFetch;

  await goto(window, '#/system');
  await flush(40);
  const input = window.document.querySelector('#sec-hub input[type=password]');
  Object.defineProperty(window, 'localStorage', {configurable: true, value: {
    get awtrixHubToken() { return ''; },
    set awtrixHubToken(value) { throw new Error('storage denied'); }
  }});
  input.value = 'unsaved-key';
  input.dispatchEvent(new window.Event('change', {bubbles: true}));
  assert.equal(input.value, '');
  const message = [...window.document.querySelectorAll('.toast')].at(-1).textContent;
  assert.match(message, /could not be saved/);
  assert.doesNotMatch(message, /Hub key saved/);

  console.log('icons-auth: previews, token download/reload, revocation, script blocking and host isolation passed');
  window.close();
  process.exit(0);
})().catch(error => { console.error(error); process.exit(1); });
