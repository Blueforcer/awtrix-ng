const TZRULE=new Map(),TZFIRST=new Map();
TZ_TABLE.forEach(([rule,zones])=>{
  const list=zones.split(' ');
  TZFIRST.set(rule,list[0]);
  list.forEach(n=>TZRULE.set(n,rule));
});
const TZFMT=new Map();
function tzFmtFor(zone){
  if(TZFMT.has(zone))return TZFMT.get(zone);
  let f=null;
  try{f=new Intl.DateTimeFormat('en',{timeZone:zone,timeZoneName:'longOffset'});}catch(e){}
  TZFMT.set(zone,f);
  return f;
}
let tzNames=null;
function tzOffset(zone,at){
  const f=tzFmtFor(zone);
  if(!f)return 0;
  const s=f.formatToParts(at).find(p=>p.type==='timeZoneName').value;
  const m=/GMT([+-])(\d+)(?::(\d+))?/.exec(s);
  return m?(m[1]==='-'?-1:1)*(Number(m[2])*60+Number(m[3]||0)):0;
}
const tzOffsetStr=o=>'UTC'+(o<0?'-':'+')+Math.floor(Math.abs(o)/60)+(Math.abs(o)%60?':'+String(Math.abs(o)%60).padStart(2,'0'):'');
function tzState(zone){
  const now=new Date(),cur=tzOffset(zone,now);
  const DAY=864e5;
  let win=cur,sum=cur,next=null;
  for(let i=1;i<=366;i++){
    const o=tzOffset(zone,new Date(+now+i*DAY));
    win=Math.min(win,o);sum=Math.max(sum,o);
    if(next===null&&o!==cur)next=new Date(+now+i*DAY);
  }
  return{off:cur,dst:sum>win&&cur===sum,changes:sum>win,next};
}
