function uploadZone(dir,accept,hint,onDone,asGif){
  const fileIn=el('input',{type:'file',accept,multiple:''});
  const list=el('div',{class:'uplist'});
  const zone=el('div',{class:'drop',role:'button',tabindex:'0'},hint,list);
  let queue=Promise.resolve();
  const handle=files=>{files=[...files];queue=queue.then(()=>run(files));};
  pressable(zone,()=>fileIn.click());
  zone.addEventListener('dragover',e=>{e.preventDefault();zone.classList.add('over');});
  zone.addEventListener('dragleave',()=>zone.classList.remove('over'));
  zone.addEventListener('drop',e=>{e.preventDefault();zone.classList.remove('over');handle(e.dataTransfer.files);});
  fileIn.addEventListener('change',()=>{handle(fileIn.files);fileIn.value='';});
  async function run(files){
    for(const f of files){
      const row=el('div',null,f.name+' - 0%');
      list.append(row);
      try{
        const conv=asGif?await iconAsGif(f,f.name):null;
        const nm=conv?conv.name:f.name;
        await uploadFile(conv?conv.blob:f,dir,nm,p=>row.replaceChildren(nm+' - '+(p*100).toFixed(0)+'%'));
        if(conv)await dropStale(dir,f.name,nm);
        row.replaceChildren(nm+' ✓ '+t('uploaded'));
      }catch(e){row.replaceChildren(f.name+' ✗ '+e.message);toast(f.name+': '+e.message,false);}
    }
    setTimeout(()=>list.replaceChildren(),4000);
    onDone();
  }
  return el('div',null,zone,fileIn);
}
let assetRev=0;
async function uploadFile(file,dir,name,onProgress,timeout){
  const mp3=dir.startsWith('/api/'),x=await xhrPost(mp3?dir:'/api/v1/files?dir='+encodeURIComponent(dir),'file',file,name,onProgress,timeout);
  if(x.status>=300){
    let m;try{m=JSON.parse(x.responseText).error.message;}catch(e){}
    const k=mp3&&{400:'sndName',404:'sndNoScript',415:'sndNoMp3',500:'sndFull',507:'sndFull'}[x.status];
    const e=new Error(k?t(k):m||'HTTP '+x.status);e.status=x.status;throw e;
  }
  assetRev++;
}
async function delFile(path){
  await api('/api/v1/files?path='+encodeURIComponent(path),{method:'DELETE'});
  assetRev++;
}
function viewIcons(view){
  const iconsGrid=el('div',{class:'grid-icons'});
  const meter=el('div',{class:'st'});
  const ownTab=el('button',null,t('icown'));
  const ownSearch=el('input',{type:'search',placeholder:t('icsearch'),'aria-label':t('icsearch')});
  const ownInfo=el('span',{class:'help',role:'status'});
  let ownFiles=[],reloadVersion=0;
  const add=el('button',{type:'button',onclick:()=>selectPane(1,true)},t('icadd'));
  const upload=uploadZone('/ICONS','.png,.jpg,.jpeg,.gif',t('icdrop'),reload,true);
  const panes=[[ownTab,el('div',null,
    el('div',{class:'icons-toolbar'},ownSearch,ownInfo,add),iconsGrid)],
    [el('button',null,t('icadd')),el('div',{hidden:'hidden'},el('div',{class:'icons-add'},
      el('section',null,el('h3',null,t('icupload')),el('p',{class:'ghelp'},t('icuploadhelp')),upload),
      el('section',{class:'hub-source'},el('h3',null,t('ichub')),el('p',{class:'ghelp'},t('ichubhelp')),
        hubLink('../icons',t('ichubopen')+' →','pri')),
      el('section',null,el('h3',null,t('iccreate')),el('p',{class:'ghelp'},t('iccreatehelp')),
        el('a',{href:'#/editor'},t('editorTab')+' →'))))]];
  function selectPane(index,focus=false){
    panes.forEach(([tab,pane],i)=>{
      tab.classList.toggle('on',i===index);tab.setAttribute('aria-selected',String(i===index));
      tab.tabIndex=i===index?0:-1;pane.hidden=i!==index;
    });
    if(focus)panes[index][0].focus();
  }
  panes.forEach(([tab,pane],i)=>{
    tab.id='icons-tab-'+i;tab.setAttribute('role','tab');tab.setAttribute('aria-controls','icons-pane-'+i);
    pane.id='icons-pane-'+i;pane.setAttribute('role','tabpanel');pane.setAttribute('aria-labelledby',tab.id);
    tab.addEventListener('click',()=>selectPane(i));
    tab.addEventListener('keydown',e=>{
      const next=e.key==='ArrowRight'?(i+1)%panes.length:e.key==='ArrowLeft'?(i+panes.length-1)%panes.length:e.key==='Home'?0:e.key==='End'?panes.length-1:null;
      if(next!==null){e.preventDefault();selectPane(next,true);}
    });
  });
  selectPane(0);
  function paintOwn(){
    const needle=ownSearch.value.trim().toLowerCase();
    const files=ownFiles.filter(f=>f.name.toLowerCase().includes(needle));
    ownInfo.textContent=needle?files.length+' '+t('of')+' '+ownFiles.length:'';
    iconsGrid.replaceChildren(...(files.length?files.map(iconTile):[
      el('div',{class:'empty'},t(ownFiles.length?'idbnone':'icnone'),
        el('button',{type:'button',onclick:()=>{if(ownFiles.length){ownSearch.value='';paintOwn();ownSearch.focus();}else selectPane(1,true);}},
          t(ownFiles.length?'icclear':'icadd')))]));
  }
  ownSearch.addEventListener('input',()=>paintOwn());
  async function reload(){
    const version=++reloadVersion;
    try{
      const{data}=await api('/api/v1/files?dir='+encodeURIComponent('/ICONS'),{cache:'no-store'});
      const files=(data.files||[]).sort((a,b)=>a.name.localeCompare(b.name));
      if(version!==reloadVersion)return;
      ownFiles=files.map(f=>({...f,state:'checking'}));
      const used=data.usedBytes||0,total=data.totalBytes||1;
      meter.replaceChildren(el('div',{class:'bar'},el('i',{class:used/total>0.9?'hot':'',
        style:'width:'+(used/total*100).toFixed(1)+'%'})),
        el('span',null,t('icstorage')+': '+fmtBytes(used)+' '+t('of')+' '+fmtBytes(total)));
      ownTab.replaceChildren(t('icown')+' ('+files.length+')');
      paintOwn();
      const checked=await iconInventory(files);
      if(version!==reloadVersion)return;
      ownFiles=checked;paintOwn();
    }catch(e){toastErr(e);}
  }
  function iconTile(f){
    const base=f.name.replace(/\.[^.]+$/,'');
    const originKey=({modified:'icmodified',checking:'icchecking',unknown:'icunknown'})[f.state]||null;
    const ft=el('div',{class:'ft'},el('span',{class:'nm',title:f.name},base),
      el('span',{class:'sz'},fmtBytes(f.size)),originKey?el('span',{class:'icon-origin','data-state':f.state},t(originKey)):null);
    const pw=el('div',{class:'pw'},
      el('img',{src:'/ICONS/'+encodeURIComponent(f.name)+'?v='+f.size+'.'+assetRev,alt:f.name,loading:'lazy'}),
      f.state==='hub'?el('span',{class:'icon-hub-badge','data-state':'hub',title:t('icfromhub')},'Hub'):null);
    const open=el('button',{title:t('more'),'aria-label':t('more')+': '+base,'aria-haspopup':'menu','aria-expanded':'false'},'⋯');
    const shut=()=>{const m=pw.querySelector('.tmenu');if(m)m.remove();open.setAttribute('aria-expanded','false');};
    const show=el('button',{type:'button',class:'icon-show','aria-label':t('icshow')+': '+base},t('icshow'));
    show.addEventListener('click',async()=>{
      show.disabled=true;
      try{await post('/api/v1/notifications',{icon:base,text:' '+base,durationMs:3000,stack:false});toast(t('shown'));}
      catch(e){toastErr(e);}finally{show.disabled=false;}
    });
    // A floating menu cannot leave a tile - .tile clips its overflow - so the
    // icon actions stay in one menu that covers the artwork.
    open.addEventListener('click',()=>{
      if(pw.querySelector('.tmenu'))return shut();
      document.querySelectorAll('.icons-page .tmenu').forEach(m=>{const b=m.closest('.tile').querySelector('[aria-expanded]');if(b)b.setAttribute('aria-expanded','false');m.remove();});
      open.setAttribute('aria-expanded','true');
      const item=(label,fn,danger)=>menuBtn(label,fn,danger,shut,{role:'menuitem'});
      const menu=el('div',{class:'tmenu',role:'menu'},
        item(t('edit'),()=>{pendingEditIcon=f.name;location.hash='#/editor';}),
        f.state==='hub'?el('a',{href:iconOriginHub(f.origin.hub)+encodeURIComponent(f.origin.slug),target:'_blank',rel:'noopener',role:'menuitem'},t('icviewhub')):
          item(t(f.state==='modified'?'icvariant':'icsend'),()=>shareRow(f,base,ft,reload)),
        item(t('rename'),()=>renameRow(f,base,ft,reload)),
        el('a',{href:'/ICONS/'+encodeURIComponent(f.name),download:f.name,role:'menuitem'},t('icdownload')),
        item(t('del'),async()=>{
          try{await delFile('/ICONS/'+f.name);toast(t('deleted'));reload();}catch(e){toastErr(e);}
        },true));
      pw.append(menu);
      menu.querySelector('[role="menuitem"]')?.focus();
    });
    const tile=el('div',{class:'tile'},pw,ft,el('div',{class:'icon-tile-actions'},show,el('div',{class:'acts'},open)));
    tile.addEventListener('keydown',e=>{if(e.key==='Escape'&&pw.querySelector('.tmenu')){shut();open.focus();}});
    return tile;
  }
  view.append(el('div',{class:'card wide icons-page'},
    el('div',{class:'ihdr'},el('div',null,el('h2',null,t('icons')),el('p',{class:'ghelp'},t('icintro'))),meter),
    el('div',{class:'segbar',role:'tablist','aria-label':t('icons')},...panes.map(p=>p[0])),
    ...panes.map(p=>p[1])));
  reload();
}
function renameRow(f,base,ft,onDone){
  const saved=[...ft.childNodes],ext=f.name.slice(base.length);
  const nameIn=el('input',{type:'text',value:base,maxlength:'32',spellcheck:'false','aria-label':t('rename')});
  const ok=el('button',{class:'pri',title:t('save'),'aria-label':t('save')},'✓');
  const back=el('button',{title:t('cancel'),'aria-label':t('cancel')},'✕');
  const restore=()=>ft.replaceChildren(...saved);
  back.addEventListener('click',restore);
  nameIn.addEventListener('keydown',e=>{if(e.key==='Enter')ok.click();else if(e.key==='Escape')restore();});
  ok.addEventListener('click',async()=>{
    const nn=nameIn.value.trim();
    if(nn===base)return restore();
    if(!/^[A-Za-z0-9_-]{1,32}$/.test(nn)){toast(t('scrbadname'),false);nameIn.focus();return;}
    ok.disabled=true;
    try{await post('/api/v1/icons/rename',{from:f.name,to:nn+ext});toast(t('renamed'));onDone();}
    catch(e){ok.disabled=false;toast(e.code==='nameTaken'?t('icclash'):e.message,false);nameIn.focus();}
  });
  ft.replaceChildren(nameIn,ok,back);
  nameIn.focus();nameIn.select();
}
// Swaps a tile's footer into a name field, so submitting needs no dialog.
function descriptiveIconName(name){return /\p{L}/u.test(String(name||'').trim());}
function shareRow(f,base,ft,onDone){
  const saved=[...ft.childNodes];
  const nameIn=el('input',{type:'text',value:descriptiveIconName(base)?base:'',placeholder:t('icpubexample'),'aria-label':t('icpubname'),required:true});
  const send=el('button',{class:'pri'},t('idbsend'));
  const back=el('button',{title:t('cancel'),'aria-label':t('cancel')},'✕');
  const restore=()=>{ft.classList.remove('share');ft.replaceChildren(...saved);};
  back.addEventListener('click',restore);
  nameIn.addEventListener('keydown',e=>{if(e.key==='Enter')send.click();else if(e.key==='Escape')restore();});
  send.addEventListener('click',async()=>{
    if(!descriptiveIconName(nameIn.value)){nameIn.setAttribute('aria-invalid','true');nameIn.focus();toast(t('idbe_descriptiveNameRequired'),false);return;}
    send.disabled=true;
    try{
      const original=await readIconBlob(f.name);
      const submission=/\.gif$/i.test(f.name)?original:await imgToGif(original);
      const d=await idbSubmit(submission,nameIn.value.trim(),f.origin&&f.origin.hub===iconApiUrl()?f.origin.slug:null);
      try{await saveIconOrigin(f.name,d.slug,await iconBlobHash(original),iconApiUrl());}catch(e){toast(t('icoriginfail'),false);}
      restore();
      toast(t(d.status==='existing'?'idbe_duplicate':'idbdone'),true,[{label:t('idbopen'),pri:true,fn:()=>window.open(d.pr,'_blank','noopener')},
        {label:t('close'),fn:()=>{}}]);
      onDone&&onDone();
    }catch(e){
      send.disabled=false;
      // This branch is the normal outcome here, not an edge case: the page is
      // served from the clock, so the Hub session cookie is cross-site and never
      // travels with the POST. Publishing happens in the Hub editor, and saying
      // only 'error' would send people looking for a fault that is not there.
      if(e.code==='notLoggedIn'){
        const has=!!hubToken();
        toast(t(has?'hubAuthRejected':'idbe_notLoggedIn'),false,[
          e.pr?{label:t('hubGet'),pri:!has,fn:()=>window.open(e.pr,'_blank','noopener')}:null,
          {label:t('hubConnect'),pri:has,fn:goToHubSettings},
          {label:t('close'),fn:()=>{}},
        ].filter(Boolean));
      }else toast(e.slug?t('idbdup')+' '+e.slug:e.message,false);
    }
  });
  const terms=el('a',{class:'hint',href:hubPage('/terms-of-service'),target:'_blank',rel:'noopener'},t('idbterms'));
  ft.classList.add('share');
  nameIn.addEventListener('input',()=>nameIn.removeAttribute('aria-invalid'));
  ft.replaceChildren(nameIn,send,back,el('span',{class:'hint'},t('icpubnamehelp')),terms);
  nameIn.focus();nameIn.select();
}
