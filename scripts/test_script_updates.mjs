import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import { createHash } from 'node:crypto';
import { test } from 'node:test';
import assert from 'node:assert/strict';

const html=readFileSync(new URL('../webui/index.html',import.meta.url),'utf8');
const code=html.split('// HUB SCRIPT UPDATES BEGIN')[1].split('// HUB SCRIPT UPDATES END')[0];
const context={TextEncoder,AbortSignal,t:k=>k,iconSha256:b=>createHash('sha256').update(b).digest('hex')};
runInNewContext(code.slice(code.indexOf('\n')),context);
const id='ZGAjqmr8hFKN';
test('origin survives local rename and detects a local edit',async()=>{
  const old=context.hubScriptLink(id,'def draw() end\r\n');
  assert.equal(context.hubScriptOrigin(old).id,id);
  assert.equal(context.hubScriptOrigin(old).code,'def draw() end\r\n');
  const release={id,sha256:context.hubScriptHash('new source')};
  assert.equal((await context.prepareHubScriptUpdate(old,release,async()=> 'new source')).modified,false);
  assert.equal((await context.prepareHubScriptUpdate(old+'# custom',release,async()=> 'new source')).modified,true);
});
test('unlinked scripts, wrong identity and changing releases fail closed',async()=>{
  const old=context.hubScriptLink(id,'old');
  await assert.rejects(context.prepareHubScriptUpdate('old',{id}),/suChanged/);
  await assert.rejects(context.prepareHubScriptUpdate(old,{id:'another'}),/suChanged/);
  await assert.rejects(context.prepareHubScriptUpdate(old,{id,sha256:context.hubScriptHash('new')},async()=> 'changed'),/suIntegrity/);
  assert.equal(context.hubScriptOrigin('# @hub https://evil.test/x hash\ncode'),null);
});
test('update checks send no local credentials and accept only the canonical Hub',async()=>{
  context.fetch=async(url,options)=>{
    assert.equal(url,'https://awtrix.de/api/v1/scripts/'+id+'/release');
    assert.equal(options.credentials,'omit');assert.equal(options.redirect,'error');
    assert.equal(options.headers.Authorization,undefined);
    return {ok:true,json:async()=>({id,sha256:'a'.repeat(64),revision:2,notes:'Fix'})};
  };
  assert.equal((await context.hubScriptRelease(id)).revision,2);
  await assert.rejects(context.hubScriptRelease('../account'),/suIntegrity/);
  context.fetch=async()=>({ok:false,status:404});
  await assert.rejects(context.hubScriptRelease(id),/suMissing/);
});
