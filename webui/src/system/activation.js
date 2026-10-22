//linux:begin
const activationConsent=async()=>(await api('/api/v1/activation')).data?.consent;
function activationDialog(answered){
  const d=el('dialog',{class:'card',style:'margin:auto;max-width:min(440px,calc(100vw - 32px));color:var(--fg);box-shadow:0 0 0 100vmax #000a','aria-labelledby':'actitle'});
  const answer=async yes=>{
    try{await api('/api/v1/activation',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({consent:yes})});d.close();d.remove();answered?.(yes);}
    catch(e){toastErr(e);}
  };
  const link=(href,key)=>el('a',{class:'ghelp',href,target:'_blank',rel:'noopener'},t(key));
  d.addEventListener('cancel',e=>e.preventDefault());
  d.append(el('h2',{id:'actitle',tabindex:'-1',autofocus:''},t('acTitle')),el('p',null,t('acWhy')),el('p',null,t('acWhat')),
    el('div',{style:'display:grid;grid-template-columns:1fr 1fr;gap:8px'},
      el('button',{onclick:()=>answer(true)},t('acYes')),
      el('button',{onclick:()=>answer(false)},t('acNo'))),
    el('p',{class:'ghelp'},
      link('https://blueforcer.github.io/awtrix-ng/tc002/getting-started/tc002/#activation','acMore'),' · ',
      link('https://awtrix.de/privacy#tc002-activation','acPrivacy')));
  document.body.append(d);
  d.showModal();
}
async function askActivation(){
  try{if(await activationConsent()!==null)return;}catch(e){return;}
  activationDialog();
}
async function activationSection(page){
  try{if(await activationConsent()!==false)return;}catch(e){return;}
  const state=el('div',{class:'badge'},t('acOff'));
  const count=el('button',{onclick:()=>activationDialog(yes=>{
    if(!yes)return;
    state.className='badge good';state.replaceChildren(t('acDone'));count.remove();
  })},t('acCount'));
  page.section('activation',t('acTitle'),'',[actionRow(t('acState'),t('acOffH'),[state,count])]);
}
//linux:end
