
function viewScripts(view){
  const ed=mkCodeEditor();
  const nameIn=el('input',{type:'text',placeholder:t('name'),maxlength:'32',spellcheck:'false',
    pattern:'[A-Za-z0-9_-]{1,32}'});
  const fileIn=el('input',{type:'file',accept:'.ax'});
  const posOut=el('span'),byteOut=el('span'),stateOut=el('span',{class:'end'});
  const errBox=el('div',{class:'ederr',style:'display:none'});
  const dirtyDot=el('i',{class:'dirty'});
  const top=el('div',{class:'edtop'});
  const enc=new TextEncoder();
  let onDevice=null;
  let updateBusy=false,checkGeneration=0;
  const hubUpdates=new Map();
  const hubPanel=el('div',{class:'script-hub-panel',hidden:true});
  const reqNote=el('div',{class:'script-hub-panel req-note',hidden:true}),reqOnly=new Set();
  const sndBtn=el('button',{class:'ico',style:'display:none'}),prog=el('div',{class:'script-hub-panel',role:'status',hidden:true});
  sndBtn.addEventListener('click',()=>{location.hash='#/audio/'+sndBtn.dataset.s;});
  const step=s=>{prog.hidden=!s;prog.textContent=s||'';},fitOk=new Set();
  let iconsHave=new Set(),sndCount=new Map();
  async function fits(name,src,yes){
    const w=fitWords(scriptNeeds(src),scriptDisplay(src)),k=name+'|'+w;
    if(!w||fitOk.has(k))return true;
    if(!await askToast(t('fitNeeds').replace('{w}',w),[[yes,1,1],[t('cancel')]]))return false;
    fitOk.add(k);return true;
  }
  function paintSounds(){
    const s=onDevice&&hasSink('mp3')&&scripts.find(x=>x.name===onDevice.name),n=s&&sndCount.get(s.name);
    sndBtn.style.display=s?'':'none';
    if(!s)return;
    const l=n?t('sndLink').replace('{n}',n):t('sndAdd');
    sndBtn.dataset.s=s.name;sndBtn.title=l;sndBtn.setAttribute('aria-label',l);
    sndBtn.replaceChildren(icon('note'),...n?[el('span',{class:'cnt'},n)]:[]);
  }
  const editorSource=src=>String(src??'').replace(/\r\n?/g,'\n');

  function setStatus(err,name){
    ed.markError(err&&err.line?err.line:0);
    errBox.style.display=err?'':'none';
    stateOut.className='end';
    if(name===undefined){stateOut.replaceChildren();return;}
    if(!err){
      const off=S.scriptsOff===true;
      stateOut.replaceChildren(off?'○ '+t('saved'):'✓ '+t('scrok'));
      stateOut.className=off?'end':'end good';
      return;
    }
    const where=[err.hook?t('scrinhook')+' '+err.hook+'()':null,
      err.line?t('scrline')+' '+err.line:null].filter(Boolean).join(' · ');
    stateOut.replaceChildren('⨯ '+(err.line?t('scrline')+' '+err.line:'ERR'));
    stateOut.className='end bad';
    errBox.replaceChildren(el('b',null,name),
      el('span',null,(where?where+' - ':'')+err.message));
  }
  function showPos(){
    const c=ed.caret();
    posOut.replaceChildren(t('scrline')+' '+c.line+':'+c.col);
  }
  function paintIcons(){
    const want=scriptIcons(ed.value);
    icoBtn.style.display=want.length?'':'none';
    if(!want.length)return;
    const miss=want.filter(id=>!iconsHave.has(id));
    icoBtn.disabled=!miss.length;
    const label=miss.length?t('scricons')+' ('+miss.length+')':t('scriconsok');
    icoBtn.title=label;icoBtn.setAttribute('aria-label',label);
    icoBtn.replaceChildren(...[icon('image'),
      miss.length?el('span',{class:'cnt'},String(miss.length)):null].filter(Boolean));
  }
  function refresh(){
    const n=enc.encode(ed.value).length;
    showPos();
    // The count is for the author to read; whether a save fits is the device's answer to give.
    byteOut.replaceChildren(n+' B');
    saveBtn.disabled=updateBusy;
    prevBtn.disabled=!onDevice||ed.value!==onDevice.src||!!onDevice.err;
    const dirty=!onDevice||ed.value!==onDevice.src||nameIn.value!==onDevice.name;
    top.classList.toggle('mod',dirty&&!!ed.value||dataDirty());
    paintData();
    paintIcons();
    paintHubUpdate();
    paintRequires();
    paintSounds();
    empty.style.display=ed.value?'none':'';
    scrDraft={name:nameIn.value,src:ed.value,dirty:dirty||dataDirty(),onDevice,stored,dtext:dataEd.value};
  }
  function load(name,src,err){
    src=editorSource(src);
    nameIn.value=name;ed.value=src;
    onDevice=err===undefined?null:{name,src,err};
    stored=null;
    setStatus(err||null,err===undefined?undefined:name);
    refresh();
    if(onDevice)loadData(name);
    window.scrollTo({top:0,behavior:'smooth'});
  }

  function freshScriptName(base){
    const taken=new Set(allFiles().map(s=>s.name));
    if(!taken.has(base))return base;
    for(let i=2;;i++)if(!taken.has(base+i))return base+i;
  }
  function loadTemplate(mod){
    selName=null;
    const name=freshScriptName(mod?'NewModule':'NewApp');
    load(name,mod?MODULE_TEMPLATE(name):SCRIPT_TEMPLATE);
    paintTree();closeTree();ed.focus();
  }
  function startTemplate(mod){guardedSwitch(()=>loadTemplate(mod));}
  const empty=el('div',{class:'edempty'},
    el('button',{class:'pri',onclick:()=>startTemplate(false)},t('newscr')));
  const impBtn=el('button',{class:'ico',title:t('import'),'aria-label':t('import')},icon('import'));
  impBtn.addEventListener('click',()=>fileIn.click());
  fileIn.addEventListener('change',async()=>{
    const f=fileIn.files[0];fileIn.value='';
    if(!f||updateBusy)return;
    try{
      const src=await f.text(),nm=scriptName(scriptMeta(src,'name'))||scriptName(f.name.replace(/\.ax$/i,''))||'Imported';
      if(updateBusy||!await fits(nm,src,t('fitImport'))||updateBusy)return;
      load(nm,src);
      toast(f.name+' '+t('scrimported'));
    }catch(e){toastErr(e);}
  });
  const saveBtn=el('button',{class:'pri ico',title:t('save'),'aria-label':t('save')},icon('save'));
  async function save(){
    if(updateBusy)return false;
    const name=nameIn.value.trim();
    if(!/^[A-Za-z0-9_-]{1,32}$/.test(name)){toast(t('scrbadname'),false);return false;}
    if(!ed.value.trim()){toast(t('scrempty'),false);return false;}
    const replacing=!!onDevice&&onDevice.name===name,taken=allFiles().some(x=>x.name===name);
    const oldName=onDevice&&onDevice.name!==name?onDevice.name:null;
    const src=ed.value,reqs=scriptRequires(src);
    if(!await fits(name,src,t('fitSave'))||updateBusy)return false;
    saveBtn.disabled=true;
    if(reqs.length){updateBusy=true;refresh();}
    try{
      const deps=await scriptRequiresFlow(name,reqs,{skip:reqOnly,run:newRun(step)});
      if(!deps)return false;
      const data=await putScript(name,src);
      const err=(data&&data.error)||null;
      if(oldName){
        try{await carryScript(oldName,name,step);}
        catch(e){if(!taken)await delScript(name,true).catch(()=>{});throw e;}
        try{await delScript(oldName,true);}
        catch(e){toastErr(e);}
      }
      onDevice={name,src,err};
      loadData(name);
      setStatus(err,name);
      selName=name;refreshTree();
      toast(err?t('scrbroken'):deps.done.length?scriptRequiresDone(name,deps):oldName?t('renamed'):t('saved'),!err&&!deps.left.length);
      if(replacing&&!err)stateOut.replaceChildren('● '+t('scrreload'));
      return true;
    }catch(e){toastErr(e);}
    finally{if(reqs.length)updateBusy=false;step();refresh();}
    return false;
  }
  saveBtn.addEventListener('click',()=>mode==='data'?saveData():save());
  const prevBtn=el('button',{class:'ico',title:t('scrpreview'),'aria-label':t('scrpreview')},icon('eye'));
  prevBtn.addEventListener('click',async()=>{
    try{
      await req('PUT','/api/v1/apps/active',{name:onDevice.name});
      toast(t('scrpreviewed'));
    }catch(e){toastErr(e);}
  });
  const icoBtn=el('button',{class:'ico',style:'display:none'},icon('image'));
  icoBtn.addEventListener('click',async()=>{
    icoBtn.disabled=true;
    try{await installScriptIcons(scriptIcons(ed.value),iconsHave);}
    catch(e){toastErr(e);}
    try{iconsHave=await installedIconIds();}catch(e){}
    paintIcons();
  });
  const expBtn=el('button',{class:'ico',title:t('export'),'aria-label':t('export')},icon('export'));
  expBtn.addEventListener('click',()=>{
    if(!ed.value.trim()){toast(t('scrempty'),false);return;}
    download((nameIn.value.trim()||'script')+'.ax',ed.value);
  });

  const pop=el('div',{class:'ac',style:'display:none'});
  let acItems=[],acSel=0;
  const acClose=()=>{pop.style.display='none';acItems=[];};
  function acWord(){
    const m=/[A-Za-z_][\w.]*$/.exec(ed.ta.value.slice(0,ed.ta.selectionStart));
    return m?{text:m[0],at:m.index}:null;
  }
  function acPaint(){
    pop.replaceChildren(...acItems.map((s,i)=>{
      const p=s.indexOf('(');
      return el('div',{class:i===acSel?'on':'',onmousedown:e=>{e.preventDefault();acSel=i;acAccept();}},
        p<0?s:s.slice(0,p),p<0?null:el('span',{class:'g'},s.slice(p)));
    }));
  }
  function acShow(force){
    const w=acWord();
    if(!w&&!force)return acClose();
    if(w&&!force&&w.text.length<2)return acClose();
    const q=w?w.text.toLowerCase():'';
    acItems=BERRY_API.concat(BERRY_MODS,BERRY_CORE).filter(s=>s.toLowerCase().startsWith(q)).slice(0,40);
    if(!acItems.length||(w&&acItems.length===1&&acItems[0].split('(')[0]===w.text))return acClose();
    acSel=0;acPaint();
    const xy=ed.caretXY();
    pop.style.cssText='left:'+Math.max(0,xy.x-6)+'px;top:'+(xy.y+4)+'px';
  }
  function acAccept(){
    const s=acItems[acSel];
    if(!s)return acClose();
    const w=acWord();
    if(w){ed.ta.selectionStart=w.at;ed.ta.selectionEnd=w.at+w.text.length;}
    const callable=s.includes('('),empty=s.endsWith('()');
    ed.insert(s.split('(')[0]+(callable?'()':''));
    if(callable&&!empty)ed.ta.selectionStart=ed.ta.selectionEnd=ed.ta.selectionEnd-1;
    acClose();
  }

  const INDENT='  ';
  const OPENS=/^\s*(def|if|elif|else|while|for|do|class|try|except)\b(?!.*\bend\b)/;
  function lineSpan(){
    const v=ed.ta.value;
    const a=v.lastIndexOf('\n',ed.ta.selectionStart-1)+1;
    let b=v.indexOf('\n',ed.ta.selectionEnd);
    return[a,b<0?v.length:b];
  }
  function mapLines(fn){
    const[a,b]=lineSpan(),v=ed.ta.value;
    const out=v.slice(a,b).split('\n').map(fn).join('\n');
    ed.ta.selectionStart=a;ed.ta.selectionEnd=b;
    ed.insert(out);
    ed.ta.selectionStart=a;ed.ta.selectionEnd=a+out.length;
  }
  ed.ta.addEventListener('keydown',e=>{
    if(acItems.length){
      if((e.key==='ArrowDown'||e.key==='ArrowUp')&&!e.shiftKey){
        e.preventDefault();
        acSel=(acSel+(e.key==='ArrowDown'?1:acItems.length-1))%acItems.length;
        acPaint();
        const on=pop.querySelector('.on');if(on)on.scrollIntoView({block:'nearest'});
        return;
      }
      if(e.key==='Enter'||e.key==='Tab'){e.preventDefault();acAccept();return;}
      if(e.key==='Escape'){e.preventDefault();acClose();return;}
    }
    const mod=(e.ctrlKey||e.metaKey)&&!e.altKey;
    if(mod&&(e.key==='.'||e.code==='Period')){e.preventDefault();acShow(true);return;}
    if(mod&&(e.key==='/'||e.key==='#'||e.code==='Backslash'||(e.code==='Slash'&&e.key!=='-'))){
      e.preventDefault();
      const[a,b]=lineSpan(),rows=ed.ta.value.slice(a,b).split('\n');
      const all=rows.filter(r=>r.trim()).every(r=>/^\s*#/.test(r));
      mapLines(r=>all?r.replace(/^(\s*)#\s?/,'$1'):r.trim()?r.replace(/^(\s*)/,'$1# '):r);
      return;
    }
    if(e.key==='Tab'){
      e.preventDefault();
      if(ed.ta.selectionStart===ed.ta.selectionEnd&&!e.shiftKey)return ed.insert(INDENT);
      mapLines(r=>e.shiftKey?r.replace(/^ {1,2}/,''):r.trim()?INDENT+r:r);
      return;
    }
    if(e.key==='Enter'){
      const v=ed.ta.value,a=v.lastIndexOf('\n',ed.ta.selectionStart-1)+1;
      const cur=v.slice(a,ed.ta.selectionStart);
      const pad=(/^\s*/.exec(cur)||[''])[0];
      e.preventDefault();
      ed.insert('\n'+pad+(OPENS.test(cur)?INDENT:''));
      return;
    }
  });
  function saveKey(e){
    if(!document.body.contains(ed.ta)){document.removeEventListener('keydown',saveKey);return;}
    if((e.ctrlKey||e.metaKey)&&!e.altKey&&(e.code==='KeyS'||e.key.toLowerCase()==='s')){
      e.preventDefault();mode==='data'?saveData():save();
    }
  }
  document.addEventListener('keydown',saveKey);
  let dedenting=false;
  ed.onEdit(()=>{
    const v=ed.ta.value,c=ed.ta.selectionStart;
    const a=v.lastIndexOf('\n',c-1)+1;
    if(!dedenting&&/^ +end$/.test(v.slice(a,c))&&(c===v.length||v[c]==='\n')){
      dedenting=true;
      ed.ta.selectionStart=a;ed.ta.selectionEnd=c;
      ed.insert(v.slice(a,c).replace(/^ {1,2}/,''));
      dedenting=false;
      return;
    }
    setStatus(undefined,undefined);
    refresh();acShow(false);
  });
  ed.onMove(showPos);
  ed.ta.addEventListener('blur',()=>setTimeout(acClose,120));
  nameIn.addEventListener('input',refresh);
  ed.box.append(empty,pop);

  const dataEd=mkCodeEditor();
  dataEd.ta.setAttribute('aria-label',t('scrdata'));
  const dPos=el('span'),dBytes=el('span'),dState=el('span',{class:'end'});
  const dErr=el('div',{class:'ederr',style:'display:none'}),dCount=el('span',{class:'ftcount'});
  const codeTab=el('button',{role:'tab'},t('scrcode'));
  const dataTab=el('button',{role:'tab'},t('scrdata'),dCount);
  const tabs=el('div',{class:'edtabs',role:'tablist'},codeTab,dataTab);
  let mode='code',stored=null;
  const dataDirty=()=>!!stored&&dataEd.value!==stored.text;
  const fmtData=o=>{
    const e=Object.entries(o);
    return e.length?'{\n'+e.map(([k,v])=>INDENT+JSON.stringify(k)+': '+JSON.stringify(v)).join(',\n')+'\n}':'{}';
  };
  function dataCheck(){
    let obj=null,err=null;
    try{
      obj=JSON.parse(dataEd.value);
      if(!obj||typeof obj!=='object'||Array.isArray(obj)){obj=null;err={message:t('dataobj')};}
    }catch(e){
      const m=/line (\d+)/.exec(e.message),p=/position (\d+)/.exec(e.message);
      err={message:e.message,line:m?+m[1]:p?dataEd.value.slice(0,+p[1]).split('\n').length:0};
    }
    dataEd.markError(err&&err.line);
    dErr.style.display=err?'':'none';
    if(err)dErr.replaceChildren(el('b',null,'JSON'),el('span',null,err.message));
    dState.className='end '+(err?'bad':'good');
    dState.replaceChildren(err?'⨯ '+(err.line?t('scrline')+' '+err.line:'JSON'):'✓ JSON');
    return obj;
  }
  function dataPos(){
    const c=dataEd.caret();
    dPos.replaceChildren(t('scrline')+' '+c.line+':'+c.col);
    dBytes.replaceChildren(enc.encode(dataEd.value).length+' B');
  }
  function setMode(m){
    mode=m;
    edwrap.classList.toggle('data',m==='data');
    codeTab.setAttribute('aria-selected',String(m==='code'));
    dataTab.setAttribute('aria-selected',String(m==='data'));
    nameIn.readOnly=m==='data';
    if(m==='data'){dataEd.repaint();dataCheck();dataPos();}
  }
  function paintData(){
    const on=!!stored&&!!onDevice&&onDevice.name===stored.name&&nameIn.value===stored.name;
    tabs.style.display=on?'':'none';
    if(!on&&mode==='data')setMode('code');
    dCount.replaceChildren(on?String(Object.keys(stored.obj).length||''):'');
  }
  async function loadData(name){
    let obj=null;
    try{({data:obj}=await api('/api/v1/apps/'+encodeURIComponent(name)+'/data'));}catch(e){}
    if(!onDevice||onDevice.name!==name)return;
    stored=obj&&typeof obj==='object'&&!Array.isArray(obj)?{name,obj,text:fmtData(obj)}:null;
    if(stored){dataEd.value=stored.text;dataCheck();}
    refresh();
  }
  async function saveData(){
    if(updateBusy||!stored)return false;
    const next=dataCheck();
    if(!next){toast(t('datafix'),false);return false;}
    const name=stored.name,prev=stored.obj,body={};
    for(const k of new Set([...Object.keys(prev),...Object.keys(next)]))
      if(JSON.stringify(prev[k])!==JSON.stringify(next[k]))body[k]=k in next?next[k]:null;
    if(!Object.keys(body).length){await loadData(name);return true;}
    saveBtn.disabled=true;
    try{
      const{data:r}=await req('PATCH','/api/v1/apps/'+encodeURIComponent(name)+'/data',body);
      const err=(r&&r.error)||null;
      if(onDevice&&onDevice.name===name){onDevice.err=err;setStatus(err,name);}
      const s=allFiles().find(x=>x.name===name);
      if(s){s.error=err;paintTree();}
      toast(err?t('scrbroken'):t('saved'),!err);
      await loadData(name);
      return true;
    }catch(e){toastErr(e);return false;}
    finally{refresh();}
  }
  codeTab.addEventListener('click',()=>setMode('code'));
  dataTab.addEventListener('click',async()=>{
    if(mode==='data')return;
    if(stored&&!dataDirty())await loadData(stored.name);
    if(stored){setMode('data');dataEd.focus();}
  });
  dataEd.onEdit(()=>{dataCheck();refresh();});
  dataEd.onMove(dataPos);
  dataEd.ta.addEventListener('keydown',e=>{
    if(e.key==='Tab'&&!e.shiftKey){e.preventDefault();dataEd.insert(INDENT);return;}
    if(e.key!=='Enter')return;
    const v=dataEd.ta.value,c=dataEd.ta.selectionStart,cur=v.slice(v.lastIndexOf('\n',c-1)+1,c);
    e.preventDefault();
    dataEd.insert('\n'+/^\s*/.exec(cur)[0]+(/[[{]\s*$/.test(cur)?INDENT:''));
  });

  const ftlist=el('div',{class:'ftlist'});
  const modlist=el('div',{class:'ftlist'});
  const treeCount=el('span',{class:'ftcount'});
  const scrCount=el('span',{class:'ftcount'});
  const modCount=el('span',{class:'ftcount'});
  const newFileBtn=el('button',{class:'pri',title:t('newscr'),'aria-label':t('newscr')},'+');
  const newModBtn=el('button',{class:'pri',title:t('newmod'),'aria-label':t('newmod')},'+');
  let scripts=[],modules=[],selName=null,renaming=false;
  const allFiles=()=>scripts.concat(modules);
  const isDirty=()=>top.classList.contains('mod');
  async function checkHubUpdates(){
    if(updateBusy)return;
    const generation=++checkGeneration;
    checkHubBtn.disabled=true;checkHubBtn.textContent=t('suChecking');
    const files=allFiles().slice(),releases=new Map();
    if(!S.caps)try{readAudioCaps((await api('/api/v1/capabilities')).data||{});}catch(e){}
    async function worker(){
      while(files.length&&generation===checkGeneration&&view.isConnected){
        const file=files.shift(),url='/api/v1/apps/script/'+encodeURIComponent(file.name);
        try{
          const {text}=await api(url);
          const origin=hubScriptOrigin(text);
          if(!origin){hubUpdates.delete(file.name);continue;}
          if(!releases.has(origin.id))releases.set(origin.id,hubScriptRelease(origin.id));
          const release=await releases.get(origin.id);
          let ready=release.sha256!==origin.sha256;
          if(!ready&&(release.sounds?.length||sndCount.has(file.name))){
            const p=soundPlan(release,(await api(url+'/sounds')).data.files);
            ready=p.upload.length+p.remove.length>0;
          }
          if(generation!==checkGeneration)return;
          hubUpdates.set(file.name,{previous:text,release,modified:hubScriptHash(origin.code)!==origin.sha256,
            state:ready?'suReady':'suCurrent'});
        }catch(e){if(generation===checkGeneration)hubUpdates.set(file.name,{state:e.status===404?'suMissing':'suOffline'});}
      }
    }
    await Promise.all([worker(),worker()]);
    if(generation!==checkGeneration||!view.isConnected)return;
    checkHubBtn.disabled=false;checkHubBtn.textContent=t('suCheck');paintTree();paintHubUpdate();paintSounds();
  }
  function paintHubUpdate(){
    hubPanel.hidden=!onDevice;
    if(!onDevice)return;
    const item=hubUpdates.get(onDevice.name);
    const origin=hubScriptOrigin(onDevice.src);
    hubPanel.hidden=!origin;
    if(!origin)return;
    hubPanel.replaceChildren(el('span',{class:'help'},t(item?.state==='suCurrent'&&item.modified?'suLocal':item?.state||'suUnknown')));
    if(!item?.release)return;
    const local=item.modified||ed.value!==onDevice.src;
    hubPanel.append(el('a',{href:'https://awtrix.de/script/'+item.release.id,target:'_blank',rel:'noopener'},t('suRevision')+' '+item.release.revision));
    if(item.state!=='suReady')return;
    if(item.release.notes)hubPanel.append(el('p',{class:'help',style:'white-space:pre-wrap'},item.release.notes));
    const misfit=fitWords(item.release.needs,item.release.display);
    if(misfit)hubPanel.append(el('p',{class:'help warn'},t('suNoFit').replace('{w}',misfit)));
    if(local)hubPanel.append(el('p',{class:'help'},t('suModified')));
    const name=onDevice.name;
    const btn=el('button',{class:'pri',disabled:updateBusy,onclick:()=>{
      if(updateBusy)return;
      toast(t(local?'suModified':'suConfirm')+(item.release.sounds?.length&&!capPresent('audio.mp3')?' '+t('sndOff'):''),false,[
        {label:t(local?'suCopy':'suUpdate'),pri:true,fn:()=>applyHubUpdate(name,item,local)},
        {label:t('cancel'),fn:()=>{}}]);
    }},t(updateBusy?'suBusy':local?'suCopy':'suUpdate'));
    hubPanel.append(btn);
  }
  function paintRequires(){
    const s=onDevice&&allFiles().find(x=>x.name===onDevice.name);
    const miss=(s?.requires||[]).filter(r=>r.missing);
    reqNote.hidden=!miss.length;
    if(!miss.length)return;
    reqNote.replaceChildren(el('span',{class:'help'},t('reqMissing')+' '+miss.map(r=>r.name).join(', ')));
    if(miss.some(r=>r.hub))reqNote.append(el('button',{disabled:updateBusy,onclick:()=>fixRequires(s.name,miss)},t('reqInstall')));
  }
  async function fixRequires(name,miss){
    if(updateBusy)return;
    updateBusy=true;refresh();
    try{const res=await scriptRequiresFlow(name,miss,{ask:false,run:newRun(step)});
      if(res.done.length||res.left.length)toast(scriptRequiresDone('',res),!res.left.length);}
    catch(e){toastErr(e);}
    finally{updateBusy=false;step();await refreshTree();refresh();}
  }
  async function applyHubUpdate(name,item,asCopy){
    if(updateBusy)return;
    if(onDevice?.name!==name)return;
    if(!asCopy&&isDirty()){toast(t('unsaved'),false);return;}
    const editorSource=ed.value,editorName=nameIn.value,run=newRun(step),hid=item.release.id;
    updateBusy=true;refresh();step(t('suBusy'));
    try{
      const caps=(await api('/api/v1/capabilities')).data||{};
      if(!caps.scriptUpdates)throw new Error(t('suFirmware'));
      readAudioCaps(caps);
      const {text:current}=await api('/api/v1/apps/script/'+encodeURIComponent(name));
      const next=await prepareHubScriptUpdate(current,item.release);
      if(current!==item.previous&&(asCopy||current!==next.source)||next.modified&&!asCopy)throw new Error(t('suChanged'));
      const target=asCopy?freshScriptName(name.slice(0,23)+'-hub'):name,url='/api/v1/apps/script/'+encodeURIComponent(target);
      const plan=soundPlan(item.release,asCopy||!item.release.sounds?[]:(await api(url+'/sounds')).data.files);
      run.pending=plan.upload;
      const deps=await scriptRequiresFlow(target,scriptRequires(next.source),{run});
      if(!deps)return;
      await soundRoom(run);
      // Existing icons are collision-checked by idbInstall; changed shared icons require
      // the explicit Icons workflow. A dependency failure never replaces the script.
      for(const id of scriptIcons(next.source))await idbInstall(id);
      const write=async prev=>{
        const {data}=await api('/api/v1/apps/script-update/'+encodeURIComponent(target),
          {method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({expected_source:prev,source:next.source})});
        if(data?.error)throw new Error(data.error.message||t('scrbroken'));
      };
      if(asCopy)await freshScript(run,target,hid,next.source,item.release,()=>write(null));
      else{step(t('suBusy'));await soundSync(target,hid,plan,step,()=>write(current));}
      toast(deps.done.length?scriptRequiresDone(target,deps):t('suDone'),!deps.left.length);
      if(!asCopy&&onDevice?.name===name){
        if(ed.value===editorSource&&nameIn.value===editorName){load(name,next.source,null);selName=name;}
        else{onDevice={name,src:next.source,err:null};refresh();}
      }
      // A copy never replaces an unsaved editor draft.
    }catch(e){
      if(asCopy)for(const n of run.created.reverse())await delScript(n,true).catch(()=>{});
      toast(e.message,false,[e.code==='hubAuthentication'?connectHubAction():{label:t('retry'),pri:true,
        fn:()=>{const i=hubUpdates.get(name);applyHubUpdate(name,i?.release?i:item,asCopy);}},{label:t('close'),fn(){}}]);
    }
    finally{updateBusy=false;step();await refreshTree();refresh();}
  }
  const checkHubBtn=el('button',{onclick:checkHubUpdates},t('suCheck'));
  async function refreshTree(){
    try{
      const{data}=await api('/api/v1/apps');
      const byName=(a,b)=>a.name.localeCompare(b.name);
      const rows=(Array.isArray(data)?data:[])
        .filter(a=>a.origin==='script'||a.origin==='module')
        .map(a=>({name:a.name,error:a.error||null,module:a.origin==='module',requires:a.meta?.requires||[]}));
      scripts=rows.filter(a=>!a.module).sort(byName);
      modules=rows.filter(a=>a.module).sort(byName);
    }catch(e){scripts=[];modules=[];}
    try{sndCount=new Map(hasSink('mp3')?((await api('/api/v1/audio/mp3')).data.scripts||[]).map(s=>[s.name,s.files.length]):[]);}catch(e){}
    paintTree();paintRequires();paintSounds();
    checkHubUpdates().catch(()=>{});
  }
  function paintTree(){
    renaming=false;
    const total=scripts.length+modules.length;
    treeCount.textContent=total?String(total):'';
    scrCount.textContent=scripts.length?String(scripts.length):'';
    modCount.textContent=modules.length?String(modules.length):'';
    const hadFocus=filetree.contains(document.activeElement);
    ftlist.replaceChildren(...(scripts.length?scripts.map(treeItem)
      :[el('div',{class:'empty'},t('scrnolist'))]));
    modlist.replaceChildren(...(modules.length?modules.map(treeItem)
      :[el('div',{class:'empty'},t('modnone'))]));
    if(hadFocus){
      const row=filetree.querySelector('.ftitem.on')||filetree.querySelector('.ftitem');
      if(row)row.focus();
    }
  }
  let pendingConfirm=null;
  function guardedSwitch(run){
    if(updateBusy)return;
    if(!isDirty())return run();
    if(pendingConfirm)pendingConfirm.remove();
    const from=nameIn.value.trim()||t('scrnone');
    pendingConfirm=toast(from+': '+t('unsaved'),false,[
      {label:t('save'),pri:true,fn:async()=>{
        if(dataDirty()&&!await saveData())return;
        if(isDirty()&&!await save())return;
        run();
      }},
      {label:t('discard'),fn:run},
      {label:t('cancel'),fn:()=>{}},
    ]);
  }
  async function reallyOpen(name){
    try{
      const{text}=await api('/api/v1/apps/script/'+encodeURIComponent(name));
      if(updateBusy)return;
      const s=allFiles().find(x=>x.name===name);
      load(name,text,(s&&s.error)||null);
      selName=name;paintTree();closeTree();
    }catch(e){toastErr(e);}
  }
  function openScript(name){
    if(name===selName||renaming)return;
    guardedSwitch(()=>reallyOpen(name));
  }
  function moveRow(item,d){
    const rows=[...filetree.querySelectorAll('.ftitem')];
    const at=rows.indexOf(item);
    if(at<0)return;
    rows[(at+d+rows.length)%rows.length].focus();
  }
  function treeItem(s){
    const on=s.name===selName;
    const item=el('div',{class:'ftitem'+(on?' on':'')+(s.error?' err':''),
      role:'button',tabindex:'0','aria-current':on?'true':null,
      title:s.error?'ERR: '+(s.error.message||''):s.name},
      icon('file'),el('span',{class:'nm'},s.name));
    item.addEventListener('click',()=>openScript(s.name));
    item.addEventListener('keydown',e=>{
      if(renaming||e.target!==item)return;
      if(e.key==='Enter'||e.key===' '){e.preventDefault();openScript(s.name);}
      else if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();moveRow(item,e.key==='ArrowDown'?1:-1);}
    });
    const ren=el('button',{title:t('rename'),'aria-label':t('rename')},icon('pen'));
    ren.addEventListener('click',()=>beginRename(s,item));
    const gone=async sounds=>{
      try{
        await delScript(s.name,sounds);
        toast(t('deleted'));
        if(selName===s.name){selName=null;load('','');}
        await refreshTree();
      }catch(e){toastErr(e);}
    };
    // A script with sounds asks what becomes of them instead of arming the button.
    const n=sndCount.get(s.name);
    const del=armable(el('button',{class:'danger',title:t('del'),'aria-label':t('del')},icon('trash')),()=>{
      if(updateBusy)return;
      if(!n)return gone(false);
      toast(t('delSndAsk').replace('{s}',s.name).replace('{n}',n),true,[{label:t('delWithSnd'),fn:()=>gone(true)},
        {label:t('keepSnd'),fn:()=>gone(false),pri:true},{label:t('cancel'),fn:()=>{}}]);
    },()=>!n);
    const fx=el('span',{class:'fx'},ren,del);
    fx.addEventListener('click',e=>e.stopPropagation());
    const hubState=hubUpdates.get(s.name);
    if(hubState?.state==='suReady')item.append(el('span',{class:'badge',title:t('suReady')},'↑'));
    item.append(fx);
    return item;
  }
  function beginRename(s,item){
    if(renaming||updateBusy)return; renaming=true;
    let done=false;
    const inp=el('input',{class:'ftren',type:'text',value:s.name,maxlength:'32',spellcheck:'false'});
    item.replaceChildren(icon('file'),inp);
    inp.focus();inp.select();
    inp.addEventListener('click',e=>e.stopPropagation());
    const cancel=()=>{if(done)return;done=true;paintTree();};
    async function commit(){
      if(done)return;
      const nn=inp.value.trim();
      if(!nn||nn===s.name)return cancel();
      done=true;
      if(!/^[A-Za-z0-9_-]{1,32}$/.test(nn)){toast(t('scrbadname'),false);return paintTree();}
      if(allFiles().some(x=>x.name===nn)){toast(t('scrclash'),false);return paintTree();}
      try{
        const{text}=await api('/api/v1/apps/script/'+encodeURIComponent(s.name));
        await api('/api/v1/apps/script/'+encodeURIComponent(nn),
          {method:'PUT',headers:{'Content-Type':'text/plain'},body:text});
        try{await carryScript(s.name,nn,step);}
        catch(e){await delScript(nn,true).catch(()=>{});throw e;}
        finally{step();}
        await delScript(s.name,true);
        if(selName===s.name){selName=nn;nameIn.value=nn;if(onDevice){onDevice.name=nn;loadData(nn);}refresh();}
        toast(t('renamed'));await refreshTree();
      }catch(e){toastErr(e);paintTree();}
    }
    inp.addEventListener('keydown',e=>{
      if(e.key==='Enter'){e.preventDefault();commit();}
      else if(e.key==='Escape'){e.preventDefault();cancel();}
    });
    inp.addEventListener('blur',commit);
  }
  const shlist=el('div',{class:'shlist'});
  function shAge(ms){
    if(ms<1000)return Math.max(0,ms)+'ms';
    if(ms<60000)return Math.round(ms/1000)+'s';
    if(ms<3600000)return Math.round(ms/60000)+'m';
    return Math.round(ms/3600000)+'h';
  }
  function shValue(e){
    if(e.type==='string')return JSON.stringify(e.value);
    if(e.value===null)return 'nan';
    return String(e.value);
  }
  function shRow(e){
    const v=shValue(e);
    return el('div',{class:'shrow',
      title:e.owner+'.'+e.key+' = '+v+'  ('+e.type+', '+shAge(e.ageMs)+')'},
      el('span',{class:'shk'},el('b',null,e.owner),'.'+e.key),
      el('span',{class:'shv'+(e.ageMs>300000?' stale':'')},v));
  }
  async function refreshShared(){
    if(!shpanel.classList.contains('open'))return;
    const{data}=await api('/api/v1/scripts/shared');
    const rows=Array.isArray(data)?data:[];
    shlist.replaceChildren(...(rows.length?rows.map(shRow)
      :[el('div',{class:'empty'},t('shnone'))]));
  }
  const shHead=el('div',{class:'fthead',role:'button',tabindex:'0','aria-expanded':'false'},
    el('h2',{title:t('shhelp')},t('sharedHead')),el('span',{class:'chev'},icon('chev')));
  const shpanel=el('div',{class:'shpanel'},shHead,shlist);
  function showShared(on){
    shpanel.classList.toggle('open',on);
    shHead.setAttribute('aria-expanded',on?'true':'false');
    try{localStorage.awtrixShared=on?'1':'0';}catch(e){}
    if(on)refreshShared().catch(()=>{});
  }
  pressable(shHead,()=>showShared(!shpanel.classList.contains('open')));
  try{if(localStorage.awtrixShared==='1')showShared(true);}catch(e){}
  newModBtn.addEventListener('click',()=>startTemplate(true));

  newFileBtn.addEventListener('click',()=>startTemplate(false));
  const treeChev=el('span',{class:'chev'},icon('chev'));
  const treeHead=el('div',{class:'fthead',role:'button',tabindex:'0','aria-label':t('scriptsTab')},
    el('h2',null,t('scriptsTab'),treeCount),treeChev);
  const scrPane=el('div',{class:'ftpane',role:'group','aria-label':t('scriptsTab')},
    el('div',{class:'ftgrp'},t('scriptsTab'),scrCount,newFileBtn),ftlist);
  const modPane=el('div',{class:'ftpane mods',role:'group','aria-label':t('modules')},
    el('div',{class:'ftgrp',title:t('modulesH')},t('modules'),modCount,newModBtn),modlist);
  const hubScripts=hubLink('../scripts',t('scrHubFind')+' ↗','fthub');
  const filetree=el('div',{class:'filetree'},treeHead,checkHubBtn,hubScripts,scrPane,modPane,shpanel);
  function setTreeOpen(open){
    if(!open&&matchMedia('(max-width:720px)').matches&&
      [scrPane,modPane,shpanel].some(p=>p.contains(document.activeElement)))treeHead.focus();
    filetree.classList.toggle('open',open);
    treeHead.setAttribute('aria-expanded',String(open));
  }
  treeHead.setAttribute('aria-expanded','false');
  const toggleTree=()=>setTreeOpen(!filetree.classList.contains('open'));
  const closeTree=()=>setTreeOpen(false);
  pressable(treeHead,toggleTree);

  top.append(nameIn,dirtyDot,el('span',{class:'grow'}),
    el('div',{class:'sec'},impBtn,expBtn,icoBtn,sndBtn,prevBtn),saveBtn);
  const edwrap=el('div',{class:'edwrap'},top,tabs,
    el('div',{class:'edcode'},hubPanel,reqNote,prog,ed.node,
      el('div',{class:'edbar'},posOut,byteOut,el('span',{class:'keys'},t('scrkeys')),stateOut),errBox),
    el('div',{class:'eddata'},dataEd.node,el('div',{class:'edbar'},dPos,dBytes,dState),dErr));
  setMode('code');
  const goBtn=el('button',null,t('scroffGo'));
  goBtn.addEventListener('click',()=>{location.hash='#system';});
  const offBanner=el('div',{class:'banner',style:'display:none'},'⏸ ',
    el('span',{class:'grow'},t('scroffTitle')),goBtn);
  const edlayout=el('div',{class:'edlayout wide'},filetree,edwrap);
  scriptsOff().then(off=>{
    if(!off)return;
    offBanner.style.display='';edlayout.classList.add('offb');setStatus(null,selName);
  });
  view.append(offBanner);
  view.append(edlayout,fileIn);
  ed.repaint();
  const draft=scrDraft;
  refresh();
  installedIconIds().then(have=>{iconsHave=have;paintIcons();}).catch(()=>{});

  if(scrPending){
    const p=scrPending;scrPending=null;
    api('/api/v1/apps/script/'+encodeURIComponent(p.name))
      .then(({text})=>{load(p.name,text,p.error);selName=p.name;paintTree();})
      .catch(toastErr);
  }else if(draft){
    onDevice=draft.onDevice||null;
    nameIn.value=draft.name;ed.value=draft.src;
    stored=draft.stored||null;
    if(stored){dataEd.value=draft.dtext;dataCheck();}
    refresh();
  }
  refreshTree();
  refreshShared().catch(()=>{});
  poller(refreshShared,3000);
}
const NESTED=['scroll'];
