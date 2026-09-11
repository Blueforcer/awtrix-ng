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
  assert(card(window).querySelector('.tile .acts button').disabled === false,
    'an installed icon remains available for deliberate reload');
}

async function testJpegEntry() {
  const { window, store } = await withGallery(ctx => { ctx.store.iconExt.firepit = 'jpg'; });
  const uploads = [];
  stubXhr(window, uploads, store);

  await search(window, 'firepit');
  card(window).querySelector('.tile .acts button').click();
  await flush(120);

  assert(uploads.length === 1, 'a JPEG entry installs (got ' + uploads.length + ' uploads)');
  assert(uploads[0] && uploads[0].files.some(f => f.name === 'firepit.jpg'),
    'and lands under .jpg, not .gif');
  assert(store.files['/ICONS'].has('firepit.jpg'), 'the clock holds the JPEG');
}

async function testAlreadyInstalled() {
  const { window } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('mail.gif', 100);
  });
  await search(window, 'mail');
  const button = card(window).querySelector('.tile .acts button');
  assert(button.disabled === false && button.textContent === 'Reload from Hub', 'an installed icon offers deliberate reload');
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
  assert(actions.length === 4, 'the menu offers script reference, edit, publish and delete');

  actions[2].click();
  const footer = tile.querySelector('.ft');
  assert(footer.querySelector('input') && footer.querySelector('input').value === 'own',
    'the submit row is prefilled with the icon name');
  assert(!!footer.querySelector('a.hint'), 'and it links the terms it is about to accept');

  footer.querySelector('input').value = 'My Own Icon';
  footer.querySelector('button').click();
  await flush(80);

  assert(store.submitted.length === 1, 'submitting posts once');
  const sent = store.submitted[0];
  assert(sent && typeof sent.get === 'function' && sent.get('name') === 'My Own Icon',
    'the display name is sent as typed');
  assert(sent && sent.get('source') === 'webui', 'the source identifies the web UI');
  assert(sent && sent.get('agree') === '1', 'the consent shown in the row is sent along');
  assert(!!tile.querySelector('.ft .nm'), 'the footer goes back to normal after a submission');

  const toast = [...window.document.querySelectorAll('.toast')].map(t => t.textContent).join(' ');
  assert(!/review/i.test(toast),
    'the Hub publishes straight away, so nothing may promise a review');
  assert(/published/i.test(toast), 'a successful submission says the icon is published');
}

async function testDeviceToken() {
  const { window, store } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
  });

  const submit = () => {
    const tile = ownGrid(window).querySelector('.tile');
    openMenu(tile)[2].click();
    tile.querySelector('.ft button').click();
  };

  submit();
  await flush(80);
  assert(store.submittedHeaders.length === 1, 'the submission went out');
  assert(!store.submittedHeaders[0].Authorization,
    'without a token nothing is sent as authorization');

  await goto(window, '#/system');
  await flush(80);
  const field = window.document.querySelector('#sec-hub input[type=password]');
  assert(!!field, 'the System tab carries the token field');
  field.value = '  tok_abc123  ';
  field.dispatchEvent(new window.Event('change', { bubbles: true }));
  await flush(40);
  assert(window.localStorage.awtrixHubToken === 'tok_abc123',
    'the field stores the token, trimmed');

  store.localIconBytes['own.gif']='GIF89a-changed-after-publication';
  await goto(window, '#/icons');
  await flush(80);
  submit();
  await flush(80);
  assert(store.submittedHeaders.length === 2, 'the second submission went out');
  assert(store.submittedHeaders[1].Authorization === 'Bearer tok_abc123',
    'a stored token travels as a bearer header');

  delete window.localStorage.awtrixHubToken;
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

async function testInlineConnection() {
  const { window } = await withGallery();
  const panel = window.document.querySelector('.hub-connect');
  assert(!!panel, 'publishing can be connected directly in the gallery');
  const input = panel.querySelector('input[type=password]');
  input.value = '  local-gallery-token  ';
  panel.querySelector('.pri').click();
  assert(window.localStorage.awtrixHubToken === 'local-gallery-token', 'save stores the trimmed token');
  assert(/saved/i.test(panel.querySelector('[role=status]').textContent), 'status confirms storage without claiming server validation');
  await goto(window, '#/system');
  assert(window.document.querySelector('#sec-hub input').value === 'local-gallery-token', 'system and gallery share the same token');
  await goto(window, '#/icons');
  const restored = window.document.querySelector('.hub-connect');
  assert(restored.querySelector('input').value === 'local-gallery-token', 'returning restores the token');
  [...restored.querySelectorAll('button')].find(b => !b.classList.contains('pri')).click();
  assert(!window.localStorage.awtrixHubToken, 'remove clears the token');
}

async function testRefreshAndRecovery() {
  const { window, store } = await withGallery();
  store.iconDb = { v: 1, icons: [['new-icon', 'Fresh icon', 8, 8, 1, 100]] };
  const refresh = () => [...card(window).querySelectorAll('button')].find(b => b.textContent === 'Refresh collection');
  refresh().click();
  await flush(80);
  assert(tiles(window).join() === 'Fresh icon', 'refresh replaces a stale catalogue');
  store.iconDb = null;
  refresh().click();
  await flush(80);
  assert(!refresh().disabled, 'refresh remains available after a failed request');
  store.iconDb = { v: 1, icons: CATALOGUE };
  refresh().click();
  await flush(80);
  assert(tiles(window).length === 4, 'a retry recovers without leaving the gallery');
}

async function testHubHandoff() {
  const { window } = await withGallery();
  window.history.replaceState(null, '', '?icon=supermario#/icons');
  await goto(window, '#/system');
  await goto(window, '#/icons');
  await flush(80);
  assert(segments(window)[1].classList.contains('on'), 'a Hub link opens the community gallery');
  assert(tiles(window).join() === 'SuperMario', 'the icon from the Hub link is already selected by search');
  window.history.replaceState(null, '', '?icon=../secret#/icons');
  await goto(window, '#/system');
  await goto(window, '#/icons');
  await flush(80);
  assert(segments(window)[0].classList.contains('on'), 'invalid incoming icon names are ignored');
}

async function testInstalledSearchAndActions() {
  const { window, store } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('weather-cloud.gif', 240);
    ctx.store.files['/ICONS'].set('coffee.gif', 120);
  });
  const search = window.document.querySelector('input[type=search]');
  search.value = 'WEATHER';
  search.dispatchEvent(new window.Event('input'));
  const tile = ownGrid(window).querySelector('.tile');
  assert(ownGrid(window).querySelectorAll('.tile').length === 1 && /weather-cloud/.test(tile.textContent),
    'installed icon search is case insensitive and filters the local list');
  assert(store.files['/ICONS'].size === 2, 'search does not change device files');
  assert(!!tile.querySelector('.icon-show'), 'display preview is directly available without opening a menu');
  let notification = null;
  const originalFetch = window.fetch;
  window.fetch = async (url, opts) => {
    if (url === '/api/v1/notifications') {
      notification = JSON.parse(opts.body);
      return {ok:true,status:200,text:async ()=>'{}'};
    }
    return originalFetch(url, opts);
  };
  tile.querySelector('.icon-show').click();
  await flush(20);
  assert(notification && notification.icon === 'weather-cloud' && notification.durationMs === 3000,
    'display preview uses the selected icon for a short notification');
  openMenu(tile);
  const download = tile.querySelector('.tmenu a[download]');
  assert(download && download.getAttribute('download') === 'weather-cloud.gif' &&
    download.getAttribute('href') === '/ICONS/weather-cloud.gif', 'download points at the original device file');
  tile.dispatchEvent(new window.KeyboardEvent('keydown', {key:'Escape',bubbles:true}));
  assert(!tile.querySelector('.tmenu') && window.document.activeElement === tile.querySelector('.acts button'),
    'Escape closes the icon menu and returns focus to its button');
  search.value = 'missing';
  search.dispatchEvent(new window.Event('input'));
  ownGrid(window).querySelector('.empty button').click();
  assert(search.value === '' && ownGrid(window).querySelectorAll('.tile').length === 2,
    'empty search offers a working reset');
}

async function testKeyboardNavigationAndUpload() {
  const { window } = await withGallery();
  const tabs = segments(window);
  tabs[0].focus();
  tabs[0].dispatchEvent(new window.KeyboardEvent('keydown', {key:'ArrowRight',bubbles:true}));
  assert(tabs[1].getAttribute('aria-selected') === 'true' && window.document.activeElement === tabs[1],
    'arrow keys select and focus the next icon tab');
  assert(tabs[0].tabIndex === -1 && tabs[1].tabIndex === 0,
    'only the selected tab stays in the tab sequence');
  tabs[1].dispatchEvent(new window.KeyboardEvent('keydown', {key:'End',bubbles:true}));
  const add = window.document.getElementById(tabs[2].getAttribute('aria-controls'));
  assert(!add.hidden && tabs[2].getAttribute('aria-selected') === 'true', 'End opens the Add panel');
  const drop = add.querySelector('.drop');
  let chosen = 0;
  add.querySelector('input[type=file]').click = () => { chosen++; };
  drop.dispatchEvent(new window.KeyboardEvent('keydown', {key:'Enter',bubbles:true}));
  assert(drop.tabIndex === 0 && chosen === 1, 'upload can be opened using the keyboard');
}

async function main() {
  await testSameIdReload();
  await testEditorDraftRecovery();
  await testDescriptivePublicationName();
  await testContentAndOrigins();
  await testConflictProtection();
  await testResolvedPublication();
  await testEditorProvenanceBridge();
  await testInstalledSearchAndActions();
  await testKeyboardNavigationAndUpload();
  await testHubHandoff();
  await testInlineConnection();
  await testRefreshAndRecovery();
  await testSegments();
  await testBrowse();
  await testInstall();
  await testJpegEntry();
  await testAlreadyInstalled();
  await testEmptyCatalogue();
  await testUnreachableCatalogue();
  await testSubmit();
  await testDeviceToken();
  await testDuplicate();
  await testNotLoggedIn();
  await testUnknownError();
  await flush(20);
  console.log(`icons-db: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
async function testSameIdReload(){
  const {window,store}=await withGallery(ctx=>{ctx.store.files['/ICONS'].set('mail.gif',100);ctx.store.localIconBytes['mail.gif']='GIF89a-mail';});
  const uploads=[];stubXhr(window,uploads,store);
  const original=(await window.iconInventory()).find(f=>f.name==='mail.gif');
  store.iconBytes.mail='GIF89a-mail-v2';
  await window.reloadHubIcon(original);
  assert(store.localIconBytes['mail.gif']==='GIF89a-mail-v2','reload replaces changed Hub bytes under the same public ID');
  assert(store.iconOrigins.get('mail.gif').sha256===window.iconSha256(new window.TextEncoder().encode('GIF89a-mail-v2')),'reload records the latest version hash');
  const linked=(await window.iconInventory()).find(f=>f.name==='mail.gif');
  store.localIconBytes['mail.gif']='my edited cloud';store.iconBytes.mail='GIF89a-mail-v3';
  let conflict=false;try{await window.reloadHubIcon(linked);}catch(e){conflict=e.code==='iconConflict';}
  assert(conflict&&store.localIconBytes['mail.gif']==='my edited cloud','reload rechecks and protects local edits made after the list opened');
  await window.reloadHubIcon(linked,{replace:true});
  assert(store.localIconBytes['mail.gif']==='GIF89a-mail-v3','explicit override replaces local edits');
  const count=uploads.length;await window.reloadHubIcon((await window.iconInventory()).find(f=>f.name==='mail.gif'));
  assert(uploads.length===count,'unchanged remote version avoids another flash write');
}
async function testEditorDraftRecovery(){
  const {window,store}=await withGallery();await goto(window,'#/editor');await flush(20);
  const frame=window.document.querySelector('#piskelFrame'),messages=[];frame.contentWindow.postMessage=m=>messages.push(m);
  const send=(m,source=frame.contentWindow)=>window.dispatchEvent(new window.MessageEvent('message',{source,origin:'https://awtrix.de',data:{ns:'awtrix',...m}}));
  const project={version:1,name:'Cloud',piskel:{layers:['pixels'],fps:8},based_on:'mail'};
  send({type:'project-changed',revision:1,project},window);
  assert(!window.localStorage.getItem('awtrixEditorDraftsV1'),'another window cannot write a draft');
  send({type:'project-save',revision:1,requestId:'draft-1',project});await flush(10);
  const records=JSON.parse(window.localStorage.getItem('awtrixEditorDraftsV1'));
  assert(records.length===1&&records[0].project.based_on==='mail','full draft and origin persist in the browser');
  assert(messages.some(m=>m.type==='project-save-result'&&m.requestId==='draft-1'&&m.ok),'manual save is acknowledged after persistence');
  assert(store.submitted.length===0,'draft save never publishes');
  send({type:'project-changed',revision:2,project:{...project,name:'Renamed cloud'}});await flush(10);
  assert(JSON.parse(window.localStorage.getItem('awtrixEditorDraftsV1'))[0].id===records[0].id,'renaming keeps the same draft identity');
  await goto(window,'#/icons');await goto(window,'#/editor');await flush(20);
  assert(window.document.querySelector('#piskelDraftList').options.length===2,'saved draft is available after reopening the editor');
  const newer=window.document.querySelector('#piskelFrame');newer.contentWindow.postMessage=m=>{
    messages.push(m);
    if(m.type==='project-request')queueMicrotask(()=>window.dispatchEvent(new window.MessageEvent('message',{source:newer.contentWindow,origin:'https://awtrix.de',data:{ns:'awtrix',type:'project-result',requestId:m.requestId,ok:true,project,revision:3}})));
    if(m.type==='project-load')queueMicrotask(()=>window.dispatchEvent(new window.MessageEvent('message',{source:newer.contentWindow,origin:'https://awtrix.de',data:{ns:'awtrix',type:'project-load-result',requestId:m.requestId,ok:true}})));
  };
  window.dispatchEvent(new window.MessageEvent('message',{source:newer.contentWindow,origin:'https://awtrix.de',data:{ns:'awtrix',type:'ready'}}));
  window.document.querySelector('#piskelDraftList').value=records[0].id;
  [...window.document.querySelectorAll('button')].find(b=>b.textContent==='Open draft').click();
  await flush(30);
  assert(messages.some(m=>m.type==='project-load'&&m.project.name==='Renamed cloud'),'opening restores the full editable project');
}
async function testContentAndOrigins(){
  const {createHash}=require('node:crypto');
  const hash=value=>createHash('sha256').update(value).digest('hex');
  const {window,store}=await withGallery(ctx=>{
    ctx.store.files['/ICONS'].set('mail.gif',100);
    ctx.store.files['/ICONS'].set('private.gif',100);
  });
  for(const input of ['', 'abc', 'a'.repeat(55), 'b'.repeat(56), 'c'.repeat(64), 'pixels'.repeat(1000)])
    assert(window.iconSha256(new window.TextEncoder().encode(input))===hash(input),'SHA-256 agrees with independent implementation for '+input.length+' bytes');
  assert(store.iconOrigins.get('mail.gif')?.sha256===hash('GIF89a-mail'),'existing exact Hub copy is identified and recorded');
  assert(ownGrid(window).querySelector('[data-state=hub]')?.textContent==='From the Hub','Hub origin is visible');
  assert(ownGrid(window).querySelector('[data-state=local]')?.textContent==='Only on this device','private icon is clearly local');
  store.localIconBytes['mail.gif']='different pixels, unchanged filename and listed size';
  await goto(window,'#/apps');await goto(window,'#/icons');await flush(80);
  const changed=[...ownGrid(window).querySelectorAll('.tile')].find(t=>t.querySelector('.nm').textContent==='mail');
  assert(changed.querySelector('[data-state=modified]')?.textContent==='Locally changed','same-size edits are detected after page navigation');
  assert(!card(window).querySelector('.tile .acts button').disabled,'changed same-name icon is not labelled installed');
  assert(openMenu(changed)[2].textContent==='Publish as a variant','changed Hub copy offers publishing a variant');
  assert(!window.validIconOrigin({name:'../mail.gif',slug:'mail',hub:'https://awtrix.de/icons/',sha256:hash('x')}),'origin cannot escape icon folder');
  assert(!window.validIconOrigin({name:'mail.gif',slug:'mail',hub:'javascript:alert(1)',sha256:hash('x')}),'origin cannot create an executable link');
  assert(!window.validIconOrigin({name:'mail.gif',slug:'mail',hub:'https://user:secret@example.com/icons/',sha256:hash('x')}),'origin cannot include credentials');
  store.originFailure=true;
  const unavailable=await window.iconInventory();
  assert(unavailable.every(f=>f.state==='unknown'),'failed origin lookup never mislabels icons as purely local');
}
async function testConflictProtection(){
  const {window,store}=await withGallery(ctx=>{
    ctx.store.files['/ICONS'].set('mail.gif',100);
    ctx.store.localIconBytes['mail.gif']='private drawing';
  });
  const uploads=[];stubXhr(window,uploads,store);
  await search(window,'mail');card(window).querySelector('.tile .acts button').click();await flush(90);
  assert(uploads.length===0&&store.localIconBytes['mail.gif']==='private drawing','Hub install never overwrites different same-name contents implicitly');
  const replace=[...window.document.querySelectorAll('.toast button')].find(b=>b.textContent==='Replace with Hub original');
  assert(!!replace,'conflict gives an explicit replacement choice');
  replace.click();await flush(120);
  assert(uploads.length===1&&store.localIconBytes['mail.gif']==='GIF89a-mail','explicit replacement installs requested Hub original');
  assert(!!store.iconOrigins.get('mail.gif'),'replacement records origin');
  store.localIconBytes['mail.gif']='edited after script page opened';
  const count=await window.installScriptIcons(['mail'],new Set(['mail']));
  assert(count===0&&uploads.length===1,'script installer rechecks bytes despite stale installed-name set');
  assert(store.localIconBytes['mail.gif']==='edited after script page opened','script installation preserves local changes');
}
async function testResolvedPublication(){
  const {createHash}=require('node:crypto');
  const {window,store}=await withGallery(ctx=>{
    ctx.store.files['/ICONS'].set('own.gif',100);
    ctx.store.localIconBytes['own.gif']='changed drawing';
    ctx.store.iconOrigins.set('own.gif',{name:'own.gif',hub:'https://awtrix.de/icons/',slug:'mail',sha256:createHash('sha256').update('old drawing').digest('hex')});
    ctx.store.submitReply={ok:true,status:'existing',slug:'supermario',pr:'https://awtrix.de/icons/supermario'};
  });
  const tile=ownGrid(window).querySelector('.tile');openMenu(tile)[2].click();tile.querySelector('.ft button').click();await flush(130);
  assert(store.submitted[0].get('response')==='resolve','publisher opts into non-error duplicate resolution');
  assert(store.submitted[0].get('based_on')==='mail','variant carries known original to Hub');
  assert(store.iconOrigins.get('own.gif')?.slug==='supermario','duplicate result links local icon to existing public entry');
  assert(store.iconOrigins.get('own.gif')?.sha256===createHash('sha256').update('changed drawing').digest('hex'),'origin stores actual local bytes, not differently encoded Hub bytes');
  const updated=ownGrid(window).querySelector('.tile');openMenu(updated);
  assert([...updated.querySelectorAll('.tmenu a')].some(a=>a.textContent==='View on Hub'&&a.href.endsWith('/supermario')),'linked copy offers original instead of publishing again');
  assert([...window.document.querySelectorAll('.toast')].some(t=>t.textContent.includes('already in the gallery')),'existing publication is a friendly successful result');
}
async function testEditorProvenanceBridge(){
  const {window,store}=await withGallery();
  await goto(window,'#/editor');await flush(30);
  const frame=window.document.querySelector('#piskelFrame'),messages=[];
  frame.contentWindow.postMessage=message=>messages.push(message);
  const original={hub:'https://awtrix.de/icons/',slug:'mail',sha256:window.iconSha256(new window.TextEncoder().encode('original'))};
  const uploads=[];stubXhr(window,uploads,store);
  const send=(source,data)=>window.dispatchEvent(new window.MessageEvent('message',{source,origin:'https://awtrix.de',data:{ns:'awtrix',...data}}));
  const publication={type:'publish',requestId:'test-1',name:'draft',mime:'image/gif',dataBase64:window.btoa('GIF89a-new'),based_on:'mail'};
  send(window,publication);await flush(30);
  assert(store.submitted.length===0,'same-origin message from another window cannot publish');
  send(frame.contentWindow,{type:'ready'});await flush(20);
  assert(messages.some(m=>m.type==='config'&&m.publishViaParent===true),'device requests explicit publication broker');
  send(frame.contentWindow,{type:'save',name:'draft',mime:'image/gif',dataBase64:window.btoa('GIF89a-new'),origin:original});await flush(90);
  assert(store.submitted.length===0,'saving an editor draft never publishes it');
  assert(store.iconOrigins.get('draft.gif')?.sha256===original.sha256,'saving a variant retains original reference for change detection');
  send(frame.contentWindow,publication);await flush(90);
  const result=messages.find(m=>m.type==='publish-result'&&m.requestId==='test-1');
  assert(result?.ok===true,'explicit publication returns matching request result');
  assert(result?.origin.sha256===window.iconSha256(new window.TextEncoder().encode('GIF89a-new')),'editor receives hash of its actual exported draft');
  assert(store.iconOrigins.get('draft.gif')?.slug==='demo','saved published draft persists new public origin');
  assert(store.submitted[0]?.get('based_on')==='mail','editor publication retains variant ancestry');
  send(frame.contentWindow,{...publication,requestId:'numeric-name',name:'34334'});await flush(40);
  const invalid=messages.find(m=>m.type==='publish-result'&&m.requestId==='numeric-name');
  assert(invalid?.ok===false&&invalid?.error==='descriptiveNameRequired','editor broker rejects numeric publication names before contacting the Hub');
  assert(store.submitted.length===1,'numeric editor publication sends no upload');
  send(frame.contentWindow,{type:'save',name:'34334',mime:'image/gif',dataBase64:window.btoa('GIF89a-local')});await flush(70);
  assert(store.localIconBytes['34334.gif']==='GIF89a-local','local saving still accepts an old LaMetric filename');
}
async function testDescriptivePublicationName(){
  const {window,store}=await withGallery(ctx=>{
    ctx.store.files['/ICONS'].set('34334.gif',100);
    ctx.store.localIconBytes['34334.gif']='GIF89a-local-lametric';
  });
  const tile=ownGrid(window).querySelector('.tile');
  openMenu(tile)[2].click();
  const field=tile.querySelector('.ft input'),button=tile.querySelector('.ft button');
  assert(field.value==='','a LaMetric number is not prefilled as a publication name');
  assert(tile.querySelector('.ft').textContent.includes('LaMetric'),'numeric icon explains that a descriptive name is needed');
  for(const value of ['', '34334', ' 123 456 ', '123-456', '１２３']){
    field.value=value;button.click();await flush(20);
    assert(store.submitted.length===0,'publication rejects an empty or numeric-only name: '+JSON.stringify(value));
  }
  assert(field.getAttribute('aria-invalid')==='true'&&window.document.activeElement===field,'invalid publication name is marked and focused');
  field.value='Grüne Wolke 2';field.dispatchEvent(new window.Event('input',{bubbles:true}));
  button.click();await flush(100);
  assert(store.submitted.length===1&&store.submitted[0].get('name')==='Grüne Wolke 2','actively entered descriptive Unicode name is published');
  assert(store.files['/ICONS'].has('34334.gif'),'publishing under a meaningful name preserves the local filename and script references');
}
main();
