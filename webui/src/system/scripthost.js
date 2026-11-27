const shScripts=async at=>(await api('/api/v1/apps',{host:at})).data
  .filter(a=>a.origin==='script'||a.origin==='module')
  .sort((a,b)=>(b.origin==='module')-(a.origin==='module'));
async function shTake(from,step){
  const out=[];
  for(const{name:n}of await shScripts(from)){
    step(t('shMove').replace('{s}',n));
    out.push({n,src:(await api('/api/v1/apps/script/'+encodeURIComponent(n),{host:from})).text,st:await takeStore(n,from)});
  }
  return out;
}
async function shGive(s,to,step){
  step(t('shMove').replace('{s}',s.n));
  await putScript(s.n,s.src,to);
  try{await putStore(s.n,to,s.st);}
  catch(e){await delScript(s.n,false,to).catch(()=>{});throw e;}
}
const shBase=a=>(/^https?:\/\//.test(a)?a:'http://'+(a.includes(':')?a:a+':8300')).replace(/\/$/,'');
const shForget=(base,uid,key,keep)=>api(base+'/api/v1/pair/'+uid+'?keep='+keep,
  {method:'DELETE',headers:{Authorization:'Bearer '+key}});
async function shPair(base,code,list,step,done=()=>{}){
  const dev=(await api('/api/v1/device')).data,g=displayGeometry();
  const ip=/^\d+\.\d+\.\d+\.\d+$/.test(dev.ipAddress||'')&&dev.ipAddress!=='0.0.0.0';
  const clock=ip?'http://'+dev.ipAddress+(location.port?':'+location.port:''):location.origin;
  const r=(await api(base+'/api/v1/pair',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({code,uid:dev.uid,name:S.sysHost||'',width:g.width,height:g.height,board:dev.boardType||'',
      clock})})).data;
  const to={url:r.url,key:r.key};
  try{
    for(const s of list)await shGive(s,to,step);
    await req('PUT','/api/v1/system',{scriptHost:r.scriptHost,scriptHostKey:r.key});
  }catch(e){
    await shForget(base,dev.uid,r.key,1).catch(()=>{});
    throw e;
  }
  await done(dev.uid);
  await post('/api/v1/device/reboot');
}
async function shMoveTo(base,code,take,step){
  await shReady();
  const from={...SH};
  let list=[];
  if(take)try{list=await shTake(from,step);}catch(e){
    step('');
    if(!await askToast(t('shGone'),[[t('shWithout'),1],[t('cancel'),0,1]]))return false;
    take=0;
  }
  await shPair(base,code,list,step,uid=>shForget(shBase(from.host.split(':')[0]),uid,from.key,take?0:1).catch(()=>{}));
}
async function shUnpair(back,step){
  await shReady();
  if(!SH.url)throw new Error(t('neterr'));
  const from={...SH},dev=(await api('/api/v1/device')).data,list=back?await shTake(from,step):[],left=[];
  await req('PUT','/api/v1/system',{scriptHost:''});
  await post('/api/v1/device/reboot');
  step(t('shWait'));
  await waitForDevice(4000);
  for(const s of list)try{await shGive(s,CLOCK,step);}catch(e){left.push(s.n+' ('+e.message+')');}
  await shForget(shBase(from.host.split(':')[0]),dev.uid,from.key,left.length||!back?1:0).catch(e=>left.push(e.message));
  return left;
}
function scriptHostSection(page){
  const h=S.sh,remote=h.mode==='remote',here=remote?h.host.split(':')[0]:'',offers=(h.offers||[]).filter(o=>o.address!==here),o=offers[0];
  let lost=0;
  const code=el('input',{type:'text',inputmode:'numeric',maxlength:'6',autocomplete:'off','aria-label':t('shCode')});
  const addr=el('input',{type:'text',autocomplete:'off',spellcheck:'false','aria-label':t('shAddr'),
    value:o?o.address+':'+o.port:''});
  const busy=el('div',{class:'mono'}),step=s=>busy.replaceChildren(s);
  const take=mkSwitch(true),takeRow=actionRow(t('shTake'),t('shTakeH'),[take.node]);
  const pair=el('button',{class:'pri'},t(remote?'shMoveTo':'shPair'));
  pair.addEventListener('click',async()=>{
    pair.disabled=true;
    try{
      const base=shBase(addr.value.trim()),c=code.value.trim();
      if(!remote){
        const list=await shTake(CLOCK,step);
        await shPair(base,c,list,step,async()=>{for(let i=list.length;i--;)await delScript(list[i].n,false,CLOCK).catch(()=>{});});
      }else if(lost)await shPair(base,c,[],step);
      else if(await shMoveTo(base,c,take.input.checked,step)===false){pair.disabled=false;return;}
      toast(t('shPaired'));step(t('shWait'));
      await waitForDevice(4000);location.reload();
    }catch(e){step('');toastErr(e);pair.disabled=false;}
  });
  const pairRows=[...offers.map(o=>{
    const use=el('button',null,o.name);
    use.addEventListener('click',()=>{addr.value=o.address+':'+o.port;code.focus();});
    return actionRow(t('shFound'),o.address+':'+o.port,[use]);
  }),actionRow(t('shAddr'),t('shAddrH'),[addr]),actionRow(t('shCode'),t('shCodeH'),[code,pair])];
  const rows=[];
  if(remote){
    const st=el('div',{class:'badge'+(h.link==='up'?' good':' bad')},
      t({up:'shUp',resolving:'shResolving',notFound:'shNotFound'}[h.link]||'shDown'));
    const again=el('div');
    rows.push(actionRow(t('shState'),h.host,[st]),again,...pairRows,takeRow);
    api('/api/v1/version',{host:1,timeout:4000}).catch(e=>{
      if(e.code!=='shAuth')return;
      lost=1;again.replaceWith(el('div',{class:'badge bad'},e.message));
      addr.value=here+':8300';pair.textContent=t('shRepair');takeRow.remove();
    });
    const unpair=armable(el('button',{class:'danger'},t('shUnpair')),async()=>{
      const back=await askToast(t('shBackAsk'),[[t('shBack'),1,1],[t('shLeave'),0]]);
      if(back===null)return;
      unpair.disabled=true;
      try{
        const left=await shUnpair(!!back,step);
        if(left.length)toast(t('shLeft').replace('{s}',left.join(', ')),false,[{label:t('close'),pri:1,fn:()=>location.reload()}]);
        else{toast(t(back?'shBackDone':'rebooting'));setTimeout(()=>location.reload(),3000);}
      }catch(e){step('');toastErr(e);unpair.disabled=false;}
    });
    rows.push(actionRow(t('shUnpair'),null,[unpair]));
  }else rows.push(...pairRows);
  rows.push(busy);
  page.section('scripthost',t('shSection'),t('shSectionH'),rows);
}
