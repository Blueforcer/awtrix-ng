/* The web UI's header parsers against the vectors the firmware and the AWTRIX Hub also run, and
   the capability names against the same file. Then the parts of the UI that use them: the app
   list reads the device's own verdict, a Hub release is judged against /api/v1/capabilities, and
   Hub links carry what the device offers.
   Run: node script-header.test.js */
const fs = require('fs');
const path = require('path');
const assert = require('node:assert/strict');
const { boot, goto, flush } = require('./harness');

const VECTORS = JSON.parse(fs.readFileSync(path.join(__dirname, '..', '..', 'test', 'fixtures', 'script_header_vectors.json'), 'utf8'));
const plain = v => JSON.parse(JSON.stringify(v));

function vectorCases(window) {
  for (const c of VECTORS.cases) {
    const display = window.scriptDisplay(c.source);
    assert.deepEqual(plain(window.scriptNeeds(c.source)), c.needs, c.name + ' (needs)');
    assert.deepEqual(display && [display.width, display.height], c.display, c.name + ' (display)');
    assert.deepEqual(plain(window.scriptIcons(c.source)), c.icons, c.name + ' (icons)');
    assert.deepEqual(plain(window.scriptRequires(c.source)).map(r => ({ name: r.name, hub: r.hub ?? null })), c.requires, c.name + ' (requires)');
  }
  assert.deepEqual([...window.eval('CAP_NAMES')].sort(), [...VECTORS.vocabulary].sort(), 'the web UI knows exactly the shared capability names');
}

function capabilityCases(window) {
  window.eval('S.caps = {audio:{rtttl:true,mp3:false,effect:false},sensors:{light:true},platform:{id:"esp32"}}; S.display = {width:32,height:8};');
  assert.equal(window.eval('capPresent("audio.rtttl")'), true);
  assert.equal(window.eval('capPresent("gamepad")'), false, 'an absent capability is not present');
  assert.equal(window.eval('capPresent("hologram")'), false);
  assert.equal(window.eval('fitWords(["audio.rtttl"], null)'), '', 'a script that fits has nothing to say');
  assert.equal(window.eval('fitWords(["gamepad","audio.effect"], {width:52,height:16})'),
    'a gamepad, layered sound, a 52×16 panel');
  assert.equal(window.eval('fitWords(undefined, undefined)'), '', 'an older Hub without the fields is not a misfit');
  assert.equal(window.eval('metaFitWords({needs:[{name:"gamepad",missing:true},{name:"audio.rtttl",missing:false}],display:{width:52,height:16,fits:false}})'),
    'a gamepad, a 52×16 panel');
  assert.equal(window.eval('metaFitWords({needs:[{name:"gamepad"}],display:null})'), '',
    'without a verdict from the device there is no claim');

  const link = new URL(window.eval('hubDeviceUrl("../scripts")'));
  assert.equal(link.origin + link.pathname, 'https://awtrix.de/scripts');
  assert.equal(link.searchParams.get('device'), 'esp32');
  assert.equal(link.searchParams.get('panel'), '32x8');
  assert.equal(link.searchParams.get('caps'), 'audio.rtttl,sensors.light');
}

const RACER_MODULE = {
  name: 'steer', origin: 'module', import: 'steer', config: false, error: null,
  meta: { name: 'Steering', icons: [], requires: [], needs: [{ name: 'gamepad', missing: true }], display: null },
};

async function appListShowsTheDevicesVerdict() {
  const { window, store } = await boot();
  try {
    store.apps = [{
      name: 'racer', enabled: true, inLoop: true, slot: 0, present: true, origin: 'script',
      skipped: false, headless: false, config: false, error: null,
      meta: { name: 'Racer', desc: '', author: '', version: '', icons: [], requires: [],
        needs: [{ name: 'gamepad', missing: true }], display: { width: 52, height: 16, fits: false } },
    }, RACER_MODULE, { name: 'fmt', origin: 'module', import: 'fmt', config: false, error: null, meta: { needs: [] } }];
    await goto(window, '#/apps');
    await flush(100);
    const verdicts = [];
    for (const name of ['Racer', 'Steering']) {
      const row = () => [...window.document.querySelectorAll('.approw')]
        .find(r => r.querySelector('.nmt').textContent === name);
      const fit = [...row().querySelectorAll('.chip.warn')].find(c => c.textContent === 'does not fit');
      assert.ok(fit, name + ' is marked as not fitting');
      fit.click();
      await flush(20);
      verdicts.push(row().querySelector('.rowinfo').textContent);
    }
    assert.deepEqual(verdicts, ['Not for this clock: needs a gamepad, a 52×16 panel.',
      'Not for this clock: needs a gamepad.'], 'a plain library module that does not fit shows why');
    const shared = window.document.getElementById('apps-mod');
    assert.ok(shared && !shared.hidden, 'the shared settings group shows for it');
    assert.deepEqual([...shared.querySelectorAll('.approw .nmt')].map(n => n.textContent), ['Steering'],
      'a plain library module that fits stays out of the way');
  } finally { window.close(); }
}

const NOT_FOR_ULANZI = '# @name Racer\n# @needs gamepad # for steering\n# @display 52x16 # the TC002 panel\ndef draw() end\n';
const MISFIT = 'Not for this clock: needs a gamepad, a 52×16 panel.';

async function saveAndImportAskFirst() {
  const { window, store, netlog } = await boot({ caps: { transitions: [], audio: { rtttl: true, mp3: false },
    display: { width: 32, height: 8 } } });
  try {
    await goto(window, '#/scripts');
    await flush(150);
    const $ = s => window.document.querySelector(s);
    const lastToast = () => [...window.document.querySelectorAll('.toast')].at(-1);
    const button = label => [...lastToast().querySelectorAll('.tacts button')].find(b => b.textContent === label);
    const puts = () => netlog.filter(l => l.startsWith('PUT /api/v1/apps/script/'));

    const input = $('.edtop').parentNode.parentNode.parentNode.querySelector('input[type=file]');
    Object.defineProperty(input, 'files', { value: [{ name: 'racer.ax', text: async () => NOT_FOR_ULANZI }], configurable: true });
    input.dispatchEvent(new window.Event('change'));
    await flush(60);
    assert.equal(lastToast().firstChild.textContent, MISFIT, 'importing a script that does not fit asks first');
    assert.deepEqual([...lastToast().querySelectorAll('.tacts button')].map(b => b.textContent), ['Import anyway', 'Cancel']);
    button('Cancel').click();
    await flush(40);
    assert.equal($('.edwrap textarea').value, '', 'cancel leaves the editor as it was');
    input.dispatchEvent(new window.Event('change'));
    await flush(60);
    button('Import anyway').click();
    await flush(60);
    assert.equal($('.edwrap textarea').value, NOT_FOR_ULANZI, 'import anyway loads it');
    assert.equal($('.edtop input[type=text]').value, 'Racer');

    $('.edtop button.pri').click();
    await flush(120);
    assert.deepEqual(puts(), ['PUT /api/v1/apps/script/Racer'], 'what was imported anyway saves without asking twice');

    const name = $('.edtop input[type=text]');
    name.value = 'Racer2'; name.dispatchEvent(new window.Event('input', { bubbles: true }));
    $('.edtop button.pri').click();
    await flush(60);
    assert.equal(lastToast().firstChild.textContent, MISFIT, 'saving under a new name asks');
    assert.deepEqual([...lastToast().querySelectorAll('.tacts button')].map(b => b.textContent), ['Save anyway', 'Cancel']);
    button('Cancel').click();
    await flush(60);
    assert.equal(puts().length, 1, 'cancel saves nothing');
    $('.edtop button.pri').click();
    await flush(60);
    button('Save anyway').click();
    await flush(150);
    assert.deepEqual(puts(), ['PUT /api/v1/apps/script/Racer', 'PUT /api/v1/apps/script/Racer2'], 'save anyway saves');
    assert.equal(store.scripts.get('Racer2'), NOT_FOR_ULANZI, 'the device never blocks it');
    $('.edtop button.pri').click();
    await flush(150);
    assert.equal(puts().length, 3, 'the answer holds for the next save');

    const fitting = '# @needs audio.rtttl # beeps\ndef draw() end\n';
    const ta = $('.edwrap textarea');
    ta.value = fitting; ta.dispatchEvent(new window.Event('input', { bubbles: true }));
    const toasts = window.document.querySelectorAll('.toast').length;
    $('.edtop button.pri').click();
    await flush(150);
    assert.equal(puts().length, 4, 'a script that fits saves straight away');
    assert.ok(![...window.document.querySelectorAll('.toast')].slice(toasts).some(t => /Not for this clock/.test(t.textContent)));
  } finally { window.close(); }
}

async function hubScriptsLink() {
  const { window } = await boot({ caps: { transitions: [], audio: { rtttl: true }, platform: { id: 'esp32' },
    display: { width: 32, height: 8 } } });
  try {
    await goto(window, '#/scripts');
    const link = new URL(window.document.querySelector('.fthub').href);
    assert.equal(link.pathname, '/scripts', 'the Hub lists scripts on their own page');
    assert.equal(link.searchParams.get('provider'), null);
    assert.equal(link.searchParams.get('platform'), null);
    assert.equal(link.searchParams.get('device'), 'esp32');
    assert.equal(link.searchParams.get('panel'), '32x8');
    assert.equal(link.searchParams.get('caps'), 'audio.rtttl');
  } finally { window.close(); }
}

(async () => {
  const { window } = await boot();
  try {
    vectorCases(window);
    capabilityCases(window);
  } finally { window.close(); }
  await appListShowsTheDevicesVerdict();
  await saveAndImportAskFirst();
  await hubScriptsLink();
  console.log(`script-header: ${VECTORS.cases.length} shared vectors, capability names, fit words, Hub links, ` +
    'app list and module badges, and the fit question on import and save passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
