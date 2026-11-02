//linux:begin
document.head.append(el('style',null,`
#sec-voice .badge{flex:none;font-size:12.5px;padding:5px 11px;white-space:nowrap;word-break:normal}
#sec-voice .badge:not(.good,.bad){border:1px solid var(--brd2)}
#sec-voice .badge::before{content:'';display:inline-block;width:8px;height:8px;margin-right:7px;border-radius:50%;background:currentColor}
#sec-voice>.rows{padding-bottom:0;margin-bottom:-1px}
#sec-voice .cfggrp{font-size:13.5px;font-weight:500;margin:0}
@media(max-width:760px){
  #sec-voice .vcstate{grid-template-columns:auto minmax(0,1fr)}
  #sec-voice .vcstate .lab{display:contents}
  #sec-voice .vcstate .ctl{justify-content:flex-end}
  #sec-voice .vcstate .help{grid-row:2;grid-column:1/-1}
  #sec-voice .badge{flex:initial;white-space:normal}
}
#sec-voice .ctl,.cfgsection.secret .ctl{flex-wrap:wrap}
#sec-voice .ctl input[type=password],.cfgsection.secret .ctl input[type=password]{flex:1 1 180px}
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
  const OFF='off',HA='ha',APP='app:',UNLINKED=['offline','connecting','error'];
  const assist=el('select',{'aria-label':t('vcAssist')});
  assist.addEventListener('change',()=>store({assistant:assist.value}));
  const state=el('div',{class:'badge'}),hint=el('span');
  const stateRow=actionRow(t('oaStatus'),hint,[state]);
  stateRow.classList.add('vcstate');
  const wakeHint=el('span');
  const wake=mkSwitch(false,()=>store({wakeWord:wake.input.checked}));
  const wakeRow=actionRow(t('vcWake'),wakeHint,[wake.node]);
  const sens=el('select',{'aria-label':t('vcSens')},['low','normal','high'].map(v=>el('option',{value:v},t('vcSens'+v))));
  sens.addEventListener('change',()=>store({wakeSensitivity:sens.value}));
  const sensRow=actionRow(t('vcSens'),t('vcSensH'),[sens]);
  const url=el('input',{type:'text',inputmode:'url',autocomplete:'off',spellcheck:'false',placeholder:'http://homeassistant.local:8123','aria-label':t('vcUrl')});
  const token=el('input',{type:'password',autocomplete:'new-password',spellcheck:'false','aria-label':t('vcToken')});
  const clear=armable(el('button',null,t('seRemove')),()=>store({clearToken:true}));
  const connect=el('button',{class:'pri'},t('vcConnect'));
  const pipe=el('select',{'aria-label':t('vcPipe')});
  pipe.addEventListener('change',()=>store({pipeline:pipe.value}));
  const pipeRow=actionRow(t('vcPipe'),t('vcPipeH'),[pipe]);
  const dev=el('input',{type:'text',autocomplete:'off',spellcheck:'false',placeholder:t('vcDevP'),'aria-label':t('vcDev')});
  dev.addEventListener('change',()=>{
    dev.value=dev.value.trim().split(/[/?#]/).filter(Boolean).pop()||'';
    store({device:dev.value});
  });
  const devRow=actionRow(t('vcDev'),t('vcDevH'),[dev]);
  const{box,rows}=scriptSectionBox('haset',t('vcHaBox'));
  rows.append(actionRow(t('vcUrl'),null,[url]),actionRow(t('vcToken'),t('vcTokenH'),[token,clear]),
    el('div',{class:'frow full'},el('div',{class:'ctl'},connect)),pipeRow,devRow);
  let cur=null,pipes='',apps='',opened=false;
  const reason=code=>STR['vce'+code]?t('vce'+code):code;
  const fix=code=>STR['vcx'+code]||'';
  const show=(nodes,on)=>{for(const node of nodes)node.style.display=on?'':'none';};
  const warn=(node,on)=>node.parentNode.classList.toggle('warn',on);
  const originOf=v=>{
    try{return new URL(/^https?:\/\//i.test(v)?v:'http://'+v).origin;}
    catch(e){return v;}
  };
  const checkConnect=()=>{
    const c=cur.config,kept=c.tokenSet&&originOf(url.value.trim())===originOf(c.url);
    connect.disabled=!url.value.trim()||!(token.value.trim()||kept);
    token.placeholder=kept?t('oaKeep'):c.tokenSet?t('vcTokenAgain'):'';
  };
  function status(d,setUp){
    if(d.error)return[reason(d.error),' bad',fix(d.error)];
    if(!setUp)return[t('vcUnset'),'',t('vcUnsetH')];
    const last=d.lastError?t('vcLast')+' '+reason(d.lastError)+'. '+fix(d.lastError):'';
    if(UNLINKED.includes(d.state))return[t('mqbusy'),'',last];
    return[t('vc'+d.state),' good',last];
  }
  function paint(d){
    const first=!cur;cur=d;
    const c=d.config,target=c.assistant,off=target===OFF,ha=target===HA;
    const app=target.startsWith(APP)?target.slice(APP.length):'';
    if(first){url.value=c.url;dev.value=c.device;}
    const names=d.assistantApps,listed=app&&!names.includes(app)?[...names,app]:names;
    const appsSig=JSON.stringify(listed);
    if(appsSig!==apps){
      apps=appsSig;
      assist.replaceChildren(el('option',{value:OFF},t('mqoff')),el('option',{value:HA},t('vcAssistHa')),
        ...listed.map(n=>el('option',{value:APP+n},n)));
    }
    assist.value=target;
    const setUp=!ha||(!!c.url&&c.tokenSet),linked=!UNLINKED.includes(d.state);
    const[text,cls,help]=status(d,setUp);
    state.className='badge'+cls;
    state.replaceChildren(text);
    hint.replaceChildren(help);
    warn(hint,!d.error&&!!d.lastError);
    show([stateRow],!off||!!d.error);
    const hears=d.wakeWordAvailable&&!off&&setUp;
    const waiting=c.wakeWord&&!d.wakeListening&&(d.state==='ready'||!linked);
    wake.input.checked=c.wakeWord;
    sens.value=c.wakeSensitivity;
    wakeHint.replaceChildren(t(waiting?'vcWakeWait':'vcWakeH'));
    warn(wakeHint,waiting);
    show([wakeRow],hears);
    show([sensRow],hears&&c.wakeWord);
    show([box],ha);
    const want=ha&&(!setUp||(!linked&&!!d.error))?true:linked?false:opened;
    if(want!==opened&&!box.contains(document.activeElement?.closest('input'))){opened=want;box.open=want;}
    show([clear],c.tokenSet);
    checkConnect();
    const speech=d.pipelines.filter(p=>p.stt_engine&&p.tts_engine).map(p=>[p.id,p.name||p.id]);
    const choices=c.pipeline&&!speech.some(([id])=>id===c.pipeline)?[...speech,[c.pipeline,c.pipeline]]:speech;
    const sig=JSON.stringify(choices);
    if(sig!==pipes){
      pipes=sig;
      pipe.replaceChildren(el('option',{value:''},t('vcPipeDef')),...choices.map(([id,name])=>el('option',{value:id},name)));
    }
    pipe.value=c.pipeline;
    show([pipeRow],speech.length>0);
    show([devRow],linked||!!c.device);
  }
  async function load(){paint((await api('/api/v1/voice',{timeout:4000})).data);}
  async function store(body){
    connect.disabled=true;
    try{
      await ownPageSend('/api/v1/voice','X-Awtrix-Voice','POST',body);
      if(body.token)token.value='';
      toast(t('saved'));
    }catch(e){
      toast(e.code==='validationFailed'&&STR['vcf'+e.field]?t('vcf'+e.field):e.message,false);
    }
    checkConnect();
    try{await load();}catch(e){}
  }
  url.addEventListener('input',checkConnect);
  token.addEventListener('input',checkConnect);
  connect.addEventListener('click',()=>{
    connect.focus();
    const body={url:originOf(url.value.trim())},secret=token.value.trim();
    url.value=body.url;
    if(secret)body.token=secret;
    store(body);
  });
  for(const field of[url,token])field.addEventListener('keydown',e=>{if(e.key==='Enter'&&!connect.disabled)connect.click();});
  const details=[stateRow,wakeRow,sensRow,box];
  show(details,false);
  page.section('voice',t('vcSection'),t('vcSectionH'),[actionRow(t('vcAssist'),t('vcAssistH'),[assist]),...details]);
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
const oauthSend=(path,method,body)=>ownPageSend(OAUTH+path,'X-Awtrix-OAuth',method,body);
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
    await oauthSend(path,'POST',body);
  }
  save.addEventListener('click',async()=>{
    try{await store();toast(t('saved'));await reload();}catch(e){toastErr(e);}
  });
  go.addEventListener('click',async()=>{
    try{
      if(s.state==='signedIn'){await oauthSend(path,'DELETE');await reload();return;}
      await store();
      const{data}=await oauthSend(path+'/start','POST');
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
  rows.append(el('div',{class:'badge warn'},t('seWarn')));
  const fields=(s.keys||[]).map(k=>{
    const label=k.label||k.key;
    const input=el('input',{type:'password',autocomplete:'new-password',spellcheck:'false','aria-label':label,
      placeholder:t(k.set?'seSet':'seEmpty')});
    const remove=k.set?armable(el('button',null,t('seRemove')),()=>
      secretSend(path+'/'+encodeURIComponent(k.key),'DELETE').then(reload).catch(toastErr)):null;
    rows.append(actionRow(label,[k.help,t('seHosts')+' '+k.hosts.join(', ')].filter(Boolean).join(' · '),[input,remove]));
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
    await oauthSend(path+'/code','POST',{code:q.get('oauth')||'',state});
    for(let i=0;i<30;i++){
      const{data}=await api(OAUTH+path);
      if(data.state==='signedIn'){toast(t('oaDone'));render();return;}
      if(data.state==='error'){toast(t('oaerror')+(data.error?' · '+data.error:''),false);return;}
      await new Promise(r=>setTimeout(r,1000));
    }
  }catch(e){toastErr(e);}
}
//linux:end
