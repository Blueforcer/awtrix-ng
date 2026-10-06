const uptimeStr=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
  return(d?d+'d ':'')+(h||d?h+'h ':'')+m+'m';};
const wifiWord=r=>r>=-55?t('excellent'):r>=-65?t('good'):r>=-75?t('fair'):t('weak');
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const TONE={ok:'var(--ok)',warn:'var(--warn)',err:'var(--err)',neutral:'var(--acc)'};
function vitalDefs(st){
  const V=[];
  if('batteryPercent'in st){
    const p=st.batteryPercent;
    V.push({ic:'bat',k:t('battery'),v:String(p),u:'%',bar:clamp(p,0,100),
      tone:p>=40?'ok':p>=20?'warn':'err',s:(st.batteryVoltage??0).toFixed(2)+' V'});
  }
  const r=st.wifiRssi;
  V.push({ic:'wifi',k:t('wifi'),v:String(r),u:'dBm',bar:clamp((r+100)*100/60,0,100),
    tone:r>=-65?'ok':r>=-75?'warn':'err',s:wifiWord(r)});
  if('lightLevel'in st){
    const l=st.lightLevel;
    V.push({ic:'sun',k:t('light'),v:String(Math.round(l)),u:'%',bar:clamp(l,0,100),
      tone:'neutral',s:(st.ldrRaw??0)+' raw'});
  }
  if('temperature'in st)
    V.push({ic:'temp',k:t('temp'),v:String(Math.round(st.temperature*10)/10),u:'°',tone:'neutral'});
  if('humidity'in st)
    V.push({ic:'hum',k:t('hum'),v:String(Math.round(st.humidity)),u:'%',
      bar:clamp(st.humidity,0,100),tone:'neutral'});
  if('fps'in st)
    V.push({ic:'fps',k:'FPS',v:String(st.fps),u:'',bar:clamp(st.fps*100/42,0,100),
      tone:st.fps>=40?'ok':st.fps>=32?'warn':'err',s:'/ 42'});
  return V;
}
function vitalTile(d){
  return el('div',{class:'vit'},
    el('div',{class:'k'},icon(d.ic),el('span',null,d.k)),
    el('div',{class:'v'},d.v,d.u?el('u',null,d.u):null),
    d.bar==null?null:el('div',{class:'b','aria-hidden':'true'},
      el('i',{style:'width:'+d.bar.toFixed(1)+'%;background:'+TONE[d.tone]})),
    d.s?el('div',{class:'s'},d.s):null);
}
const metaItem=(k,v,cls)=>el('span',null,k+' ',el('b',cls?{class:cls}:null,String(v)));
function viewDash(view){
  const SC=10;
  const initial=displayGeometry();
  const cv=el('canvas',{id:'screen',width:initial.width*SC,height:initial.height*SC,
    style:'aspect-ratio:'+initial.width+'/'+initial.height,role:'img','aria-label':t('livepre')});
  const ctx=cv.getContext('2d');
  let pw=initial.width,ph=initial.height;
  const bri=el('input',{type:'range',min:0,max:255,value:S.stats?S.stats.brightness:120,
    'aria-label':t('brightness')});
  const briVal=el('span',{class:'val'},String(S.stats?S.stats.brightness:120));
  bri.addEventListener('input',()=>briVal.replaceChildren(bri.value));
  bri.addEventListener('input',debounce(()=>req('PATCH','/api/v1/settings',{brightness:Number(bri.value)}).catch(toastErr),300));
  const autoLabel=FIELDS.autoBrightness.l;
  const autoSw=mkSwitch(false,async()=>{
    setAuto(autoSw.input.checked);
    try{await req('PATCH','/api/v1/settings',{autoBrightness:autoSw.input.checked});}
    catch(e){toastErr(e);}
  });
  autoSw.input.setAttribute('aria-label',autoLabel);
  const autoLab=el('span',{class:'lab'},autoLabel);
  const setAuto=on=>{autoSw.input.checked=on;bri.disabled=on&&hasLight();};
  const showAuto=()=>{
    const d=hasLight()?'':'none';
    autoSw.node.style.display=d;autoLab.style.display=d;
    setAuto(autoSw.input.checked);
  };
  api('/api/v1/settings').then(({data})=>{setAuto(!!data.autoBrightness);paintRot(data.autoTransition!==false);}).catch(()=>{});
  if(S.light===null)api('/api/v1/capabilities').then(({data})=>{readAudioCaps(data||{});showAuto();}).catch(()=>{});
  showAuto();
  const powerSw=mkSwitch(S.stats?!!S.stats.matrixPower:true,async()=>{
    try{await req('PATCH','/api/v1/display',{power:powerSw.input.checked});}catch(e){toastErr(e);}
  });
  powerSw.input.setAttribute('aria-label',t('power'));
  const iconBtn=(ic,lbl,fn)=>{
    const b=el('button',{class:'icon',title:lbl,'aria-label':lbl});
    b.append(icon(ic));b.addEventListener('click',fn);return b;
  };
  const appBtn=(ic,url,lbl)=>iconBtn(ic,lbl,()=>post(url).catch(toastErr));
  // The autoTransition setting, one tap away: paused, the current app stays until someone moves on.
  let rotating=true;
  const rotBtn=iconBtn('pause','',async()=>{
    paintRot(!rotating);
    try{await req('PATCH','/api/v1/settings',{autoTransition:rotating});}
    catch(e){paintRot(!rotating);toastErr(e);}
  });
  const paintRot=on=>{
    rotating=on;
    const lbl=t(on?'rotpause':'rotplay');
    rotBtn.title=lbl;rotBtn.setAttribute('aria-label',lbl);rotBtn.setAttribute('aria-pressed',String(!on));
    rotBtn.replaceChildren(icon(on?'pause':'play'));
  };
  paintRot(true);
  const REC_MAX=240,PNG_SC=100,GIF_SC=20;
  let rec=null,recMs=0,shot=null,native=false;
  const scale=(big,px)=>native?1:Math.max(1,Math.min(big,Math.floor(Math.sqrt(px/(pw*ph)))));
  const stamp=()=>new Date().toISOString().replace(/[-:T]/g,'').slice(0,14);
  const snapBtn=iconBtn('import',t('snap'),()=>{
    if(!shot)return;
    const sc=scale(PNG_SC,16777216);
    const c=el('canvas',{width:pw*sc,height:ph*sc});
    paint(c.getContext('2d'),shot,pw,ph,sc);
    c.toBlob(b=>b&&download('awtrix-'+stamp()+'.png',b));
  });
  const recBtn=iconBtn('rec',t('rec'),()=>toggleRec());
  function toggleRec(){
    const done=rec;
    rec=done?null:[];recMs=0;
    const lbl=t(rec?'recstop':'rec');
    recBtn.classList.toggle('danger',!!rec);
    recBtn.title=lbl;recBtn.setAttribute('aria-label',lbl);
    if(done&&done.length){
      let sc=scale(GIF_SC,Math.min(2e7,6e7/done.length)),gif;
      while((gif=gifEncode(done,pw,ph,sc)).length>8e6&&sc>1)sc=Math.max(1,Math.floor(sc*Math.sqrt(7.6e6/gif.length)));
      download('awtrix-'+stamp()+'.gif',new Blob([gif],{type:'image/gif'}));
    }
  }
  const nativeBtn=el('button',{class:'icon native',title:t('recNative'),'aria-label':t('recNative'),'aria-pressed':'false'},'1:1');
  nativeBtn.addEventListener('click',()=>{native=!native;nativeBtn.setAttribute('aria-pressed',String(native));});
  view.append(el('div',{class:'hero'},cv,
    el('div',{class:'hctl'},
      powerSw.node,el('span',{class:'lab'},t('power')),
      el('span',{class:'lab'},t('brightness')),bri,briVal,
      autoSw.node,autoLab,
      appBtn('prev','/api/v1/apps/previous',t('prevapp')),
      rotBtn,
      appBtn('next','/api/v1/apps/next',t('nextapp')),
      iconBtn('bell',t('dismiss'),async()=>{
        try{await req('DELETE','/api/v1/notifications/active');toast(t('dismissed'));}
        catch(e){toastErr(e);}}),
      snapBtn,recBtn,nativeBtn)));
  pollScreen((px,{width:W,height:H})=>{
    if(W!==pw||H!==ph){cv.width=W*SC;cv.height=H*SC;cv.style.aspectRatio=W+'/'+H;pw=W;ph=H;}
    shot=px;
    if(rec){
      const f={t:Date.now(),px:px.slice(0,W*H)},d=rec.length?gifDelay(rec.at(-1),f)*10:0;
      if(recMs+d+100>1e4)toggleRec();
      else{rec.push(f);recMs+=d;if(rec.length>=REC_MAX)toggleRec();}
    }
    paint(ctx,px,W,H,SC);
  },()=>rec?40:250);
  const vg=el('div',{class:'vitals'});
  const meta=el('div',{class:'meta'});
  view.append(vg,meta);
  function renderVitals(st){
    vg.replaceChildren(...vitalDefs(st).map(vitalTile));
    const upd=updAvailable(updCached());
    meta.replaceChildren(
      upd?metaItem(t('version'),st.version+' → '+upd.version+' '+t('updavail'),'warn'):metaItem(t('version'),st.version),
      metaItem(t('host'),st.hostname||S.sysHost),metaItem(t('ip'),st.ipAddress),
      metaItem(t('uptime'),uptimeStr(st.uptimeSeconds)),
      metaItem(t('ram'),fmtBytes(st.freeHeapBytes)),
      ...(st.psramTotalBytes?[metaItem(t('psram'),fmtBytes(st.psramFreeBytes)+' / '+fmtBytes(st.psramTotalBytes))]
        :st.soc==='esp32s3'?[metaItem(t('psram'),t('psramnone'),'warn')]:[]),
      metaItem(t('curapp'),st.currentApp));
  }
  if(S.stats)renderVitals(S.stats);
  poller(async()=>{
    const{data}=await api('/api/v1/device',{timeout:4000});
    setStats(data);
    powerSw.input.checked=!!data.matrixPower;
    if(document.activeElement!==bri){bri.value=data.brightness;briVal.replaceChildren(String(data.brightness));}
    renderVitals(data);
  },2000);
}
