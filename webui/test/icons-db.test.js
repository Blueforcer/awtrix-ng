/* Icon database on the Icons tab.
   The catalogue lives outside the device - the browser fetches the manifest and
   the GIF bytes itself and only the install writes to the clock - so everything
   here is driven through the mocked external endpoints in the harness. */
const { boot, goto, flush, stubXhr } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) { pass++; } else { fail++; console.error('  ✗ ' + msg); }
}

const CATALOGUE = [
  ['mail', '', 8, 8, 1, 100],
  ['supermario', 'SuperMario', 8, 8, 153, 12844],
  ['firepit', 'Firepit', 32, 8, 12, 3000],
  ['clock', '', 32, 8, 1, 400],
];

function card(window) {
  return window.document.querySelector('.idb');
}
// The icons on the clock and the catalogue both render into .grid-icons; only
// the catalogue's sits inside .idb.
function ownGrid(window) {
  return [...window.document.querySelectorAll('.grid-icons')].find(g => !g.closest('.idb'));
}
function segments(window) {
  return [...window.document.querySelectorAll('.segbar button')];
}
// Four icon buttons never fit the narrowest tile, so every action sits behind
// the one button the tile does show.
function openMenu(tile) {
  tile.querySelector('.acts button').click();
  return [...tile.querySelectorAll('.tmenu button')];
}
function tiles(window) {
  return [...card(window).querySelectorAll('.tile .nm')].map(n => n.textContent);
}
async function search(window, text) {
  const input = card(window).querySelector('input[type=text]');
  input.value = text;
  input.dispatchEvent(new window.Event('input'));
  await flush(260); // the field is debounced by 200 ms
}

async function withGallery(extra) {
  const ctx = await boot();
  ctx.store.iconDb = { v: 1, icons: CATALOGUE };
  for (const row of CATALOGUE) ctx.store.iconBytes[row[0]] = 'GIF89a-' + row[0];
  if (extra) extra(ctx);
  await goto(ctx.window, '#/icons');
  await flush(60);
  return ctx;
}

async function testBrowse() {
  const { window } = await withGallery();
  assert(!!card(window), 'the gallery is mounted on #/icons');
  assert(tiles(window).length === 4, 'all four catalogue entries render');
  assert(tiles(window).includes('SuperMario'),
    'a display name that differs from the slug is shown, not the slug');

  await search(window, 'mario');
  assert(tiles(window).join() === 'SuperMario', 'search matches the display name');
  await search(window, 'clo');
  assert(tiles(window).join() === 'clock', 'search matches the slug');
  await search(window, 'nothing-like-this');
  assert(card(window).querySelectorAll('.tile').length === 0 &&
    !!card(window).querySelector('.empty'), 'a search with no hits shows the empty note');
  await search(window, '');

  const size = card(window).querySelector('select');
  size.value = '32x8';
  size.dispatchEvent(new window.Event('change'));
  await flush(20);
  assert(tiles(window).sort().join() === 'Firepit,clock', 'the size filter keeps only 32x8');
  size.value = '';
  size.dispatchEvent(new window.Event('change'));

  const anim = card(window).querySelector('input[type=checkbox]');
  anim.checked = true;
  anim.dispatchEvent(new window.Event('change'));
  await flush(20);
  assert(tiles(window).sort().join() === 'Firepit,SuperMario',
    'the animated filter keeps only multi-frame icons');
}

async function testInstall() {
  const { window, store } = await withGallery();
  const uploads = [];
  stubXhr(window, uploads, store);

  await search(window, 'mario');
  const button = card(window).querySelector('.tile .acts button');
  assert(button.disabled === false, 'an icon that is not on the clock can be installed');
  button.click();
  await flush(80);

  assert(uploads.length === 1, 'installing uploads exactly once (got ' + uploads.length + ')');
  assert(uploads[0] && uploads[0].url.includes('dir=%2FICONS'),
    'the upload targets the ICONS directory');
  assert(uploads[0] && uploads[0].files.some(f => f.name === 'supermario.gif'),
    'the file is named after the slug');
  assert(card(window).querySelector('.tile .acts button').disabled === true,
    'the button locks once the icon is on the clock');
}

async function testAlreadyInstalled() {
  const { window } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('mail.gif', 100);
  });
  await search(window, 'mail');
  const button = card(window).querySelector('.tile .acts button');
  assert(button.disabled === true, 'an icon already on the clock is marked as installed');
}

/* The Hub launches with nothing in it, so "loaded and empty" is a normal state
   and has to read differently from "still loading" and from "no search hits". */
async function testEmptyCatalogue() {
  const ctx = await boot();
  ctx.store.iconDb = { v: 1, icons: [] };
  await goto(ctx.window, '#/icons');
  await flush(80);
  const note = card(ctx.window).querySelector('.empty');
  assert(!!note, 'an empty catalogue says so instead of showing a blank pane');
  assert(!/matches|gefunden/i.test(note ? note.textContent : ''),
    'and does not blame the search, which was never run');
  assert(card(ctx.window).querySelector('.help').textContent === '',
    'no count is offered when there is nothing to count');
}

async function testUnreachableCatalogue() {
  const ctx = await boot();
  ctx.store.iconDb = null; // json() answers null, so the rows never materialise
  await goto(ctx.window, '#/icons');
  await flush(60);
  const grid = ownGrid(ctx.window);
  assert(card(ctx.window).querySelectorAll('.tile').length === 0,
    'no gallery tiles when the catalogue cannot be read');
  assert(!!grid && grid.querySelectorAll('.tile, .empty').length > 0,
    'the device icon list still renders when the catalogue is down');
}

async function testSubmit() {
  const { window, store } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
  });
  const grid = ownGrid(window);
  const tile = grid.querySelector('.tile');
  const actions = openMenu(tile);
  assert(actions.length === 4, 'a device tile offers show, edit, submit and delete');

  actions[2].click();
  const footer = tile.querySelector('.ft');
  assert(footer.querySelector('input') && footer.querySelector('input').value === 'own',
    'the submit row is prefilled with the icon name');

  footer.querySelector('input').value = 'My Own Icon';
  footer.querySelector('button').click();
  await flush(80);

  assert(store.submitted.length === 1, 'submitting posts once');
  const sent = store.submitted[0];
  assert(sent && typeof sent.get === 'function' && sent.get('name') === 'My Own Icon',
    'the display name is sent as typed');
  assert(sent && sent.get('source') === 'webui', 'the source identifies the web UI');
  assert(!!tile.querySelector('.ft .nm'), 'the footer goes back to normal after a submission');

  const toast = [...window.document.querySelectorAll('.toast')].map(t => t.textContent).join(' ');
  assert(!/review/i.test(toast),
    'the Hub publishes straight away, so nothing may promise a review');
  assert(/published/i.test(toast), 'a successful submission says the icon is published');
}

/* Publishing from the clock's own page always lands here: the page is served
   from http://<device-ip>, so the Hub session cookie is cross-site and never
   reaches the POST. The answer has to point at the Hub, not read as a fault. */
async function testNotLoggedIn() {
  const { window } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
    ctx.store.submitCode = 401;
    ctx.store.submitReply = { ok: false, error: 'notLoggedIn',
      message: 'Sign in on the AWTRIX Hub to publish',
      pr: 'https://example.invalid/login' };
  });
  const grid = ownGrid(window);
  const tile = grid.querySelector('.tile');
  openMenu(tile)[2].click();
  tile.querySelector('.ft button').click();
  await flush(80);

  const toast = [...window.document.querySelectorAll('.toast')].pop();
  assert(!!toast && /Hub/.test(toast.textContent),
    'being signed out names the Hub as the place to publish');
  const action = toast && [...toast.querySelectorAll('.tacts button')][0];
  assert(!!action, 'the sign-in URL is offered as an action, not just described');

  let opened = '';
  window.open = url => { opened = url; };
  action.click();
  assert(opened === 'https://example.invalid/login',
    'the action opens the URL the Hub handed back');
}

/* An error code the client has no sentence for must still reach the user
   readably - the vocabulary is complete, but the server may outgrow it. */
async function testUnknownError() {
  const { window } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
    ctx.store.submitCode = 400;
    ctx.store.submitReply = { ok: false, error: 'somethingNew' };
  });
  const grid = ownGrid(window);
  const tile = grid.querySelector('.tile');
  openMenu(tile)[2].click();
  tile.querySelector('.ft button').click();
  await flush(80);

  const toast = [...window.document.querySelectorAll('.toast')].map(t => t.textContent).join(' ');
  assert(/somethingNew/.test(toast) && !/idbe_/.test(toast),
    'an unknown code is shown raw, never as the missing translation key');
}

async function testDuplicate() {
  const { window, store } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
    ctx.store.submitReply = { ok: false, error: 'duplicate', slug: 'mail' };
  });
  const grid = ownGrid(window);
  const tile = grid.querySelector('.tile');
  openMenu(tile)[2].click();
  tile.querySelector('.ft button').click();
  await flush(80);

  const toast = [...window.document.querySelectorAll('.toast')].map(t => t.textContent).join(' ');
  assert(/mail/.test(toast), 'a duplicate names the icon that already holds the content');
  assert(!!tile.querySelector('.ft input'),
    'a rejected submission keeps the row open so the name can be changed');
}

/* The page is about the icons on the clock; the catalogue and the two upload
   forms are alternatives you switch to, not a queue you scroll past. */
async function testSegments() {
  const { window } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
    ctx.store.files['/ICONS'].set('two.gif', 120);
  });
  const segs = segments(window);
  assert(segs.length === 3, 'three ways in: the clock, the catalogue, adding one');
  assert(/\(2\)/.test(segs[0].textContent),
    'the first segment counts what is on the clock (got "' + segs[0].textContent + '")');

  // The card leads with the header row and the segment bar, then one pane each.
  const paneOf = i => segs[i].closest('.card').querySelectorAll(':scope > div')[i + 2];
  assert(segs[0].classList.contains('on') && paneOf(0).hidden === false,
    'the icons on the clock are what the page opens on');
  assert(paneOf(1).hidden && paneOf(2).hidden, 'the other two start out of the way');

  segs[1].click();
  assert(paneOf(0).hidden && !paneOf(1).hidden, 'picking the catalogue swaps the pane');
  assert(!segs[0].classList.contains('on') && segs[1].classList.contains('on'),
    'exactly one segment reads as current');
  assert(!!card(window).querySelector('.tile'), 'the catalogue is the pane that got shown');

  segs[2].click();
  const add = paneOf(2);
  assert(!add.hidden && !!add.querySelector('.drop') && !!add.querySelector('.lam'),
    'adding an icon holds both the drop zone and the LaMetric field');
}

async function main() {
  await testSegments();
  await testBrowse();
  await testInstall();
  await testAlreadyInstalled();
  await testEmptyCatalogue();
  await testUnreachableCatalogue();
  await testSubmit();
  await testDuplicate();
  await testNotLoggedIn();
  await testUnknownError();
  await flush(20);
  console.log(`icons-db: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main();
