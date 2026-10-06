function viewLog(view){
  const con=el('div',{class:'console'});
  let next=0,autoscroll=true,lines=[];
  con.addEventListener('scroll',()=>{
    autoscroll=con.scrollTop+con.clientHeight>=con.scrollHeight-8;
    asSw.input.checked=autoscroll;
  });
  const clearBtn=el('button',null,t('clear'));
  clearBtn.addEventListener('click',()=>{lines=[];con.replaceChildren();});
  const copyBtn=el('button',null,t('copy'));
  copyBtn.addEventListener('click',async()=>{await copyText(lines.join('\n'));toast(t('copied'));});
  const asSw=mkSwitch(true,()=>{autoscroll=asSw.input.checked;});
  view.append(el('div',{class:'card wide'},
    el('h2',null,t('log')),el('div',{class:'ghelp'},t('loghelp')),
    el('div',{class:'row'},clearBtn,copyBtn,el('span',{class:'grow'}),
      el('span',{class:'mono'},t('autoscroll')),asSw.node),
    con));
  poller(async()=>{
    const{data}=await api('/api/v1/logs?after='+next,{timeout:4000});
    if(!data||!Array.isArray(data.lines))return;
    next=data.next;
    if(data.lines.length){
      lines.push(...data.lines);
      if(lines.length>800){lines=lines.slice(-600);con.replaceChildren(lines.join('\n'));}
      else data.lines.forEach(l=>con.append(l+'\n'));
      if(autoscroll)con.scrollTop=con.scrollHeight;
    }
  },1000);
}
