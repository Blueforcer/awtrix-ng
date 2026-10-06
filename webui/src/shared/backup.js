function download(filename,data,type){
  const blob=data instanceof Blob?data:new Blob([data],{type:type||'text/plain'});
  const url=URL.createObjectURL(blob);
  const a=el('a',{href:url,download:filename});
  document.body.append(a);a.click();a.remove();
  setTimeout(()=>URL.revokeObjectURL(url),1000);
}
const crc32=(()=>{const t=new Uint32Array(256);
  for(let i=0;i<256;i++){let c=i;for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[i]=c>>>0;}
  return b=>{let c=0xFFFFFFFF;for(let i=0;i<b.length;i++)c=t[(c^b[i])&0xFF]^(c>>>8);return(c^0xFFFFFFFF)>>>0;};})();
function zipStore(entries){
  const enc=new TextEncoder(),parts=[],central=[];let off=0;
  const u16=v=>[v&0xFF,(v>>8)&0xFF],u32=v=>[v&0xFF,(v>>8)&0xFF,(v>>16)&0xFF,(v>>>24)&0xFF];
  const now=new Date(),year=Math.max(1980,Math.min(2107,now.getFullYear()));
  const dosTime=(now.getHours()<<11)|(now.getMinutes()<<5)|(now.getSeconds()>>1);
  const dosDate=((year-1980)<<9)|((now.getMonth()+1)<<5)|now.getDate();
  for(const e of entries){
    const name=enc.encode(e.name);
    const data=e.data instanceof Uint8Array?e.data:enc.encode(String(e.data));
    const crc=crc32(data),sz=data.length;
    parts.push(new Uint8Array([...u32(0x04034b50),...u16(20),...u16(0),...u16(0),...u16(dosTime),...u16(dosDate),
      ...u32(crc),...u32(sz),...u32(sz),...u16(name.length),...u16(0)]),name,data);
    central.push(new Uint8Array([...u32(0x02014b50),...u16(20),...u16(20),...u16(0),...u16(0),...u16(dosTime),...u16(dosDate),
      ...u32(crc),...u32(sz),...u32(sz),...u16(name.length),...u16(0),...u16(0),...u16(0),...u16(0),
      ...u32(0),...u32(off)]),name);
    off+=30+name.length+sz;
  }
  let cdSize=0;for(const c of central)cdSize+=c.length;
  const eocd=new Uint8Array([...u32(0x06054b50),...u16(0),...u16(0),...u16(entries.length),...u16(entries.length),
    ...u32(cdSize),...u32(off),...u16(0)]);
  return new Blob([...parts,...central,eocd],{type:'application/zip'});
}
const baseName=n=>String(n).split('/').pop();
async function collectBackup(cats){
  const entries=[],included=[];
  const bytes=async p=>{const r=await fetch(p);if(!r.ok)throw new Error(p+': HTTP '+r.status);return new Uint8Array(await r.arrayBuffer());};
  const text=async p=>(await fetch(p)).text();
  const list=async d=>((await api('/api/v1/files?dir='+encodeURIComponent(d))).data.files||[]);
  const grab=async dir=>{for(const f of await list(dir)){const b=baseName(f.name);
    entries.push({name:dir.slice(1)+'/'+b,data:await bytes(dir+'/'+encodeURIComponent(b))});}};
  let sys=null;
  if(cats.wifi||cats.settings)sys=(await api('/api/v1/system?secrets=1')).data;
  if(cats.wifi){entries.push({name:'config/wifi.json',
    data:JSON.stringify({wifiSsid:sys.wifiSsid||'',wifiPass:sys.wifiPass||''})});included.push('wifi');}
  if(cats.settings){
    const s={...sys};delete s.wifiSsid;delete s.wifiPass;
    entries.push({name:'config/system.json',data:JSON.stringify(s)});
    entries.push({name:'config/settings.json',data:JSON.stringify((await api('/api/v1/settings')).data)});
    included.push('settings');}
  if(cats.icons){await grab('/ICONS');
    try{const{data}=await api('/api/v1/icons/origins');if(!Array.isArray(data.icons))throw new Error(t('icoriginfail'));
      entries.push({name:'config/icon-origins.json',data:JSON.stringify(data)});}catch(e){if(e.status!==404)throw e;}
    included.push('icons');}
  if(cats.melodies){for(const m of((await api('/api/v1/audio/melodies')).data.melodies||[]))
    entries.push({name:'MELODIES/'+m.name+'.txt',data:m.rtttl});included.push('melodies');}
  if(cats.palettes){await grab('/PALETTES');included.push('palettes');}
  if(cats.mp3){await grab('/MP3');included.push('mp3');}
  if(cats.scripts){await grab('/SCRIPTS');
    for(const s of(await api('/api/v1/audio/mp3')).data.scripts||[])for(const f of s.files){const p='SCRIPTS/'+s.name+'/'+f.name;
      entries.push({name:p,data:await bytes('/'+p)});}
    included.push('scripts');}
  if(cats.apporder){try{entries.unshift({name:'apploop.json',data:await text('/apploop.json')});included.push('apporder');}catch(e){}}
  entries.unshift({name:'manifest.json',data:JSON.stringify(
    {app:'awtrix-ng',backupFormat:1,createdAt:new Date().toISOString(),
     device:{hostname:S.sysHost},categories:included})});
  return entries;
}
function xhrPost(url,field,file,name,onProgress,timeout=0){
  return new Promise((res,rej)=>{
    const fd=new FormData();fd.append(field,file,name);
    const x=new XMLHttpRequest();
    x.open('POST',url);
    x.timeout=timeout;
    x.ontimeout=()=>rej(new Error(t('neterr')));
    x.upload.onprogress=e=>{if(e.lengthComputable&&onProgress)onProgress(e.loaded/e.total);};
    x.onload=()=>res(x);
    x.onerror=()=>rej(new Error(t('neterr')));
    x.send(fd);
  });
}
async function restoreBackup(file,onProgress){
  const x=await xhrPost('/api/v1/restore','file',file,file.name,onProgress);
  let d={};try{d=JSON.parse(x.responseText);}catch(e){}
  const er=d.error;
  if(x.status>=300)throw new Error(er?.message||er?.code||(typeof er=='string'?er:'HTTP '+x.status));
  return d;
}
