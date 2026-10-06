const { boot, goto, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  ✗ ' + msg); }
}

function context() {
  return { fillStyle: '', calls: [], fillRect(x, y, w, h) {
    this.calls.push({ x, y, w, h, color: this.fillStyle });
  } };
}

const system = { hostname: 'test', panelWidth: 32, panels: 1, panelWiring: 'rows',
  panelColorOrder: 'grb', pinMatrix: 32, pinBtnLeft: 26, pinBtnSelect: 27,
  pinBtnRight: 14 };
const audio = { rtttl: false, track: false, mp3: false, radio: false };

async function testFixedNativeDisplay() {
  const canvasContext = context();
  const pixels = Array(52 * 16).fill(0);
  pixels[pixels.length - 1] = 0xAABBCC;
  const { window } = await boot({ canvasContext, system,
    caps: { transitions: [], audio, gpio: null, platform: { id: 'linux' },
      display: { width: 52, height: 16, configurable: false } },
    screen: { width: 52, height: 16, pixels } });
  let cv = window.document.querySelector('#screen');
  assert(cv.width > 0 && cv.width / 52 === cv.height / 16, 'native preview keeps all 52×16 pixels');
  assert(cv.style.aspectRatio.replace(/\s/g, '') === '52/16', 'native preview uses the device aspect ratio');
  assert(canvasContext.calls.some(p => p.color === 'rgb(170,187,204)' && p.x === cv.width - cv.width / 52 && p.y === cv.height - cv.height / 16),
    'the bottom-right native pixel is painted at its own coordinate');
  await goto(window, '#/system');
  assert(!window.document.querySelector('#sec-gpio'), 'fixed GPIO has no ESP pin controls');
  const panel = window.document.querySelector('#sec-panel');
  assert(panel && panel.textContent.includes('52 × 16'), 'fixed panel shows its native dimensions');
  assert(panel && !panel.querySelector('input,select'), 'fixed panel has no editable ESP wiring fields');
  await goto(window, '#/');
  cv = window.document.querySelector('#screen');
  assert(cv.width > 0 && cv.width / 52 === cv.height / 16, 'navigation retains native dimensions');
  window.close();
}

async function testLegacyControlsAndGeometry() {
  const { window } = await boot({ canvasContext: context(), system,
    caps: { transitions: [], audio }, screen: { pixels: Array(256).fill(0) } });
  const cv = window.document.querySelector('#screen');
  assert(cv.width > 0 && cv.width / 32 === cv.height / 8, 'legacy screen still defaults to 32×8');
  await goto(window, '#/system');
  assert(!!window.document.querySelector('#sec-gpio'), 'omitted GPIO capabilities preserve legacy controls');
  assert(!!window.document.querySelector('#sec-panel input'), 'legacy panel geometry remains editable');
  window.close();
}

async function testFrameDimensionsOverrideCapabilities() {
  const { window } = await boot({ canvasContext: context(),
    caps: { transitions: [], audio, display: { width: 52, height: 16, configurable: false } },
    screen: { width: 64, height: 8, pixels: Array(512).fill(0) } });
  const cv = window.document.querySelector('#screen');
  assert(cv.width > 0 && cv.width / 64 === cv.height / 8, 'screen response dimensions are authoritative for its frame');
  window.close();
}

async function testNativeEditorAndImport() {
  const {window}=await boot({canvasContext:context(),system,caps:{transitions:[],audio,
    display:{width:52,height:16,configurable:false}}});
  await goto(window,'#/editor');
  const frame=window.document.querySelector('#piskelFrame');
  assert(new URL(frame.src).searchParams.get('sizes').includes('52x16'),'editor offers native panel size');
  const messages=[];frame.contentWindow.postMessage=m=>messages.push(m);
  window.dispatchEvent(new window.MessageEvent('message',{source:frame.contentWindow,
    origin:'https://awtrix.de',data:{ns:'awtrix',type:'ready'}}));
  assert(messages.some(m=>m.type==='config'&&m.sizes.includes('52x16')),'editor handshake carries native size');
  assert(new URL(frame.src).searchParams.get('max')==='52x16'&&messages.some(m=>m.type==='config'&&m.max==='52x16'),
    'editor allows any size up to the panel');
  assert(messages.some(m=>m.type==='config'&&m.protocol===2),'the host announces Live protocol 2');
  const send=data=>window.dispatchEvent(new window.MessageEvent('message',{source:frame.contentWindow,
    origin:'https://awtrix.de',data:{ns:'awtrix',...data}}));
  const tr=window.eval('t');
  const toasts=message=>[...window.document.querySelectorAll('.toast')].filter(t=>t.textContent===message);
  const SIZE=tr('edLiveSize')+' '+tr('edLiveKeep');
  const COLORS=tr('edLiveColors')+' '+tr('edLiveKeep');
  const STILL=tr('edLiveStill');
  send({type:'live-too-large',w:128,h:32,reason:'size'});await flush(5);
  send({type:'live-too-large',w:128,h:32,reason:'size'});await flush(5);
  assert(toasts(SIZE).length===1,'too many pixels is reported once, as such');
  send({type:'live-too-large',w:52,h:16,reason:'colors'});await flush(5);
  assert(toasts(COLORS).length===1,'too many colours is reported as such');
  send({type:'live-off'});await flush(20);
  send({type:'live-too-large',w:52,h:16,reason:'colors'});await flush(5);
  assert(toasts(COLORS).length===2,'after Live off and on again the same reason is reported again');
  send({type:'live-off'});await flush(20);
  const bitmap={type:'live',mode:'bitmap',w:52,h:16,dataBase64:'AAAA'};
  send({...bitmap,still:true});await flush(20);send({...bitmap,still:true});await flush(20);
  assert(toasts(STILL).length===1,'a still frame for an animation is told once per Live session');
  send({...bitmap});await flush(20);
  assert(toasts(STILL).length===1,'a plain frame says nothing');
  send({type:'live-off'});await flush(20);
  send({...bitmap,still:true});await flush(20);
  assert(toasts(STILL).length===2,'a new Live session tells it again');
  send({type:'live-off'});await flush(20);
  let liveBody=null;const originalFetch=window.fetch;
  window.fetch=async(url,opts)=>{
    if(url==='/api/v1/notifications'){liveBody=JSON.parse(opts.body);return {ok:true,status:200,text:async()=>'{}'};}
    return originalFetch(url,opts);
  };
  send({type:'live',mode:'gif',mime:'image/gif',dataBase64:'R0lGODlh'});await flush(20);
  assert(liveBody?.icon==='data:image/gif;base64,R0lGODlh','a Live animation is sent as a GIF data URL');
  window.fetch=originalFetch;
  send({type:'live-off'});await flush(20);
  window.createImageBitmap=async()=>({width:52,height:16,close(){}});
  window.imgToGif=async()=>new window.Blob(['GIF'],{type:'image/gif'});
  const result=await window.iconAsGif(new window.Blob(['png'],{type:'image/png'}),'panel.png');
  assert(result.name==='panel.gif','native 52x16 PNG is converted to GIF');
  window.createImageBitmap=async()=>({width:53,height:16,close(){}});
  let rejected=false;try{await window.iconAsGif(new window.Blob(['png'],{type:'image/png'}),'large.png');}
  catch(e){rejected=e.message.includes('52 × 16');}
  assert(rejected,'oversize input is rejected before the original PNG can be uploaded');
  window.close();
}

async function testPendingWidth() {
  const {window}=await boot({canvasContext:context(),system:{...system,panelWidth:8,panels:8},caps:{transitions:[],audio,
    display:{width:32,height:8,configurable:true,minWidth:32,maxWidth:128,minHeight:8,maxHeight:8,maxPixels:1024,
      requestedWidth:64,requestedHeight:8,restartRequired:true}}});
  await goto(window,'#/system');
  const panel=window.document.querySelector('#sec-panel');
  assert(panel.textContent.includes('64 × 8 = 512'),'panel summary shows the configured width at 8 rows');
  assert(panel.textContent.includes('8 panels of 8 × 8'),'panel summary names the chained panels');
  assert(panel.textContent.includes('32 × 8'),'pending panel size keeps active dimensions visible');
  assert(!panel.querySelector('[name="panelHeight"]')&&!panel.textContent.includes('Panel height'),'no panel height field');
  window.close();
}

async function main() {
  await testFixedNativeDisplay();
  await testLegacyControlsAndGeometry();
  await testFrameDimensionsOverrideCapabilities();
  await testNativeEditorAndImport();
  await testPendingWidth();
  await flush(20);
  console.log(`display-profile: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main();
