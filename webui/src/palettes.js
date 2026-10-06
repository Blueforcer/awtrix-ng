const PAL_BUILTIN=[
  ['Cloud','0000FF 00008B 00008B 00008B 00008B 00008B 00008B 00008B 0000FF 00008B 87CEEB 87CEEB ADD8E6 FFFFFF ADD8E6 87CEEB'],
  ['Lava','000000 800000 000000 800000 8B0000 8B0000 800000 8B0000 8B0000 8B0000 FF0000 FFA500 FFFFFF FFA500 FF0000 8B0000'],
  ['Ocean','191970 00008B 191970 000080 00008B 0000CD 2E8B57 008080 5F9EA0 0000FF 008B8B 6495ED 7FFFD4 2E8B57 00FFFF 87CEFA'],
  ['Forest','006400 006400 556B2F 006400 008000 228B22 6B8E23 008000 2E8B57 66CDAA 32CD32 9ACD32 90EE90 7CFC00 66CDAA 228B22'],
  ['Stripe','FF0000 000000 AB5500 000000 ABAB00 000000 00FF00 000000 00AB55 000000 0000FF 000000 5500AB 000000 AB0055 000000'],
  ['Party','5500AB 84007C B5004B E5001B E81700 B84700 AB7700 ABAB00 AB5500 DD2200 F2000E C2003E 8F0071 5F00A1 2F00D0 0007F9'],
  ['Heat','000000 330000 660000 990000 CC0000 FF0000 FF3300 FF6600 FF9900 FFCC00 FFFF00 FFFF33 FFFF66 FFFF99 FFFFCC FFFFFF'],
  ['Rainbow','FF0000 D52A00 AB5500 AB7F00 ABAB00 56D500 00FF00 00D52A 00AB55 0056AA 0000FF 2A00D5 5500AB 7F0081 AB0055 D5002B'],
];
const palFind=name=>PAL_BUILTIN.find(p=>p[0].toLowerCase()===name.toLowerCase());
const palBuiltin=name=>{
  const hit=palFind(name);
  return{stops:(hit?hit[1].split(' '):[]).map(h=>({c:'#'+h,p:0})),placed:false};
};
const palIsBuiltin=name=>{const hit=palFind(name);return hit?hit[0]:'';};
const palNew=()=>({stops:[{c:'#FF0000',p:0},{c:'#0000FF',p:100}],placed:false});
function palLerp(a,b,f){
  const mix=sh=>{const va=a>>sh&255,v=va+(((b>>sh&255)-va)*f|0);return v<0?0:v>255?255:v;};
  return mix(16)<<16|mix(8)<<8|mix(0);
}
function palExpand(stops){
  const n=Math.min(stops.length,PAL_MAX),out=new Array(16);
  if(!n)return out.fill(0);
  const src=stops.slice(0,n).map(h=>parseInt(h.slice(1),16));
  if(n===1)return out.fill(src[0]);
  for(let i=0;i<16;i++){
    const pos=i*(n-1)/15,lo=Math.floor(pos);
    out[i]=palLerp(src[lo],src[Math.min(lo+1,n-1)],pos-lo);
  }
  return out;
}
function palExpandPlaced(stops){
  const n=Math.min(stops.length,PAL_MAX),out=new Array(16);
  if(!n)return out.fill(0);
  const src=stops.slice(0,n).map(s=>({v:parseInt(s.c.slice(1),16),p:s.p}))
                            .sort((a,b)=>a.p-b.p);
  if(n===1)return out.fill(src[0].v);
  for(let i=0;i<16;i++){
    const at=i*100;
    if(at<=src[0].p*15){out[i]=src[0].v;continue;}
    let lo=0;
    while(lo+1<n&&src[lo+1].p*15<=at)lo++;
    if(lo+1>=n){out[i]=src[n-1].v;continue;}
    const a=src[lo].p*15,b=src[lo+1].p*15;
    out[i]=palLerp(src[lo].v,src[lo+1].v,b>a?(at-a)/(b-a):0);
  }
  return out;
}
const palTable=v=>v.placed?palExpandPlaced(v.stops):palExpand(v.stops.map(s=>s.c));
const palEven=(i,n)=>n>1?i*100/(n-1):0;
const palPosOf=(v,i)=>v.placed?v.stops[i].p:palEven(i,v.stops.length);
const palFileText=v=>
  v.stops.map(s=>s.c.slice(1).toUpperCase()+(v.placed?'@'+s.p:'')).join('\n')+'\n';
function palParseFile(text){
  const out=[];
  let placed=0;
  for(const raw of text.split(/\r?\n/)){
    if(out.length>=PAL_MAX)break;
    let s=raw.trim();
    if(s.startsWith('#'))s=s.slice(1);
    if(!s)continue;
    const at=s.indexOf('@'),hex=at<0?s:s.slice(0,at);
    if(!/^[0-9A-Fa-f]{6}$/.test(hex))return null;
    let p=0;
    if(at>=0){
      const tail=s.slice(at+1);
      if(!/^\d{1,3}$/.test(tail))return null;
      p=parseInt(tail,10);
      if(p>100)return null;
      placed++;
    }
    out.push({c:'#'+hex.toUpperCase(),p});
  }
  if(!out.length||(placed&&placed!==out.length))return null;
  if(placed)out.sort((a,b)=>a.p-b.p);
  return{stops:out,placed:!!placed};
}
const palScale8=(v,s)=>(v*(s+1))>>8;
function palSample(ent,index,blend){
  const hi=index>>4,lo=index&15,e=ent[hi];
  let r=e>>16&255,g=e>>8&255,b=e&255;
  if(lo&&blend){
    const nx=ent[hi===15?0:hi+1],f2=lo<<4,f1=255-f2;
    r=palScale8(r,f1)+palScale8(nx>>16&255,f2);
    g=palScale8(g,f1)+palScale8(nx>>8&255,f2);
    b=palScale8(b,f1)+palScale8(nx&255,f2);
  }
  return'#'+((r<<16|g<<8|b)>>>0).toString(16).padStart(6,'0');
}
const palObs=new ResizeObserver(es=>es.forEach(e=>e.target.isConnected?palPaint(e.target):palObs.unobserve(e.target)));
function palRamp(cls){
  const cv=el('canvas',{class:'ramp'+(cls?' '+cls:'')});
  palObs.observe(cv);
  return cv;
}
function palSet(cv,v,blend){
  cv._v={stops:v.stops.map(s=>({c:s.c,p:s.p})),placed:v.placed};
  cv._bl=blend;
  palPaint(cv);
}
function palPaint(cv){
  const w=cv.clientWidth,h=cv.clientHeight;
  if(!w||!h||!cv._v)return;
  const dpr=Math.min(window.devicePixelRatio||1,2);
  cv.width=Math.max(1,Math.round(w*dpr));
  cv.height=Math.max(1,Math.round(h*dpr));
  const ent=palTable(cv._v),g=cv.getContext('2d'),n=cv.width;
  for(let x=0;x<n;x++){
    g.fillStyle=palSample(ent,Math.round((n>1?x/(n-1):0)*240),cv._bl);
    g.fillRect(x,0,1,cv.height);
  }
}
function viewPalettes(view){
  let files={};
  let sel=null;
  let pend='';
  let pal=palNew();
  let selRef=null;
  let blend=true,mark='',dragging=false;
  const palKey=v=>(v.placed?'p:':'e:')+v.stops.map(s=>s.c+'@'+s.p).join(',');
  const dirty=()=>palKey(pal)!==mark;

  const list=el('div',{class:'pallist'});
  const listCount=el('span',{class:'ftcount'});
  const listHd=el('div',{class:'palhd',role:'button',tabindex:'0','aria-expanded':'false'},
                   el('h2',null,t('palettes')),listCount,
                   el('span',{class:'chev'},icon('chev')));
  const fold=open=>{
    listHd.parentNode.classList.toggle('open',open);
    listHd.setAttribute('aria-expanded',open?'true':'false');
  };
  pressable(listHd,()=>fold(!listHd.parentNode.classList.contains('open')));
  const nameIn=el('input',{type:'text',maxlength:'24',placeholder:t('name')});
  const count=el('span',{class:'palcount'});
  const prev=palRamp('big');
  const track=el('div',{class:'palhandles'});
  const bar=el('div',{class:'palbar'},prev,track);
  const note=el('div',{class:'palnote'});
  const colPick=colorPick('#FF0000',()=>setColor(colPick.get()));
  const colIn=colPick.input;
  colIn.setAttribute('aria-label',t('palstopcol'));
  const posIn=el('input',{type:'number',min:'0',max:'100',step:'1','aria-label':t('palstoppos')});
  const rmBtn=el('button',{class:'icon danger',title:t('palrmstop'),'aria-label':t('palrmstop')},'✕');
  const evenBtn=el('button',null,t('paleven'));
  const blendSw=mkSwitch(true,()=>{blend=blendSw.input.checked;paintPrev();});
  const newBtn=el('button',null,icon('plus'),' '+t('newpal'));
  const addBtn=el('button',null,icon('plus'),' '+t('paladd'));
  const tryBtn=el('button',null,t('tryit'));
  const saveBtn=el('button',{class:'pri'},t('save'));

  function load(kind,name,v,unsaved){
    sel=kind?{kind,name}:null;
    pend=kind?'':name;
    pal={stops:v.stops.map(s=>({c:s.c,p:s.p})),placed:v.placed};
    selRef=pal.stops[0];
    mark=unsaved?'':palKey(pal);
    paintEditor();
  }
  function guard(fn){
    if(!dirty()){fn();return;}
    toast(t('palunsaved'),false,[{label:t('discard'),fn},
                                 {label:t('save'),pri:true,fn:()=>save().then(ok=>{if(ok)fn();})}]);
  }
  function pick(kind,name){
    guard(()=>{
      const own=files[name.toLowerCase()];
      load(kind,name,kind==='builtin'&&(!own||own.bad)?palBuiltin(name):own.pal);
      paintList();fold(false);
    });
  }
  function dup(name,v){
    guard(()=>{
      let i=2,n=name+i;
      while(files[n.toLowerCase()]||palIsBuiltin(n))n=name+(++i);
      load(null,n,v,true);
      paintList();fold(false);
    });
  }

  function row(kind,name,v,over){
    const r=el('div',{class:'palrow'+(sel&&sel.kind===kind&&sel.name===name?' on':''),
                      role:'button',tabindex:'0'});
    const acts=el('div',{class:'acts'});
    if(v){
      const cp=el('button',{class:'icon',title:t('paldup'),'aria-label':t('paldup')},icon('copy'));
      cp.addEventListener('click',()=>dup(name,v));
      acts.append(cp);
    }
    if(kind==='user'||over){
      const lbl=over?t('palreset'):t('del');
      const rm=armable(el('button',{class:'icon danger',title:lbl,'aria-label':lbl},icon('trash')),
        async()=>{
          const f=files[name.toLowerCase()];
          try{
            await delFile('/PALETTES/'+f.file);
            toast(over?t('palrestored'):t('deleted'));
            await reload();
            if(sel&&sel.name===name){
              if(over)load('builtin',name,palBuiltin(name));
              else load(null,'',palNew());
            }
            paintList();
          }catch(e){toastErr(e);}
        });
      acts.append(rm);
    }
    const body=v?palRamp():el('div',{class:'palbad'},t('palbad'));
    if(v)palSet(body,v,true);
    r.replaceChildren(el('div',{class:'l1'},el('span',{class:'nm'},name),
                        over?el('span',{class:'tag'},t('paloverr')):null),
                      acts,body);
    if(v)pressable(r,e=>{if(!e.target.closest('button'))pick(kind,name);});
    return r;
  }
  function paintList(){
    const kids=[el('div',{class:'grp'},t('palbuiltin'))];
    PAL_BUILTIN.forEach(([n])=>{
      const own=files[n.toLowerCase()];
      kids.push(row('builtin',n,own&&!own.bad?own.pal:palBuiltin(n),!!own));
    });
    const mine=Object.keys(files).filter(k=>!palIsBuiltin(k)).sort().map(k=>files[k]);
    kids.push(el('div',{class:'grp'},t('palyours'),
                el('span',{class:'mono'},String(mine.length))));
    kids.push(...(mine.length?mine.map(f=>row('user',f.name,f.bad?null:f.pal,false))
                            :[el('div',{class:'empty'},t('palnone'))]));
    list.replaceChildren(...kids);
    listCount.replaceChildren(String(PAL_BUILTIN.length+mine.length));
  }

  function place(){
    if(pal.placed)return;
    const n=pal.stops.length;
    pal.stops.forEach((s,i)=>{s.p=Math.round(palEven(i,n));});
    pal.placed=true;
    evenBtn.disabled=false;
  }
  const posFromX=(clientX,rect)=>clamp(Math.round((clientX-rect.left)/Math.max(1,rect.width)*100),0,100);
  const sortStops=()=>pal.stops.sort((a,b)=>a.p-b.p);
  function addStop(p){
    const st={c:palSample(palTable(pal),Math.round(p/100*240),blend).toUpperCase(),p};
    place();
    pal.stops.push(st);
    sortStops();
    selRef=st;
  }

  function markSel(){
    const i=pal.stops.indexOf(selRef);
    [...track.children].forEach((h,j)=>h.classList.toggle('on',j===i));
  }

  function paintHandles(){
    track.replaceChildren(...pal.stops.map((s,i)=>{
      const h=el('button',{type:'button',class:'palh'+(s===selRef?' on':''),
                           style:'left:'+palPosOf(pal,i)+'%',
                           title:t('palstop')+' '+(i+1),
                           'aria-label':t('palstop')+' '+(i+1)+', '+Math.round(palPosOf(pal,i))+'%'},
                 el('i',{style:'background:'+s.c}));
      h.addEventListener('pointerdown',e=>{
        if(e.button)return;
        e.preventDefault();
        selRef=s;markSel();paintStop();
        const rect=bar.getBoundingClientRect();
        const start=palPosOf(pal,pal.stops.indexOf(s));
        let moved=false;
        onDrag(ev=>{
          const p=posFromX(ev.clientX,rect);
          if(!moved&&p===start)return;
          moved=dragging=true;
          place();
          s.p=p;
          h.style.left=p+'%';
          h.classList.add('drag');
          posIn.value=String(p);
          paintPrev();
        },()=>{
          if(moved){sortStops();paintHandles();paintStop();}
          setTimeout(()=>{dragging=false;},0);
        });
      });
      h.addEventListener('click',e=>{
        e.stopPropagation();
        if(s===selRef)return;
        selRef=s;markSel();paintStop();
      });
      h.addEventListener('keydown',e=>{
        const d=e.key==='ArrowLeft'?-1:e.key==='ArrowRight'?1:0;
        if(!d)return;
        e.preventDefault();
        place();
        s.p=clamp(s.p+d*(e.shiftKey?10:1),0,100);
        sortStops();
        selRef=s;paintHandles();paintStop();paintPrev();
        const again=[...track.children][pal.stops.indexOf(s)];
        if(again)again.focus();
      });
      return h;
    }));
  }
  bar.addEventListener('click',e=>{
    if(dragging||e.target.closest('.palh'))return;
    if(pal.stops.length>=PAL_MAX){toast(t('palfull'),false);return;}
    addStop(posFromX(e.clientX,bar.getBoundingClientRect()));
    paintStops();
  });

  function paintStop(){
    const i=pal.stops.indexOf(selRef);
    if(i<0){selRef=pal.stops[0];return paintStop();}
    colPick.set(selRef.c);
    posIn.value=String(Math.round(palPosOf(pal,i)));
    rmBtn.disabled=pal.stops.length<2;
  }
  function setColor(hex){
    selRef.c=hex.toUpperCase();
    const i=pal.stops.indexOf(selRef);
    const h=[...track.children][i];
    if(h)h.firstChild.style.background=selRef.c;
    colPick.set(selRef.c);
    paintPrev();
  }
  posIn.addEventListener('input',()=>{
    const v=parseInt(posIn.value,10);
    if(!isFinite(v))return;
    place();
    selRef.p=clamp(v,0,100);
    sortStops();
    paintHandles();paintPrev();
  });
  rmBtn.addEventListener('click',()=>{
    if(pal.stops.length<2)return;
    const i=pal.stops.indexOf(selRef);
    pal.stops.splice(i,1);
    selRef=pal.stops[Math.min(i,pal.stops.length-1)];
    paintStops();
  });
  evenBtn.addEventListener('click',()=>{
    pal.placed=false;
    pal.stops.forEach(s=>{s.p=0;});
    paintStops();
  });
  addBtn.addEventListener('click',()=>{
    if(pal.stops.length>=PAL_MAX){toast(t('palfull'),false);return;}
    if(!pal.placed){
      pal.stops.push({c:pal.stops[pal.stops.length-1].c,p:0});
      selRef=pal.stops[pal.stops.length-1];
    }else{
      let at=0,gap=-1;
      for(let i=0;i+1<pal.stops.length;i++){
        const g=pal.stops[i+1].p-pal.stops[i].p;
        if(g>gap){gap=g;at=i;}
      }
      addStop(gap>0?Math.round((pal.stops[at].p+pal.stops[at+1].p)/2)
                   :Math.min(100,pal.stops[pal.stops.length-1].p+1));
    }
    paintStops();
  });

  function paintStops(){
    count.replaceChildren(pal.stops.length+'/'+PAL_MAX+' '+t('palstops'));
    count.classList.toggle('max',pal.stops.length>=PAL_MAX);
    evenBtn.disabled=!pal.placed;
    paintHandles();paintStop();paintPrev();
  }
  function paintPrev(){palSet(prev,pal,blend);}
  function paintSave(){
    const bi=sel&&sel.kind==='builtin';
    saveBtn.replaceChildren(bi?t('paloverride')
      :sel&&nameIn.value.trim()!==sel.name?t('palsaveas'):t('save'));
  }
  function paintEditor(){
    const bi=sel&&sel.kind==='builtin';
    nameIn.value=sel?sel.name:pend;
    nameIn.disabled=!!bi;
    note.replaceChildren(sel&&!bi?t('palownhelp'):'');
    paintSave();paintStops();
  }
  nameIn.addEventListener('input',paintSave);
  newBtn.addEventListener('click',()=>guard(()=>{
    load(null,'',palNew());
    paintList();fold(false);nameIn.focus();
  }));
  tryBtn.addEventListener('click',async()=>{
    const arr=pal.placed?pal.stops.map(s=>({color:s.c,pos:s.p})):pal.stops.map(s=>s.c);
    try{
      await post('/api/v1/notifications',
                 {text:'',durationMs:4000,effect:'Plasma',palette:arr,paletteBlend:blend});
      toast(t('shown'));
    }catch(e){toastErr(e);}
  });
  saveBtn.addEventListener('click',()=>save());

  async function save(){
    const name=nameIn.value.trim();
    if(!/^[A-Za-z0-9_-]{1,24}$/.test(name)){toast(t('palname'),false);return false;}
    const bi=palIsBuiltin(name);
    if(bi&&!(sel&&sel.kind==='builtin')){
      return new Promise(res=>{
        toast(bi+': '+t('palwillover'),false,
              [{label:t('cancel'),fn:()=>res(false)},
               {label:t('save'),pri:true,fn:()=>write(bi).then(res)}]);
      });
    }
    return write(bi||name);
  }
  async function write(name){
    saveBtn.disabled=true;
    try{
      await uploadFile(new Blob([palFileText(pal)],{type:'text/plain'}),'/PALETTES',name+'.txt',null);
      sel={kind:palIsBuiltin(name)?'builtin':'user',name};
      pend='';mark=palKey(pal);
      toast(t('palsaved'));
      await reload();
      paintEditor();
      return true;
    }catch(e){toastErr(e);return false;}
    finally{saveBtn.disabled=false;}
  }

  async function reload(){
    try{
      const{data}=await api('/api/v1/files?dir='+encodeURIComponent('/PALETTES'));
      const next={};
      await Promise.all((data.files||[]).filter(f=>/\.txt$/i.test(f.name)).map(async f=>{
        const base=f.name.replace(/\.txt$/i,'');
        try{
          const{text}=await api('/PALETTES/'+encodeURIComponent(f.name),{cache:'no-store'});
          const v=palParseFile(text);
          next[base.toLowerCase()]=v?{name:base,file:f.name,pal:v}
                                    :{name:base,file:f.name,bad:true};
        }catch(e){}
      }));
      files=next;
      paintList();
    }catch(e){toastErr(e);}
  }

  view.append(el('div',{class:'palwrap'},
    el('div',{class:'card palcard'},listHd,
      el('div',{class:'palbody'},
        el('div',{class:'ghelp'},t('palhelp')),
        list,
        el('div',{class:'row'},newBtn))),
    el('div',{class:'card'},
      el('h2',null,t('paledit')),
      el('div',{class:'palhead'},nameIn,count,
        el('span',{class:'palbl',title:t('palblendh')},
          el('span',{class:'mono'},t('palblend')),blendSw.node)),
      bar,
      el('div',{class:'palsel'},colPick.node,
        el('span',{class:'mono pl'},t('palstoppos')),posIn,
        el('span',{class:'grow'}),evenBtn,rmBtn),
      note,
      el('div',{class:'row palacts'},addBtn,el('span',{class:'grow'}),tryBtn,saveBtn))));
  load(null,'',palNew());
  reload();
}
