const { boot, goto, flush, stubXhr } = require('./harness');
const { createHash } = require('node:crypto');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  ✗ ' + msg); }
}

const release = (tag, extra = {}) => ({
  tag_name: tag, draft: false, prerelease: false, published_at: '2026-09-01T10:00:00Z',
  html_url: 'https://github.com/Blueforcer/awtrix-ng/releases/tag/' + tag,
  assets: ['firmware-awtrix-ng.bin', 'firmware-awtrix-ng-s3-octal.bin', 'firmware-awtrix-ng-s3-quad.bin']
    .map(name => ({ name, browser_download_url: 'https://github.com/Blueforcer/awtrix-ng/releases/download/' + tag + '/' + name })),
  ...extra,
});

const githubCalls = netlog => netlog.filter(l => l.includes('api.github.com')).length;
const status = window => window.document.getElementById('upd-status');
const install = window => window.document.getElementById('upd-install');

async function openSystem(opts) {
  const ctx = await boot();
  ctx.window.localStorage.clear();
  ctx.store.githubLatest = opts.latest;
  if (opts.device) Object.assign(ctx.store.device, opts.device);
  await goto(ctx.window, '#/system');
  await flush(60);
  assert(githubCalls(ctx.netlog)===0,'opening the page never checks automatically');
  ctx.window.document.querySelector('button[aria-label="Check for updates"]').click();
  await flush(60);
  return ctx;
}

async function testNewerReleaseOffersTheMatchingFile() {
  const { window, netlog } = await openSystem({ latest: release('v1.1.2'), device: { updateImage: 'firmware-awtrix-ng-s3-quad.bin' } });
  assert(githubCalls(netlog) === 1, 'clicking Check for updates asks GitHub once');
  assert(status(window) && /1\.1\.2/.test(status(window).textContent), 'the row names the newer version');
  assert(!install(window).disabled&&install(window).textContent==='Download & install', 'successful check changes the same button into install');
  assert(!window.document.getElementById('upd-dl'), 'no separate download link');
  const notes=window.document.getElementById('upd-notes');
  assert(notes.querySelector('svg use')?.getAttribute('href')==='#i-file' && notes.getAttribute('aria-label')==='Release notes', 'release notes use the existing accessible document icon');
  assert(notes.href.endsWith('/v1.1.2'),'document icon links to the matching release');
  assert(notes.nextElementSibling===install(window),'release notes sit directly left of the install button');
  await goto(window, '#/');
  await flush(60);
  const meta = window.document.querySelector('.meta');
  assert(meta && /1\.1\.2/.test(meta.textContent), 'the dashboard mentions the available version');
  window.close();
}

async function testSameVersionIsUpToDate() {
  const { window } = await openSystem({ latest: release('v1.1.1') });
  assert(status(window) && /1\.1\.1/.test(status(window).textContent) && !/1\.1\.2/.test(status(window).textContent),
    'the row reports the running version as current');
  assert(install(window).textContent==='Check for updates'&&!install(window).disabled, 'current firmware keeps the check action');
  window.close();
}

async function testPrereleaseDoesNotCount() {
  const { window } = await openSystem({ latest: release('v1.2.0', { prerelease: true }) });
  assert(install(window).textContent==='Check for updates', 'a pre-release does not become an install action');
  window.close();
}

async function testCheckIsCachedAcrossVisits() {
  const { window, netlog } = await openSystem({ latest: release('v1.1.2') });
  await goto(window, '#/');
  await goto(window, '#/system');
  await flush(60);
  assert(githubCalls(netlog) === 1, 'a second visit reuses the cached answer');
  assert(install(window).textContent==='Download & install','cached update keeps the install action');
  assert(install(window).closest('.ctl').querySelectorAll('button').length===1,'only one update button is shown');
  window.close();
}

async function testUnreachableGithubShowsNotification() {
  const ctx = await boot();
  ctx.window.localStorage.clear();
  const realFetch = ctx.window.fetch;
  ctx.window.fetch = async (input, opts) => {
    const url = typeof input === 'string' ? input : input.url;
    if (url.includes('api.github.com')) throw new TypeError('Failed to fetch');
    return realFetch(input, opts);
  };
  await goto(ctx.window, '#/system');
  ctx.window.document.querySelector('button[aria-label="Check for updates"]').click();
  await flush(60);
  const s = status(ctx.window);
  assert(s && s.textContent==='Version 1.1.1', 'the row keeps only the running version after a failed check');
  assert(ctx.window.document.querySelector('.toast.err')?.textContent.includes('Could not check'), 'failed check uses an error notification');
  assert(install(ctx.window).textContent==='Check for updates'&&!install(ctx.window).disabled, 'failed check can be retried');
  ctx.window.close();
}

async function testRateLimitHasSpecificNotification(){
  const ctx=await boot();ctx.window.localStorage.clear();
  const previous=ctx.window.fetch;
  ctx.window.fetch=async (url,opts)=>String(url).includes('api.github.com')
    ? {ok:false,status:403,headers:{get:()=> '0'}} : previous(url,opts);
  await goto(ctx.window,'#/system');
  ctx.window.document.querySelector('button[aria-label="Check for updates"]').click();await flush(60);
  assert(ctx.window.document.querySelector('.toast.err')?.textContent.includes('anonymous requests'),'rate limit is explained without blaming connectivity');
  assert(status(ctx.window).textContent==='Version 1.1.1','rate limit leaves a neutral version row');
  ctx.window.close();
}

async function testOfflineSkipsTheCheck() {
  const ctx = await boot();
  ctx.window.localStorage.clear();
  ctx.store.githubLatest = release('v1.1.2');
  Object.defineProperty(ctx.window.navigator, 'onLine', { value: false, configurable: true });
  await goto(ctx.window, '#/system');
  await flush(60);
  ctx.window.document.querySelector('button[aria-label="Check for updates"]').click();await flush(60);
  assert(githubCalls(ctx.netlog) === 0, 'no request leaves the browser while offline');
  ctx.window.close();
}

async function firmwareCase(change={},xhrStatus=200){
  const ctx=await openSystem({latest:release('v1.1.2')});
  const bytes=new Uint8Array(512);bytes[0]=0xe9;bytes[100]=42;
  const image=ctx.store.device.updateImage;
  const asset={size:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};
  const manifest={version:'v1.1.2',assets:{[image]:asset}};
  if(change.version)manifest.version=change.version;
  if(change.hash)asset.sha256='0'.repeat(64);
  if(change.size)asset.size=change.size;
  const fetched=[],uploads=[];
  const previous=ctx.window.fetch;
  ctx.window.fetch=async (url,opts)=>{
    if(!String(url).includes('/firmware/ota/'))return previous(url,opts);
    fetched.push({url,opts});
    if(change.network)throw new TypeError('Failed to fetch');
    if(url.endsWith('index.json'))return {ok:true,json:async()=>manifest};
    let sent=false;
    return {ok:true,body:{getReader:()=>({read:async()=>{
      if(sent)return {done:true};sent=true;
      return {done:false,value:change.truncated?bytes.slice(0,100):bytes};
    },cancel:async()=>{}})}};
  };
  stubXhr(ctx.window,uploads);
  if(xhrStatus!==200){
    const Base=ctx.window.XMLHttpRequest;
    ctx.window.XMLHttpRequest=class extends Base {constructor(){super();this.status=xhrStatus;this.responseText='{"error":"wrongChip"}';}};
  }
  const btn=ctx.window.document.getElementById('upd-install');
  assert(btn&&!btn.hidden,'matching release offers direct installation');
  btn.click();await flush(10);
  assert(fetched.length===0&&uploads.length===0,'first click asks for confirmation without downloading or flashing');
  btn.click();await flush(100);
  return {...ctx,fetched,uploads,btn};
}

async function testBrowserFirmwareInstall(){
  const ctx=await firmwareCase();
  assert(ctx.fetched.length===2,'browser downloads manifest and image');
  assert(ctx.fetched[1].url.endsWith('/v1.1.2/firmware-awtrix-ng.bin'),'download is pinned to the requested version and board');
  assert(ctx.fetched.every(r=>r.opts.credentials==='omit'),'device credentials are not sent to the download host');
  assert(ctx.uploads.length===1&&ctx.uploads[0].url==='/update','verified image uses the existing OTA endpoint');
  assert(ctx.uploads[0].files[0].name==='firmware-awtrix-ng.bin','OTA upload carries the matching filename');
  assert(/Rebooting/.test(ctx.window.document.getElementById('fw-status').textContent),'successful upload reports restart');
  assert(ctx.btn.disabled,'another update is blocked while restarting');
  ctx.window.close();
}

async function testBadFirmwareNeverUploads(){
  for(const change of [{version:'v1.1.1'},{hash:true},{size:511},{truncated:true},{network:true}]){
    const ctx=await firmwareCase(change);
    assert(ctx.uploads.length===0,'mismatched, corrupted, truncated or unavailable firmware is never uploaded');
    assert(!ctx.btn.disabled,'failed download allows a retry');
    assert(ctx.window.document.querySelector('.toast.err')?.textContent.length>0,'download failure uses an error notification');
    assert(ctx.window.document.getElementById('fw-status').textContent==='','failure leaves no inline error');
    ctx.window.close();
  }
}

async function testRejectedFirmwareAllowsRetry(){
  const ctx=await firmwareCase({},400);
  assert(!ctx.btn.disabled,'device rejection allows a retry');
  assert(/wrongChip/.test(ctx.window.document.querySelector('.toast.err')?.textContent),'device rejection is shown');
  ctx.window.close();
}

async function testNoAutomaticCheckAfterNavigation(){
  const ctx=await boot();ctx.window.localStorage.clear();
  for(const hash of ['#/system','#/','#/system'])await goto(ctx.window,hash);
  assert(githubCalls(ctx.netlog)===0,'page visits and navigation never initiate a release check');
  assert(status(ctx.window).textContent==='Version 1.1.1','unchecked row shows the installed version');
  assert(!ctx.window.document.querySelector('.toast.err'),'no automatic update error notifications');
  ctx.window.close();
}

async function main() {
  await testNoAutomaticCheckAfterNavigation();
  await testBrowserFirmwareInstall();
  await testBadFirmwareNeverUploads();
  await testRejectedFirmwareAllowsRetry();
  await testNewerReleaseOffersTheMatchingFile();
  await testSameVersionIsUpToDate();
  await testPrereleaseDoesNotCount();
  await testCheckIsCachedAcrossVisits();
  await testUnreachableGithubShowsNotification();
  await testOfflineSkipsTheCheck();
  await testRateLimitHasSpecificNotification();
  await flush(20);
  console.log(`update-check: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
