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
const download = window => window.document.getElementById('upd-dl');

async function openSystem(opts) {
  const ctx = await boot();
  ctx.window.localStorage.clear();
  ctx.store.githubLatest = opts.latest;
  if (opts.device) Object.assign(ctx.store.device, opts.device);
  await goto(ctx.window, '#/system');
  await flush(60);
  return ctx;
}

async function testNewerReleaseOffersTheMatchingFile() {
  const { window, netlog } = await openSystem({ latest: release('v1.1.2'), device: { updateImage: 'firmware-awtrix-ng-s3-quad.bin' } });
  assert(githubCalls(netlog) === 1, 'opening the System page asks GitHub once');
  assert(status(window) && /1\.1\.2/.test(status(window).textContent), 'the row names the newer version');
  const a = download(window);
  assert(a && !a.hidden, 'a download link is offered');
  assert(a && a.href.endsWith('/v1.1.2/firmware-awtrix-ng-s3-quad.bin'), 'the link picks the file from updateImage');
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
  assert(download(window) && download(window).hidden, 'no download link without an update');
  window.close();
}

async function testPrereleaseDoesNotCount() {
  const { window } = await openSystem({ latest: release('v1.2.0', { prerelease: true }) });
  assert(download(window) && download(window).hidden, 'a pre-release is not offered');
  window.close();
}

async function testCheckIsCachedAcrossVisits() {
  const { window, netlog } = await openSystem({ latest: release('v1.1.2') });
  await goto(window, '#/');
  await goto(window, '#/system');
  await flush(60);
  assert(githubCalls(netlog) === 1, 'a second visit reuses the cached answer');
  const btn = window.document.querySelector('button[aria-label="Check for updates"]');
  assert(!!btn, 'the row has a check button');
  if (btn) { btn.click(); await flush(60); }
  assert(githubCalls(netlog) === 2, 'the button asks GitHub again');
  window.close();
}

async function testUnreachableGithubIsReportedInTheRow() {
  const ctx = await boot();
  ctx.window.localStorage.clear();
  const realFetch = ctx.window.fetch;
  ctx.window.fetch = async (input, opts) => {
    const url = typeof input === 'string' ? input : input.url;
    if (url.includes('api.github.com')) throw new TypeError('Failed to fetch');
    return realFetch(input, opts);
  };
  await goto(ctx.window, '#/system');
  await flush(60);
  const s = status(ctx.window);
  assert(s && s.textContent.length > 0 && !/1\.1\.2/.test(s.textContent), 'the row says GitHub could not be reached');
  assert(download(ctx.window) && download(ctx.window).hidden, 'nothing is offered when the check failed');
  ctx.window.close();
}

async function testOfflineSkipsTheCheck() {
  const ctx = await boot();
  ctx.window.localStorage.clear();
  ctx.store.githubLatest = release('v1.1.2');
  Object.defineProperty(ctx.window.navigator, 'onLine', { value: false, configurable: true });
  await goto(ctx.window, '#/system');
  await flush(60);
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
    assert(ctx.window.document.getElementById('fw-status').textContent.length>0,'download failure is explained');
    ctx.window.close();
  }
}

async function testRejectedFirmwareAllowsRetry(){
  const ctx=await firmwareCase({},400);
  assert(!ctx.btn.disabled,'device rejection allows a retry');
  assert(/wrongChip/.test(ctx.window.document.getElementById('fw-status').textContent),'device rejection is shown');
  ctx.window.close();
}

async function main() {
  await testBrowserFirmwareInstall();
  await testBadFirmwareNeverUploads();
  await testRejectedFirmwareAllowsRetry();
  await testNewerReleaseOffersTheMatchingFile();
  await testSameVersionIsUpToDate();
  await testPrereleaseDoesNotCount();
  await testCheckIsCachedAcrossVisits();
  await testUnreachableGithubIsReportedInTheRow();
  await testOfflineSkipsTheCheck();
  await flush(20);
  console.log(`update-check: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
