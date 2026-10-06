const RTTTL_SEMI={c:-9,d:-7,e:-5,f:-4,g:-2,a:0,b:2};
const RTTTL_MAX=512;
const RTTTL_GAP=0.006;
const noteMs=(d,timeUnit)=>Math.floor(d*timeUnit/2);
function rtttlParse(s){
  const bad=(error,index)=>({ok:false,error,index});
  if(s.length>RTTTL_MAX)return bad('melody is longer than '+RTTTL_MAX+' characters',RTTTL_MAX);
  const c1=s.indexOf(':');if(c1<0)return bad("missing ':' after the melody name",s.length);
  const c2=s.indexOf(':',c1+1);if(c2<0)return bad("missing ':' before the notes",s.length);
  const title=s.slice(0,c1).trim();
  if(!title)return bad('the melody name is empty',0);
  if(title.length>24)return bad('the melody name is longer than 24 characters',24);
  const def={d:4,o:6,b:63},seen={};
  const dtxt=s.slice(c1+1,c2);
  for(const part of dtxt.split(',')){
    const at=c1+1+dtxt.indexOf(part);
    if(!part.trim())continue;
    const m=/^\s*([dobDOB])\s*=\s*(\d+)\s*$/.exec(part);
    if(!m)return bad("expected 'd=', 'o=' or 'b=' here",at);
    const k=m[1].toLowerCase(),v=+m[2];
    if(seen[k])return bad("'"+k+"' is set twice",at);
    seen[k]=1;
    if(k==='d'&&![1,2,4,8,16,32].includes(v))return bad("'d' must be 1, 2, 4, 8, 16 or 32",at);
    if(k==='o'&&(v<4||v>7))return bad("'o' must be 4, 5, 6 or 7",at);
    if(k==='b'&&(v<10||v>300))return bad("'b' must be between 10 and 300",at);
    def[k]=v;
  }
  const timeUnit=Math.floor(Math.floor(60*1000*4/def.b)/32);
  const notes=[];
  let i=c2+1;
  const rest=s.slice(c2+1);
  const parts=rest.split(',');
  for(const part of parts){
    const at=i;i+=part.length+1;
    const m=/^\s*(\d+)?([a-gpA-GP])(#)?(\.)?(\d)?(\.)?\s*$/.exec(part);
    if(!m)return bad(part.trim()?"'"+part.trim()+"' is not a note":'empty note',at);
    const dur=m[1]===undefined?def.d:+m[1];
    if(![1,2,4,8,16,32].includes(dur))return bad('note length must be 1, 2, 4, 8, 16 or 32',at);
    const letter=m[2].toLowerCase(),isRest=letter==='p';
    if(m[3]&&isRest)return bad('a rest cannot be sharp',at);
    if(m[3]&&(letter==='b'||letter==='e'))return bad("'"+letter+"#' is not a note; use the next letter",at);
    if(m[4]&&m[6])return bad('note is dotted twice',at);
    const oct=m[5]===undefined?def.o:+m[5];
    if(oct<4||oct>7)return bad('octave must be 4, 5, 6 or 7',at);
    let units=64/dur;
    if(m[4]||m[6])units+=units/2;
    notes.push({f:isRest?0:Math.round(440*2**(((oct-4)*12+RTTTL_SEMI[letter]+(m[3]?1:0))/12)),d:units});
  }
  if(!notes.length)return bad('the melody has no notes',c2+1);
  const ms=notes.reduce((a,n)=>a+noteMs(n.d,timeUnit),0);
  return{ok:true,title,timeUnit,notes,durationMs:ms};
}
let audioCtx=null,audioStop=null,audioPreviewBtn=null,devicePlayBtn=null,devicePlayTimer=null,audioTick=()=>{};
function playState(btn,on,local=false){
  const lbl=t(on?(local?'meloprestop':'melostop'):(local?'melopre':'meloplay'));
  const name=btn._audioName&&btn._audioName();
  btn.classList.toggle('playing',on);btn.replaceChildren(on?'■':local?'🎧':'▶');
  btn.title=lbl;btn.setAttribute('aria-label',lbl+(name?': '+name:''));
  btn.setAttribute('aria-pressed',on?'true':'false');
}
function audioPlayButton(name,local=false){
  const b=el('button',{class:'icon '+(local?'preview-play':'device-play')});b._audioName=typeof name==='function'?name:()=>name;
  playState(b,false,local);return b;
}
function previewStop(){
  if(audioStop){audioStop();audioStop=null;}
  if(audioPreviewBtn){playState(audioPreviewBtn,false,true);audioPreviewBtn=null;}
}
function previewPlay(p,btn){
  previewStop();
  try{
    audioCtx=audioCtx||new(window.AudioContext||window.webkitAudioContext)();
  }catch(e){toast(t('neterr'),false);return;}
  if(audioCtx.state==='suspended')audioCtx.resume();
  const osc=audioCtx.createOscillator(),gain=audioCtx.createGain();
  osc.type='square';
  const t0=audioCtx.currentTime+0.02;
  let at=t0;
  gain.gain.setValueAtTime(0,t0);
  for(const n of p.notes){
    const dur=noteMs(n.d,p.timeUnit)/1000;
    if(n.f){
      osc.frequency.setValueAtTime(n.f,at);
      const gap=dur>2*RTTTL_GAP?RTTTL_GAP:0;
      gain.gain.setValueAtTime(0.13,at);
      gain.gain.setValueAtTime(0,at+dur-gap);
    }else gain.gain.setValueAtTime(0,at);
    at+=dur;
  }
  osc.connect(gain);gain.connect(audioCtx.destination);
  osc.start(t0);osc.stop(at+0.05);
  audioStop=()=>{try{gain.gain.cancelScheduledValues(audioCtx.currentTime);
    gain.gain.setValueAtTime(0,audioCtx.currentTime);osc.stop();}catch(e){}};
  audioPreviewBtn=btn;playState(btn,true,true);
  osc.onended=()=>{audioStop=null;if(audioPreviewBtn===btn){playState(btn,false,true);audioPreviewBtn=null;}};
}
async function devicePlayToggle(btn,body,durationMs=0){
  const stop=devicePlayBtn===btn;
  try{
    await post(stop?'/api/v1/audio/stop':'/api/v1/audio/play',stop?{group:'station' in body?'radio':'alert'}:body);
    if(devicePlayBtn)playState(devicePlayBtn,false);
    clearTimeout(devicePlayTimer);devicePlayTimer=null;devicePlayBtn=stop?null:btn;
    if(!stop){
      playState(btn,true);toast(t('playing'));
      if(durationMs)devicePlayTimer=setTimeout(()=>{
        if(devicePlayBtn===btn){playState(btn,false);devicePlayBtn=null;}
      },durationMs+250);
    }
    setTimeout(()=>audioTick(),300);
  }catch(e){toastErr(e);}
}
function markRows(list,name,busy){
  let active=null;
  list.querySelectorAll('.arow').forEach(r=>{
    const on=name!==''&&r.dataset.name===name;
    r.classList.toggle('on',on);
    if(on)active=r.querySelector('.device-play');
  });
  if(active&&devicePlayBtn!==active){
    if(devicePlayBtn)playState(devicePlayBtn,false);
    devicePlayBtn=active;playState(active,true);
  }else if(!active&&!busy&&devicePlayBtn&&list.contains(devicePlayBtn)){
    playState(devicePlayBtn,false);devicePlayBtn=null;clearTimeout(devicePlayTimer);devicePlayTimer=null;
  }
}
function editForm(list,anchor,inputs,msg,extra,save){
  const cur=list.querySelector('.aform');
  if(cur)cur.close();
  const empty=list.querySelector('.nohit');
  const cancel=el('button',null,t('cancel'));
  const f=el('div',{class:'aform'},inputs,msg,el('div',{class:'acts'},extra,el('span',{class:'grow'}),cancel,save));
  f.close=()=>{
    if(audioPreviewBtn&&f.contains(audioPreviewBtn))previewStop();
    if(anchor)f.replaceWith(anchor);else f.remove();
    if(empty&&!list.firstChild)list.append(empty);
  };
  cancel.addEventListener('click',f.close);
  f.addEventListener('keydown',e=>{
    if(e.key==='Escape')f.close();
    else if(e.key==='Enter'&&e.target.tagName==='INPUT'&&!save.disabled)save.click();
  });
  if(empty)empty.remove();
  if(anchor)anchor.replaceWith(f);else list.append(f);
  inputs[0].focus();
  return f;
}
const rowBtn=(k,name,ic)=>el('button',{class:k==='del'?'danger icon':'icon',title:t(k),'aria-label':t(k)+': '+name},icon(ic));
const meloInfo=p=>p.notes.length+' '+t('melonotes')+' · '+(p.durationMs/1000).toFixed(1)+' s';
function melodiesSection(){
  const list=el('div',{class:'alib'});
  let saved=[];

  async function reload(){
    try{
      const{data}=await api('/api/v1/audio/melodies');
      const melodies=(data.melodies||[]).sort((a,b)=>a.name.localeCompare(b.name));
      saved=melodies.map(m=>m.name);
      list.replaceChildren(...(melodies.length?melodies.map(row):[el('div',{class:'nohit'},t('melonone'))]));
    }catch(e){toastErr(e);}
  }

  function row(m){
    const p=m.valid===false?{ok:false}:rtttlParse(String(m.rtttl||''));
    const pre=audioPlayButton(m.name,true),play=audioPlayButton(m.name);
    pre.disabled=play.disabled=!p.ok;
    pre.addEventListener('click',()=>audioPreviewBtn===pre?previewStop():previewPlay(p,pre));
    play.addEventListener('click',()=>devicePlayToggle(play,{rtttl:m.rtttl},p.durationMs));
    const edit=rowBtn('edit',m.name,'pen');
    const del=armable(rowBtn('del',m.name,'trash'),async()=>{
      try{await api('/api/v1/audio/melodies/'+encodeURIComponent(m.name),{method:'DELETE'});toast(t('deleted'));reload();}
      catch(e){toastErr(e);}
    });
    const r=el('div',{class:'arow'+(p.ok?'':' bad'),'data-name':m.name},play,el('span',{class:'nm'},m.name),
      el('span',{class:'sz'},p.ok?meloInfo(p):t('melobroken')),pre,edit,del);
    edit.addEventListener('click',()=>form(m,r));
    return r;
  }

  function form(m,anchor){
    const orig=m?m.name:'';
    const body=m?String(m.rtttl||'').split(':').slice(1).join(':'):'';
    const nameIn=el('input',{type:'text',placeholder:t('meloname'),maxlength:'24',value:orig,'aria-label':t('meloname')});
    const rtIn=el('input',{type:'text',class:'rt',placeholder:t('melortttl'),value:body,'aria-label':'RTTTL'});
    const msg=el('small');
    const name=()=>nameIn.value.trim();
    const pre=audioPlayButton(name,true),play=audioPlayButton(name);
    const save=el('button',{class:'pri'},t('save'));
    const f=editForm(list,anchor,[nameIn,rtIn],msg,[pre,play],save);
    const full=()=>(name()||'x')+':'+rtIn.value.trim();

    function check(){
      const p=rtttlParse(full()),n=name(),rt=rtIn.value.trim();
      const nameOk=/^[A-Za-z0-9_-]{1,24}$/.test(n),dup=n!==orig&&saved.includes(n);
      const err=rt&&!p.ok?p.error:dup?t('meloexists'):n&&!nameOk?t('melobadname'):'';
      f.classList.toggle('bad',!!err);
      msg.replaceChildren(err||(p.ok?meloInfo(p):''));
      save.disabled=!(p.ok&&nameOk&&!dup&&(n!==orig||rt!==body));
      pre.disabled=play.disabled=!p.ok;
      return p;
    }
    rtIn.addEventListener('paste',e=>{
      const txt=(e.clipboardData||window.clipboardData).getData('text').trim();
      const parts=txt.split(':');
      if(parts.length<3)return;
      e.preventDefault();
      if(!name())nameIn.value=parts[0].trim().replace(/[^A-Za-z0-9_-]/g,'').slice(0,24);
      rtIn.value=parts.slice(1).join(':');
      check();
    });
    const recheck=debounce(check,150);
    nameIn.addEventListener('input',recheck);
    rtIn.addEventListener('input',recheck);
    pre.addEventListener('click',()=>{
      if(audioPreviewBtn===pre)return previewStop();
      const p=check();if(p.ok)previewPlay(p,pre);
    });
    play.addEventListener('click',()=>{const p=check();if(p.ok)devicePlayToggle(play,{rtttl:full()},p.durationMs);});
    save.addEventListener('click',async()=>{
      const n=name();
      save.disabled=true;
      try{
        await req('PUT','/api/v1/audio/melodies/'+encodeURIComponent(n),{rtttl:rtIn.value.trim()});
        if(orig&&n!==orig)await api('/api/v1/audio/melodies/'+encodeURIComponent(orig),{method:'DELETE'});
        toast(t('saved'));reload();
      }catch(e){toastErr(e);save.disabled=false;}
    });
    check();
  }

  const add=el('button',null,'+ '+t('newmelo'));
  add.addEventListener('click',()=>form(null,null));
  reload();
  return[list,el('div',{class:'row',style:'margin:10px 0 0'},add)];
}
function radioSection(){
  const list=el('div',{class:'alib'});
  let stations=null,live='',busy=false;
  const host=u=>{try{return new URL(u).host;}catch(e){return u;}};

  function paint(){
    list.replaceChildren(...(stations.length?stations.map(row):[el('div',{class:'nohit'},t('radionone'))]));
    markRows(list,live,busy);
  }
  // The API takes the whole list, so every change sends every station.
  async function put(next,done){
    try{await req('PUT','/api/v1/audio/stations',{stations:next});
      stations=(await api('/api/v1/audio/stations',{cache:'no-store'})).data.stations||[];paint();toast(t(done));return true;}
    catch(e){toastErr(e);return false;}
  }

  function row(s,i){
    const play=audioPlayButton(s.name);
    play.addEventListener('click',()=>devicePlayToggle(play,{station:s.name}));
    const edit=rowBtn('edit',s.name,'pen');
    const del=armable(rowBtn('del',s.name,'trash'),()=>put(stations.filter((_,j)=>j!==i),'deleted'));
    const r=el('div',{class:'arow','data-name':s.name},play,
      el('span',{class:'nm',title:s.url},s.name,el('small',null,host(s.url))),edit,del);
    edit.addEventListener('click',()=>form(s,i,r));
    return r;
  }

  function form(s,i,anchor){
    const name=el('input',{value:s?s.name:'',placeholder:t('radioname'),maxlength:'24','aria-label':t('radioname')});
    const url=el('input',{value:s?s.url:'',placeholder:t('radiourl'),'aria-label':t('radiourl')});
    const msg=el('small');
    const test=audioPlayButton(()=>name.value.trim()||url.value.trim());
    const save=el('button',{class:'pri'},t('save'));
    const f=editForm(list,anchor,[name,url],msg,[test],save);
    const check=()=>{
      const n=name.value.trim(),u=url.value.trim();
      const bad=u&&!/^https?:\/\//i.test(u)?t('radiobadurl'):stations.some((o,j)=>j!==i&&o.name===n)?t('radiodupe'):'';
      f.classList.toggle('bad',!!bad);
      msg.replaceChildren(bad);
      save.disabled=!n||!u||!!bad||!!s&&n===s.name&&u===s.url;
      test.disabled=!u||!!bad;
    };
    [name,url].forEach(x=>x.addEventListener('input',check));
    test.addEventListener('click',()=>devicePlayToggle(test,{station:url.value.trim()}));
    save.addEventListener('click',async()=>{
      const next=stations.slice(),v={name:name.value.trim(),url:url.value.trim()};
      if(s)next[i]=v;else next.push(v);
      save.disabled=true;
      if(!await put(next,'saved'))check();
    });
    check();
  }

  function mark(d){
    busy=!!d.radio?.playing;
    live=busy?d.radio.station:'';
    if(stations===null){stations=d.stations||[];paint();}
    else markRows(list,live,busy);
  }

  const add=el('button',null,'+ '+t('radioadd'));
  add.addEventListener('click',()=>{if(stations)form(null,-1,null);});
  return{nodes:[list,el('div',{class:'row',style:'margin:10px 0 0'},add)],mark};
}
function mp3Section(want){
  const lib=el('div',{class:'alib'}),grps=el('div'),opened=new Set([want]);
  let extra;
  const q=el('input',{type:'search',placeholder:t('mp3find'),'aria-label':t('mp3find')});
  const meter=el('div',{class:'mp3meter'});
  const nohit=el('div',{class:'nohit gone'},t('mp3nohit'));
  let playing='';
  const box=el('div',null,lib,grps),mark=name=>{playing=name;markRows(box,name,false);};
  const size=fs=>fmtBytes(fs.reduce((a,f)=>a+f.size,0));
  const mp3s=fs=>(fs||[]).filter(f=>/\.mp3$/i.test(f.name)).sort((a,b)=>a.name.localeCompare(b.name));

  function row(f,s){
    const n=f.name.replace(/\.mp3$/i,'');
    const pre=audioPlayButton(n,true);
    pre.addEventListener('click',()=>{
      if(audioPreviewBtn===pre)return previewStop();
      previewStop();
      const a=new Audio((s?'/SCRIPTS/'+s.name+'/':'/MP3/')+encodeURIComponent(f.name)+'?v='+f.size+'.'+assetRev);
      audioPreviewBtn=pre;playState(pre,true,true);audioStop=()=>a.pause();
      a.onended=()=>{if(audioPreviewBtn===pre)previewStop();};
      a.play().catch(()=>{if(audioPreviewBtn===pre)previewStop();});
    });
    const play=audioPlayButton(n);
    play.addEventListener('click',()=>devicePlayToggle(play,{file:s?s.name+'/'+n:n}));
    const del=armable(rowBtn('del',n,'trash'),async()=>{
      try{await api((s?'/api/v1/apps/script/'+s.name+'/sounds/':'/api/v1/audio/mp3/')+encodeURIComponent(n),{method:'DELETE'});
        toast(t('deleted'));reload();}
      catch(e){toastErr(e);}
    },undefined,s&&t('sndOwner').replace('{s}',s.title));
    const nm=el('span',{class:'nm',title:n},n);
    const ren=s?null:rowBtn('rename',n,'pen');
    if(ren)ren.addEventListener('click',()=>rename(nm,n));
    return el('div',{class:'arow','data-name':s?s.name+'/'+n:n},play,nm,
      el('span',{class:'sz'},fmtBytes(f.size)),pre,ren,del);
  }
  function rename(nm,n){
    if(!nm.isConnected)return;
    const inp=el('input',{class:'nm',type:'text',value:n,maxlength:'32',spellcheck:'false','aria-label':t('rename')+': '+n});
    let done=false;
    const back=()=>{done=true;inp.replaceWith(nm);};
    async function commit(){
      if(done)return;
      const nn=inp.value.trim();
      if(!nn||nn===n)return back();
      done=true;
      if(!/^[A-Za-z0-9_-]{1,32}$/.test(nn)){toast(t('scrbadname'),false);return back();}
      try{await post('/api/v1/audio/mp3/rename',{from:n,to:nn});assetRev++;toast(t('renamed'));reload();}
      catch(e){toast(e.code==='nameTaken'?t('sndClash'):e.message,false);back();}
    }
    inp.addEventListener('keydown',e=>{
      if(e.key==='Enter'){e.preventDefault();commit();}
      else if(e.key==='Escape'){e.preventDefault();if(!done)back();}
    });
    inp.addEventListener('blur',commit);
    nm.replaceWith(inp);
    inp.focus();inp.select();
  }
  function group(s){
    const fs=mp3s(s.files),g=el('details',{class:'agrp',id:'snd-'+s.name,open:opened.has(s.name)?'':null},
      el('summary',null,s.title+(s.orphan?' · '+t('sndOrphan'):'')+' · '+fs.length+' · '+size(fs)),
      el('div',{class:'alib'},fs.map(f=>row(f,s))),
      s.orphan?null:el('div',{class:'mp3bar'},uploadZone('/api/v1/apps/script/'+s.name+'/sounds','.mp3,audio/mpeg','⬆ '+t('upload'),reload)));
    g.addEventListener('toggle',()=>opened[g.open?'add':'delete'](s.name));
    return g;
  }

  function filter(){
    const n=q.value.trim().toLowerCase();
    lib.querySelectorAll('.arow').forEach(r=>r.classList.toggle('gone',!!n&&!r.dataset.name.toLowerCase().includes(n)));
    nohit.classList.toggle('gone',!n||!!lib.querySelector('.arow:not(.gone)'));
  }
  q.addEventListener('input',debounce(filter,120));

  async function reload(){
    try{
      const{data}=await api('/api/v1/audio/mp3',{cache:'no-store'});
      const files=mp3s(data.files),all=files.concat(...(data.scripts||[]).map(s=>mp3s(s.files)));
      const used=data.usedBytes||0,total=data.totalBytes||1;
      meter.replaceChildren(
        el('div',{class:'bar'},el('i',{class:used/total>0.9?'hot':'',style:'width:'+(used/total*100).toFixed(1)+'%'})),
        el('span',null,all.length+' '+t('mp3n')+' · '+size(all)+' · '+
          fmtBytes(Math.max(0,total-used))+' '+t('free')));
      lib.replaceChildren(...files.map(f=>row(f)),nohit);
      if(!files.length)lib.replaceChildren(el('div',{class:'nohit'},t('nofiles')));
      const list=data.scripts||[],has=n=>list.some(s=>s.name===n);
      if(want&&!has(want)){
        const a=(await api('/api/v1/apps').catch(()=>({data:0}))).data.find?.(x=>x.name===want&&x.origin==='script');
        if(a)extra={name:want,title:a.meta?.name||want,files:[]};
      }
      if(extra&&!has(extra.name))list.push(extra);
      grps.replaceChildren(...list.sort((a,b)=>a.title.localeCompare(b.title)).map(group));
      filter();mark(playing);
      const g=want&&document.getElementById('snd-'+want);want='';
      if(g){g.scrollIntoView?.({block:'center'});g.firstChild.focus({preventScroll:true});}
    }catch(e){toastErr(e);}
  }

  reload();
  return{nodes:[el('div',{class:'mp3bar'},q,uploadZone('/api/v1/audio/mp3','.mp3,audio/mpeg','⬆ '+t('upload'),reload)),
    meter,box],mark};
}
function nowPlaying(){
  const name=el('b'),src=el('span',{class:'src'});
  const stop=el('button',{class:'icon',title:t('melostop'),'aria-label':t('melostop')},'■');
  const box=el('div',{class:'nowp',role:'status'},el('span',{class:'eq'},el('i'),el('i'),el('i')),
    el('div',{class:'txt'},el('small',null,t('nowon')),name),src,stop);
  stop.addEventListener('click',async()=>{
    try{await post('/api/v1/audio/stop',{});}catch(e){toastErr(e);return;}
    if(devicePlayBtn)playState(devicePlayBtn,false);
    devicePlayBtn=null;clearTimeout(devicePlayTimer);devicePlayTimer=null;
    audioTick();
  });
  return{box,show(d){
    const r=d.radio||{},a=d.alert?.playing,m=heard(d),on=!!(r.playing||m);
    const txt=r.playing?r.station+(r.title?' – '+r.title:''):m||(r.error||t('nowidle'));
    box.classList.toggle('on',on);stop.disabled=!on;
    if(name.textContent!==txt)name.textContent=txt;
    src.textContent=r.playing?t('radioTab'):m?SYSF[a?'alertVolume':'appVolume'].l:'';
  }};
}
const heard=d=>d.alert?.playing?d.alert.name:d.app?.playing?d.app.name:'';
const heardRow=d=>d.alert?.playing?d.alert.name:d.app?.playing&&d.app.name.includes('/')?d.app.name:'';
async function viewAudio(view){
  const g=gen;
  if(!S.aud){
    try{readAudioCaps((await api('/api/v1/capabilities')).data||{});}
    catch(e){view.append(el('div',{class:'card wide'},t('neterr')));return;}
    if(g!==gen)return;
  }
  if(!anyAudioCap()){view.append(el('div',{class:'card wide'},t('audnone')));return;}
  const{section,panel}=sectionShell(view);
  const vols=[];
  const slider=key=>{
    let timer=null,held=false,edits=0;
    const f=mkField(key,Object.assign({},SYSF[key],{h:''}),0,()=>{
      edits++;clearTimeout(timer);
      const id=timer=setTimeout(()=>req('PATCH','/api/v1/settings',{[key]:f.get()}).catch(toastErr)
        .finally(()=>{if(timer===id)timer=null;}),300);
    });
    const up=()=>{held=false;removeEventListener('pointerup',up);removeEventListener('pointercancel',up);};
    f.row.querySelector('input[type=range]').addEventListener('pointerdown',()=>{
      held=true;addEventListener('pointerup',up);addEventListener('pointercancel',up);
    });
    vols.push({key,f,mark:()=>timer!==null||held?-1:edits});f.row.classList.add('vol');
    return f.row;
  };

  const np=hasSink('mp3','radio')?nowPlaying():null;
  const marks=[];
  if(np)panel.append(np.box);
  const mixKeys=['volume'].concat(hasSink('radio')?['radioVolume']:[],['appVolume','alertVolume']);
  section('mixer',t('mixer'),'',mixKeys.map(slider),1);
  poller(async()=>{
    const before=vols.map(v=>v.mark());
    const{data}=await api('/api/v1/settings');
    vols.forEach((v,i)=>{if(v.key in data&&before[i]>=0&&v.mark()===before[i])v.f.set(data[v.key]);});
  },2000,true);
  if(hasSink('mp3')){
    const mp3s=mp3Section(location.hash.split('/')[2]);
    marks.push(d=>mp3s.mark(heardRow(d)));
    section('mp3',t('mp3s'),t('mp3shelp'),mp3s.nodes,1);
  }
  if(hasSink('radio')){
    const radio=radioSection();
    marks.push(radio.mark);
    section('radio',t('radioTab'),t('radioH'),radio.nodes,1);
  }
  if(hasSink('rtttl'))
    section('melodies',t('melodies'),t('melodieshelp'),melodiesSection(),1);
  if(np){
    audioTick=async()=>{
      if(g!==gen)return;
      try{const{data}=await api('/api/v1/audio');np.show(data);marks.forEach(m=>m(data));}catch(e){}
    };
    audioTick();
    poller(audioTick,4000);
  }
}
const PAL_MAX=16;
