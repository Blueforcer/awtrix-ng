// Catalogue and submission live under one prefix on the AWTRIX Hub; the two
// constants stay separate so a test hub can take submissions while the icons
// still come from production. The localStorage keys below override both and are
// the only lever if the Hub ever moves hostname.
const ICONDB_URL_DEFAULT='https://awtrix.de/icons/';
const ICONAPI_URL_DEFAULT='https://awtrix.de/icons/';
function cfgUrl(key,def){
  let u=def;
  try{u=localStorage[key]||def;}catch(e){}
  return u.endsWith('/')?u:u+'/';
}
const iconDbUrl=()=>cfgUrl('awtrixIconDbUrl',ICONDB_URL_DEFAULT);
const iconApiUrl=()=>cfgUrl('awtrixIconApiUrl',ICONAPI_URL_DEFAULT);
const iconOriginHub=hub=>hub==='https://hub.flows.blueforcer.de/icons/'&&iconDbUrl()===ICONDB_URL_DEFAULT?ICONDB_URL_DEFAULT:hub;
let idbCache=null;
async function idbLoad(){
  if(!idbCache){
    const r=await fetch(iconDbUrl()+'index.json',{cache:'no-cache',signal:AbortSignal.timeout(10000)});
    if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();
    idbCache=(d.icons||[]).map(row=>({slug:row[0],name:row[1]||row[0],w:row[2],h:row[3],
      frames:row[4],bytes:row[5],key:(row[0]+' '+(row[1]||'')).toLowerCase()}));
  }
  return idbCache;
}
// No 'icons/' segment: that came from the GitHub Pages directory layout and
// would ask the Hub for /icons/icons/<slug>.gif, which is a 404 - the Hub
// serves the bytes straight under the catalogue prefix.
const idbBytesUrl=(slug,ext)=>iconDbUrl()+encodeURIComponent(slug)+'.'+ext;
function hubDownloadError(key){const error=new Error(t(key));error.code='hubAuthentication';return error;}
async function hubDownloadFile(value){
  const url=new URL(value),configured=new URL(iconApiUrl());
  // Provenance files may contain arbitrary hosts. Never forward our token there.
  if(url.protocol!=='https:'||url.origin!==configured.origin||url.username||url.password)
    throw new Error(t('hubWrongHost'));
  return hubFetch(url.href,{},true,'idbfail');
}
async function hubFetch(url,headers,auth,offline,ms=15000){
  if(auth){
    const token=hubToken();
    if(!token)throw hubDownloadError('hubAuthRequired');
    headers.Authorization='Bearer '+token;
  }
  let r;
  try{r=await fetch(url,{credentials:'omit',redirect:'error',cache:'no-store',headers,signal:AbortSignal.timeout(ms)});}
  catch(e){throw new Error(t(offline));}
  if(r.status===401||r.status===403)throw hubDownloadError('hubAuthRejected');
  return r;
}
let focusHubSettings=false;
function goToHubSettings(){
  focusHubSettings=true;
  const section=$('#sec-hub');
  if(currentRoute()==='system'&&section){
    focusHubSettings=false;
    section.scrollIntoView?.({behavior:'smooth',block:'start'});
    section.querySelector('input')?.focus();
    return;
  }
  location.hash='#/system';
}
function connectHubAction(){return{label:t('hubConnect'),fn:goToHubSettings};}
async function idbFetch(slug){
  let last=404;
  for(const ext of ['gif','jpg']){
    const r=await hubDownloadFile(idbBytesUrl(slug,ext));
    if(r.ok)return{blob:await r.blob(),ext};
    last=r.status;
    if(r.status!==404)break;
  }
  throw new Error(t(last===404?'hubIconUnavailable':'hubDownloadFailed'));
}
// WebCrypto is not available on an HTTP device origin. This small SHA-256
// fallback uses the same bytes on HTTP and HTTPS; it never hashes GIF metadata
// as a substitute for the Hub's decoded-content fingerprint.
function iconSha256(bytes){
  const primes=[];
  for(let n=2;primes.length<64;n++)if(primes.every(p=>n%p))primes.push(n);
  const k=primes.map(p=>Math.cbrt(p)*2**32>>>0),h=primes.slice(0,8).map(p=>Math.sqrt(p)*2**32>>>0);
  const padded=new Uint8Array(Math.ceil((bytes.length+9)/64)*64);padded.set(bytes);padded[bytes.length]=128;
  const v=new DataView(padded.buffer);v.setUint32(padded.length-8,Math.floor(bytes.length/0x20000000));v.setUint32(padded.length-4,bytes.length*8);
  const w=new Uint32Array(64),rr=(x,n)=>(x>>>n)|(x<<(32-n));
  for(let offset=0;offset<padded.length;offset+=64){
    for(let i=0;i<16;i++)w[i]=v.getUint32(offset+i*4);
    for(let i=16;i<64;i++){const x=w[i-15],y=w[i-2];w[i]=w[i-16]+(rr(x,7)^rr(x,18)^(x>>>3))+w[i-7]+(rr(y,17)^rr(y,19)^(y>>>10));}
    let[a,b,c,d,e,f,g,j]=h;
    for(let i=0;i<64;i++){const t1=(j+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^(~e&g))+k[i]+w[i])>>>0;
      const t2=((rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c)))>>>0;
      j=g;g=f;f=e;e=(d+t1)>>>0;d=c;c=b;b=a;a=(t1+t2)>>>0;}
    [a,b,c,d,e,f,g,j].forEach((n,i)=>h[i]=(h[i]+n)>>>0);
  }
  return h.map(n=>n.toString(16).padStart(8,'0')).join('');
}
async function iconBlobHash(blob){
  const bytes=await new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(new Uint8Array(r.result));r.onerror=reject;r.readAsArrayBuffer(blob);});
  return iconSha256(bytes);
}
function validIconOrigin(o){
  if(!o||!/^[-A-Za-z0-9_]{1,32}\.(gif|jpg)$/.test(o.name)||!/^[-a-z0-9_]{1,32}$/.test(o.slug)||!/^[a-f0-9]{64}$/.test(o.sha256))return false;
  try{const u=new URL(o.hub);return u.protocol==='https:'&&!u.username&&!u.password&&!u.search&&!u.hash&&u.pathname.endsWith('/');}catch(e){return false;}
}
async function saveIconOrigin(name,slug,sha256,hub=iconDbUrl()){
  const origin={name,hub:new URL(hub,location.href).href,slug,sha256};
  if(!validIconOrigin(origin))throw new Error(t('icoriginfail'));
  await req('PUT','/api/v1/icons/origins',origin);return origin;
}
async function readIconBlob(name){
  const r=await fetch('/ICONS/'+encodeURIComponent(name),{cache:'no-store'});
  if(!r.ok)throw new Error('HTTP '+r.status);return r.blob();
}
async function hubIconMetadata(slug,hub=iconDbUrl()){
  let r;
  try{r=await fetch(hub+encodeURIComponent(slug)+'/metadata.json',{cache:'no-cache',signal:AbortSignal.timeout(10000)});}
  catch(e){throw new Error(t('idbfail'));}
  if(!r.ok)throw new Error(t(r.status===404?'hubIconUnavailable':'idbfail'));
  const m=await r.json();if(m.slug!==slug||!/^[a-f0-9]{64}$/.test(m.sha256))throw new Error(t('idbfail'));return m;
}
async function iconInventory(files){
  if(!files){const{data}=await api('/api/v1/files?dir=%2FICONS',{cache:'no-store'});files=data.files||[];}
  let origins=[],originsReadable=true;
  try{const{data}=await api('/api/v1/icons/origins',{cache:'no-store'});
    if(!Array.isArray(data.icons))throw new Error(t('icoriginfail'));origins=data.icons.filter(validIconOrigin);
  }catch(e){originsReadable=e.status===404;}
  const records=new Map(origins.map(o=>[o.name,o]));
  let catalogue=[];
  if(files.some(f=>!records.has(f.name))&&navigator.onLine!==false)try{catalogue=await idbLoad();}catch(e){}
  const published=new Set(catalogue.map(r=>r.slug));
  const out=[];
  // Sequential reads keep the small device's single HTTP connection responsive.
  for(const f of files){
    const item={...f,state:originsReadable?'local':'unknown',origin:records.get(f.name)||null,sha256:null};
    try{
      item.sha256=await iconBlobHash(await readIconBlob(f.name));
      if(item.origin)item.state=item.sha256===item.origin.sha256?'hub':'modified';
      else{const slug=f.name.replace(/\.[^.]+$/,'');
        if(published.has(slug))try{
          const m=await hubIconMetadata(slug);
          if(m.sha256===item.sha256){item.origin=await saveIconOrigin(f.name,slug,item.sha256);item.state='hub';}
        }catch(e){}
      }
    }catch(e){item.state='unknown';}
    out.push(item);
  }
  return out;
}
async function idbInstall(slug){
  const{blob,ext}=await idbFetch(slug),hash=await iconBlobHash(blob);
  const{data}=await api('/api/v1/files?dir=%2FICONS',{cache:'no-store'});
  const existing=(data.files||[]).filter(f=>f.name.replace(/\.[^.]+$/,'')===slug);
  let same=false;
  for(const f of existing){const localHash=await iconBlobHash(await readIconBlob(f.name));
    if(localHash!==hash)throw new Error(t('icconflict')+' '+slug);
    if(f.name===slug+'.'+ext&&localHash===hash)same=true;
  }
  if(!same)await uploadFile(blob,'/ICONS',slug+'.'+ext,null);
  await dropStale('/ICONS',slug+'.'+(ext==='gif'?'jpg':'gif'),slug+'.'+ext);
  try{await saveIconOrigin(slug+'.'+ext,slug,hash);}catch(e){toast(t('icoriginfail'),false);}
  return ext;
}
async function installedIconIds(){
  const files=await iconInventory();
  return new Set(files.filter(f=>f.state==='hub'&&iconOriginHub(f.origin.hub)===iconDbUrl()&&f.origin.slug===f.name.replace(/\.[^.]+$/,'')).map(f=>f.origin.slug));
}
async function installScriptIcons(ids,have){
  // Always recheck the actual bytes: an editor or another browser may have
  // changed a same-named file since the script page was opened.
  if(!hubToken()){toast(t('hubAuthRequired'),false,[connectHubAction()]);return 0;}
  const done=[],broke=[];
  for(const id of ids){try{await idbInstall(id);done.push(id);}catch(e){broke.push(id+': '+e.message);}}
  if(done.length)toast(t('scriconsok')+': '+done.join(', '));
  if(broke.length)toast(t('scriconserr')+' '+broke.join(', '),false,[connectHubAction()]);
  return done.length;
}
async function idbSubmit(blob,name,basedOn,update=null){
  name=String(name||'').trim();
  if(!descriptiveIconName(name)){const err=new Error(t('idbe_descriptiveNameRequired'));err.code='descriptiveNameRequired';throw err;}
  const fd=new FormData();
  fd.append('file',blob,'icon.gif');
  fd.append('name',name);
  fd.append('source','webui');
  fd.append('agree','1');
  fd.append('response','resolve');
  if(basedOn)fd.append('based_on',basedOn);
  if(update){fd.append('update_slug',update.slug);fd.append('expected_sha256',update.sha256);}
  const tok=hubToken();
  let r;
  try{r=await fetch(iconApiUrl()+'submit',
    tok?{method:'POST',body:fd,headers:{Authorization:'Bearer '+tok}}:{method:'POST',body:fd});}
  catch(e){throw new Error(t('idbfail'));}
  let d={};
  try{d=await r.json();}catch(e){}
  if(r.ok&&d.ok){idbCache=null;return d;}
  // Prefer translated messages, then the Hub's explanation. Never expose a raw
  // error code when the user needs a next step.
  const known=STR['idbe_'+d.error];
  const err=new Error((known?t('idbe_'+d.error):'')||d.message||t('hubPublishFailed'));
  err.code=d.error||'';
  err.slug=d.slug&&d.error==='duplicate'?d.slug:null;
  err.pr=d.pr||'';
  throw err;
}
