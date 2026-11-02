const assert = require('node:assert/strict');
const {boot, goto, flush} = require('./harness');

(async()=>{
  for(const enabled of [false,true]){
    const {window}=await boot({caps:{transitions:[],tlsTrust:enabled}});
    const fallback=window.fetch,requests=[];
    let state={usable:true,hosts:[{host:'ha.local',pin:null,ca:false,pending:'a'.repeat(64)}]};
    window.fetch=async(url,options={})=>{
      if(!String(url).startsWith('/api/v1/tls/trust'))return fallback(url,options);
      const method=options.method||'GET',body=options.body?JSON.parse(options.body):null;
      requests.push({url:String(url),method,body,headers:options.headers});
      if(method==='POST')state.hosts[0]={...state.hosts[0],pin:body.fingerprint,pending:null};
      if(method==='PUT')state.hosts.push({host:'new.local',pin:null,ca:true,pending:null});
      if(method==='DELETE')state.hosts=[];
      return new Response(JSON.stringify(state));
    };
    try{
      await goto(window,'#/system');await flush(80);
      const section=window.document.querySelector('#sec-certificates');
      assert.equal(!!section,enabled,'capability gates certificate management');
      if(!enabled){assert.equal(requests.length,0);continue;}
      const button=text=>[...section.querySelectorAll('button')].find(b=>b.textContent===text);
      assert.ok(section.textContent.includes('ha.local'));
      assert.ok(section.textContent.includes('a'.repeat(64)));
      button('Trust certificate').click();await flush(80);
      const approval=requests.find(r=>r.method==='POST');
      assert.equal(approval.url,'/api/v1/tls/trust/ha.local/pin');
      assert.deepEqual(approval.body,{fingerprint:'a'.repeat(64)});
      assert.equal(approval.headers['X-Awtrix-Trust'],'1');
      assert.ok(button('Remove pin'));
      section.querySelector('input[type=text]').value='new.local';
      const inputs=section.querySelectorAll('input[type=file]'),input=inputs[inputs.length-1];
      Object.defineProperty(input,'files',{value:[{size:12,text:async()=>'test-ca-pem'}],configurable:true});
      input.dispatchEvent(new window.Event('change'));await flush(80);
      const upload=requests.find(r=>r.method==='PUT');
      assert.equal(upload.url,'/api/v1/tls/trust/new.local/ca');
      assert.deepEqual(upload.body,{certificate:'test-ca-pem'});
      assert.ok(button('Remove CA'));
      const forget=button('Forget host');forget.click();forget.click();await flush(80);
      assert.ok(requests.some(r=>r.method==='DELETE'));
      state={usable:false,hosts:[]};
      await window.eval('api("/api/v1/tls/trust")');
      // A fresh page reflects the fail-closed store and offers explicit recovery.
      await goto(window,'#/apps');await goto(window,'#/system');await flush(80);
      assert.ok(window.document.querySelector('#sec-certificates').textContent.includes('Certificate store unavailable'));
    }finally{window.close();}
  }
  console.log('certificates: capability, pending approval, CA upload, removal and unavailable store passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
