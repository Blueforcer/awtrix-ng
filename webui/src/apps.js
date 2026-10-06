function cfgDef(f,builtin){
  const[l,h,opt]=builtin&&APPF[f.key]||[];
  const d={w:f.type==='bool'?'toggle':f.type,l:l||f.label||f.key,
    h:l?h:f.help||'',
    min:f.min,max:f.max,step:f.step,unit:f.unit,dflt:f.default,
    nullable:!!f.nullable,numericColor:!!builtin||!!f.nullable,linkLabel:true};
  if(f.type==='select')d.opt=(f.options||[]).map(o=>{
    const x=opt&&opt.find(x=>String(x[0])===String(o));
    return[o,x?x[1]:o];
  });
  return d;
}
const warnBadge=w=>el('div',{class:'badge bad',title:t('cfgWarnH')},w);
const errText=(name,e)=>'ERR:'+name+(e.line?' ('+t('scrline')+' '+e.line+')':'')+' - '+e.message;
const errBadge=(name,e)=>el('div',{class:'badge bad'},errText(name,e));
const metaBits=m=>[m.desc||null,m.author?'by '+m.author:null,m.version?'v'+m.version:null];
const editScript=a=>{scrPending={name:a.name,error:a.error||null};location.hash='#/scripts';};
const configUrl=(name,builtin)=>'/api/v1/apps/'+(builtin?'builtin/':'')+encodeURIComponent(name)+'/config';
async function fillConfigPanel(name,box,saveError,builtin){
  const groupOpen=new Map([...box.querySelectorAll('.cfgsection')].map(s=>[s.dataset.group,s.open]));
  box.replaceChildren(el('div',{class:'mono'},t('cfgLoading')));
  let d;
  try{({data:d}=await api(configUrl(name,builtin)));}
  catch(e){
    box.replaceChildren(el('div',{class:'badge bad'},e.message));
    return false;
  }

  const fields=(d&&Array.isArray(d.fields))?d.fields:[];
  const warnings=(d&&Array.isArray(d.warnings))?d.warnings:[];
  if(!fields.length){
    box.replaceChildren(...warnings.map(warnBadge),el('div',{class:'mono'},t('cfgNone')));
    return true;
  }
  const rows=fields.map(f=>mkField(f.key,cfgDef(f,builtin),f.value,paint));
  const save=el('button',{class:'pri'},t('save'));
  const disc=el('button',null,t('discard'));
  const defs=el('button',{class:'defaults',title:t('cfgDefaults')},t('cfgDefaults'));
  const err=el('div',{class:'badge bad'});
  err.hidden=!saveError;
  if(saveError)err.replaceChildren(errText(name,saveError));
  const bar=el('div',{class:'cfgbar'},defs,el('span',{class:'grow'}),disc,save);
  function paint(){
    const dirty=rows.some(m=>m.dirty());
    save.disabled=!dirty;disc.disabled=!dirty;
  }
  save.addEventListener('click',async()=>{
    const body={};
    rows.forEach((m,i)=>{
      if(!m.dirty())return;
      const path=Array.isArray(fields[i].path)&&fields[i].path.length?fields[i].path:[m.key];
      let target=body;
      path.slice(0,-1).forEach(k=>{target=target[k]||(target[k]={});});
      target[path.at(-1)]=m.get();
    });
    if(!Object.keys(body).length)return;
    save.disabled=true;
    try{
      const{data}=await req('PATCH',configUrl(name,builtin),body);
      if(!(data&&data.error))toast(t(builtin?'cfgSavedBuiltin':'cfgSaved'));
      await fillConfigPanel(name,box,data&&data.error,builtin);
      return;
    }catch(e){toastErr(e);}
    paint();
  });
  disc.addEventListener('click',()=>{rows.forEach(m=>m.revert());paint();});
  defs.addEventListener('click',()=>{
    rows.forEach(m=>{if(m.def.dflt!==undefined)m.fill(m.def.dflt);});
    paint();
  });

  const nodes=[],sections=new Map();
  rows.forEach((m,i)=>{
    const group=typeof fields[i].group==='string'?fields[i].group:'';
    if(group){
      let section=sections.get(group);
      if(!section){
        const label=builtin&&{time:'cfgGroupTime',date:'cfgGroupDate',calendar:'cfgGroupCalendar',weekday:'cfgGroupWeekday'}[group];
        const content=el('div',{class:'cfgrows'});
        section=el('details',{class:'cfgsection','data-group':group},
          el('summary',null,icon('chev'),el('h3',{class:'cfggrp'},label?t(label):group)),content);
        section.open=!!groupOpen.get(group);
        sections.set(group,section);nodes.push(section);
      }
      section.lastChild.append(m.row);
    }else{
      nodes.push(m.row);
    }
  });
  box.replaceChildren(...warnings.map(warnBadge),...nodes,err,bar);
  paint();
  return true;
}
async function viewApps(view){
  //linux:begin
  loadOauthApps();
  //linux:end
  let loop=[],background=[],ondemand=[],disabled=[],modules=[],dragFrom=null,saving=Promise.resolve();
  const panels=new Map(),infos=new Map();
  let panelTaken=new Set();
  const names=()=>[...loop.map(a=>a.name),...background.map(a=>a.name)];
  const snapshot=()=>({loop:[...loop],background:[...background],disabled:[...disabled]});
  const restore=s=>{({loop,background,disabled}=s);};

  function group(id,title,help){
    const collapsible=id!=='loop',headingId='apps-'+id+'-heading',helpId='apps-'+id+'-help';
    const count=el('span',{class:'cnt'});
    const list=el('div',{class:'applist'});
    const head=el(collapsible?'summary':'header',{class:'appgrphead',
      'aria-labelledby':headingId,'aria-describedby':helpId},
      el('h2',{id:headingId},collapsible?icon('chev'):null,title,' ',count),
      el('div',{class:'ghelp',id:helpId},help));
    // Built once; a render replaces only the rows inside list.
    const card=el(collapsible?'details':'section',{class:'card wide appgrp',id:'apps-'+id,
      'aria-labelledby':headingId},head,list);
    return{card,list,count};
  }
  const G={
    loop:group('loop',t('apploop'),t('apploopH')),
    od:group('od',t('ondemandapps'),t('ondemandH')),
    bg:group('bg',t('background'),t('backgroundH')),
    off:group('off',t('disabledapps'),t('disabledH')),
    mod:group('mod',t('sharedcfg'),t('sharedcfgH')),
  };
  const offBanner=el('div',{class:'banner',hidden:'hidden'},'⏸ ',el('span',{class:'grow'},t('appsscroff')));

  // Every change is saved at once; the toast offers the way back.
  function persist(before,msg){
    render();
    const body={order:names(),disabled:disabled.map(a=>a.name)};
    saving=saving.then(async()=>{
      try{
        await req('PUT','/api/v1/apps/order',body);
        if(!msg)return;
        const d=toast(msg,true,before?[{label:t('undo'),fn:()=>{restore(before);persist(null,t('undone'));}}]:undefined);
        if(before)setTimeout(()=>d.remove(),6000);
      }catch(e){toastErr(e);await reload();}
    });
    return saving;
  }

  function cfgPanel(a){
    const key=a.origin+':'+a.name;
    //linux:begin
    if(a.origin!=='builtin'&&S.oauthApps&&S.oauthApps.has(a.name))a={...a,config:true};
    //linux:end
    if(!a.config||panelTaken.has(key))return[null,null];
    panelTaken.add(key);
    let box=panels.get(key);
    if(!box){box=el('div',{class:'appcfg',hidden:'hidden'});panels.set(key,box);}
    const toggle=async()=>{
      const open=box.hidden;
      box.hidden=!open;
      render();
      if(open&&!box.dataset.loaded&&await fillConfigPanel(a.name,box,null,a.origin==='builtin'))box.dataset.loaded='1';
    };
    const label=t(box.hidden?'cfgTitle':'cfgHide');
    const btn=el('button',{class:'icon cfgbtn'+(box.hidden?'':' pri'),title:label,'aria-label':label,
      'aria-expanded':String(!box.hidden),onclick:toggle},icon('settings'));
    return[btn,box];
  }

  // A state chip explains itself on a tap, which also works where there is no hover.
  function chipsOf(a,mode){
    const fit=metaFitWords(a.meta||{}),open=infos.get(a.name);
    const list=[a.error&&['Err','bad',errText(a.name,a.error)],fit&&['Fit','warn',t('fitNeeds').replace('{w}',fit)],
      a.present===false&&['Gone','warn',t('chipGoneH')],mode==='loop'&&a.skipped&&['Skip','',t('chipSkipH')],
      mode==='od'&&a.inLoop&&['Run','good',t('chipRunH')]].filter(Boolean);
    const cur=list.find(c=>c[0]===open);
    return[list.map(([k,cls])=>el('button',{class:'chip '+cls,'aria-expanded':String(k===open),
      onclick:()=>{infos.set(a.name,k===open?null:k);render();}},t('chip'+k))),
      cur&&el('div',{class:'rowinfo '+cur[1]},cur[2])];
  }

  const kindOf=a=>t('kind'+({module:'Module',script:'Script',pushed:'Pushed'}[a.origin]||
    (a.present===false?'Pushed':'Builtin')));
  // One undoable step: apply the change, save it, offer Undo.
  function change(fn,msg=t('orderSaved')){
    const before=snapshot();
    fn();
    persist(before,msg);
  }
  const move=(idx,to)=>change(()=>{const[m]=loop.splice(idx,1);loop.splice(to,0,m);});

  function appRow(a,idx,mode){
    const inLoop=mode==='loop',scr=a.origin==='script'||a.origin==='module';
    const m=a.meta||{};
    const row=el('div',{class:'approw'+(inLoop?' drag':'')+(mode==='off'?' off':'')});
    let grip=null;
    if(inLoop){
      grip=el('button',{class:'grip',title:t('dragord'),'aria-label':t('dragord')},icon('grip'));
      grip.addEventListener('pointerdown',e=>{
        e.preventDefault();
        try{grip.setPointerCapture(e.pointerId);}catch(_){}
        const before=snapshot(),was=names().join();
        dragFrom=idx;
        row.classList.add('dover');
        onDrag(ev=>{
          const at=document.elementFromPoint(ev.clientX,ev.clientY);
          const cell=at&&at.closest?at.closest('.approw'):null;
          const under=cell?[...G.loop.list.children].indexOf(cell):-1;
          if(under<0||dragFrom==null||under===dragFrom)return;
          const[x]=loop.splice(dragFrom,1);loop.splice(under,0,x);
          dragFrom=under;render();
          const moved=G.loop.list.children[under];
          if(moved)moved.classList.add('dover');
        },()=>{
          dragFrom=null;
          if(names().join()!==was)persist(before,t('orderSaved'));else render();
        });
      });
    }
    const title=m.name||a.name;
    const switchOff=()=>change(()=>{
      if(inLoop)loop.splice(idx,1);else background=background.filter(x=>x!==a);
      if(!loop.some(x=>x.name===a.name))disabled.push(a);
    },t('appOff').replace('{n}',title));
    const switchOn=()=>change(()=>{
      disabled=disabled.filter(x=>x!==a);
      if(a.headless)background=[...background,a].sort((x,y)=>x.name<y.name?-1:1);
      else loop.push(a);
    },t('appOn').replace('{n}',title));
    const show=async()=>{
      try{await req('PUT','/api/v1/apps/active',{name:a.name});toast(t(mode==='od'?'started':'shown'));}
      catch(e){toastErr(e);}
    };
    const remove=async()=>{
      try{await req('DELETE','/api/v1/apps/'+encodeURIComponent(a.name));}
      catch(e){toastErr(e);return;}
      loop=loop.filter(x=>x!==a);background=background.filter(x=>x!==a);disabled=disabled.filter(x=>x!==a);
      persist(null,t('deleted'));
    };
    const[cfgBtn,cfgBox]=cfgPanel(a);
    const[chips,info]=chipsOf(a,mode);
    const sw=mode==='loop'||mode==='bg'||mode==='off'
      ?mkSwitch(mode!=='off',mode==='off'?switchOn:switchOff):null;
    if(sw)sw.input.setAttribute('aria-label',t('appRuns').replace('{n}',title));
    const menu=rowMenu([
      inLoop&&idx>0?[t('moveUp'),()=>move(idx,idx-1)]:null,
      inLoop&&idx<loop.length-1?[t('moveDown'),()=>move(idx,idx+1)]:null,
      inLoop?[t('dupapp'),()=>change(()=>loop.splice(idx+1,0,a))]:null,
      scr?[t('edit'),()=>editScript(a)]:null,
      (m.icons||[]).length?[t('scricons'),()=>grabIcons(a)]:null,
      (a.origin==='pushed'||a.present===false)?[t('del'),remove,true]:null,
    ],'more');
    const sub=[kindOf(a),m.desc].filter(Boolean).join(' · ');
    const action=inLoop||mode==='od'?el('button',{
      class:'appaction'+(mode==='od'?' pri':''),onclick:show},
      mode==='od'?'▶ '+t('start'):t('switchto')):null;
    const acts=el('div',{class:'racts'},cfgBtn,sw&&sw.node,menu,action);
    [grip?el('span',{class:'lead'},grip,el('span',{class:'pos'},String(idx+1))):null,
     el('div',{class:'rowmain'},
       el('div',{class:'nm'},el('span',{class:'nmt'},title),...chips),
       el('div',{class:'sub'},sub)),
     acts,info,cfgBox].forEach(n=>{if(n)row.append(n);});
    return row;
  }

  async function grabIcons(a){
    const ids=(a.meta&&a.meta.icons)||[];
    if(!ids.length)return;
    try{await installScriptIcons(ids,await installedIconIds());}
    catch(e){toastErr(e);}
  }

  function render(){
    panelTaken=new Set();
    const fill=(g,items,mode,empty)=>{
      g.list.replaceChildren(...items.map((a,i)=>appRow(a,i,mode)));
      if(!items.length&&empty)g.list.replaceChildren(el('div',{class:'mono'},empty));
      g.count.replaceChildren(String(items.length));
    };
    fill(G.loop,loop,'loop',t('noapps'));
    fill(G.od,ondemand,'od');
    fill(G.bg,background,'bg');
    fill(G.off,disabled,'off');
    const shownMods=modules.filter(a=>a.config||a.error||metaFitWords(a.meta));
    fill(G.mod,shownMods,'mod');
    G.od.card.hidden=!ondemand.length;
    G.bg.card.hidden=!background.length;
    G.off.card.hidden=!disabled.length;
    G.mod.card.hidden=!shownMods.length;
  }

  async function reload(){
    const{data}=await api('/api/v1/apps');
    let all=Array.isArray(data)?data:[];
    const scrOff=await scriptsOff();
    if(scrOff)all=all.filter(a=>a.origin!=='script'&&a.origin!=='module');
    offBanner.hidden=!scrOff;
    const byName=(x,y)=>x.name<y.name?-1:1;
    modules=all.filter(a=>a.origin==='module').sort(byName);
    all=all.filter(a=>a.origin!=='module');
    ondemand=all.filter(a=>a.ondemand).sort(byName);
    all=all.filter(a=>!a.ondemand);
    loop=all.filter(a=>a.inLoop||(a.enabled&&!a.headless));
    background=all.filter(a=>!a.inLoop&&a.enabled&&a.headless).sort(byName);
    disabled=all.filter(a=>!a.enabled);
    render();
  }

  view.classList.add('apps-page');
  view.append(offBanner,
    G.loop.card,G.od.card,G.bg.card,G.off.card,G.mod.card);
  try{await reload();}
  catch(e){view.replaceChildren(el('div',{class:'card wide'},t('neterr')));}
}
