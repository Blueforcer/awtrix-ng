//linux:begin
document.head.append(el('style',null,`
#sec-voice .badge:not(.bad){flex:none;white-space:nowrap;word-break:normal}
#sec-voice .ctl{flex-wrap:wrap}
#sec-voice .ctl input[type=password]{flex:1 1 180px}
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
  const state=el('div',{class:'badge'}),tokenState=el('div',{class:'badge'});
  const url=el('input',{type:'text',inputmode:'url',autocomplete:'off',spellcheck:'false',placeholder:'http://homeassistant.local:8123','aria-label':t('vcUrl')});
  const token=el('input',{type:'password',autocomplete:'new-password',spellcheck:'false','aria-label':t('vcToken')});
  const pipe=el('select',{'aria-label':t('vcPipe')});
  const dev=el('input',{type:'text',autocomplete:'off',spellcheck:'false',placeholder:t('vcDevP'),'aria-label':t('vcDev')});
  const on=mkSwitch(false,()=>store({enabled:on.input.checked},t('saved')));
  const save=el('button',{class:'pri'},t('vcSave'));
  let cur=null,pipes='';
  const clear=armable(el('button',null,t('vcTokenClear')),()=>store({clearToken:true,enabled:false},t('saved')));
  const reason=code=>code?(STR['vce'+code]?t('vce'+code):code):'';
  function paint(d){
    const first=!cur;cur=d;
    const c=d.config||{};
    if(first){url.value=c.url||'';dev.value=c.device||'';}
    on.input.checked=!!c.enabled;
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
      await api('/api/v1/voice',{method:'POST',headers:{'Content-Type':'application/json','X-Awtrix-Voice':'1'},body:JSON.stringify(body)});
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
    actionRow(t('mqStatus'),null,[state]),
    actionRow(t('vcOn'),t('vcOnH'),[on.node]),
    actionRow(t('vcUrl'),null,[url]),
    actionRow(t('vcToken'),t('vcTokenH'),[tokenState,token,clear]),
    actionRow(t('vcPipe'),t('vcPipeH'),[pipe]),
    actionRow(t('vcDev'),t('vcDevH'),[dev]),
    actionRow('',null,[save])]);
  poller(load,2000);
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
const OAUTH='/api/v1/oauth/';
const oauthSend=(path,body,method='POST')=>api(OAUTH+path,{method,
  headers:{'Content-Type':'application/json','X-Awtrix-OAuth':'1'},body:JSON.stringify(body||{})});
async function loadOauthApps(){
  if(!S.oauth||S.oauthLoading)return;
  S.oauthLoading=true;
  try{
    const{data}=await api('/api/v1/oauth');
    const names=(data.apps||[]).map(a=>a.name).sort();
    if(names.join('\n')!==[...(S.oauthApps||[])].sort().join('\n')){
      S.oauthApps=new Set(names);
      if(location.hash.startsWith('#/apps'))render();
    }
  }catch(e){}
  S.oauthLoading=false;
}
async function oauthSection(name,open){
  const path=encodeURIComponent(name);
  const{data:s}=await api(OAUTH+path);
  const rows=el('div',{class:'cfgrows'});
  const box=el('details',{class:'cfgsection oauth'},
    el('summary',null,icon('chev'),el('h3',{class:'cfggrp'},t('oaSection')+(s.provider?' · '+s.provider:''))),rows);
  box.open=!!open;
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
// Scripts with an @oauth line get the sign-in as their own first section, also after every save.
const fillScriptConfig=fillConfigPanel;
fillConfigPanel=async(name,box,saveError,builtin)=>{
  const open=box.querySelector('.cfgsection.oauth')?.open;
  const ok=await fillScriptConfig(name,box,saveError,builtin);
  if(!ok||builtin||!S.oauthApps||!S.oauthApps.has(name))return ok;
  try{
    const section=await oauthSection(name,open);
    [...box.children].find(n=>n.textContent===t('cfgNone'))?.remove();
    box.prepend(section);
  }catch(e){box.append(el('div',{class:'badge bad'},e.message));}
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
