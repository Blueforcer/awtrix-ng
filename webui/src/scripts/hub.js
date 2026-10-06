// HUB SCRIPT UPDATES BEGIN — no credentials or device addresses are sent to the Hub.
function hubScriptOrigin(source){
  const match=/^# @hub ([A-Za-z0-9]{12}) ([a-f0-9]{64})\r?\n/.exec(source);
  return match?{id:match[1],sha256:match[2],code:source.slice(match[0].length)}:null;
}
function hubScriptHash(source){return iconSha256(new TextEncoder().encode(source));}
function hubScriptLink(id,code){
  if(!/^[A-Za-z0-9]{12}$/.test(id))throw new Error(t('suIntegrity'));
  return '# @hub '+id+' '+hubScriptHash(code)+'\n'+code;
}
const HUB_API='https://awtrix.de/api/v1/scripts/';
async function hubScriptRelease(id,part='release'){
  if(!/^[A-Za-z0-9]{12}$/.test(id)||!['release','source'].includes(part))throw new Error(t('suIntegrity'));
  const r=await hubFetch(HUB_API+id+'/'+part,
    {Accept:part==='release'?'application/json':'text/plain'},part==='source','suOffline');
  if(!r.ok){const e=new Error(t(r.status===404?'suMissing':'suOffline'));e.status=r.status;throw e;}
  if(part==='source')return r.text();
  const m=await r.json();
  if(m.id!==id||!Number.isSafeInteger(m.revision)||m.revision<1||!/^[a-f0-9]{64}$/.test(m.sha256)||typeof m.notes!=='string')throw new Error(t('suIntegrity'));
  if(!m.sounds?.every?.(s=>typeof s?.name=='string'&&/^[\w-]{1,32}$/.test(s.name)&&/^[a-f0-9]{64}$/.test(s.sha256)&&Number.isSafeInteger(s.bytes)&&s.bytes>=0))delete m.sounds;
  return m;
}
function planSoundSync(sounds,files){
  const on=new Map();
  for(const f of files||[]){const m=/^([\w-]{1,32})\.mp3$/.exec(f?.name);if(m)on.set(m[1],f.sha256);}
  return{upload:sounds.filter(s=>on.get(s.name)!==s.sha256),remove:[...on.keys()].filter(n=>!sounds.some(s=>s.name===n))};
}
function soundSpace(up,u){
  const needed=up.reduce((a,s)=>a+Math.ceil(s.bytes/4096)*4096+4096,0);
  if(!Number.isFinite(u?.usedBytes)||!(u.totalBytes>0))return{needed,free:null,fits:null};
  const free=Math.max(0,u.totalBytes-u.usedBytes);
  return{needed,free,fits:!needed||needed+32768<=free};
}
const transferMs=(b,r=16384)=>Math.min(6e5,15e3+Math.ceil(b/r)*1e3);
const soundPlan=(rel,files)=>!Array.isArray(rel.sounds)||rel.sounds.length&&!capPresent('audio.mp3')?{upload:[],remove:[]}:planSoundSync(rel.sounds,files);
async function hubSound(id,s){
  const f=s.name+'.mp3';let b;
  try{const r=await hubFetch(HUB_API+id+'/sounds/'+f,{},true,'',transferMs(s.bytes,131072));if(r.ok)b=await r.blob();}
  catch(e){if(e.code)throw e;}
  if(!b)throw new Error(t('sndDl').replace('{f}',f));
  if(await iconBlobHash(b)!==s.sha256)throw new Error(t('suIntegrity'));
  return b;
}
async function soundSync(name,id,plan,step,put){
  const url='/api/v1/apps/script/'+name+'/sounds';let i=0;
  for(const s of plan.upload){
    const f=s.name+'.mp3';
    step(t('sndStep').replace('{i}',++i).replace('{n}',plan.upload.length).replace('{f}',f));
    const b=await hubSound(id,s);
    await uploadFile(b,url,f,null,transferMs(s.bytes)).catch(e=>{e.message=f+': '+e.message;throw e;});
  }
  await put();
  if(plan.remove.length)step(t('sndDel'));
  for(const n of plan.remove)await api(url+'/'+n,{method:'DELETE'}).catch(e=>{if(e.status!==404)throw e;});
}
async function carrySounds(from,to,step){
  const{data}=await api('/api/v1/apps/script/'+from+'/sounds').catch(e=>{if(e.status===404)return{data:{}};throw e;});
  const fs=data.files||[];let i=0;
  if(soundSpace(fs.map(f=>({bytes:f.size})),data).fits===false)throw new Error(t('sndFull'));
  for(const f of fs){
    step(t('sndStep').replace('{i}',++i).replace('{n}',fs.length).replace('{f}',f.name));
    const r=await fetch('/SCRIPTS/'+from+'/'+f.name);
    if(!r.ok)throw new Error(f.name+': HTTP '+r.status);
    await uploadFile(await r.blob(),'/api/v1/apps/script/'+to+'/sounds',f.name,null,transferMs(f.size)).catch(e=>{e.message=f.name+': '+e.message;throw e;});
  }
}
async function carryStore(from,to){
  const u=n=>'/api/v1/apps/'+encodeURIComponent(n),g=async p=>(await api(p)).data;
  const[d,a,b]=await Promise.all([g(u(from)+'/data'),g(u(from)+'/config'),g(u(to)+'/config')]),c={};
  for(const f of a.fields)d[f.key]=f.value;
  for(const f of b.fields){
    const v=d[f.key];delete d[f.key];
    if(typeof v===typeof f.default&&(!f.options||f.options.includes(v))&&JSON.stringify(v)!==JSON.stringify(f.value))c[f.key]=v;
  }
  for(const f of a.fields)delete d[f.key];
  if(Object.keys(d).length)await req('PATCH',u(to)+'/data',d);
  if(Object.keys(c).length)await req('PATCH',u(to)+'/config',c);
}
const carryScript=async(from,to,step)=>{await carrySounds(from,to,step);await carryStore(from,to);};
// Deleting a script keeps its sounds; with sounds they go first, so a failure leaves the script.
async function delScript(n,sounds){
  if(sounds)await req('DELETE','/api/v1/apps/script/'+encodeURIComponent(n)+'/sounds');
  await req('DELETE','/api/v1/apps/'+encodeURIComponent(n));
}
const newRun=(step=()=>{})=>({step,created:[]});
async function soundRoom(run,more=[]){
  const up=run.roomed?[]:(run.pending||[]).concat(more);run.roomed=1;
  if(!up.length)return;
  const s=soundSpace(up,(await api('/api/v1/audio/mp3')).data);
  if(s.fits===false)throw new Error(t('sndRoom').replace('{n}',fmtBytes(s.needed)).replace('{f}',fmtBytes(s.free)));
}
async function freshScript(run,name,id,src,rel,create){
  const plan=soundPlan(rel,[]),say=()=>run.step(t('sndInst').replace('{s}',name));
  say();await create();run.created.push(name);
  try{await soundSync(name,id,plan,run.step,()=>plan.upload.length&&(say(),putScript(name,src).catch(()=>toast(t('sndRestart').replace('{s}',name),false))));}
  catch(e){run.created.pop();await delScript(name,true).catch(()=>{});throw e;}
}
async function prepareHubScriptUpdate(previous,release,readSource=hubScriptRelease){
  const origin=hubScriptOrigin(previous);
  if(!origin||origin.id!==release.id)throw new Error(t('suChanged'));
  const code=await readSource(origin.id,'source');
  if(hubScriptHash(code)!==release.sha256)throw new Error(t('suIntegrity'));
  return {source:hubScriptLink(origin.id,code),modified:hubScriptHash(origin.code)!==origin.sha256};
}
function scriptRequiresHave(apps){
  return new Set(apps.filter(a=>a.origin==='script'||a.origin==='module').map(a=>a.origin==='module'?a.import||a.name:a.name));
}
async function resolveScriptRequires(missing,have,taken,releases=new Map()){
  const seen=new Set(have),deps=[],left=[];
  async function walk(list,depth){
    for(const r of list){
      if(seen.has(r.name))continue;
      seen.add(r.name);
      if(!r.hub){left.push(r.name);continue;}
      try{
        if(taken.has(r.name))throw new Error(t('scrclash'));
        if(!releases.has(r.hub))releases.set(r.hub,hubScriptRelease(r.hub));
        const release=await releases.get(r.hub).catch(e=>{if(e.status===404)return null;throw e;});
        if(!release){left.push(r.name);continue;}
        const code=await hubScriptRelease(r.hub,'source');
        if(hubScriptHash(code)!==release.sha256)throw new Error(t('suIntegrity'));
        if(depth<4)await walk(scriptRequires(code),depth+1);
        deps.push({name:r.name,hub:r.hub,code,sounds:release.sounds});
      }catch(e){if(!e.dep){e.dep=r.name;e.message=r.name+': '+e.message;}throw e;}
    }
  }
  await walk(missing,1);
  return{deps,left};
}
async function putScript(name,source){
  for(let attempt=0;;attempt++){
    try{return(await api('/api/v1/apps/script/'+encodeURIComponent(name),
      {method:'PUT',headers:{'Content-Type':'text/plain'},body:source})).data;}
    catch(e){if(e.code!=='serviceBusy'||attempt>=3)throw e;await new Promise(res=>setTimeout(res,900));}
  }
}
async function installScriptDeps(deps,run=newRun()){
  await soundRoom(run,deps.flatMap(d=>soundPlan(d,[]).upload));
  for(const d of deps){
    try{
      for(const id of scriptIcons(d.code))await idbInstall(id);
      const src=hubScriptLink(d.hub,d.code);
      await freshScript(run,d.name,d.hub,src,d,()=>putScript(d.name,src));
    }catch(e){e.message=t('reqFail').replace('{d}',d.name)+' '+e.message;throw e;}
  }
}
let asking=null;
function askToast(msg,acts){
  if(asking)asking();
  return new Promise(resolve=>{
    const done=v=>{asking=null;resolve(v);};
    const box=toast(msg,false,acts.map(([label,v,pri])=>({label,pri,fn:()=>done(v)})));
    asking=()=>{box.remove();done(null);};
  });
}
function askScriptRequires(target,missing,releases){
  const hub=missing.some(r=>r.hub);
  const list=el('ul',{style:'margin:6px 0 0;padding-left:18px'},missing.map(r=>{
    const hubName=el('span'),state=el('span',null,t(r.hub?'reqHub':'reqSelf'));
    if(r.hub){
      if(!releases.has(r.hub))releases.set(r.hub,hubScriptRelease(r.hub));
      releases.get(r.hub).then(m=>{
        if(typeof m.name==='string'&&m.name&&m.name!==r.name)hubName.textContent=' ('+m.name+')';
        const misfit=fitWords(m.needs,m.display);
        if(misfit)state.textContent=t('reqNoFit').replace('{w}',misfit);
        if(m.sounds?.length&&!capPresent('audio.mp3'))state.append(' · '+t('sndOff'));
      })
        .catch(e=>{if(e.status===404)state.textContent=t('reqGone');});
    }
    return el('li',null,el('b',null,r.name),hubName,': ',state);
  }));
  return askToast(el('div',null,t('reqNeeds').replace('{s}',target),list),
    [...hub?[[t('reqWith'),'deps',1]]:[],[t('reqOnly'),'only',!hub],[t('cancel'),null]]);
}
async function scriptRequiresFlow(target,reqs,{ask=true,skip,run}={}){
  const none={done:[],left:[]};
  if(!reqs.length)return none;
  const{data}=await api('/api/v1/apps');
  const apps=Array.isArray(data)?data:[],have=scriptRequiresHave(apps);
  have.add(target);
  const missing=reqs.filter(r=>!have.has(r.name)),key=target+':'+missing.map(r=>r.name).join();
  if(!missing.length||skip?.has(key))return none;
  const releases=new Map();
  if(ask){
    const pick=await askScriptRequires(target,missing,releases);
    if(pick==='only')skip?.add(key);
    if(pick!=='deps')return pick&&none;
  }
  const plan=await resolveScriptRequires(missing,have,new Set(apps.map(a=>a.name)),releases);
  await installScriptDeps(plan.deps,run);
  return{done:plan.deps.map(d=>d.name),left:plan.left};
}
function scriptRequiresDone(name,res){
  return t(name?'reqDone':'reqGot').replace('{s}',name).replace('{d}',res.done.join(', '))+
    (res.left.length?' · '+t('reqLeft')+' '+res.left.join(', '):'');
}
// HUB SCRIPT UPDATES END
