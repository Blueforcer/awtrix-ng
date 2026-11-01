//linux:begin
document.head.append(el('style',null,`
#sec-voice .badge:not(.bad){flex:none;white-space:nowrap;word-break:normal}
#sec-voice .ctl{flex-wrap:wrap}
#sec-voice .ctl input[type=password]{flex:1 1 180px}
.cfgsection.secret .ctl{flex-wrap:wrap}
.cfgsection.secret .ctl input[type=password]{flex:1 1 180px}
`));
const GPSTATE={ready:'mqup',waiting:'mqdown',connecting:'mqbusy',pairing:'scanning'};
function gamepadSection(page){
  const pair=el('button',null,t('gpPairBtn')),help=el('span');
  const phones=[1,2].map(player=>{
    const state=el('div',{class:'badge good','data-phone':player});
    const row=actionRow(t('gpPhone')+' '+player,null,[state]);
    row.style.display='none';
    return {player,state,row};
  });
  let cur=null,pending=false;
  const load=async()=>paint((await api('/api/v1/gamepad',{timeout:4000})).data);
  const rows=[1,2].map(id=>{
    const state=el('div',{class:'badge','data-gamepad':id});
    const forget=armable(el('button',{style:'display:none','aria-label':t('gpForget')+' '+t('gpDevice')+' '+id},t('gpForget')),async()=>{
      forget.disabled=true;
      try{await req('DELETE','/api/v1/gamepad/'+id);await load();}
      catch(e){toastErr(e);}
      finally{forget.disabled=false;}
    });
    return {id,state,forget,row:actionRow(t('gpDevice')+' '+id,null,[state,forget])};
  });
  const paint=d=>{
    cur=d;
    const devices=d.devices||[],pairing=devices.some(p=>p.state==='pairing');
    const full=devices.length===2&&devices.every(p=>p.address);
    for(const row of rows){
      const p=devices.find(p=>p.id===row.id)||{state:'unpaired'};
      row.state.className='badge'+(p.state==='ready'?' good':'');
      row.state.replaceChildren(t(GPSTATE[p.state]||'gpNone')+(p.name?' · '+p.name:'')+
        (p.player?' · '+t('gpPlayer')+' '+p.player:''));
      row.forget.style.display=p.address?'':'none';
    }
    for(const p of phones){
      const r=(d.remotes||[]).find(r=>r.player===p.player);
      p.row.style.display=r?'':'none';
      p.state.replaceChildren(r?r.name+' · '+t('gpPlayer')+' '+r.player:'');
    }
    pair.disabled=pending||pairing||full;
    help.replaceChildren(t(full?'gpFull':'gpPairH'));
  };
  pair.disabled=true;
  pair.addEventListener('click',async()=>{
    pending=true;pair.disabled=true;
    try{await post('/api/v1/gamepad/pair');await load();}
    catch(e){toastErr(e);}
    finally{pending=false;if(cur)paint(cur);}
  });
  page.section('gamepad','Gamepad','',[
    ...rows.map(r=>r.row),...phones.map(p=>p.row),actionRow(t('gpPair'),help,[pair])]);
  poller(load,()=>!cur||cur.devices?.some(p=>p.state==='pairing'||p.state==='connecting')?2000:5000);
}
function voiceSection(page){
  const HA='ha',APP='app:';
  const state=el('div',{class:'badge'}),tokenState=el('div',{class:'badge'});
  const url=el('input',{type:'text',inputmode:'url',autocomplete:'off',spellcheck:'false',placeholder:'http://homeassistant.local:8123','aria-label':t('vcUrl')});
  const token=el('input',{type:'password',autocomplete:'new-password',spellcheck:'false','aria-label':t('vcToken')});
  const pipe=el('select',{'aria-label':t('vcPipe')});
  const dev=el('input',{type:'text',autocomplete:'off',spellcheck:'false',placeholder:t('vcDevP'),'aria-label':t('vcDev')});
  const assist=el('select',{'aria-label':t('vcAssist')});
  assist.addEventListener('change',()=>store({assistant:assist.value},t('saved')));
  const on=mkSwitch(false,()=>store({enabled:on.input.checked},t('saved')));
  const wake=mkSwitch(false,()=>store({wakeWord:wake.input.checked},t('saved')));
  const wakeRow=actionRow(t('vcWake'),t('vcWakeH'),[wake.node]);
  const save=el('button',{class:'pri'},t('vcSave'));
  const clear=armable(el('button',null,t('vcTokenClear')),()=>store({clearToken:true,enabled:false},t('saved')));
  const haRows=[actionRow(t('mqStatus'),null,[state]),actionRow(t('vcOn'),t('vcOnH'),[on.node]),
    actionRow(t('vcUrl'),null,[url]),actionRow(t('vcToken'),t('vcTokenH'),[tokenState,token,clear]),
    actionRow(t('vcPipe'),t('vcPipeH'),[pipe]),actionRow(t('vcDev'),t('vcDevH'),[dev]),actionRow('',null,[save])];
  let cur=null,pipes='',apps='';
  const reason=code=>code?(STR['vce'+code]?t('vce'+code):code):'';
  function paint(d){
    const first=!cur;cur=d;
    const c=d.config||{};
    if(first){url.value=c.url||'';dev.value=c.device||'';}
    on.input.checked=!!c.enabled;
    wake.input.checked=!!c.wakeWord;
    wakeRow.style.display=d.wakeWordAvailable?'':'none';
    const names=d.assistantApps||[],target=c.assistant||HA;
    if(first||JSON.stringify(names)!==apps){
      apps=JSON.stringify(names);
      assist.replaceChildren(el('option',{value:HA},t('vcAssistHa')),...names.map(n=>el('option',{value:APP+n},n)));
    }
    if(![...assist.options].some(o=>o.value===target))assist.append(el('option',{value:target},target.slice(APP.length)));
    assist.value=target;
    for(const row of haRows)row.style.display=target===HA?'':'none';
    tokenState.className='badge'+(c.tokenSet?' good':'');
    tokenState.replaceChildren(t(c.tokenSet?'vcTokenSet':'vcTokenNone'));
    token.placeholder=c.tokenSet?t('vcTokenKeep'):'';
    clear.disabled=!c.tokenSet;
    const list=(d.pipelines||[]).filter(p=>p.stt_engine&&p.tts_engine);
    const sig=JSON.stringify(list);
    if(first||sig!==pipes){
      pipes=sig;
      const want=first?c.pipeline||'':pipe.value;
      pipe.replaceChildren(el('option',{value:''},t('vcPipeDef')),
        ...list.map(p=>el('option',{value:p.id},p.name||p.id)));
      if(want&&!list.some(p=>p.id===want))pipe.append(el('option',{value:want},want));
      pipe.value=want;
    }
    const why=c.enabled?reason(d.error):'';
    state.className='badge'+(why?' bad':d.state==='ready'?' good':'');
    state.replaceChildren(why||t('vc'+d.state));
  }
  async function load(){paint((await api('/api/v1/voice',{timeout:4000})).data);}
  async function store(body,done){
    save.disabled=true;
    try{
      await ownPageSend('/api/v1/voice','X-Awtrix-Voice','POST',body);
      if(body.token)token.value='';
      toast(done);
    }catch(e){
      toast(e.code==='validationFailed'&&STR['vcf'+e.field]?t('vcf'+e.field):e.message,false);
    }
    save.disabled=false;
    try{await load();}catch(e){}
  }
  save.addEventListener('click',()=>{
    const body={url:url.value.trim(),pipeline:pipe.value,device:dev.value.trim().split(/[/?#]/).filter(Boolean).pop()||'',enabled:true};
    dev.value=body.device;
    if(token.value)body.token=token.value;
    store(body,t('saved'));
  });
  page.section('voice',t('vcSection'),t('vcSectionH'),[
    actionRow(t('vcAssist'),t('vcAssistH'),[assist]),wakeRow,...haRows]);
  poller(load,2000);
}
const SV='/api/v1/speech/voice';
function speechVoiceSection(page){
  const info=el('div',{class:'badge'});
  const fileIn=el('input',{type:'file',accept:'.atts',style:'display:none'});
  const up=el('button',null,t('svUpload'));up.addEventListener('click',()=>fileIn.click());
  const done=r=>{paint(r.data);toast(t('saved'));};
  const builtin=armable(el('button',{style:'display:none'},t('svBuiltin')),()=>req('DELETE',SV).then(done,toastErr));
  const line=el('input',{type:'text',placeholder:t('svLineP'),'aria-label':t('svLine')});
  const say=el('button',null,t('svSay'));
  say.addEventListener('click',()=>post('/api/v1/audio/play',{speech:line.value.trim()||t('svLineP')}).catch(toastErr));
  fileIn.addEventListener('change',async()=>{
    const f=fileIn.files[0];fileIn.value='';
    if(!f)return;
    const fd=new FormData();fd.append('voice',f,f.name);
    up.disabled=true;
    try{done(await api(SV,{method:'POST',body:fd,timeout:60000}));}catch(e){toastErr(e);}
    up.disabled=false;
  });
  function paint(d){
    info.className='badge'+(d.name?' good':' bad');
    info.replaceChildren(d.name?d.name+' · '+String(d.language).toUpperCase()+(d.uploaded?'':' · '+t('svBuiltinTag')):t('svNone'));
    builtin.style.display=d.uploaded?'':'none';
  }
  page.section('speechvoice',t('svSection'),t('svSectionH'),[
    actionRow(t('svActive'),null,[info,builtin]),
    actionRow(t('svPack'),t('svPackH'),[up,fileIn]),
    actionRow(t('svLine'),null,[line,say])]);
  api(SV,{timeout:4000}).then(r=>paint(r.data),()=>{});
}
const IPSTATE={off:'mqoff',waiting:'ipWait',connecting:'mqbusy',ready:'mqup'};
function brokerTrustRows(page){
  const cIn=el('input',{type:'file',accept:'.pem,.crt,.cer',multiple:''});
  cIn.addEventListener('change',async()=>{
    const x=await Promise.all([...cIn.files].map(f=>f.text()));cIn.value='';
    if(x.length)req('PUT','/api/v1/mqtt/tls/ca',{certificate:x.join('\n')}).then(done,toastErr);
  });
  const up=el('button',null,'Upload');up.addEventListener('click',()=>cIn.click());
  const done=r=>{paint(r.data);toast(t('saved'));};
  const load=async()=>paint((await api('/api/v1/mqtt/tls',{timeout:4000})).data);
  const fp=el('div',{class:'badge bad'}),ca=el('div',{class:'badge'});
  const cDel=armable(el('button',null,t('del')),()=>req('DELETE','/api/v1/mqtt/tls/ca').then(done,toastErr));
  let pending='';
  const trust=el('button',{class:'pri'},'Trust');
  trust.addEventListener('click',()=>req('PUT','/api/v1/system',{mqttTlsPin:pending}).then(()=>{toast(t('saved'));load();},toastErr));
  const mRow=actionRow('Broker certificate',
    'Not trusted. Compare the SHA-256 with your broker.',[fp,trust]);
  const cRow=actionRow('Broker CA',
    'Replaces public CAs and trusted certificates.',[ca,up,cDel,cIn]);
  function paint(d){
    pending=d.pending||'';
    mRow.style.display=pending?'':'none';
    fp.replaceChildren(pending);
    const bad=d.ca==='unusable';
    ca.className=bad?'badge bad':'badge';
    ca.replaceChildren(bad?'Unusable':d.ca==='uploaded'?'Uploaded':'Public CAs');
    cDel.style.display=bad||d.ca==='uploaded'?'':'none';
  }
  page.panel.querySelector('#sec-mqtt .rows')?.append(mRow,cRow);
  paint({});
  poller(load,5000);
}
function iphoneSection(page){
  const state=el('div',{class:'badge'});
  const sw=mkSwitch(false,()=>store({enabled:sw.input.checked}));
  let cur={};
  const load=async()=>paint((await api('/api/v1/iphone',{timeout:4000})).data);
  const store=body=>req('PUT','/api/v1/iphone',body).then(r=>paint(r.data),e=>{toastErr(e);load().catch(()=>{});});
  const forget=armable(el('button',{style:'display:none'},t('ipForget')),()=>
    req('DELETE','/api/v1/iphone/phone').then(r=>{paint(r.data);toast(t('ipForgot'));},toastErr));
  function paint(d){
    cur=d;
    const p=d.phone;
    sw.input.checked=!!d.enabled;
    state.className='badge'+(d.state==='ready'?' good':'');
    state.replaceChildren(t(IPSTATE[d.state]||'mqoff')+(p?' · '+(p.name||p.addr):''));
    forget.style.display=p?'':'none';
  }
  page.section('iphone','iPhone','',[
    actionRow(t('ipOn'),t('ipOnH'),[sw.node]),
    actionRow(t('mqStatus'),null,[state,forget])]);
  poller(load,()=>cur.state==='connecting'?2000:5000);
}
const ownPageSend=(url,marker,method,body)=>api(url,{method,
  headers:{'Content-Type':'application/json',[marker]:'1'},body:JSON.stringify(body||{})});
function scriptSectionBox(cls,title,open){
  const rows=el('div',{class:'cfgrows'});
  const box=el('details',{class:'cfgsection '+cls},
    el('summary',null,icon('chev'),el('h3',{class:'cfggrp'},title)),rows);
  box.open=!!open;
  return{box,rows};
}
const OAUTH='/api/v1/oauth/';
const oauthSend=(path,body,method='POST')=>ownPageSend(OAUTH+path,'X-Awtrix-OAuth',method,body);
async function oauthSection(name,open){
  const path=encodeURIComponent(name);
  const{data:s}=await api(OAUTH+path);
  const{box,rows}=scriptSectionBox('oauth',t('oaSection')+(s.provider?' · '+s.provider:''),open);
  const reload=async()=>box.replaceWith(await oauthSection(name,box.open));
  const id=el('input',{type:'text',autocomplete:'off',spellcheck:'false',value:s.clientId||'','aria-label':t('oaId')});
  const secret=el('input',{type:'password',autocomplete:'new-password',spellcheck:'false','aria-label':t('oaSecret'),
    placeholder:s.clientSecretSet?t('oaKeep'):''});
  const state=el('div',{class:'badge'+(s.state==='signedIn'?' good':s.invalid||s.state==='error'?' bad':'')},
    s.invalid?t('oaInvalid'):t('oa'+s.state)+(s.error?' · '+s.error:''));
  const save=el('button',null,t('save'));
  const go=el('button',{class:'pri'},t(s.state==='signedIn'?'oaSignOut':'oaSignIn'));
  go.disabled=!!s.invalid;
  async function store(){
    if(id.value.trim()===(s.clientId||'')&&!secret.value)return;
    const body={clientId:id.value.trim()};
    if(secret.value)body.clientSecret=secret.value;
    await oauthSend(path,body);
  }
  save.addEventListener('click',async()=>{
    try{await store();toast(t('saved'));await reload();}catch(e){toastErr(e);}
  });
  go.addEventListener('click',async()=>{
    try{
      if(s.state==='signedIn'){await oauthSend(path,null,'DELETE');await reload();return;}
      await store();
      const{data}=await oauthSend(path+'/start');
      location.assign(data.url);
    }catch(e){toastErr(e);}
  });
  rows.append(actionRow(t('oaStatus'),null,[state]),
    actionRow(t('oaId'),null,[id]),
    actionRow(t('oaSecret'),s.pkce?t('oaSecretPkce'):null,[secret]),
    el('div',{class:'cfgbar'},el('span',{class:'grow'}),save,go));
  return box;
}
const SECRETS='/api/v1/secrets/';
const secretSend=(path,method,body)=>ownPageSend(SECRETS+path,'X-Awtrix-Secret',method,body);
async function secretSection(name,open){
  const path=encodeURIComponent(name);
  const{data:s}=await api(SECRETS+path);
  const{box,rows}=scriptSectionBox('secret',t('seSection'),open);
  if(s.invalid){
    rows.append(el('div',{class:'badge bad'},t('seInvalid')+' · '+s.invalid));
    return box;
  }
  const reload=async()=>box.replaceWith(await secretSection(name,box.open));
  const fields=(s.keys||[]).map(k=>{
    const label=k.label||k.key;
    const input=el('input',{type:'password',autocomplete:'new-password',spellcheck:'false','aria-label':label,
      placeholder:t(k.set?'seSet':'seEmpty')});
    const remove=k.set?armable(el('button',null,t('seRemove')),()=>
      secretSend(path+'/'+encodeURIComponent(k.key),'DELETE').then(reload).catch(toastErr)):null;
    rows.append(actionRow(label,k.help||null,[input,remove]));
    return[k.key,input];
  });
  const save=el('button',{class:'pri'},t('save'));
  save.addEventListener('click',async()=>{
    const filled=fields.filter(([,input])=>input.value);
    if(!filled.length)return;
    try{
      await secretSend(path,'PUT',Object.fromEntries(filled.map(([key,input])=>[key,input.value])));
      toast(t('saved'));
      await reload();
    }catch(e){toastErr(e);}
  });
  rows.append(el('div',{class:'cfgbar'},el('span',{class:'grow'}),save));
  return box;
}
const SCRIPT_SECTIONS=[
  {cap:'oauth',apps:'oauthApps',list:'/api/v1/oauth',names:d=>(d.apps||[]).map(a=>a.name),cls:'oauth',make:oauthSection,first:true},
  {cap:'secret',apps:'secretApps',list:'/api/v1/secrets',names:d=>d.apps||[],cls:'secret',make:secretSection}];
const hasScriptSection=name=>SCRIPT_SECTIONS.some(s=>S[s.apps]?.has(name));
async function loadScriptSection(s){
  if(!S[s.cap]||s.loading)return;
  s.loading=true;
  try{
    const names=s.names((await api(s.list)).data).sort();
    if(names.join('\n')!==[...(S[s.apps]||[])].sort().join('\n')){
      S[s.apps]=new Set(names);
      if(location.hash.startsWith('#/apps'))render();
    }
  }catch(e){}
  s.loading=false;
}
function loadScriptSections(){SCRIPT_SECTIONS.forEach(loadScriptSection);}
const fillScriptConfig=fillConfigPanel;
fillConfigPanel=async(name,box,saveError,builtin)=>{
  const open=new Map(SCRIPT_SECTIONS.map(s=>[s.cls,box.querySelector('.cfgsection.'+s.cls)?.open]));
  const ok=await fillScriptConfig(name,box,saveError,builtin);
  if(!ok||builtin)return ok;
  for(const s of SCRIPT_SECTIONS){
    if(!S[s.apps]?.has(name))continue;
    try{
      const section=await s.make(name,open.get(s.cls));
      [...box.children].find(n=>n.textContent===t('cfgNone'))?.remove();
      if(s.first)box.prepend(section);else box.append(section);
    }catch(e){box.append(el('div',{class:'badge bad'},e.message));}
  }
  return ok;
};
// Back from the provider through the Hub: #oauth=<code>&state=<state>, or #oauth_error=<reason>.
async function oauthReturn(){
  const q=new URLSearchParams(location.hash.slice(1));
  history.replaceState(null,'','#/apps');
  render();
  const state=q.get('state')||'';
  let app='';
  try{
    const bytes=Uint8Array.from(atob(state.replace(/-/g,'+').replace(/_/g,'/')),c=>c.charCodeAt(0));
    app=JSON.parse(new TextDecoder().decode(bytes)).a||'';
  }catch(e){}
  if(!app)return;
  if(q.has('oauth_error')){toast(t('oaerror')+' · '+q.get('oauth_error'),false);return;}
  try{
    const path=encodeURIComponent(app);
    await oauthSend(path+'/code',{code:q.get('oauth')||'',state});
    for(let i=0;i<30;i++){
      const{data}=await api(OAUTH+path);
      if(data.state==='signedIn'){toast(t('oaDone'));render();return;}
      if(data.state==='error'){toast(t('oaerror')+(data.error?' · '+data.error:''),false);return;}
      await new Promise(r=>setTimeout(r,1000));
    }
  }catch(e){toastErr(e);}
}
//linux:end
