function actionRow(label,help,ctls,key){
  return el('div',{class:'frow'},
    el('div',{class:'lab'},
      el('div',{class:'l1'},el('b',null,label),key?el('span',{class:'key'},key):null),
      help?el('div',{class:'help'},help):null),
    el('div',{class:'ctl'},ctls));
}
function progressRow(){
  const fill=el('i',{style:'width:0%'});
  const row=el('div',{class:'frow full',style:'display:none'},el('div',{class:'bar'},fill));
  return{row,
    show(){fill.style.width='0%';row.style.display='';},
    hide(){row.style.display='none';},
    set(p){fill.style.width=(p*100).toFixed(1)+'%';}};
}
const RELEASES_API='https://api.github.com/repos/Blueforcer/awtrix-ng/releases/latest';
const OTA_BASE='https://blueforcer.github.io/awtrix-ng/firmware/ota/';
let firmwareBusy=false;
const UPD_KEY='awtrixUpdate';
const UPD_TTL_MS=6*3600*1000;
const verParts=v=>String(v||'').replace(/^v/,'').split('.').map(n=>parseInt(n,10)||0);
function verNewer(a,b){
  const x=verParts(a),y=verParts(b);
  for(let i=0;i<3;i++)if((x[i]||0)!==(y[i]||0))return(x[i]||0)>(y[i]||0);
  return false;
}
function updCached(){
  try{const c=JSON.parse(localStorage.getItem(UPD_KEY)||'null');if(c&&Date.now()-c.at<UPD_TTL_MS)return c;}catch(e){}
  return null;
}
async function updCheck(){
  if(navigator.onLine===false)throw new Error(t('updfail'));
  const r=await fetch(RELEASES_API,{headers:{Accept:'application/vnd.github+json'},cache:'no-store',signal:AbortSignal.timeout(10000)});
  if(!r.ok)throw new Error(t((r.status===403&&r.headers?.get('x-ratelimit-remaining')==='0')||r.status===429?'updrate':'updfail'));
  const j=await r.json();
  const assets={};
  for(const a of j.assets||[])if(a&&a.name)assets[a.name]=a.browser_download_url;
  const c={at:Date.now(),version:String(j.tag_name||'').replace(/^v/,''),usable:!j.draft&&!j.prerelease,
    published:String(j.published_at||'').slice(0,10),notes:j.html_url||'',assets};
  try{localStorage.setItem(UPD_KEY,JSON.stringify(c));}catch(e){}
  return c;
}
function updAvailable(c){
  const run=S.stats&&S.stats.version;
  return c&&c.usable&&run&&verNewer(c.version,run)?c:null;
}
async function downloadFirmware(release,onProgress){
  const image=S.stats&&S.stats.updateImage;
  const awup=image==='awtrix-ng-tc002.awup';
  if(!awup&&!['firmware-awtrix-ng.bin','firmware-awtrix-ng-s3-octal.bin','firmware-awtrix-ng-s3-quad.bin'].includes(image))throw new Error(t('updnotready'));
  const options={credentials:'omit',cache:'no-store',signal:AbortSignal.timeout(120000)};
  const response=await fetch(OTA_BASE+'index.json',options);
  if(!response.ok)throw new Error(t('updnotready'));
  const manifest=await response.json();
  const asset=manifest.assets&&manifest.assets[image];
  if(manifest.version!=='v'+release.version||!/^v\d+\.\d+\.\d+$/.test(manifest.version)||!asset)throw new Error(t('updnotready'));
  if(!Number.isSafeInteger(asset.size)||asset.size<256||asset.size>(awup?8:16)*1024*1024||!/^[a-f0-9]{64}$/.test(asset.sha256))throw new Error(t('updbadfile'));
  const r=await fetch(OTA_BASE+manifest.version+'/'+image,options);
  if(!r.ok)throw new Error('HTTP '+r.status);
  const reader=r.body.getReader(),bytes=new Uint8Array(asset.size);
  let offset=0;
  try{
    for(;;){
      const {done,value}=await reader.read();
      if(done)break;
      if(offset+value.length>bytes.length)throw new Error(t('updbadfile'));
      bytes.set(value,offset);offset+=value.length;onProgress(offset/bytes.length);
    }
  }finally{await reader.cancel();}
  if(offset!==asset.size||(awup?String.fromCharCode(...bytes.subarray(0,8))!=='AWUPD003':bytes[0]!==0xe9)||iconSha256(bytes)!==asset.sha256)throw new Error(t('updbadfile'));
  return new File([bytes],image,{type:'application/octet-stream'});
}
function updateRow(install){
  let available=null;
  const installBtn=armable(el('button',{id:'upd-install','aria-label':t('updchk')},t('updchk')),()=>{
    if(available)install(available);else show(true);
  },()=>!!available);
  const notes=el('a',{id:'upd-notes',style:'padding:4px;line-height:0',href:'#',target:'_blank',rel:'noopener',title:t('updnotes'),'aria-label':t('updnotes')},icon('file'));
  const status=el('div',{id:'upd-status',class:'help'},'');
  notes.hidden=true;
  async function show(force){
    if(firmwareBusy)return;
    available=null;
    const cached=updCached();
    status.textContent=t('version')+' '+((S.stats&&S.stats.version)||'');
    if(!force&&!cached)return;
    installBtn.disabled=true;if(force)status.textContent=t('updchecking');
    try{
      const avail=updAvailable(force?await updCheck():cached);
      const img=S.stats&&S.stats.updateImage;
      if(avail){
        status.textContent=t('updnew').replace('{v}',avail.version)+(avail.published?' ('+avail.published+')':'');
        if(img&&avail.assets[img]){available=avail;installBtn.disabled=false;}
        notes.hidden=!avail.notes;
        if(!notes.hidden)notes.href=avail.notes;
      }else{
        status.textContent=t('updcur').replace('{v}',(S.stats&&S.stats.version)||'');
        notes.hidden=true;
      }
    }catch(e){status.textContent=t('version')+' '+((S.stats&&S.stats.version)||'');notes.hidden=true;toast(e.message===t('updrate')?e.message:t('updfail'),false);}
    installBtn.textContent=t(available?'updinstall':'updchk');
    installBtn.setAttribute('aria-label',installBtn.textContent);
    installBtn.disabled=false;
  }
  show(false);
  const row=actionRow(t('update'),null,[notes,installBtn]);
  row.firstChild.append(status);
  return row;
}
async function waitForDevice(){
  const pause=ms=>new Promise(r=>setTimeout(r,ms));
  await pause(15000);
  for(const end=Date.now()+180000;Date.now()<end;await pause(3000)){
    try{if((await fetch('/api/v1/device',{cache:'no-store',signal:AbortSignal.timeout(3000)})).ok)return;}catch(e){}
  }
}
function maintenanceSection(page){
  const awup=/\.awup$/.test((S.stats&&S.stats.updateImage)||'');
  const fwIn=el('input',{type:'file',accept:awup?'.awup':'.bin'});
  const fwBtn=el('button',null,t('choose'));
  const fw=progressRow();
  fwBtn.addEventListener('click',()=>fwIn.click());
  const fwStatus=el('div',{id:'fw-status',class:'help',role:'status'},'');
  async function installFirmware(source){
    if(firmwareBusy){toast(t('updbusy'),false);return;}
    firmwareBusy=true;fwBtn.disabled=true;fw.show();
    const warn=e=>{e.preventDefault();e.returnValue='';};
    window.addEventListener('beforeunload',warn);
    const installBtn=document.getElementById('upd-install');
    if(installBtn)installBtn.disabled=true;
    try{
      let file=source;
      if(!(source instanceof File)){
        fwStatus.textContent=t('upddownloading');
        file=await downloadFirmware(source,p=>{fw.set(p);fwStatus.textContent=t('upddownloading')+' '+Math.round(p*100)+'%';});
      }
      fw.set(0);fwStatus.textContent=t('upduploading');
      const x=await xhrPost('/update','firmware',file,file.name,
        p=>{fw.set(p);fwStatus.textContent=t('upduploading')+' '+Math.round(p*100)+'%';},120000);
      let result;try{result=JSON.parse(x.responseText);}catch(e){}
      if(x.status<200||x.status>=300||!result||result.ok!==true)throw new Error((awup&&result&&result.error&&result.error.message)||'HTTP '+x.status+' - '+x.responseText.slice(0,80));
      fwStatus.textContent=t('rebooting');toast(t('rebooting'));
      window.removeEventListener('beforeunload',warn);
      if(result.applying)waitForDevice().then(()=>location.reload());
      else setTimeout(()=>location.reload(),12000);
    }catch(e){
      fwStatus.textContent='';toastErr(e);fw.hide();
      firmwareBusy=false;fwBtn.disabled=false;if(installBtn)installBtn.disabled=false;
      window.removeEventListener('beforeunload',warn);
    }
  }
  fwIn.addEventListener('change',()=>{const file=fwIn.files[0];if(file)installFirmware(file);});
  fwIn.addEventListener('click',()=>{fwIn.value='';});
  const rebootBtn=armable(el('button',null,t('reboot')),()=>doReboot());
  const resetBtn=armable(el('button',{class:'danger'},t('resetset')),async()=>{
    try{await post('/api/v1/settings/reset');toast(t('saved'));}catch(e){toastErr(e);}
  });
  const hostIn=el('input',{type:'text',placeholder:S.sysHost,autocomplete:'off'});
  const eraseBtn=el('button',{class:'danger'},t('erase'));
  eraseBtn.addEventListener('click',async()=>{
    if(!S.sysHost||hostIn.value!==S.sysHost){toast(t('eraseNo'),false);return;}
    eraseBtn.disabled=true;
    try{await post('/api/v1/device/factory-reset');toast(t('rebooting'));setTimeout(()=>location.reload(),12000);}
    catch(e){toastErr(e);eraseBtn.disabled=false;}
  });
  page.section('maint',t('maint'),t('mainthelp'),[
    updateRow(installFirmware),
    actionRow(awup?t('fwfile').replace('.bin','.awup'):t('fwfile'),null,[fwBtn,fwIn]),
    fwStatus,fw.row,
    actionRow(t('reboot'),null,[rebootBtn]),
    actionRow(t('resetset'),t('resetsetH'),[resetBtn]),
    actionRow(t('factory'),t('factoryH'),[hostIn,eraseBtn]),
  ]);
}
const BK_CATS=[['wifi','bkWifi'],['settings','bkSettings'],['icons','bkIcons'],
  ['melodies','bkMelodies'],['palettes','bkPalettes'],['mp3','bkMp3'],
  ['scripts','bkScripts'],['apporder','bkApporder']];
function backupSection(page){
  page.section('backup',t('bkSection'),t('bkSectionH'),[...backupRows(),...restoreRows(t('bkRestoreH'))]);
}
function backupRows(){
  const checks={};
  const avail=BK_CATS.filter(([id])=>
    (id!=='melodies'||hasSink('rtttl'))&&(id!=='mp3'||hasSink('mp3')));
  const all=el('input',{type:'checkbox',checked:true});
  const dl=el('button',{class:'pri'},t('bkDownload'));
  const syncAll=()=>{const n=avail.filter(([id])=>checks[id].checked).length;
    all.checked=n===avail.length;all.indeterminate=n>0&&n<avail.length;dl.disabled=!n;};
  const boxes=[el('label',{style:'display:inline-flex;align-items:center;gap:.3rem;margin-right:1rem'},all,t('bkAll')),...avail.map(([id,lbl])=>{
    const cb=el('input',{type:'checkbox',checked:true});checks[id]=cb;cb.addEventListener('change',syncAll);
    return el('label',{style:'display:inline-flex;align-items:center;gap:.3rem;margin-right:1rem'},cb,t(lbl));
  })];
  all.addEventListener('change',()=>{avail.forEach(([id])=>{checks[id].checked=all.checked;});syncAll();});
  dl.addEventListener('click',async()=>{
    const cats={};
    avail.forEach(([id])=>{cats[id]=checks[id].checked;});
    dl.disabled=true;dl.replaceChildren(t('bkWorking'));
    try{
      const blob=zipStore(await collectBackup(cats));
      download('awtrix-backup-'+(S.sysHost||'awtrix')+'-'+new Date().toISOString().slice(0,10)+'.zip',blob,'application/zip');
      toast(t('bkDone'));
    }catch(e){toastErr(e);}
    dl.replaceChildren(t('bkDownload'));syncAll();
  });
  return [el('div',{class:'frow full'},el('div',null,...boxes)),actionRow(t('bkTitle'),t('bkDownloadH'),[dl])];
}
function restoreRows(hint){
  const rsIn=el('input',{type:'file',accept:'.zip,application/zip'});
  const rsBtn=el('button',null,t('bkRestore'));
  const rs=progressRow();
  rsBtn.addEventListener('click',()=>rsIn.click());
  rsIn.addEventListener('click',()=>{rsIn.value='';});
  rsIn.addEventListener('change',async()=>{
    const f=rsIn.files[0];if(!f)return;
    rsBtn.disabled=true;rs.show();
    try{
      const r=await restoreBackup(f,p=>rs.set(p));
      const a=r.applied||{},wl=r.warnings||[];
      toast(t('bkRestored')+(wl.length?' - '+wl.length+' '+t('bkWarnings'):''));
      wl.slice(0,5).forEach(w=>toast(w,false));
      if(a.wifi||a.system||a.settings||a.appLoop)
        toast(t('bkRebootQ'),true,[{label:t('rebootnow'),pri:true,fn:()=>doReboot()}]);
    }catch(e){toastErr(e);}
    rsBtn.disabled=false;rs.hide();
  });
  return [actionRow(t('bkRestoreT'),hint,[rsBtn,rsIn]),rs.row];
}
