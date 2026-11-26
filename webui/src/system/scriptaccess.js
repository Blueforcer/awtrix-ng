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
