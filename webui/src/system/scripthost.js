const shScripts=async at=>(await api('/api/v1/apps',{host:at})).data
  .filter(a=>a.origin==='script'||a.origin==='module')
  .sort((a,b)=>(b.origin==='module')-(a.origin==='module'));
async function shCopy(n,from,to,step){
  step(t('shMove').replace('{s}',n));
  await putScript(n,(await api('/api/v1/apps/script/'+encodeURIComponent(n),{host:from})).text,to);
  try{await carryStore(n,n,from,to);}
  catch(e){await delScript(n,false,to).catch(()=>{});throw e;}
}
const shBase=a=>(/^https?:\/\//.test(a)?a:'http://'+(a.includes(':')?a:a+':8300')).replace(/\/$/,'');
const shForget=(base,uid,key,keep)=>api(base+'/api/v1/pair/'+uid+'?keep='+keep,
  {method:'DELETE',headers:{Authorization:'Bearer '+key}});
async function shPair(base,code,move,step){
  const dev=(await api('/api/v1/device')).data,g=displayGeometry(),list=move?await shScripts(CLOCK):[];
  const ip=/^\d+\.\d+\.\d+\.\d+$/.test(dev.ipAddress||'')&&dev.ipAddress!=='0.0.0.0';
  const clock=ip?'http://'+dev.ipAddress+(location.port?':'+location.port:''):location.origin;
  const r=(await api(base+'/api/v1/pair',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({code,uid:dev.uid,name:S.sysHost||'',width:g.width,height:g.height,board:dev.boardType||'',
      clock})})).data;
  const to={url:r.url,key:r.key};
  try{
    for(const a of list)await shCopy(a.name,CLOCK,to,step);
    await req('PUT','/api/v1/system',{scriptHost:r.scriptHost,scriptHostKey:r.key});
  }catch(e){
    await shForget(base,dev.uid,r.key,1).catch(()=>{});
    throw e;
  }
  for(let i=list.length;i--;)await delScript(list[i].name,false,CLOCK).catch(()=>{});
  await post('/api/v1/device/reboot');
}
async function shUnpair(back,step){
  await shReady();
  if(!SH.url)throw new Error(t('neterr'));
  const from={...SH},dev=(await api('/api/v1/device')).data,list=back?await shScripts(from):[],left=[];
  await req('PUT','/api/v1/system',{scriptHost:''});
  await post('/api/v1/device/reboot');
  step(t('shWait'));
  await waitForDevice(4000);
  for(const a of list)try{await shCopy(a.name,from,CLOCK,step);}catch(e){left.push(a.name+' ('+e.message+')');}
  await shForget(shBase(from.host.split(':')[0]),dev.uid,from.key,left.length||!back?1:0).catch(e=>left.push(e.message));
  return left;
}
function scriptHostSection(page){
  const h=S.sh,remote=h.mode==='remote',o=h.offers?.[0];
  const code=el('input',{type:'text',inputmode:'numeric',maxlength:'6',autocomplete:'off','aria-label':t('shCode')});
  const addr=el('input',{type:'text',autocomplete:'off',spellcheck:'false','aria-label':t('shAddr'),
    value:remote?h.host.split(':')[0]+':8300':o?o.address+':'+o.port:''});
  const busy=el('div',{class:'mono'}),step=s=>busy.replaceChildren(s);
  const pair=el('button',{class:'pri'},t(remote?'shRepair':'shPair'));
  pair.addEventListener('click',async()=>{
    pair.disabled=true;
    try{
      await shPair(shBase(addr.value.trim()),code.value.trim(),!remote,step);
      toast(t('shPaired'));step(t('shWait'));
      await waitForDevice(4000);location.reload();
    }catch(e){step('');toastErr(e);pair.disabled=false;}
  });
  const pairRows=[...(h.offers||[]).map(o=>{
    const use=el('button',null,o.name);
    use.addEventListener('click',()=>{addr.value=o.address+':'+o.port;code.focus();});
    return actionRow(t('shFound'),o.address+':'+o.port,[use]);
  }),actionRow(t('shAddr'),t('shAddrH'),[addr]),actionRow(t('shCode'),t('shCodeH'),[code,pair])];
  const rows=[];
  if(remote){
    const st=el('div',{class:'badge'+(h.link==='up'?' good':' bad')},
      t({up:'shUp',resolving:'shResolving',notFound:'shNotFound'}[h.link]||'shDown'));
    const again=el('div');
    rows.push(actionRow(t('shState'),h.host,[st]),again);
    api('/api/v1/version',{host:1,timeout:4000})
      .catch(e=>{if(e.code==='shAuth')again.replaceWith(el('div',{class:'badge bad'},e.message),...pairRows);});
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
