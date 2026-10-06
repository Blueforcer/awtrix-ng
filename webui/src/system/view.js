function mergeDisplayData(settings,display){
  const data={...settings,power:!!display.power,overlay:display.overlay||'',
    overlaySpeed:Math.round(((display.overlaySettings||{}).speed??1)*100)};
  for(const n of NESTED){
    for(const[k,v]of Object.entries(data[n]||{}))data[n+'.'+k]=v;
    delete data[n];
  }
  for(const k in NULLABLE)if(k in data&&data[k]==null)data[k]=NULLABLE[k];
  return data;
}
async function viewDisplay(view){
  let data;
  try{
    if(!S.transitions.length||!S.aud){
      const caps=(await api('/api/v1/capabilities')).data||{};
      if(!S.transitions.length)S.transitions=caps.transitions||[];
      if(!S.aud)readAudioCaps(caps);
    }
    data=mergeDisplayData((await api('/api/v1/settings')).data,(await api('/api/v1/display')).data);
  }catch(e){view.append(el('div',{class:'card wide'},t('neterr')));return;}
  FIELDS.autoBrightness.g=hasLight()?'bright':'hidden';
  FIELDS.brightness.h=hasLight()?BRI_H:'';
  //linux:begin
  FIELDS.enlargeApps.g=S.enlargeApps?'text':'hidden';
  //linux:end
  const page=settingsPage(view,async(payload)=>{
    const disp={},set={};
    for(const[k,v]of Object.entries(payload)){
      if(k==='power')disp.power=v;
      else if(k==='overlay')disp.overlay=v===''?null:v;
      else if(k==='overlaySpeed')disp.overlaySettings={speed:v/100};
      else{
        const n=NESTED.find(p=>k.startsWith(p+'.'));
        if(n)(set[n]||(set[n]={}))[k.slice(n.length+1)]=v;
        else set[k]=(k in NULLABLE&&v===NULLABLE[k])?null:v;
      }
    }
    if(Object.keys(set).length)await req('PATCH','/api/v1/settings',set);
    if(Object.keys(disp).length)await req('PATCH','/api/v1/display',disp);
  },async()=>{});
  renderSettingsInto(page,data,FIELDS,SET_GROUPS);
  advancedSection(page,data,FIELDS,[...SOUND_KEYS,...Object.keys(APPF),'weekdayBar','dateWeekdayBar']);
}
async function viewSystem(view){
  let data;
  try{data=(await api('/api/v1/system')).data;}
  catch(e){view.append(el('div',{class:'card wide'},t('neterr')));return;}
  if(!S.sysHost){try{setStats((await api('/api/v1/device',{timeout:4000})).data);}catch(e){}}
  if(!S.gpioKnown||!S.aud){
    try{
      const caps=(await api('/api/v1/capabilities')).data||{};
      readAudioCaps(caps);
    }catch(e){}
    if(S.gpio)applyGpioCaps(S.gpio);
  }
  LIGHT_KEYS.forEach(k=>SYSF[k].g=hasLight()?'hw':'hidden');
  SYSF.dfplayer.g=data.pinDfRx>=0&&data.pinDfTx>=0?'sndhw':'hidden';
  //linux:begin
  Object.keys(SYSF).forEach(k=>{if(SYSF[k].g==='hw'&&k!=='lowBatteryThreshold')SYSF[k].g='hidden';});
  SYSF.mqttTls.g=S.mqttTls?'mqtt':'hidden';
  //linux:end
  if(!S.ap){
    try{
      const set=(await api('/api/v1/settings')).data;
      SOUND_KEYS.forEach(k=>{if(k in set)data[k]=set[k];});
    }catch(e){}
    //linux:begin
    const g=on=>on?'sndhw':'hidden';
    SYSF.musicSource.g=g(S.microphone===true);
    SYSF.bootSound.g=g(S.bootSound);
    //linux:end
  }
  ['wifiPass','mqttPass','authPass'].forEach(k=>{if(!(k in data))data[k]='';});
  (S.ap?provisioningPage:systemPage)(view,data);
}
function systemSettingsPage(view,data){
  const rbBtn=el('button',{class:'pri'},t('rebootnow'));
  const closeBtn=el('button',{class:'bx',title:t('rbclose'),'aria-label':t('rbclose')},'✕');
  closeBtn.addEventListener('click',()=>{banner.style.display='none';});
  const banner=el('div',{class:'banner rbstick',style:'display:none'},'⚠️ ',el('span',{class:'grow'},t('rebootreq')),rbBtn,closeBtn);
  rbBtn.addEventListener('click',()=>doReboot(rbBtn));
  view.append(banner);
  const toSettings=k=>!!(SYSF[k]&&SYSF[k].api==='settings');
  const num=v=>Number(v)||0;
  let width=num(data&&data.panelWidth)*num(data&&data.panels);
  return settingsPage(view,async payload=>{
    const cfg={},set={};
    for(const[k,v]of Object.entries(payload))(toSettings(k)?set:cfg)[k]=v;
    if(Object.keys(set).length)await req('PATCH','/api/v1/settings',set);
    if(Object.keys(cfg).length)await req('PUT','/api/v1/system',cfg);
  },async payload=>{
    const keys=Object.keys(payload).filter(k=>!toSettings(k));
    const w=width?num(payload.panelWidth??data.panelWidth)*num(payload.panels??data.panels):0;
    if(keys.some(k=>REBOOT_KEYS.has(k)||!SYSF[k])||(w&&w!==width))banner.style.display='';
    if(w)width=w;
    if('panelWidth'in payload)data.panelWidth=payload.panelWidth;
    if('panels'in payload)data.panels=payload.panels;
  });
}
function provisioningPage(view,data){
  view.append(el('div',{class:'banner blue'},'📶 ',t('apbanner')));
  const page=systemSettingsPage(view,data);
  const fields=Object.fromEntries(['wifiSsid','wifiPass','hostname'].map(k=>[k,SYSF[k]]));
  renderSettingsInto(page,data,fields,[['wifi','grpWifi','grpWifiH']]);
  page.section('maint',t('maint'),t('apmaintH'),
    [actionRow(t('reboot'),null,[armable(el('button',null,t('reboot')),()=>doReboot())])]);
  page.section('backup',t('bkSectionAp'),t('bkRestoreH'),[el('div',{class:'banner blue captive-hint'},
    el('span',null,t('bkCaptiveHint')),el('a',{href:location.origin+'/'},location.host)),...restoreRows(null)]);
}
function systemPage(view,data){
  if(TZRULE.get(data.tzName)!==data.tz)
    data.tzName=TZFIRST.get(data.tz)||'UTC';
  const page=systemSettingsPage(view,data);
  const groups=SYS_GROUPS.filter(([id])=>
    (id!=='panel'||S.display?.configurable!==false)&&
    (id!=='gpio'||!S.gpioKnown||S.gpio!==null));
  const sysFields=renderSettingsInto(page,data,SYSF,groups);
  if(S.display?.configurable===false)
    page.section('panel',t('grpPanel'),'',
      [actionRow(t('pnSize'),null,[el('div',{class:'badge good'},
        S.display.width+' × '+S.display.height)])]);
  advancedSection(page,data,SYSF);
  if(sysFields.hostname&&S.sysHost)
    sysFields.hostname.row.querySelector('input').placeholder=S.sysHost;
  if(sysFields.mqttPrefix&&S.sysUid)
    sysFields.mqttPrefix.row.querySelector('input').placeholder=S.sysUid;
  const panelSec=sysFields.panelWidth?page.panel.querySelector('#sec-panel'):null;
  if(panelSec){
    const size=el('div',{class:'badge good'});
    panelSec.querySelector('.rows').prepend(actionRow(t('pnSize'),t('pnSizeH'),[size]));
    page.onUpdate.push(()=>paintPanelSize(sysFields,size));
    page.update();
  }
  hubTokenSection(page);
  //linux:begin
  if(S.gamepad)gamepadSection(page);
  if(S.voice)voiceSection(page);
  if(S.iphone)iphoneSection(page);
  if(S.mqttTls&&sysFields.mqttTls){
    const tl=sysFields.mqttTls,mp=sysFields.mqttPort;
    if(tl&&mp)tl.row.querySelector('input').addEventListener('change',()=>{
      const p=Number(mp.get());
      if(p===1883||p===8883){mp.fill(tl.get()?8883:1883);page.update();}
    });
    brokerTrustRows(page);
  }
  //linux:end
  const statusBadge=(id,label,help)=>{
    const sec=page.panel.querySelector('#sec-'+id);
    if(!sec)return null;
    const badge=el('div',{class:'badge'});
    sec.querySelector('.rows').prepend(actionRow(t(label),help,[badge]));
    return badge;
  };
  const mq=statusBadge('mqtt','mqStatus',t('mqStatusH')),mi=statusBadge('share','miStatus',null);
  const paintLinks=d=>{if(mq)paintMqtt(mq,d&&d.mqtt);if(mi)paintMirror(mi,d&&d.mirror);};
  paintLinks(S.stats);
  if(mq||mi)poller(async()=>{
    const{data}=await api('/api/v1/device',{timeout:4000});
    setStats(data);paintLinks(data);
  },5000);
  const gpioSec=(S.gpio&&S.gpio.soc!=='esp32')?null:page.panel.querySelector('#sec-gpio');
  if(gpioSec){
    const PRESETS={
      ulanzi:{pinMatrix:32,pinBtnLeft:26,pinBtnSelect:27,pinBtnRight:14,pinBattery:34,pinLdr:35,
        pinBuzzer:15,pinI2cSda:21,pinI2cScl:22,pinDfRx:23,pinDfTx:18,dfplayer:false},
      awtrix2:{pinMatrix:21,pinBtnLeft:26,pinBtnSelect:16,pinBtnRight:5,pinBattery:-1,pinLdr:36,
        pinBuzzer:-1,pinI2cSda:17,pinI2cScl:22,pinDfRx:23,pinDfTx:18,dfplayer:true}};
    const presetBtn=(lbl,id)=>{
      const b=el('button',null,lbl);
      b.addEventListener('click',()=>{
        Object.entries(PRESETS[id]).forEach(([k,v])=>{if(sysFields[k])sysFields[k].fill(v);});
        page.update();toast(t('presetApplied'));
      });
      return b;
    };
    gpioSec.querySelector('.rows').prepend(
      actionRow(t('preset'),t('presetH'),[presetBtn(t('presetUlanzi'),'ulanzi'),presetBtn(t('presetAwtrix2'),'awtrix2')]));
  }
  if(S.gpio){
    page.onUpdate.push(()=>paintPinUse(sysFields));
    const sel=sysFields.pinBtnSelect;
    if(sel&&S.gpio.rtc){
      const note=el('div',{class:'help'});
      sel.row.querySelector('.lab').append(note);
      page.onUpdate.push(()=>paintWakePin(sysFields,note));
    }
    page.update();
  }
  maintenanceSection(page);
  backupSection(page);
}
async function doReboot(btn){
  if(btn)btn.disabled=true;
  try{await post('/api/v1/device/reboot');}
  catch(e){toastErr(e);if(btn)btn.disabled=false;return;}
  toast(t('rebooting'));
  setTimeout(()=>location.reload(),9000);
}
const MQERR={noWifi:'mqeNoWifi',hostNotFound:'mqeHost',refused:'mqeRefused',
  badCredentials:'mqeAuth',rejected:'mqeRejected',timeout:'mqeTimeout',lost:'mqeLost'};
const MQSTATE={disabled:'mqoff',offline:'mqdown',connecting:'mqbusy',connected:'mqup'};
function paintMirror(node,m){
  if(!m){node.replaceChildren();node.className='badge';return;}
  const parts=[];
  if(m.sharing)parts.push(t('miShared')+': '+m.viewers);
  if(m.state!=='off'||!parts.length){
    let state=t('mi'+m.state);
    if(m.state==='sizeMismatch'&&m.sourceWidth)state+=' ('+m.sourceWidth+' × '+m.sourceHeight+')';
    parts.push(state);
  }
  const bad=['offline','notFound','sizeMismatch','noMemory'].includes(m.state);
  node.className='badge'+(bad?' bad':m.state==='showing'||m.sharing?' good':'');
  node.replaceChildren(parts.join(' · '));
}
function paintMqtt(node,m){
  if(!m){node.replaceChildren();node.className='badge';return;}
  const parts=[t(MQSTATE[m.state]||'mqdown')];
  if(m.state==='connected'&&m.endpoint)parts.push(m.endpoint);
  if(m.error)parts.push(MQERR[m.error]?t(MQERR[m.error]):m.error);
  if(m.retryInMs>0)parts.push(t('mqRetry')+' '+Math.ceil(m.retryInMs/1000)+' s');
  node.className='badge'+(m.state==='connected'?' good':m.state==='offline'?' bad':'');
  node.replaceChildren(parts.join(' · '));
}
function hubTokenSection(page){
  const inp=el('input',{type:'password',autocomplete:'off',spellcheck:'false',value:hubToken(),'aria-label':t('hubTokenLbl')});
  const clear=el('button',null,t('hubClear'));
  const open=el('a',{href:hubPage('/account/settings'),target:'_blank',rel:'noopener'},t('hubGet'));
  const state=el('div',{class:'badge'});
  function paint(){
    const has=!!hubToken();
    state.className='badge'+(has?' good':'');
    state.replaceChildren(has?t('hubOn'):t('hubOff'));
    clear.disabled=!has;
  }
  function store(v){
    try{if(v)localStorage.awtrixHubToken=v;else delete localStorage.awtrixHubToken;}
    catch(e){inp.value=hubToken();toast(t('hubStorageFail'),false);return false;}
    paint();return true;
  }
  inp.addEventListener('change',()=>{
    const v=inp.value.trim();
    inp.value=v;if(store(v))toast(v?t('hubStored'):t('hubCleared'));
  });
  inp.addEventListener('keydown',e=>{if(e.key==='Enter')inp.blur();});
  clear.addEventListener('click',()=>{inp.value='';if(store(''))toast(t('hubCleared'));});
  paint();
  const section=page.section('hub',t('hubSection'),t('hubSectionH'),[
    actionRow(t('hubTokenLbl'),t('hubTokenH'),[state,inp,clear]),
    actionRow(t('hubGetLbl'),t('hubGetH'),[open]),
  ]);
  if(focusHubSettings){
    focusHubSettings=false;
    requestAnimationFrame(()=>{
      section.scrollIntoView?.({behavior:'smooth',block:'start'});
      inp.focus();
    });
  }
}
