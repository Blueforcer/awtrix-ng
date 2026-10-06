let gen=0;
function poller(fn,ms,eager){
  const g=gen;let fails=0;
  (async function run(){
    if(g!==gen)return;
    const base=typeof ms==='function'?ms():ms;
    const t0=Date.now();
    let d=base;
    if(eager||!document.hidden){
      eager=false;
      try{await fn();fails=0;d=Math.max(0,base-(Date.now()-t0));}
      catch(e){fails++;d=Math.min(base*4*fails,10000);}
    }
    setTimeout(run,d);
  })();
}
function setStats(d){
  S.stats=d;
  S.scriptsOff=d.scriptingRunning===false;
  if(d.hostname){S.sysHost=d.hostname;document.title=d.hostname+' – AWTRIX NG';}
  if(d.uid)S.sysUid=d.uid;
}
const TABS=[['','dash',viewDash],['apps','appsTab',viewApps],['scripts','scriptsTab',viewScripts],
  ['icons','icons',viewIcons],['editor','editorTab',viewEditor],['audio','audioTab',viewAudio],
  ['palettes','palettes',viewPalettes],['display','display',viewDisplay],
  ['system','system',viewSystem],['log','log',viewLog]];
function currentRoute(){
  const r=location.hash.replace(/^#\/?/,'').split('/')[0];
  // The Sounds and Radio tabs merged into Audio; old bookmarks still land somewhere sensible.
  return S.ap?'system':({sounds:'audio',radio:'audio'}[r]||r);
}
function nav(){
  const cur=currentRoute(),bar=$('#nav');
  const tabs=(S.ap?TABS.filter(([r])=>r==='system')
    :TABS.filter(([r])=>r!=='audio'||anyAudioCap())).map(([r,k])=>['#/'+r,t(k)]);
  bar.setAttribute('aria-label',t('mainnav'));
  const links=[...bar.children];
  if(links.length!==tabs.length||links.some((a,i)=>a.getAttribute('href')!==tabs[i][0]||a.textContent!==tabs[i][1]))
    bar.replaceChildren(...tabs.map(([h,l])=>el('a',{href:h},l)));
  for(const a of bar.children){
    const on=a.getAttribute('href')==='#/'+cur;
    a.classList.toggle('on',on);
    if(on)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current');
  }
  const on=bar.querySelector('a.on');
  if(on){
    const b=bar.getBoundingClientRect(),r=on.getBoundingClientRect();
    if(r.left<b.left)bar.scrollLeft-=b.left-r.left;
    else if(r.right>b.right)bar.scrollLeft+=r.right-b.right;
  }
}
function render(){
  gen++;
  const r=currentRoute();
  $('#skip').textContent=t('skip');
  nav();
  const view=el('main',{id:'view'});
  $('#view').replaceWith(view);
  (TABS.find(([n])=>n===r)||TABS[0])[2](view);
}
window.addEventListener('hashchange',render);
$('#kofibtn').addEventListener('click',()=>window.open('https://ko-fi.com/blueforcer','_blank','noopener'));
$('#hubbtn').addEventListener('click',()=>window.open('https://awtrix.de/','_blank','noopener'));
const DOCS_CLOCK={esp32:'esp32/',esp32s3:'esp32-s3/',tc002:'tc002/'};
$('#docsbtn').addEventListener('click',()=>window.open('https://blueforcer.github.io/awtrix-ng/'+
  (DOCS_CLOCK[S.caps?.platform?.id]||''),'_blank','noopener'));
const themeUse=$('#themebtn').querySelector('use');
function paintTheme(){themeUse.setAttribute('href',document.documentElement.dataset.theme==='light'?'#i-moon':'#i-sun');}
$('#themebtn').addEventListener('click',()=>{
  const d=document.documentElement.dataset;
  d.theme=d.theme==='light'?'dark':'light';
  try{localStorage.awtrixTheme=d.theme;}catch(e){}
  paintTheme();
  notifyPiskelTheme();
});
