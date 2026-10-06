
const BERRY_KW=new Set(['if','elif','else','while','for','def','end','class','break',
  'continue','return','true','false','nil','var','do','import','as','try','except',
  'raise','static','self']);
const BERRY_MEM=new Set(BERRY_API.filter(s=>s.includes('.')).map(s=>s.split('.')[1].split('(')[0]));
const BERRY_FN=new Set(BERRY_API.filter(s=>!s.includes('.')).map(s=>s.split('(')[0]));
const BERRY_MODSET=new Set(BERRY_MODS);
const BERRY_CORESET=new Set(BERRY_CORE);

const BERRY_TOK=/(#-[\s\S]*?(?:-#|$))|(#[^\n]*)|('(?:\\.|[^'\\])*'?|"(?:\\.|[^"\\])*"?)|(0[xX][0-9a-fA-F]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)|([A-Za-z_]\w*)|([(){}[\]])|([-+*/%<>=!&|^~?:.,;]+)/g;

function matchBracket(src,pos){
  const open='([{',close=')]}';
  for(const at of [pos,pos-1]){
    const ch=src[at];
    if(ch===undefined)continue;
    let dir=0,mate='';
    if(open.includes(ch)){dir=1;mate=close[open.indexOf(ch)];}
    else if(close.includes(ch)){dir=-1;mate=open[close.indexOf(ch)];}
    else continue;
    let depth=0;
    for(let i=at;i>=0&&i<src.length;i+=dir){
      if(src[i]===ch)depth++;
      else if(src[i]===mate&&!--depth)return dir>0?[at,i]:[i,at];
    }
  }
  return null;
}

function mkCodeEditor(){
  const gut=el('div',{class:'gut'});
  const hl=el('div',{class:'hl','aria-hidden':'true'});
  const ta=el('textarea',{class:'ta',spellcheck:'false',wrap:'off',autocapitalize:'off',
    autocomplete:'off',autocorrect:'off','aria-label':'Berry source'});
  const box=el('div',{class:'box'},hl,ta);
  const node=el('div',{class:'ed'},gut,box);
  const errStrip=el('i',{class:'err'}),curStrip=el('i',{class:'cur'});
  let errLine=0,lines=0,onEdit=null,onMove=null,frame=0;

  const cs=()=>getComputedStyle(ta);
  const metric=()=>({h:parseFloat(cs().lineHeight),top:parseFloat(cs().paddingTop),
    left:parseFloat(cs().paddingLeft)});
  let charW=0;
  function measure(){
    const s=cs();
    const probe=el('span',{style:'position:absolute;visibility:hidden;white-space:pre;'+
      'font-family:'+s.fontFamily+';font-size:'+s.fontSize},'0'.repeat(40));
    box.append(probe);
    charW=probe.getBoundingClientRect().width/40;probe.remove();
  }

  function paint(){
    frame=0;
    const src=ta.value,m=metric(),nodes=[errStrip,curStrip];
    const pair=document.activeElement===ta?matchBracket(src,ta.selectionStart):null;
    let last=0,prev='',prev2='';
    BERRY_TOK.lastIndex=0;
    for(let g;(g=BERRY_TOK.exec(src));){
      if(g.index>last)nodes.push(src.slice(last,g.index));
      last=BERRY_TOK.lastIndex;
      const txt=g[0];
      let cls='';
      if(g[1]||g[2])cls='c';
      else if(g[3])cls='s';
      else if(g[4])cls='n';
      else if(g[5]){
        if(BERRY_KW.has(txt))cls='k';
        else if(BERRY_MODSET.has(txt))cls='a';
        else if(prev==='.'&&BERRY_MODSET.has(prev2)&&BERRY_MEM.has(txt))cls='a';
        else if(BERRY_FN.has(txt))cls='a';
        else if(BERRY_CORESET.has(txt))cls='b';
      }else{
        cls=(pair&&(g.index===pair[0]||g.index===pair[1]))?'o m':'o';
      }
      nodes.push(cls?el('span',{class:cls},txt):txt);
      if(g[5]||g[6]||g[7]){prev2=prev;prev=txt;}
    }
    if(last<src.length)nodes.push(src.slice(last));
    nodes.push('\n');
    hl.replaceChildren(...nodes);

    const n=src.split('\n').length,cur=src.slice(0,ta.selectionStart).split('\n').length;
    if(n!==lines){
      lines=n;
      gut.replaceChildren(...Array.from({length:n},(_,i)=>el('b',null,String(i+1))));
    }
    for(let i=0;i<gut.children.length;i++){
      const b=gut.children[i];
      b.className=(i+1===errLine?'err':i+1===cur?'on':'');
    }
    errStrip.style.cssText=errLine&&errLine<=n
      ? 'top:'+(m.top+(errLine-1)*m.h)+'px;height:'+m.h+'px'
      : 'display:none';
    curStrip.style.cssText=cur===errLine?'display:none'
      :'top:'+(m.top+(cur-1)*m.h)+'px;height:'+m.h+'px';
    sync();
    if(onMove)onMove();
  }
  function schedule(){if(!frame)frame=requestAnimationFrame(paint);}
  function sync(){hl.scrollTop=gut.scrollTop=ta.scrollTop;hl.scrollLeft=ta.scrollLeft;}

  function insert(text){
    if(!document.execCommand||!document.execCommand('insertText',false,text)){
      const s=ta.selectionStart,e=ta.selectionEnd;
      ta.value=ta.value.slice(0,s)+text+ta.value.slice(e);
      ta.selectionStart=ta.selectionEnd=s+text.length;
    }
    changed();
  }
  function changed(){errLine=0;schedule();if(onEdit)onEdit();}

  ta.addEventListener('input',changed);
  ta.addEventListener('scroll',sync);
  ['click','keyup','select'].forEach(ev=>ta.addEventListener(ev,schedule));
  ta.addEventListener('blur',schedule);

  return {
    node,ta,box,
    get value(){return ta.value;},
    set value(v){ta.value=v;lines=0;errLine=0;schedule();},
    focus(){ta.focus();},
    onEdit(fn){onEdit=fn;},
    onMove(fn){onMove=fn;},
    markError(line){
      errLine=line||0;
      if(errLine){
        const m=metric();
        const y=m.top+(errLine-1)*m.h;
        if(y<ta.scrollTop||y>ta.scrollTop+ta.clientHeight-m.h)
          ta.scrollTop=Math.max(0,y-ta.clientHeight/2);
      }
      schedule();
    },
    caret(){
      const upto=ta.value.slice(0,ta.selectionStart),nl=upto.lastIndexOf('\n');
      return{line:upto.split('\n').length,col:upto.length-nl};
    },
    caretXY(){
      const m=metric(),c=this.caret();
      if(!charW)measure();
      return{x:m.left+(c.col-1)*charW-ta.scrollLeft,y:m.top+c.line*m.h-ta.scrollTop};
    },
    insert,
    repaint(){lines=0;schedule();},
  };
}
