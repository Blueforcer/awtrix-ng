const SVGNS='http://www.w3.org/2000/svg';
function icon(id){
  const s=document.createElementNS(SVGNS,'svg');
  s.setAttribute('class','ic');s.setAttribute('aria-hidden','true');
  const u=document.createElementNS(SVGNS,'use');u.setAttribute('href','#i-'+id);
  s.append(u);return s;
}
const gifQ=(c,s)=>((((c>>16&255)>>s<<s)<<16)|(((c>>8&255)>>s<<s)<<8)|((c&255)>>s<<s));
function gifPalette(px,n){
  for(let s=0;;s++){
    const m=new Map([[0,0]]);
    for(let i=0;i<n;i++){const q=gifQ(px[i],s);if(!m.has(q))m.set(q,m.size);}
    if(m.size<=256)return{m,s};
  }
}
function gifLzw(idx,minCode){
  const out=[],clear=1<<minCode,eoi=clear+1;
  let dict=new Map(),next=eoi+1,size=minCode+1,cur=0,bits=0;
  const put=c=>{cur|=c<<bits;bits+=size;while(bits>=8){out.push(cur&255);cur>>=8;bits-=8;}};
  put(clear);
  let pre=idx[0];
  for(let i=1;i<idx.length;i++){
    const k=idx[i],v=dict.get(pre*256+k);
    if(v!==undefined){pre=v;continue;}
    put(pre);
    if(next<4096){dict.set(pre*256+k,next++);while(next>(1<<size)&&size<12)size++;}
    else{put(clear);dict=new Map();next=eoi+1;size=minCode+1;}
    pre=k;
  }
  put(pre);put(eoi);
  if(bits)out.push(cur&255);
  return out;
}
function gifEncode(frames,W,H,SC){
  const b=[],w=W*SC,h=H*SC;
  const u16=v=>b.push(v&255,v>>8&255),str=s=>{for(const c of s)b.push(c.charCodeAt(0));};
  str('GIF89a');u16(w);u16(h);b.push(0x70,0,0);
  b.push(0x21,0xFF,0x0B);str('NETSCAPE2.0');b.push(3,1,0,0,0);
  for(let i=0;i<frames.length;i++){
    const f=frames[i],nx=frames[i+1],{m,s}=gifPalette(f.px,W*H),pal=[...m.keys()];
    let bits=1;while((1<<bits)<m.size)bits++;
    b.push(0x21,0xF9,4,4);u16(gifDelay(f,nx));b.push(0,0);
    b.push(0x2C);u16(0);u16(0);u16(w);u16(h);b.push(0x80|(bits-1));
    for(let j=0;j<(1<<bits);j++){const c=pal[j]|0;b.push(c>>16&255,c>>8&255,c&255);}
    const idx=[],row=[],gap=gifGap(SC),lit=SC-gap;
    for(let y=0;y<H;y++){
      row.length=0;
      for(let x=0;x<W;x++){
        const v=m.get(gifQ(f.px[y*W+x],s));
        for(let k=0;k<lit;k++)row.push(v);
        for(let k=0;k<gap;k++)row.push(0);
      }
      for(let k=0;k<lit;k++)for(let j=0;j<row.length;j++)idx.push(row[j]);
      for(let k=0;k<gap;k++)for(let j=0;j<row.length;j++)idx.push(0);
    }
    const mc=Math.max(2,bits),lz=gifLzw(idx,mc);
    b.push(mc);
    for(let j=0;j<lz.length;j+=255){
      const n=Math.min(255,lz.length-j);
      b.push(n);for(let k=0;k<n;k++)b.push(lz[j+k]);
    }
    b.push(0);
  }
  b.push(0x3B);
  return new Uint8Array(b);
}
const gifGap=SC=>SC>1?Math.max(1,Math.round(SC/10)):0;
const gifDelay=(f,nx)=>Math.min(25,Math.max(2,Math.round((nx?nx.t-f.t:100)/10)));
async function imgToGif(src,tw,th){
  const bmp=(typeof ImageBitmap!=='undefined'&&src instanceof ImageBitmap)?src:await createImageBitmap(src);
  const w=tw||bmp.width,h=th||bmp.height;
  const cv=el('canvas',{width:w,height:h}),g=cv.getContext('2d');
  g.imageSmoothingEnabled=w!==bmp.width||h!==bmp.height;
  g.drawImage(bmp,0,0,w,h);
  if(bmp.close)bmp.close();
  const d=g.getImageData(0,0,w,h).data,px=[];
  for(let i=0;i<w*h;i++){
    const a=d[i*4+3]/255;
    px.push((Math.round(d[i*4]*a)<<16)|(Math.round(d[i*4+1]*a)<<8)|Math.round(d[i*4+2]*a));
  }
  return new Blob([gifEncode([{t:0,px}],w,h,1)],{type:'image/gif'});
}
async function iconAsGif(file,name){
  if(!/^image\/(png|jpeg)$/.test(file.type||'')&&!/\.(png|jpe?g)$/i.test(name||''))return null;
  let bmp;
  try{bmp=await createImageBitmap(file);}catch(e){throw new Error(t('idbe_badFormat'));}
  const w=bmp.width,h=bmp.height,{width:mw,height:mh}=displayGeometry();
  if(bmp.close)bmp.close();
  if(w>mw||h>mh)throw new Error('Image must fit the panel: '+mw+' × '+mh);
  return{blob:await imgToGif(file),name:String(name).replace(/\.[^.]+$/,'')+'.gif'};
}
async function dropStale(dir,from,to){
  for(const n of new Set([from,to.replace(/\.gif$/i,'.jpg')])){
    if(n===to)continue;
    try{await delFile(dir+'/'+n);}catch(e){}
  }
}
function paint(g,px,W,H,SC){
  const lit=SC-gifGap(SC);
  g.fillStyle='#000';g.fillRect(0,0,W*SC,H*SC);
  for(let i=0;i<W*H&&i<px.length;i++){
    const c=px[i];
    g.fillStyle='rgb('+(c>>16&255)+','+(c>>8&255)+','+(c&255)+')';
    g.fillRect((i%W)*SC,((i/W)|0)*SC,lit,lit);
  }
}
const pollScreen=(fn,ms)=>poller(async()=>{
  const{data}=await api('/api/v1/display/screen',{timeout:4000});
  if(Array.isArray(data?.pixels))fn(data.pixels,displayGeometry(data));
},ms);
