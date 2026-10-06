function mkSwitch(checked,onchange){
  const i=el('input',{type:'checkbox',checked});
  if(onchange)i.addEventListener('change',onchange);
  return{node:el('label',{class:'switch'},i,el('i')),input:i};
}
const WEEKDAYS=[['sunday','Sun'],['monday','Mon'],['tuesday','Tue'],['wednesday','Wed'],
  ['thursday','Thu'],['friday','Fri'],['saturday','Sat']];
const WIDGETS={
  toggle(ctl,def,value,onInput){
    const s=mkSwitch(!!value,onInput);
    ctl.append(s.node);
    return{input:s.input,get:()=>s.input.checked,set:v=>s.input.checked=!!v};
  },
  slider(ctl,def,value,onInput){
    const input=el('input',{type:'range',min:def.min??0,max:def.max??255,step:def.step??1,value:Number(value)||0});
    const val=el('span',{class:'val'},String(value)+(def.unit||''));
    const show=v=>val.replaceChildren(String(v)+(def.unit||''));
    input.addEventListener('input',()=>{show(input.value);onInput&&onInput();});
    ctl.append(input,val);
    return{input,get:()=>Number(input.value),set:v=>{input.value=v;show(v);}};
  },
  color(ctl,def,value,onInput){
    const pick=colorPick(value,onInput);
    const getColor=()=>def.numericColor?parseInt(pick.get().slice(1),16):pick.get();
    if(!def.nullable){
      ctl.append(pick.node);
      return{input:pick.input,get:getColor,set:pick.set};
    }
    const inherit=mkSwitch(value==null,()=>{paint();onInput&&onInput();});
    inherit.input.setAttribute('aria-label',t('cfgInherit'));
    const paint=()=>{
      pick.input.disabled=inherit.input.checked;
      pick.node.classList.toggle('inherited',inherit.input.checked);
      pick.node.setAttribute('aria-hidden',String(inherit.input.checked));
    };
    const set=v=>{inherit.input.checked=v==null;if(v!=null)pick.set(v);paint();};
    ctl.append(el('div',{class:'inheritcolor'},
      el('span',{class:'inherit'},inherit.node,el('span',null,t('cfgInherit'))),pick.node));
    paint();
    return{input:pick.input,get:()=>inherit.input.checked?null:getColor(),set};
  },
  select(ctl,def,value,onInput){
    const opts=(def.opt==='transitions'?S.transitions.map(n=>[n,n]):def.opt).slice();
    if(value!=null&&!opts.some(o=>String(o[0])===String(value)))
      opts.unshift([value,(def.fmt?def.fmt(value):String(value))+' - '+t('optStored')]);
    const input=el('select',null,opts.map(([v,l])=>el('option',{value:String(v),selected:String(v)===String(value)},l)));
    input.addEventListener('change',onInput);
    ctl.append(input);
    const numeric=opts.length>0&&typeof opts[0][0]==='number';
    return{input,get:()=>numeric?Number(input.value):input.value,set:v=>input.value=String(v)};
  },
  number(ctl,def,value,onInput){
    const input=el('input',{type:'number',min:def.min,max:def.max,step:def.step??1,value});
    input.addEventListener('input',onInput);
    ctl.append(def.unit
      ?el('span',{class:'unitwrap',style:'--ul:'+def.unit.length+'ch'},input,el('span',{class:'unit'},def.unit))
      :input);
    return{input,get:()=>Number(input.value),set:v=>input.value=v};
  },
  days(ctl,def,value,onInput){
    const boxes=WEEKDAYS.map(([id,lbl])=>{
      const i=el('input',{type:'checkbox',checked:Array.isArray(value)&&value.includes(id)});
      if(onInput)i.addEventListener('change',onInput);
      return{id,input:i,node:el('label',{class:'daybox'},i,el('span',null,lbl))};
    });
    ctl.append(el('div',{class:'days'},...boxes.map(b=>b.node)));
    return{input:boxes[0].input,
      get:()=>boxes.filter(b=>b.input.checked).map(b=>b.id),
      set:v=>boxes.forEach(b=>b.input.checked=Array.isArray(v)&&v.includes(b.id))};
  },
  secret(ctl,def,value,onInput){
    const input=el('input',{type:'password',autocomplete:'new-password',
      placeholder:'(unchanged)'});
    input.addEventListener('input',onInput);
    const eye=el('button',{type:'button',class:'wificaret','aria-label':'Show password','aria-pressed':'false'},icon('eye'));
    const again=def.confirm?el('input',{type:'password',autocomplete:'new-password',
      placeholder:'Repeat password','aria-label':'Repeat password'}):null;
    const match=()=>!again||again.value===input.value;
    const mark=()=>{if(!again)return;again.style.display=input.value?'':'none';again.classList.toggle('pwbad',!!again.value&&!match());};
    const show=on=>{input.type=on?'text':'password';if(again)again.type=input.type;eye.classList.toggle('on',on);eye.setAttribute('aria-pressed',String(on));};
    eye.addEventListener('click',()=>show(input.type==='password'));
    const wrap=el('div',{class:'wifiwrap'},input,eye);
    if(again){input.addEventListener('input',mark);again.addEventListener('input',mark);ctl.append(el('div',{class:'pwpair'},wrap,again));mark();}
    else ctl.append(wrap);
    return{input,blank:true,check:match,get:()=>input.value,set:()=>{input.value='';if(again)again.value='';show(false);mark();}};
  },
  wifi(ctl,def,value,onInput){
    const wrap=el('div',{class:'wifiwrap'});
    const input=el('input',{type:'text',value:value??'',autocomplete:'off',
      role:'combobox','aria-autocomplete':'list','aria-expanded':'false'});
    input.addEventListener('input',onInput);
    const caret=el('button',{type:'button',class:'wificaret',tabindex:'-1',
      'aria-label':t('networks')},'▾');
    const menu=el('div',{class:'wifimenu',role:'listbox'});
    menu.hidden=true;
    wrap.append(input,caret,menu);
    let nets=[];
    const onOutside=e=>{if(!wrap.contains(e.target))close();};
    function close(){
      if(menu.hidden)return;
      menu.hidden=true;input.setAttribute('aria-expanded','false');
      document.removeEventListener('pointerdown',onOutside);
    }
    function open(){
      if(!nets.length)return;
      menu.replaceChildren(...nets.map(n=>{
        const row=el('div',{class:'wifiopt',role:'option'},
          el('span',null,n.ssid),
          el('span',{class:'g'},n.rssi+' dBm'+(n.enc?' 🔒':'')));
        row.addEventListener('click',()=>{input.value=n.ssid;onInput&&onInput();close();});
        return row;
      }));
      input.blur();
      menu.hidden=false;input.setAttribute('aria-expanded','true');
      document.addEventListener('pointerdown',onOutside);
    }
    caret.addEventListener('pointerdown',e=>{e.preventDefault();menu.hidden?open():close();});
    input.addEventListener('keydown',e=>{if(e.key==='Escape')close();});
    const btn=el('button',{type:'button'},t('scan'));
    btn.addEventListener('click',async()=>{
      btn.disabled=true;btn.replaceChildren(t('scanning'));
      try{
        let r=await api('/api/v1/system/wifi-scan');
        for(let i=0;i<20&&r.status===202;i++){await new Promise(s=>setTimeout(s,1000));r=await api('/api/v1/system/wifi-scan');}
        if(Array.isArray(r.data)){
          const seen=new Set();
          nets=r.data.filter(n=>n.ssid&&!seen.has(n.ssid)&&seen.add(n.ssid)).sort((a,b)=>b.rssi-a.rssi);
          toast(nets.length+' '+t('networks'));
          open();
        }
      }catch(e){toastErr(e);}
      btn.disabled=false;btn.replaceChildren(t('scan'));
    });
    ctl.append(wrap,btn);
    return{input,get:()=>input.value,set:v=>input.value=v??''};
  },
  tz(ctl,def,value,onInput){
    const sel=el('select',null);
    const prev=el('div',{class:'tzprev'});
    const detect=el('button',{type:'button',title:t('tzDetectH')},t('tzDetect'));
    let cur=(TZRULE.has(value)&&tzFmtFor(value))?value:'UTC';
    const now=new Date();
    const label=n=>(n.includes('/')?n.split('/').slice(1).join('/'):n).replace(/_/g,' ')
      +' · '+tzOffsetStr(tzOffset(n,now));
    const areas=new Map();
    (tzNames||=[...TZRULE.keys()].filter(n=>tzFmtFor(n)).sort()).forEach(n=>{const a=n.includes('/')?n.split('/')[0]:'-';
      if(!areas.has(a))areas.set(a,[]);areas.get(a).push(n);});
    sel.replaceChildren(...[...areas].map(([a,ns])=>
      el('optgroup',{label:a},ns.map(n=>el('option',{value:n},label(n))))));
    sel.value=cur;
    function preview(){
      const s=tzState(cur),loc='en-GB';
      const at=new Intl.DateTimeFormat(loc,{timeZone:cur,dateStyle:'medium',timeStyle:'short'}).format(new Date());
      const dst=!s.changes?t('tzNoDst'):s.dst?t('tzDstOn'):t('tzDstOff');
      const nxt=s.changes&&s.next?' · '+t('tzNext')+' '+new Intl.DateTimeFormat(loc,{dateStyle:'medium'}).format(s.next):'';
      prev.replaceChildren(at+' · '+tzOffsetStr(s.off)+' · '+dst+nxt);
    }
    sel.addEventListener('input',()=>{cur=sel.value||cur;preview();if(onInput)onInput();});
    detect.addEventListener('click',()=>{
      const guess=Intl.DateTimeFormat().resolvedOptions().timeZone;
      if(!TZRULE.has(guess)){toast(t('tzUnknown')+' '+guess,false);return;}
      sel.value=guess;sel.dispatchEvent(new Event('input',{bubbles:true}));
    });
    preview();
    ctl.append(el('div',{class:'tzctl'},el('div',{class:'tzrow'},detect,sel),prev));
    return{input:sel,get:()=>cur,
      set:v=>{cur=TZRULE.has(v)?v:'UTC';sel.value=cur;preview();}};
  },
  raw(ctl,def,value,onInput){
    const input=el('input',{type:'text',value:typeof value==='string'?value:JSON.stringify(value)});
    input.addEventListener('input',onInput);
    ctl.append(input);
    return{input,
      get:()=>{try{return JSON.parse(input.value);}catch(e){return input.value;}},
      set:v=>input.value=typeof v==='string'?v:JSON.stringify(v)};
  },
  text(ctl,def,value,onInput,key){
    const input=el('input',{type:'text',value:value??''});
    if(def.list){
      const id='dl_'+key;input.setAttribute('list',id);
      if(!document.getElementById(id))document.body.append(el('datalist',{id},def.list.map(v=>el('option',{value:v}))));
    }
    input.addEventListener('input',onInput);
    ctl.append(input);
    return{input,get:()=>input.value,set:v=>input.value=v??''};
  },
};
let configFieldSeq=0;
function mkField(key,def,value,onInput){
  const row=actionRow(def.l,def.h,[],key);
  const{input,get,set:setv,blank,check}=(WIDGETS[def.w]||WIDGETS.text)(row.lastChild,def,value,onInput,key);
  if(def.linkLabel){
    const id='cfg-field-'+(++configFieldSeq),title=row.querySelector('.lab b'),help=row.querySelector('.help');
    title.id=id+'-label';
    if(help)help.id=id+'-help';
    if(def.w==='days'){
      const group=row.querySelector('.days');
      group.setAttribute('role','group');group.setAttribute('aria-labelledby',title.id);
      if(help)group.setAttribute('aria-describedby',help.id);
    }else{
      input.id=id;
      const label=el('label',{id:title.id,for:id},title.textContent);
      title.replaceWith(label);
      if(help)input.setAttribute('aria-describedby',help.id);
      const inherit=row.querySelector('.inherit .switch input'),inheritText=row.querySelector('.inherit > span');
      if(inherit&&inheritText){
        inherit.id=id+'-inherit';
        const inheritLabel=el('label',{id:id+'-inherit-label',for:inherit.id},inheritText.textContent);
        inheritText.replaceWith(inheritLabel);
        inherit.removeAttribute('aria-label');
        inherit.setAttribute('aria-labelledby',label.id+' '+inheritLabel.id);
        if(help)inherit.setAttribute('aria-describedby',help.id);
      }
    }
  }
  let initVal=blank?'':get();
  let init=JSON.stringify(initVal);
  const rebase=()=>{if(blank)setv();else{initVal=get();init=JSON.stringify(initVal);}};
  return{row,get,def,key,rebase,ok:()=>!check||check(),
    dirty(){return blank?input.value.length>0:JSON.stringify(get())!==init;},
    revert(){setv(initVal);},
    fill(v){setv(v);},
    set(v){setv(v);if(!blank){initVal=get();init=JSON.stringify(initVal);}}};
}
function saveBar(){
  const count=el('span',{class:'n'},'0');
  const saveBtn=el('button',{class:'pri'},t('save'));
  const discBtn=el('button',null,t('discard'));
  const bar=el('div',{id:'savebar',style:'display:none'},
    count,el('span',null,t('unsaved')),el('span',{class:'grow'}),discBtn,saveBtn);
  const show=n=>{count.replaceChildren(String(n));bar.style.display=n?'':'none';};
  return{bar,saveBtn,discBtn,show};
}
function sectionShell(view){
  const subnav=el('div',{class:'subnav'});
  new ResizeObserver(()=>document.documentElement.style.setProperty('--subnav-h',subnav.offsetHeight+'px')).observe(subnav);
  const panel=el('div',{class:'panel'});
  const spy=('IntersectionObserver'in window)?new IntersectionObserver(es=>{
    es.forEach(e=>{
      if(!e.isIntersecting)return;
      subnav.querySelectorAll('a').forEach(a=>a.classList.toggle('on',a.dataset.sec===e.target.id));
    });
  },{rootMargin:'-15% 0px -70% 0px'}):null;
  function section(id,title,help,nodes,pad){
    const sec=el('section',{class:'section',id:'sec-'+id},
      el('header',null,el('h2',null,title),help?el('div',{class:'ghelp'},help):null),
      el('div',{class:pad?'rows pad':'rows'},nodes));
    panel.append(sec);
    const a=el('a',{'data-sec':'sec-'+id},title);
    a.addEventListener('click',()=>sec.scrollIntoView({behavior:'smooth',block:'start'}));
    subnav.append(a);
    if(spy)spy.observe(sec);
    return sec;
  }
  view.append(el('div',{class:'settings'},subnav,panel));
  return{section,panel};
}
function settingsPage(view,saveFn,afterSave){
  const{section,panel}=sectionShell(view);
  const allFields=[];
  const onUpdate=[];
  const{bar,saveBtn,discBtn,show}=saveBar();
  function update(){
    show(allFields.filter(f=>f.dirty()).length);
    allFields.forEach(f=>{
      if(!f.def.showIf)return;
      const [depKey,eq]=String(f.def.showIf).split('=');
      const dep=allFields.find(x=>x.key===depKey);
      f.row.style.display=dep&&(eq===undefined?dep.get():String(dep.get())===eq)?'':'none';
    });
    onUpdate.forEach(fn=>fn());
  }
  saveBtn.addEventListener('click',async()=>{
    const bad=allFields.find(f=>f.dirty()&&!f.ok());
    if(bad){toast('Passwords do not match.',false);bad.row.scrollIntoView({block:'center'});return;}
    const payload={};
    allFields.filter(f=>f.dirty()).forEach(f=>{
      payload[f.key]=f.get();
      if(f.def.derive)Object.assign(payload,f.def.derive(f.get()));
    });
    saveBtn.disabled=true;
    try{
      await saveFn(payload);
      allFields.forEach(f=>f.rebase());
      toast(t('saved'));
      await afterSave(payload);
    }catch(e){toastErr(e);}
    saveBtn.disabled=false;update();
  });
  discBtn.addEventListener('click',()=>{allFields.filter(f=>f.dirty()).forEach(f=>f.revert());update();});
  function addFields(fields){
    fields.forEach(f=>{
      allFields.push(f);
      f.row.querySelectorAll('input,select').forEach(i=>i.addEventListener('input',update));
    });
    update();
  }
  view.append(bar);
  return{section,addFields,panel,update,onUpdate};
}
function renderSettingsInto(page,data,meta,groups){
  const byKey={};
  for(const[gid,gt,gh,lead]of groups){
    const keys=Object.keys(meta).filter(k=>meta[k].g===gid&&(k in data||meta[k].w==='secret'));
    if(!keys.length)continue;
    const fields=keys.map(k=>mkField(k,meta[k],meta[k].load?meta[k].load(data[k],data):data[k],null));
    fields.forEach(f=>byKey[f.key]=f);
    page.addFields(fields);
    page.section(gid,t(gt),t(gh),[...(lead||[]),...fields.map(f=>f.row)]);
  }
  return byKey;
}
function advancedSection(page,data,meta,alsoKnown){
  const known=new Set([...Object.keys(meta),...(alsoKnown||[])]);
  const extra=Object.keys(data).filter(k=>!known.has(k));
  if(!extra.length)return;
  const fields=extra.map(k=>mkField(k,F('adv','raw',k,''),data[k],null));
  page.addFields(fields);
  page.section('adv',t('advanced'),t('advhelp'),fields.map(f=>f.row));
}
