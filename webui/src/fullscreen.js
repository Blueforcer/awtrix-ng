function viewFullscreen(){
  document.documentElement.classList.add('fs');
  const initial=displayGeometry();
  const cv=el('canvas',{id:'screen',width:initial.width*10,height:initial.height*10,role:'img','aria-label':t('livepre')});
  document.body.replaceChildren(cv);
  const ctx=cv.getContext('2d');
  let pw=initial.width,ph=initial.height;
  const fit=()=>{
    const sc=Math.max(1,Math.floor(Math.min(innerWidth/pw,innerHeight/ph)));
    if(cv.width!==pw*sc||cv.height!==ph*sc){cv.width=pw*sc;cv.height=ph*sc;}
    return sc;
  };
  addEventListener('resize',()=>fit());
  pollScreen((px,d)=>{pw=d.width;ph=d.height;paint(ctx,px,pw,ph,fit());},250);
}
