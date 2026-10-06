let scrPending=null;
let scrDraft=null;
window.addEventListener('beforeunload',e=>{if(scrDraft&&scrDraft.dirty)e.preventDefault();});
