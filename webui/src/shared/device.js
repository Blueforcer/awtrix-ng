const SOUND_KEYS=Object.keys(SYSF).filter(k=>SYSF[k].api==='settings');
const S={transitions:[],stats:null,ap:false,sysHost:'',sysUid:'',gpio:null,aud:null,
  scriptsOff:null,display:null,gpioKnown:false,light:null,caps:null};
function readAudioCaps(caps){
  S.caps=caps;
  const a=caps.audio||{};
  S.aud={rtttl:a.rtttl===true,track:a.track===true,mp3:a.mp3===true,radio:a.radio===true};
  S.microphone=caps.microphone===true;
  //linux:begin
  S.gamepad=caps.gamepad===true&&caps.ble===true;
  S.voice=caps.voice===true;
  S.bootSound=caps.bootSound===true;
  S.enlargeApps=caps.enlargeApps===true;
  S.iphone=caps.ble===true;
  S.mqttTls=caps.mqttTls===true;
  S.oauth=caps.oauth===true;
  if(S.oauth)loadOauthApps();
  //linux:end
  if(typeof caps.sensors?.light==='boolean')S.light=caps.sensors.light;
  if(Object.hasOwn(caps,'gpio')){
    S.gpio=caps.gpio;S.gpioKnown=true;
    if(S.gpio)applyGpioCaps(S.gpio);
  }
  const d=caps.display;
  if(d&&Number.isInteger(d.width)&&d.width>0&&Number.isInteger(d.height)&&d.height>0)
    S.display={...d,width:d.width,height:d.height,configurable:d.configurable!==false};
}
function displayGeometry(data){
  const d=data||{},fallback=S.display||{width:32,height:8};
  return {width:Number.isInteger(d.width)&&d.width>0?d.width:fallback.width,
    height:Number.isInteger(d.height)&&d.height>0?d.height:fallback.height};
}
function editorSizes(){
  const {width:w,height:h}=displayGeometry();
  return [...new Set([[8,8],[16,16],[32,8],[w,h]]
    .filter(([x,y])=>x<=w&&y<=h).map(([x,y])=>x+'x'+y))];
}
function editorMax(){const {width,height}=displayGeometry();return width+'x'+height;}
// The names a script can ask for with @needs, each a boolean in /api/v1/capabilities. The same
// list is in test/fixtures/script_header_vectors.json; script-header.test.js keeps both equal.
const CAP_NAMES=['audio.mp3','audio.rtttl','audio.song','audio.speech','audio.track','audio.radio','audio.effect','microphone','sensors.light','ble','gamepad','oauth','crypto','tcp'];
//linux:begin
CAP_NAMES.push('layout');
//linux:end
const CAP_WORDS={gamepad:'capGamepad',ble:'capBle','audio.effect':'capEffect','audio.song':'capSong',microphone:'capMic','audio.speech':'capSpeech','audio.mp3':'capMp3',
  'audio.radio':'capRadio','audio.rtttl':'capRtttl','audio.track':'capTrack','sensors.light':'capLight',oauth:'capOauth',crypto:'capCrypto',tcp:'capTcp'};
function capPresent(name){
  let v=S.caps;
  for(const k of String(name).split('.'))v=v&&typeof v==='object'?v[k]:undefined;
  return v===true;
}
const capWords=name=>CAP_WORDS[name]?t(CAP_WORDS[name]):name;
const panelWords=(w,h)=>t('capPanel').replace('{w}',w).replace('{h}',h);
// What a script asks for that this AWTRIX lacks, in words; '' when it fits or before the
// capabilities have arrived. For a script that is not installed yet, e.g. a Hub release.
function fitWords(needs,display){
  if(!S.caps)return '';
  const words=(Array.isArray(needs)?needs:[]).filter(n=>!capPresent(n)).map(capWords);
  const g=displayGeometry();
  if(display&&(display.width>g.width||display.height>g.height))words.push(panelWords(display.width,display.height));
  return words.join(', ');
}
// The same for an installed script, from what the device itself reported in /api/v1/apps.
function metaFitWords(m){
  const words=(Array.isArray(m?.needs)?m.needs:[]).filter(n=>n&&n.missing===true).map(n=>capWords(n.name));
  if(m?.display&&m.display.fits===false)words.push(panelWords(m.display.width,m.display.height));
  return words.join(', ');
}
const fitBadge=words=>words?el('div',{class:'badge warn'},t('fitNeeds').replace('{w}',words)):null;
// A Hub page for this device: the Hub filters by what the device reports, no product name.
function hubDeviceUrl(path){
  const url=new URL(path,iconDbUrl());
  if(S.caps){
    const g=displayGeometry(),id=S.caps.platform&&S.caps.platform.id;
    if(typeof id==='string'&&id)url.searchParams.set('device',id);
    url.searchParams.set('panel',g.width+'x'+g.height);
    url.searchParams.set('caps',CAP_NAMES.filter(capPresent).join(','));
  }
  return url.href;
}
const hubLink=(path,label,cls)=>{
  const a=el('a',{class:cls,href:hubDeviceUrl(path),target:'_blank',rel:'noopener'},label);
  a.addEventListener('click',()=>{a.href=hubDeviceUrl(path);});
  return a;
};
// Unknown counts as present, so nothing is hidden before the capabilities have arrived.
const hasSink=(...k)=>!S.aud||k.some(x=>S.aud[x]);
const anyAudioCap=()=>hasSink('rtttl','track','mp3','radio');
const hasLight=()=>S.light!==false;
const LIGHT_KEYS=['minBrightness','maxBrightness','ldrFactor','ldrGamma','ldrOnGround','brightnessSmoothing'];
async function scriptsOff(){
  if(S.scriptsOff===null){
    try{S.scriptsOff=(await api('/api/v1/device',{timeout:4000})).data.scriptingRunning===false;}
    catch(e){S.scriptsOff=false;}
  }
  return S.scriptsOff;
}
function applyGpioCaps(c){
  const dflt=c.defaults||{};
  const gp=p=>'GPIO '+p;
  const inR=(rs,p)=>(rs||[]).some(r=>p>=r[0]&&p<=r[1]);
  const usable=[];
  for(let p=0;p<=c.max;p++)
    if(!inR(c.missing,p)&&!(c.reserved||[]).some(r=>p>=r.lo&&p<=r.hi))usable.push(p);
  const drive=usable.filter(p=>!inR(c.inputOnly,p));
  const adc=usable.filter(p=>inR(c.adc1,p));
  const opts=(k,pool,off)=>{
    const o=pool.map(p=>[p,p===dflt[k]?gp(p)+' (default)':gp(p)]);
    SYSF[k].w='select';SYSF[k].fmt=gp;
    SYSF[k].opt=off?[[-1,'Not connected']].concat(o):o;
  };
  opts('pinMatrix',c.matrix||[],false);
  ['pinBattery','pinLdr'].forEach(k=>opts(k,adc,true));
  ['pinBtnLeft','pinBtnSelect','pinBtnRight','pinBuzzer','pinI2cSda','pinI2cScl','pinDfTx',
   'pinI2sBclk','pinI2sLrclk','pinI2sDout','pinI2sMclk','pinAmpEnable'].forEach(k=>opts(k,drive,true));
  opts('pinDfRx',usable,true);
  const i2s=dflt.pinI2sBclk>=0?'gpio':'hidden';
  ['pinI2sBclk','pinI2sLrclk','pinI2sDout','pinI2sMclk','pinAmpEnable'].forEach(k=>{SYSF[k].g=i2s;});
}
const PINKEYS=['pinMatrix','pinBtnLeft','pinBtnSelect','pinBtnRight','pinBattery','pinLdr',
  'pinBuzzer','pinI2cSda','pinI2cScl','pinDfRx','pinDfTx','pinI2sBclk','pinI2sLrclk','pinI2sDout',
  'pinI2sMclk','pinAmpEnable'];
const REBOOT_KEYS=new Set(['wifiSsid','wifiPass','netStatic','ip','gateway','subnet','dns1','dns2',
  'wifiConnectTimeout','hostname','webPort','mqttEnabled','mqttHost','mqttPort','mqttUser',
  'mqttPass','mqttPrefix','statsInterval','dfplayer','scriptingEnabled'].concat(PINKEYS));
//linux:begin
REBOOT_KEYS.add('mqttTls');
//linux:end
function paintPinUse(fields){
  const by=new Map();
  PINKEYS.forEach(k=>{
    const f=fields[k];if(!f)return;
    const v=f.get();if(v>=0&&!by.has(v))by.set(v,SYSF[k].l);
  });
  PINKEYS.forEach(k=>{
    const f=fields[k],sel=f&&f.row.querySelector('select');if(!sel)return;
    const mine=f.get();
    for(const o of sel.options){
      const v=Number(o.value),who=by.get(v);
      o.textContent=o.textContent.split(' · ')[0]+
        (v>=0&&who&&v!==mine?' · '+t('pinUsed')+': '+who:'');
    }
  });
}
function paintWakePin(fields,out){
  const f=fields.pinBtnSelect,rtc=S.gpio&&S.gpio.rtc;
  if(!f||!rtc)return;
  const p=f.get();
  out.textContent=(p>=0&&!rtc.some(r=>p>=r[0]&&p<=r[1]))?t('wakeNo'):'';
}
function paintPanelSize(fields,out){
  const pw=Number(fields.panelWidth.get()),n=Number(fields.panels.get()),w=pw*n,d=S.display||{};
  const bad=(w<(d.minWidth||32)||w>(d.maxWidth||128))?t('pnBadWidth'):'';
  out.className='badge '+(bad?'bad':'good');
  out.replaceChildren(bad||w+' × 8 = '+(w*8)+' '+t('pnLeds')+' · '+
    (n>1?n+' '+t('pnPanelsOf')+' '+pw+' × 8':t('pnOne')));
  if(d.restartRequired)out.append(el('div',null,
    'Active: '+d.width+' × '+d.height+' · '+t('rebootreq')));
}
