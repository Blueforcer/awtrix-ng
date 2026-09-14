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

function ownGrid(window) {
  return window.document.querySelector('.grid-icons');
}
function segments(window) {
  return [...window.document.querySelectorAll('.segbar button')];
}
// The icon actions stay behind the one menu button shown on each tile.
function openMenu(tile) {
  tile.querySelector('.acts button').click();
  return [...tile.querySelectorAll('.tmenu button')];
}
async function withGallery(extra) {
  const ctx = await boot();
  ctx.window.localStorage.awtrixHubToken = 'test-hub-token';
  ctx.store.iconDb = { v: 1, icons: CATALOGUE };
  for (const row of CATALOGUE) ctx.store.iconBytes[row[0]] = 'GIF89a-' + row[0];
  if (extra) extra(ctx);
  await goto(ctx.window, '#/icons');
  await flush(60);
  return ctx;
}

async function testSubmit() {
  const { window, store } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
  });
  const grid = ownGrid(window);
  const tile = grid.querySelector('.tile');
  const actions = openMenu(tile);
  assert(actions.length === 3, 'the menu offers edit, publish and delete');
  assert(!/Copy for script/.test(tile.textContent), 'installed icons no longer offer the script-copy action');

  actions[1].click();
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
    ctx.window.localStorage.removeItem('awtrixHubToken');
    ctx.store.files['/ICONS'].set('own.gif', 240);
  });

  const submit = () => {
    const tile = ownGrid(window).querySelector('.tile');
    openMenu(tile)[1].click();
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
  openMenu(tile)[1].click();
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
  openMenu(tile)[1].click();
  tile.querySelector('.ft button').click();
  await flush(80);

  const toast = [...window.document.querySelectorAll('.toast')].map(t => t.textContent).join(' ');
  assert(/could not be published/.test(toast) && !/somethingNew|idbe_|HTTP/.test(toast),
    'an unknown code gets a helpful message without internal codes');
}

async function testDuplicate() {
  const { window, store } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
    ctx.store.submitReply = { ok: false, error: 'duplicate', slug: 'mail' };
  });
  const grid = ownGrid(window);
  const tile = grid.querySelector('.tile');
  openMenu(tile)[1].click();
  tile.querySelector('.ft button').click();
  await flush(80);

  const toast = [...window.document.querySelectorAll('.toast')].map(t => t.textContent).join(' ');
  assert(/mail/.test(toast), 'a duplicate names the icon that already holds the content');
  assert(!!tile.querySelector('.ft input'),
    'a rejected submission keeps the row open so the name can be changed');
}

/* The page only manages local icons. New icons come from a file, the editor,
   or the full Hub website; there is no second catalogue embedded here. */
async function testSegments() {
  const { window } = await withGallery(ctx => {
    ctx.store.files['/ICONS'].set('own.gif', 240);
    ctx.store.files['/ICONS'].set('two.gif', 120);
  });
  const segs = segments(window);
  assert(segs.length === 2, 'two ways in: icons on the clock and adding one');
  assert(/\(2\)/.test(segs[0].textContent),
    'the first segment counts what is on the clock (got "' + segs[0].textContent + '")');

  // The card leads with the header row and the segment bar, then one pane each.
  const paneOf = i => segs[i].closest('.card').querySelectorAll(':scope > div')[i + 2];
  assert(segs[0].classList.contains('on') && paneOf(0).hidden === false,
    'the icons on the clock are what the page opens on');
  assert(paneOf(1).hidden, 'the add panel starts out of the way');

  segs[1].click();
  assert(paneOf(0).hidden && !paneOf(1).hidden, 'picking Add swaps the pane');
  assert(!segs[0].classList.contains('on') && segs[1].classList.contains('on'),
    'exactly one segment reads as current');
  const addPane = paneOf(1);
  assert(!!addPane.querySelector('.drop'), 'Add contains local file upload');
  assert(!addPane.querySelector('.lam') && !/LaMetric/.test(addPane.textContent), 'the LaMetric downloader is gone');
  const hubLink = addPane.querySelector('.hub-source a');
  assert(hubLink?.href === 'https://awtrix.de/icons/' && hubLink.target === '_blank',
    'Add links to the full AWTRIX Hub icon gallery');
  assert(!window.document.querySelector('.idb'), 'the embedded Hub gallery is gone');
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
  const add = window.document.getElementById(tabs[1].getAttribute('aria-controls'));
  assert(!add.hidden && tabs[1].getAttribute('aria-selected') === 'true', 'End opens the Add panel');
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
  await testSegments();
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
  assert(ownGrid(window).querySelector('[data-state=local]')?.textContent==='Only on AWTRIX','private icon is clearly local');
  store.localIconBytes['mail.gif']='different pixels, unchanged filename and listed size';
  await goto(window,'#/apps');await goto(window,'#/icons');await flush(80);
  const changed=[...ownGrid(window).querySelectorAll('.tile')].find(t=>t.querySelector('.nm').textContent==='mail');
  assert(changed.querySelector('[data-state=modified]')?.textContent==='Changed on AWTRIX','same-size edits are detected after page navigation');
  assert(openMenu(changed)[1].textContent==='Share as a new icon','changed Hub copy offers publishing a variant');
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
  let conflict=false;try{await window.idbInstall('mail');}catch(e){conflict=e.code==='iconConflict';}
  assert(conflict&&uploads.length===0&&store.localIconBytes['mail.gif']==='private drawing','Hub install never overwrites different same-name contents implicitly');
  await window.idbInstall('mail',{replace:true});
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
  const tile=ownGrid(window).querySelector('.tile');openMenu(tile)[1].click();tile.querySelector('.ft button').click();await flush(130);
  assert(store.submitted[0].get('response')==='resolve','publisher opts into non-error duplicate resolution');
  assert(store.submitted[0].get('based_on')==='mail','variant carries known original to Hub');
  assert(store.iconOrigins.get('own.gif')?.slug==='supermario','duplicate result links local icon to existing public entry');
  assert(store.iconOrigins.get('own.gif')?.sha256===createHash('sha256').update('changed drawing').digest('hex'),'origin stores actual local bytes, not differently encoded Hub bytes');
  const updated=ownGrid(window).querySelector('.tile');openMenu(updated);
  assert([...updated.querySelectorAll('.tmenu a')].some(a=>a.textContent==='View on Hub'&&a.href.endsWith('/supermario')),'linked copy offers original instead of publishing again');
  assert([...window.document.querySelectorAll('.toast')].some(t=>t.textContent.includes('already on the Hub')),'existing publication is a friendly successful result');
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
  openMenu(tile)[1].click();
  const field=tile.querySelector('.ft input'),button=tile.querySelector('.ft button');
  assert(field.value==='','a LaMetric number is not prefilled as a publication name');
  assert(tile.querySelector('.ft').textContent.includes('Numbers alone'),'numeric icon explains that a descriptive name is needed');
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
