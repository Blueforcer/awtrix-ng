/* Script data: the Data tab of the Scripts editor shows what a script stored,
   as JSON, and sends back only what the user changed.

   Run:  node script-data.test.js   (in-memory mock, offline) */
const { boot, goto, flush } = require('./harness');

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

const $ = (w, s) => w.document.querySelector(s);
const tabs = w => $(w, '.edtabs');
const tabButton = (w, i) => tabs(w).querySelectorAll('button')[i];
const dataText = w => $(w, '.eddata textarea');
const isDirty = w => $(w, '.edtop').classList.contains('mod');
const saveBtn = w => [...$(w, '.edtop').querySelectorAll('button')].find(b => b.classList.contains('pri'));

async function edit(w, text) {
  const ta = dataText(w);
  ta.value = text;
  ta.dispatchEvent(new w.Event('input', { bubbles: true }));
  await flush(40);
}

async function open(w, name) {
  [...w.document.querySelectorAll('.ftitem')]
    .find(i => i.querySelector('.nm')?.textContent === name).click();
  await flush(80);
}

async function withGame(data, extra = {}) {
  const { window, store } = await boot();
  store.scripts.set('Game', '# @name Game\nreturn nil');
  store.data.Game = data;
  for (const [n, src] of Object.entries(extra)) store.scripts.set(n, src);
  await goto(window, '#/scripts');
  await flush(100);
  return { window, store };
}

async function scenarioTabBelongsToASavedScript() {
  console.log('\nScenario A: the Data tab belongs to a saved script');
  const { window } = await withGame({ unl: 7, best: [12, 40] });
  assert(tabs(window).style.display === 'none', 'no Data tab while no script is open');
  await open(window, 'Game');
  assert(tabs(window).style.display !== 'none', 'a saved script shows the Data tab');
  assert(tabButton(window, 1).textContent.includes('2'), 'the tab counts the stored keys');
  $(window, '.filetree .ftpane:not(.mods) .ftgrp button.pri').click();
  await flush(80);
  assert(tabs(window).style.display === 'none', 'a new, unsaved script has no Data tab');
  window.close();
}

async function scenarioInvalidJsonIsCaught() {
  console.log('\nScenario B: invalid JSON is caught before anything is sent');
  const { window, store } = await withGame({ unl: 7 });
  await open(window, 'Game');
  tabButton(window, 1).click();
  await flush(60);
  assert(window.document.querySelector('.edwrap').classList.contains('data'), 'the Data tab opens');
  assert(dataText(window).value.includes('"unl": 7'), 'the editor shows the stored values');

  await edit(window, '{\n  "unl": 7,\n  "best": [1,\n}');
  const err = $(window, '.eddata .ederr');
  assert(err.style.display !== 'none', 'broken JSON shows the error box');
  assert($(window, '.eddata .edbar .end').classList.contains('bad'), 'and turns the status red');
  saveBtn(window).click();
  await flush(60);
  assert(store.dataPatch === null, 'nothing is sent while the JSON is broken');

  await edit(window, '[1,2]');
  saveBtn(window).click();
  await flush(60);
  assert(err.style.display !== 'none' && store.dataPatch === null, 'a root that is not an object is refused too');

  await edit(window, '{"unl": 8}');
  assert(err.style.display === 'none', 'fixing the JSON clears the error');
  window.close();
}

async function scenarioOnlyChangesAreSent() {
  console.log('\nScenario C: only what changed is sent');
  const { window, store } = await withGame({ unl: 7, best: [12, 40], cont: 3 });
  await open(window, 'Game');
  tabButton(window, 1).click();
  await flush(60);
  await edit(window, '{\n  "unl": 9,\n  "cont": 3\n}');
  assert(isDirty(window), 'an edit marks the script as unsaved');
  saveBtn(window).click();
  await flush(120);
  assert(JSON.stringify(store.dataPatch) === '{"unl":9,"best":null}',
    'the changed key and the removed one are sent, the untouched one is not');
  assert(dataText(window).value.includes('"unl": 9') && !dataText(window).value.includes('best'),
    'the editor then shows what the device holds');
  assert(!isDirty(window), 'and the script reads as saved');
  window.close();
}

async function scenarioUnsavedDataIsKeptAndGuarded() {
  console.log('\nScenario D: unsaved data survives a tab switch and guards a script switch');
  const { window } = await withGame({ unl: 7 }, { Other: '# @name Other\nreturn nil' });
  await open(window, 'Game');
  tabButton(window, 1).click();
  await flush(60);
  await edit(window, '{"unl": 1}');
  tabButton(window, 0).click();
  await flush(40);
  tabButton(window, 1).click();
  await flush(60);
  assert(dataText(window).value === '{"unl": 1}', 'switching to Code and back keeps the draft');

  await open(window, 'Other');
  assert($(window, '.edtop input[type=text]').value === 'Game', 'opening another script does not drop it');
  assert(!!$(window, '#toasts .toast .tacts'), 'but asks what to do with it');
  window.close();
}

async function renameTo(w, from, to) {
  const item = [...w.document.querySelectorAll('.ftitem')].find(i => i.querySelector('.nm')?.textContent === from);
  item.querySelector('button[title="Rename"]').click();
  const input = item.querySelector('input.ftren');
  input.value = to;
  input.dispatchEvent(new w.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
  await flush(300);
}

async function scenarioRenameKeepsDataAndSettings() {
  console.log('\nScenario E: a renamed script keeps what it stored and its settings');
  const { window, store } = await withGame({ unl: 7, best: [1, 2] });
  const speed = value => ({ fields: [{ key: 'speed', type: 'number', default: 3, value }] });
  store.configs.Game = speed(5);
  store.configs.Rally = speed(3);
  await renameTo(window, 'Game', 'Rally');
  assert(store.scripts.has('Rally') && !store.scripts.has('Game'), 'the script has its new name');
  assert(JSON.stringify(store.data.Rally) === '{"unl":7,"best":[1,2]}', 'its stored data moved along');
  assert(JSON.stringify(store.configPatch) === '{"speed":5}', 'and so did the setting the user changed');

  store.data.Plain = {};
  store.scripts.set('Plain', '# @name Plain\nreturn nil');
  store.configPatch = null;
  store.dataPatch = null;
  await goto(window, '#/');
  await goto(window, '#/scripts');
  await flush(100);
  await renameTo(window, 'Plain', 'Plain2');
  assert(store.scripts.has('Plain2') && store.dataPatch === null && store.configPatch === null,
    'a script that stored nothing is renamed without writing any data');
  window.close();
}

async function scenarioRenameFollowsTheNewSettings() {
  console.log('\nScenario F: a rename with edited settings keeps only what still fits');
  const { window, store } = await withGame({ unl: 7 });
  store.configs.Game = { fields: [
    { key: 'speed', type: 'number', default: 3, value: 5 },
    { key: 'city', type: 'text', default: 'Rom', value: 'Wien' },
    { key: 'mode', type: 'select', default: 'a', value: 'b', options: ['a', 'b'] }] };
  store.configs.Rally = { fields: [
    { key: 'speed', type: 'text', default: 'x', value: 'x' },
    { key: 'mode', type: 'select', default: 'a', value: 'a', options: ['a', 'c'] },
    { key: 'unl', type: 'number', default: 1, value: 1 }] };
  await renameTo(window, 'Game', 'Rally');
  assert(JSON.stringify(store.configPatch) === '{"unl":7}',
    'a stored value the new source declares becomes its setting; a changed type, a lost option and a dropped setting are not sent');
  assert(store.dataPatch === null, 'nothing is left over for the data');
  window.close();
}

async function main() {
  await scenarioTabBelongsToASavedScript();
  await scenarioInvalidJsonIsCaught();
  await scenarioOnlyChangesAreSent();
  await scenarioUnsavedDataIsKeptAndGuarded();
  await scenarioRenameKeepsDataAndSettings();
  await scenarioRenameFollowsTheNewSettings();
  console.log(failures === 0 ? '\nALL PASS' : `\n${failures} FAILURE(S)`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch(e => { console.error(e); process.exit(2); });
