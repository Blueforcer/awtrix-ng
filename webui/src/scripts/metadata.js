const SCRIPT_TEMPLATE=`# @name My App
# @desc What this app shows

# An app is a class; the last line returns an instance. draw() is required,
# every other method (init, loop, on_button, setup) is optional.
class MyApp
  var n

  def init()
    self.n = store.get("count", 0)
  end

  def draw()           # every frame while visible
    clear()
    # width()/height() are native pixels; y is the text BASELINE
    text(1, 6, "n=" + str(self.n), 0x00A0FF)
  end
end

return MyApp()
`;
const MODULE_TEMPLATE=name=>`# @module
# @desc Shared helpers

# Other scripts write "import ${name}" and call ${name}.twice(2).
var m = module("${name}")
m.twice = def (v) return v * 2 end
return m
`;
function scriptMeta(src,key){
  const m=new RegExp('^#[ \\t]*@'+key+'[ \\t]+(.+)$','m').exec(src);
  return m?m[1].trim():'';
}
const ICONS_MAX=32;
function scriptIcons(src){
  const out=[];
  for(const raw of String(src||'').split(/\r?\n/)){
    const line=raw.trim();
    if(!line)continue;
    if(line[0]!=='#')break;
    const m=/^#[ \t]*@icons\b[ \t]*(.*)$/i.exec(line);
    if(!m)continue;
    for(const id of m[1].split('#')[0].split(/[,\s]+/)){
      if(!/^[A-Za-z0-9_-]{1,32}$/.test(id))continue;
      if(out.indexOf(id)<0&&out.length<ICONS_MAX)out.push(id);
    }
  }
  return out;
}
function scriptRequires(src){
  const out=[];
  for(const raw of String(src||'').split(/\r?\n/)){
    const line=raw.trim();
    if(!line)continue;
    if(line[0]!=='#')break;
    const m=/^#[ \t]*@requires(?:[ \t]+(.*))?$/i.exec(line);
    if(!m||out.length>=8)continue;
    const[name,hub]=(m[1]||'').split('#')[0].split(/[ \t]+/);
    if(!/^[A-Za-z0-9_-]{1,32}$/.test(name)||(hub&&!/^[A-Za-z0-9]{12}$/.test(hub))||out.some(r=>r.name===name))continue;
    out.push(hub?{name,hub}:{name});
  }
  return out;
}
const NEEDS_MAX=8;
function scriptNeeds(src){
  const out=[];
  for(const raw of String(src||'').split(/\r?\n/)){
    const line=raw.trim();
    if(!line)continue;
    if(line[0]!=='#')break;
    if(/^#[ \t]*@oauth[ \t]+\S/i.test(line)&&out.length<NEEDS_MAX&&!out.includes('oauth')){out.push('oauth');continue;}
    const m=/^#[ \t]*@needs(?:[ \t]+(.*))?$/i.exec(line);
    if(!m)continue;
    for(const word of (m[1]||'').split('#')[0].split(/[ \t,]+/)){
      const name=word.toLowerCase();
      if(out.length>=NEEDS_MAX||name.length>32||!/^[a-z][a-z0-9]*(\.[a-z][a-z0-9]*)*$/.test(name)||out.includes(name))continue;
      out.push(name);
    }
  }
  return out;
}
function scriptDisplay(src){
  let out=null;
  for(const raw of String(src||'').split(/\r?\n/)){
    const line=raw.trim();
    if(!line)continue;
    if(line[0]!=='#')break;
    const m=/^#[ \t]*@display[ \t]+(\d{1,3})[ \t]*x[ \t]*(\d{1,3})[ \t]*(#|$)/i.exec(line);
    if(m&&+m[1]>0&&+m[2]>0)out={width:+m[1],height:+m[2]};
  }
  return out;
}
const scriptName=s=>s.replace(/[^A-Za-z0-9_-]/g,'').slice(0,32);
