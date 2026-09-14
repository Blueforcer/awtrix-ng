const { boot, goto, flush } = require('./harness');
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const id = 'ZGAjqmr8hFKN';
const hash = s => createHash('sha256').update(s).digest('hex');
const linked = code => `# @hub ${id} ${hash(code)}\n${code}`;
const old = linked('old source');
const next = 'new source';

async function scenario({ modified = false, conflict = false, draft = false } = {}) {
  const { window, store } = await boot();
  try {
    window.AbortSignal = AbortSignal;
    window.localStorage.awtrixHubToken = 'script-test-token';
    store.caps.scriptUpdates = true;
    const current = old + (modified ? '\n# custom' : '');
    store.scripts.set('Demo', current);
    const fetchDevice = window.fetch;
    const writes = [];
    let releaseSource;
    window.fetch = async (url, opts = {}) => {
      if (String(url).startsWith('https://awtrix.de/api/v1/scripts/')) {
        assert.equal(opts.credentials, 'omit');
        if (url.endsWith('/release')) {
          assert.equal(opts.headers.Authorization, undefined, 'release metadata stays public');
          return new Response(JSON.stringify({
            id, revision: 2, sha256: hash(next), notes: '<img src=x onerror=alert(1)>'
          }));
        }
        assert.equal(opts.headers.Authorization, 'Bearer script-test-token');
        if (draft) await new Promise(resolve => { releaseSource = resolve; });
        return new Response(next);
      }
      if (String(url).startsWith('/api/v1/apps/script-update/')) {
        const name = url.split('/').pop();
        const body = JSON.parse(opts.body);
        writes.push({ name, body });
        if (conflict) return new Response(JSON.stringify({ error: { message: 'Changed meanwhile' } }), { status: 409 });
        assert.equal(body.expected_source, modified ? null : current);
        store.scripts.set(name, body.source);
        return new Response('{"ok":true}');
      }
      return fetchDevice(url, opts);
    };
    await goto(window, '#/scripts');
    await flush(100);
    window.document.querySelector('.ftitem').click();
    await flush();
    const panel = window.document.querySelector('.script-hub-panel');
    assert.equal(panel.querySelector('a').textContent, 'Hub publication 2');
    assert.equal(panel.querySelectorAll('img').length, 0, 'release notes are plain text');
    assert.ok(panel.querySelector('button'), 'an available update has an action');
    panel.querySelector('button').click();
    window.document.querySelector('.toast .tacts button.pri').click();
    await flush(60);
    if (draft) {
      assert.ok(releaseSource, 'download is pending');
      const editor = window.document.querySelector('.edwrap textarea');
      editor.value += '\n# new unsaved work';
      editor.dispatchEvent(new window.Event('input', { bubbles: true }));
      releaseSource();
    }
    await flush(150);
    assert.equal(writes.length, 1);
    assert.equal(store.scripts.get('Demo'), modified || conflict ? current : linked(next));
    if (modified) {
      assert.notEqual(writes[0].name, 'Demo');
      assert.equal(store.scripts.get(writes[0].name), linked(next));
    }
    if (draft) assert.ok(window.document.querySelector('.edwrap textarea').value.endsWith('# new unsaved work'));
    if (!modified && !conflict && !draft) assert.equal(window.document.querySelector('.edwrap textarea').value, linked(next));
  } finally { window.close(); }
}

async function tokenRequiredScenario() {
  const { window, store } = await boot();
  try {
    window.AbortSignal = AbortSignal;
    store.caps.scriptUpdates = true;
    store.scripts.set('Demo', old);
    const fetchDevice = window.fetch;
    let sourceRequests = 0;
    window.fetch = async (url, opts = {}) => {
      if (String(url).endsWith('/release')) return new Response(JSON.stringify({
        id, revision: 2, sha256: hash(next), notes: ''
      }));
      if (String(url).endsWith('/source')) { sourceRequests++; return new Response(next); }
      return fetchDevice(url, opts);
    };
    await goto(window, '#/scripts');
    await flush(100);
    window.document.querySelector('.ftitem').click();
    await flush();
    window.document.querySelector('.script-hub-panel button').click();
    window.document.querySelector('.toast .tacts button.pri').click();
    await flush(100);
    assert.equal(sourceRequests, 0, 'source is not requested without a Hub token');
    assert.equal(store.scripts.get('Demo'), old, 'the installed script stays unchanged');
    const connect = [...window.document.querySelectorAll('.toast .tacts button')]
      .find(button => button.textContent === 'Connect to Hub');
    assert.ok(connect, 'the blocked update points to Hub settings');
    connect.click();
    await flush(80);
    assert.equal(window.location.hash, '#/system');
    assert.equal(window.document.activeElement, window.document.querySelector('#sec-hub input[type=password]'));
  } finally { window.close(); }
}

async function unlinkedScenario() {
  const { window, store } = await boot();
  try {
    store.caps.scriptUpdates = true;
    store.scripts.set('Local', '# @name Local\nreturn nil');
    await goto(window, '#/scripts');
    await flush(100);
    window.document.querySelector('.ftitem').click();
    await flush();
    assert.equal(window.document.querySelector('.script-hub-panel').hidden, true);
  } finally { window.close(); }
}

(async () => {
  await unlinkedScenario();
  await tokenRequiredScenario();
  await scenario();
  await scenario({ modified: true });
  await scenario({ conflict: true });
  await scenario({ draft: true });
  console.log('hub-script-updates: 6 workflows passed (unlinked, token gate, update, copy, conflict, in-flight draft)');
})().catch(error => { console.error(error); process.exitCode = 1; });
