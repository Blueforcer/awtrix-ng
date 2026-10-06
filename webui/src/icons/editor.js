// index.html is spelled out on purpose: whether the Hub's web server resolves a
// bare /piskel/ to the directory index is not something this repo can promise,
// and a miss would frame a 404 instead of the editor.
const PISKEL_URL_DEFAULT='https://awtrix.de/piskel/index.html';
function hubToken(){try{return localStorage.awtrixHubToken||'';}catch(e){return'';}}
function hubPage(p){try{return new URL(p,iconApiUrl()).href;}catch(e){return iconApiUrl();}}
function getPiskelUrl(){try{return localStorage.awtrixPiskelUrl||PISKEL_URL_DEFAULT;}catch(e){return PISKEL_URL_DEFAULT;}}
function piskelOrigin(){try{return new URL(getPiskelUrl()).origin;}catch(e){return'';}}
function piskelReply(){
  const target=$('#piskelFrame')?.contentWindow,origin=piskelOrigin();
  return message=>target?.postMessage({ns:'awtrix',...message},origin);
}
function postToPiskel(msg){
  const f=$('#piskelFrame'),o=piskelOrigin();
  if(f&&f.contentWindow&&o)f.contentWindow.postMessage(Object.assign({ns:'awtrix'},msg),o);
}
function notifyPiskelTheme(){postToPiskel({type:'theme',theme:document.documentElement.dataset.theme==='light'?'light':'dark'});}
let pendingEditIcon=null;
let piskelReady=false;
let piskelIconLoad=null;
function resetPiskelIconLoad(){
  if(piskelIconLoad)clearTimeout(piskelIconLoad.timer);
  piskelIconLoad=null;
}

function b64ToBlob(b64,mime){
  const bin=atob(b64),len=bin.length,arr=new Uint8Array(len);
  for(let i=0;i<len;i++)arr[i]=bin.charCodeAt(i);
  return new Blob([arr],{type:mime||'application/octet-stream'});
}
function blobToB64(blob){return new Promise((res,rej)=>{
  const fr=new FileReader();
  fr.onload=()=>res(String(fr.result).split(',')[1]||'');
  fr.onerror=()=>rej(new Error(t('neterr')));
  fr.readAsDataURL(blob);
});}
function iconIdFrom(name){return(String(name||'').replace(/\.[^.]+$/,'').replace(/[^A-Za-z0-9_-]/g,'_').slice(0,32))||'icon';}
async function piskelSendList(){
  try{const{data}=await api('/api/v1/files?dir='+encodeURIComponent('/ICONS'),{cache:'no-store'});
    postToPiskel({type:'list-result',files:data.files||[],usedBytes:data.usedBytes,totalBytes:data.totalBytes});
  }catch(e){postToPiskel({type:'list-result',files:[],error:e.message});}
}
async function piskelSendIcon(name){
  if(piskelIconLoad)return;
  const target=$('#piskelFrame')?.contentWindow;
  if(!target)return;
  piskelIconLoad={target};
  try{
    const r=await fetch('/ICONS/'+encodeURIComponent(name),{cache:'no-store'});
    if(!r.ok)throw new Error('HTTP '+r.status);
    const blob=await r.blob();
    let origin=null;
    try{const{data}=await api('/api/v1/icons/origins');origin=(data.icons||[]).find(o=>o.name===name&&validIconOrigin(o))||null;}catch(e){}
    if(target!==$('#piskelFrame')?.contentWindow)return;
    const requestId='icon-'+Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);
    const timer=setTimeout(()=>{
      if(piskelIconLoad?.requestId!==requestId)return;
      resetPiskelIconLoad();
      if(target===$('#piskelFrame')?.contentWindow)toast(t('edIconOpenError'),false);
    },15000);
    piskelIconLoad={requestId,timer,target};
    postToPiskel({type:'load-result',requestId,name,mime:blob.type||'image/gif',dataBase64:await blobToB64(blob),origin,
      based_on:origin&&origin.hub===iconApiUrl()?origin.slug:null});
  }catch(e){resetPiskelIconLoad();toastErr(e);}
}
async function piskelSave(m){
  const reply=piskelReply();
  const id=iconIdFrom(m.name),fn=id+'.gif';
  let blob=b64ToBlob(m.dataBase64,m.mime||'image/gif');
  try{
    if(m.mime==='image/jpeg')blob=await imgToGif(blob);
    await uploadFile(blob,'/ICONS',fn,null);
    await dropStale('/ICONS',id+'.jpg',fn);
    if(m.origin&&validIconOrigin({...m.origin,name:fn})){
      try{await saveIconOrigin(fn,m.origin.slug,m.origin.sha256,m.origin.hub);}catch(e){toast(t('icoriginfail'),false);}
    }
    reply({type:'save-result',ok:true,name:fn});
    toast(fn+' '+t('uploaded'));
  }catch(e){reply({type:'save-result',ok:false,name:fn,error:e.message});toastErr(e);}
}
const LIVE_NAME='draw-preview';
let piskelPublishing=false;
async function piskelPublish(m){
  const reply=piskelReply();
  if(piskelPublishing){reply({type:'publish-result',requestId:m.requestId,ok:false,error:'busy',message:t('hubPublishing')});return;}
  piskelPublishing=true;
  try{
    if(!['image/gif','image/jpeg'].includes(m.mime)||typeof m.dataBase64!=='string'||m.dataBase64.length>90000)throw new Error(t('idbfail'));
    const blob=b64ToBlob(m.dataBase64,m.mime);
    let update=null;
    if(m.action==='update'){
      if(!/^[-a-z0-9_]{1,32}$/.test(m.slug||'')||!/^[a-f0-9]{64}$/.test(m.expected_sha256||''))throw new Error(t('idbfail'));
      update={slug:m.slug,sha256:m.expected_sha256};
    }
    const d=await idbSubmit(blob,String(m.name||''),/^[-a-z0-9_]{1,32}$/.test(m.based_on||'')?m.based_on:null,update);
    const origin={hub:iconApiUrl(),slug:d.slug,sha256:await iconBlobHash(blob)};
    const name=iconIdFrom(m.name)+'.gif';
    try{if(await iconBlobHash(await readIconBlob(name))===origin.sha256)await saveIconOrigin(name,origin.slug,origin.sha256,origin.hub);}catch(e){}
    reply({type:'publish-result',requestId:m.requestId,ok:true,...d,origin});
  }catch(e){reply({type:'publish-result',requestId:m.requestId,ok:false,error:e.code||'failed',message:e.message,pr:e.pr||'',slug:e.slug||''});}
  finally{piskelPublishing=false;}
}
let piskelLiveOn=false,lastLiveErr='',liveStill=false,piskelLiveGeneration=0,piskelLiveQueue=Promise.resolve();
async function piskelLive(m){
  piskelLiveOn=true;
  const generation=piskelLiveGeneration;
  const body=m.mode==='bitmap'
    ?{hold:true,stack:false,name:LIVE_NAME,text:'',draw:[['bitmap',0,0,m.w,m.h,m.dataBase64]]}
    :{hold:true,stack:false,name:LIVE_NAME,text:'',icon:'data:'+(m.mime||'image/gif')+';base64,'+m.dataBase64};
  piskelLiveQueue=piskelLiveQueue.catch(()=>{}).then(async()=>{
    if(!piskelLiveOn||generation!==piskelLiveGeneration)return;
    try{
      await post('/api/v1/notifications',body);lastLiveErr='';
      if(m.still&&!liveStill){liveStill=true;toast(t('edLiveStill'));}
    }catch(e){if(e.message!==lastLiveErr){lastLiveErr=e.message;toastErr(e);}}
  });
  return piskelLiveQueue;
}
function piskelLiveTooLarge(m){
  piskelLiveOn=true;
  const generation=piskelLiveGeneration;
  piskelLiveQueue=piskelLiveQueue.catch(()=>{}).then(()=>{
    const msg=t(m.reason==='colors'?'edLiveColors':'edLiveSize')+' '+t('edLiveKeep');
    if(generation===piskelLiveGeneration&&msg!==lastLiveErr){lastLiveErr=msg;toast(msg,false);}
  });
}
async function piskelLiveOff(){
  piskelLiveOn=false;lastLiveErr='';liveStill=false;
  const generation=++piskelLiveGeneration;
  await piskelLiveQueue;
  if(piskelLiveOn||generation!==piskelLiveGeneration)return;
  try{await api('/api/v1/notifications/'+LIVE_NAME,{method:'DELETE'});}catch(e){}
}
window.addEventListener('message',e=>{
  const frame=$('#piskelFrame');
  if(!frame||e.source!==frame.contentWindow||e.origin!==piskelOrigin())return;
  const m=e.data;if(!m||m.ns!=='awtrix')return;
  if(m.type==='icon-load-result'&&piskelIconLoad?.requestId===m.requestId){
    resetPiskelIconLoad();
    if(m.ok===false)toast(m.message||m.error||t('edIconOpenError'),false);
    return;
  }
  if(m.type==='ready'){
    piskelReady=true;
    const l=$('#piskelFrame')&&document.querySelector('.piskelload');if(l)l.remove();
    notifyPiskelTheme();
    postToPiskel({type:'config',host:'awtrix',protocol:2,sizes:editorSizes(),max:editorMax(),publishViaParent:true});
    if(pendingEditIcon){const n=pendingEditIcon;pendingEditIcon=null;piskelSendIcon(n);}
  }else if(m.type==='save')piskelSave(m);
  else if(m.type==='publish')piskelPublish(m);
  else if(m.type==='list')piskelSendList();
  else if(m.type==='load'&&m.name)piskelSendIcon(m.name);
  else if(m.type==='live')piskelLive(m);
  else if(m.type==='live-too-large')piskelLiveTooLarge(m);
  else if(m.type==='live-off')piskelLiveOff();
});
window.addEventListener('hashchange',()=>{if(piskelLiveOn&&currentRoute()!=='editor')piskelLiveOff();});
window.addEventListener('pagehide',()=>{if(piskelLiveOn){try{fetch('/api/v1/notifications/'+LIVE_NAME,{method:'DELETE',keepalive:true});}catch(e){}}});
function viewEditor(view){
  if(piskelLiveOn)piskelLiveOff();
  piskelReady=false;
  resetPiskelIconLoad();
  const url=getPiskelUrl();
  const theme=document.documentElement.dataset.theme==='light'?'light':'dark';
  // The editor is handed the same Hub the gallery uses. Without it a framed
  // editor falls back to whatever host it was built against, and an override in
  // localStorage would reach the gallery but not the editor inside it.
  const src=url+(url.includes('?')?'&':'?')+'theme='+encodeURIComponent(theme)+
    '&sizes='+editorSizes().join(',')+'&max='+editorMax()+
    '&host=awtrix&iconapi='+encodeURIComponent(iconApiUrl())+'&v='+Date.now();
  const loading=el('div',{class:'piskelload'},el('span',{class:'spin'}),el('span',null,t('edLoading')));
  const frame=el('iframe',{id:'piskelFrame',src,title:t('editorTab'),allow:'clipboard-read; clipboard-write'});
  view.append(el('div',{class:'piskelwrap wide'},loading,frame));
  setTimeout(()=>{
    if(!piskelReady&&document.contains(loading)){
      loading.classList.add('err');
      loading.replaceChildren(el('div',null,t('edUnreachable')));
    }
  },6000);
}
