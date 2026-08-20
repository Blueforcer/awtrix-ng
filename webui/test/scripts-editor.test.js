/* Script editor: the "unsaved changes" state must track real edits, not tab
   switches.

   Regression guard for the bug where creating+saving a script, leaving the
   Scripts tab and coming back made the editor falsely report unsaved changes.
   The symptom is `.edtop.mod` (the dirty dot) being set, which is what isDirty()
   reads and what fires the "unsaved changes" toast on "+"/open-another.

   Run:  node scripts-editor.test.js         (in-memory mock, offline)
         node scripts-editor.test.js --sim   (against a sim on :8080) */
const { boot, bootSim, goto, flush, stubXhr } = require('./harness');
const USE_SIM = !!process.env.SIM || process.argv.includes('--sim');

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

const start = () => (USE_SIM ? bootSim() : boot());
const q = window => (s => window.document.querySelector(s));
const isDirty = window => window.document.querySelector('.edtop').classList.contains('mod');

// Type into the editor the way a user would: append text + fire an input event.
async function typeInto(window, ta, text) {
  ta.value = ta.value + text;
  ta.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(40);
}

async function newSavedScript(window) {
  const $ = q(window);
  $('.filetree .ftpane:not(.mods) .ftgrp button.pri').click(); // "+" -> template
  await flush(40);
  const saveBtn = [...$('.edtop').querySelectorAll('button')].find(b => b.classList.contains('pri'));
  saveBtn.click();
  await flush(80);
  return { name: $('.edtop input[type=text]').value, src: $('.edwrap textarea').value };
}

// A saved script must read as clean after a detour to another tab and back.
async function scenarioStaysClean() {
  console.log('\nScenario A: saved script stays clean across a tab switch');
  const { window } = await start();
  const $ = q(window);

  await goto(window, '#/scripts');
  assert(!isDirty(window), 'empty editor starts clean');

  const saved = await newSavedScript(window);
  assert(!isDirty(window), 'after save the editor is clean');

  await goto(window, '#/');        // leave to dashboard
  await goto(window, '#/scripts'); // come back

  assert($('.edtop input[type=text]').value === saved.name, 'name restored on return');
  assert($('.edwrap textarea').value === saved.src, 'source restored on return');
  assert(!isDirty(window), 'returning to a saved script does NOT report unsaved changes');
}

// A genuine unsaved edit must survive the detour and still read as dirty.
async function scenarioDirtySurvives() {
  console.log('\nScenario B: an unsaved edit survives the tab switch and stays dirty');
  const { window } = await start();
  const $ = q(window);

  await goto(window, '#/scripts');
  await newSavedScript(window);

  const ta = $('.edwrap textarea');
  await typeInto(window, ta, '\n# edited but not saved');
  const editedSrc = ta.value;
  assert(isDirty(window), 'editing marks the buffer dirty');

  await goto(window, '#/');
  await goto(window, '#/scripts');

  assert($('.edwrap textarea').value === editedSrc, 'unsaved edit survived the detour');
  assert(isDirty(window), 'restored unsaved edit still reads as dirty');
}

async function scenarioIconButton() {
  console.log('\nScenario C: the @icons button installs what the clock is missing');
  if (USE_SIM) { console.log('  SKIP: needs the mocked icon database'); return; }
  const { window, store } = await boot();
  const $ = q(window);
  store.iconBytes = { '2105': 'GIF89a-2105', '2106': 'GIF89a-2106' };
  store.files['/ICONS'].set('2105.gif', 1);
  const uploads = [];
  stubXhr(window, uploads, store);

  await goto(window, '#/scripts');
  const btn = () => [...$('.edtop .sec').querySelectorAll('button')]
    .find(b => b.querySelector('use') && b.querySelector('use').getAttribute('href') === '#i-image');

  assert(btn().style.display === 'none', 'no @icons line, no button');

  const ta = $('.edwrap textarea');
  await typeInto(window, ta, '# @icons 2105, 2106 4242\ndef draw() end\n');
  await flush(60);

  assert(btn().style.display !== 'none', 'an @icons line shows the button');
  assert(btn().querySelector('.cnt').textContent === '2', 'the badge counts only the missing ones');

  btn().click();
  await flush(200);

  const names = uploads.map(u => u.files.map(f => f.name).join('')).sort();
  assert(names.join(',') === '2106.gif', 'only the missing id was fetched and uploaded');
  assert(store.files['/ICONS'].has('2106.gif'), 'the icon landed on the clock');
  assert(btn().querySelector('.cnt').textContent === '1', 'an id the database does not have stays missing');
  assert(!btn().disabled, 'and the button stays pressable for another try');
}

async function main() {
  console.log('Backend: ' + (USE_SIM ? 'SIMULATOR (http://localhost:8080)' : 'in-memory mock'));
  await scenarioStaysClean();
  await scenarioDirtySurvives();
  await scenarioIconButton();
  console.log(failures === 0 ? '\nALL PASS' : `\n${failures} FAILURE(S)`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch(e => { console.error(e); process.exit(2); });
