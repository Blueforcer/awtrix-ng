/* Apps tab: the groups, and what every change sends.

   An app has three independent properties - `enabled` (it runs), `inLoop` (it
   is drawn) and `present` (it exists on the device right now). A headless
   script is enabled but never drawn; a pushed app whose sender is quiet is
   enabled and not drawn either, and it must keep its slot rather than look
   switched off. So every save states both halves outright: `order` is what
   runs, in order, and `disabled` is exactly what is switched off. Deriving
   `disabled` from what the page happens to see would switch off every app
   pushed since it loaded. There is no Save button: each change is sent at once,
   and its toast offers Undo.

   Run:  node apps-tab.test.js */
const { boot, goto, flush, stubXhr } = require('./harness');

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

const INVENTORY = [
  { name: 'Time', enabled: true, inLoop: true, slot: 0, present: true, origin: 'builtin' },
  { name: 'co2', enabled: true, inLoop: false, slot: 1, present: false, origin: null },
  { name: 'Weather', enabled: true, inLoop: true, slot: 2, present: true, origin: 'script',
    headless: false, skipped: true, config: true, error: null,
    meta: { name: 'Weather', desc: 'Forecast', icons: ['2105', '2106'] } },
  { name: 'Doorbell', enabled: true, inLoop: false, slot: 3, present: true, origin: 'script',
    headless: true, skipped: false, error: null, meta: {} },
  { name: 'Bridge', enabled: false, inLoop: false, slot: null, present: true, origin: 'script',
    headless: true, skipped: false, error: { message: 'boom', line: 3 }, meta: {} },
  { name: 'Date', enabled: false, inLoop: false, slot: null, present: true, origin: 'builtin' },
  { name: 'Racer', enabled: true, inLoop: false, slot: null, present: true, origin: 'script',
    headless: false, ondemand: true, skipped: false, config: true, error: null, meta: { name: 'Racer' } },
  { name: 'Maze', enabled: true, inLoop: true, slot: null, present: true, origin: 'script',
    headless: false, ondemand: true, skipped: false, error: null, meta: {} },
  { name: 'location', origin: 'module', import: 'location', config: true, error: null, meta: {} },
  { name: 'fmt', origin: 'module', import: 'fmt', config: false, error: null, meta: {} },
];

const CONFIG = {
  location: {
    fields: [
      { key: 'city', type: 'text', label: 'City', default: 'Berlin', value: 'Wien' },
    ],
    warnings: [],
  },
  Weather: {
    fields: [
      { key: 'city', type: 'text', label: 'City', maxlen: 16,
        default: 'Berlin', value: 'Rom' },
      { key: 'metric', type: 'bool', label: 'Celsius', default: true, value: false },
      { key: 'every', type: 'number', label: 'Refresh', unit: 'min',
        min: 1, max: 60, default: 15, value: 30 },
      { key: 'mode', type: 'select', label: 'Show',
        options: ['now', 'today'], default: 'now', value: 'today' },
      { key: 'tint', type: 'color', label: 'Colour', default: 16746496, value: 65280 },
    ],
    warnings: ['line 9: unknown type \'boolean\''],
  },
};

async function run() {
  const { window, store } = await boot();
  const doc = window.document;
  store.apps = INVENTORY;
  store.configs = CONFIG;
  await goto(window, '#/apps');

  const group = id => doc.getElementById('apps-' + id);
  const shown = id => !group(id).hidden;
  const names = id => [...group(id).querySelectorAll('.approw .nmt')].map(n => n.textContent);
  const rowFor = name => [...doc.querySelectorAll('.approw')]
    .find(r => r.querySelector('.nmt') && r.querySelector('.nmt').textContent === name);
  // Rarer actions live behind the row's ⋯ menu, labelled with words.
  const btn = (row, label) => {
    const m = row.querySelector('.rowmenu .mbtn');
    if (m) m.click();
    return [...row.querySelectorAll('.rowmenu .mlist > button')]
      .find(b => b.textContent.trim() === label);
  };
  // Frequent ones sit in the row itself.
  const act = (row, label) => [...row.querySelectorAll('.racts > button')]
    .find(b => b.textContent.trim().endsWith(label));
  const switchOf = row => row.querySelector('.racts .switch input');
  const flip = async row => {
    const s = switchOf(row);
    s.checked = !s.checked;
    s.dispatchEvent(new window.Event('change', { bubbles: true }));
    await flush(40);
  };
  const lastToast = () => [...doc.querySelectorAll('#toasts .toast')].pop();
  const undo = () => [...lastToast().querySelectorAll('button')].find(b => b.textContent === 'Undo');

  // ---- where each app lands ----------------------------------------------
  assert(names('loop').join(',') === 'Time,co2,Weather',
    'the display group holds the drawn apps in order, then the enabled ones nothing is sending');
  assert(names('od').join(',') === 'Maze,Racer', 'the device menu group holds the on-demand scripts');
  assert(names('bg').join(',') === 'Doorbell', 'the background group holds the running headless script');
  assert(names('off').sort().join(',') === 'Bridge,Date', 'switched off holds everything that does not run');
  assert(group('loop').tagName === 'SECTION' && !group('loop').querySelector('summary'),
    'the display group stays visible without a collapse control');
  for (const id of ['od', 'bg', 'off', 'mod']) {
    const card = group(id), summary = card.querySelector('summary');
    assert(card.tagName === 'DETAILS' && !card.open,
      id + ' starts as a closed native details group');
    assert(summary && summary.classList.contains('appgrphead') &&
           doc.getElementById(summary.getAttribute('aria-labelledby')) === card.querySelector('h2') &&
           doc.getElementById(summary.getAttribute('aria-describedby')) === card.querySelector('.ghelp'),
      id + ' has the common named header with count and help');
  }
  assert(names('mod').join(',') === 'location',
    'shared settings hold the modules with settings, and leave out plain library code');
  assert(!doc.querySelector('#savebar'), 'there is no save bar to forget');

  const count = id => group(id).querySelector('h2 .cnt').textContent;
  assert(count('loop') === '3' && count('od') === '2' && count('bg') === '1' && count('off') === '2',
    'each group heading counts its apps');
  for (const id of ['od', 'bg', 'off']) group(id).querySelector('summary').click();
  assert(group('od').open && group('bg').open && group('off').open && !group('mod').open,
    'each summary opens its own group independently');
  const groupCards = Object.fromEntries(['od', 'bg', 'off', 'mod'].map(id => [id, group(id)]));

  // ---- state chips explain themselves ------------------------------------
  const chip = (row, text) => [...row.querySelectorAll('.chip')].find(c => c.textContent === text);
  assert(!!chip(rowFor('co2'), 'no data'), 'an enabled app nobody is sending is marked, not dropped');
  assert(!!chip(rowFor('Weather'), 'skipped'), 'a script that sat this round out says so');
  assert(!!chip(rowFor('Maze'), 'running'), 'an on-demand script that runs says so');
  chip(rowFor('co2'), 'no data').click();
  await flush(20);
  assert(rowFor('co2').querySelector('.rowinfo').textContent.includes('No data yet'),
    'tapping a chip shows what it means');
  chip(rowFor('Bridge'), 'error').click();
  await flush(20);
  assert(rowFor('Bridge').querySelector('.rowinfo.bad').textContent.includes('ERR:Bridge'),
    'an error chip opens the message');
  assert(rowFor('Weather').querySelector('.sub').textContent.startsWith('Script'),
    'each row says what kind of app it is');

  // ---- every change is saved at once, and can be undone ------------------
  await flip(rowFor('Weather'));
  assert(JSON.stringify(store.order) ===
         JSON.stringify({ order: ['Time', 'co2', 'Doorbell'], disabled: ['Bridge', 'Date', 'Weather'] }),
    'switching an app off saves at once, naming everything that is off');
  assert(names('off').includes('Weather'), 'and moves it to switched off');
  assert(Object.entries(groupCards).every(([id, card]) => group(id) === card) &&
         group('od').open && group('bg').open && group('off').open && !group('mod').open,
    'an order change preserves every group container and its independent open state');
  assert(lastToast().textContent.includes('Weather switched off') && !!undo(),
    'the toast names what happened and offers Undo');
  undo().click();
  await flush(40);
  assert(JSON.stringify(store.order.order) === JSON.stringify(['Time', 'co2', 'Weather', 'Doorbell']),
    'Undo puts it back in its old place and saves that');
  assert(names('loop').join(',') === 'Time,co2,Weather', 'on the page too');

  await flip(rowFor('Date'));
  assert(names('loop').join(',') === 'Time,co2,Weather,Date', 'switching an app on adds it to the end of the display');
  await flip(rowFor('Bridge'));
  assert(names('bg').join(',') === 'Bridge,Doorbell', 'a headless script goes back to the background');
  assert(JSON.stringify(store.order.order) ===
         JSON.stringify(['Time', 'co2', 'Weather', 'Date', 'Bridge', 'Doorbell']) &&
         store.order.disabled.length === 0,
    'the body carries the display in order, then the background scripts');
  assert(!shown('off'), 'the switched-off group hides once it is empty');

  await flip(rowFor('Doorbell'));
  assert(store.order.disabled.join(',') === 'Doorbell', 'a background script switches off the same way');
  assert(shown('off') && group('off').open,
    'a group keeps its open state when it disappears empty and later has an app again');
  await flip(rowFor('Doorbell'));

  // ---- order ----------------------------------------------------------------
  assert(!btn(rowFor('Time'), 'Move up'), 'the first app cannot move up');
  btn(rowFor('Time'), 'Move down').click();
  await flush(40);
  assert(store.order.order.slice(0, 2).join(',') === 'co2,Time', 'Move down swaps it with the next and saves');
  btn(rowFor('Time'), 'Move up').click();
  await flush(40);
  btn(rowFor('Time'), 'Duplicate').click();
  await flush(40);
  assert(store.order.order.slice(0, 2).join(',') === 'Time,Time', 'Duplicate shows an app twice per round');
  assert(!!rowFor('Time').querySelector('.grip'), 'display rows can be dragged');
  assert(!rowFor('Doorbell').querySelector('.grip'), 'other rows cannot');

  // ---- show and start --------------------------------------------------------
  assert(!!rowFor('Racer').querySelector('.cfgbtn') && !rowFor('Maze').querySelector('.cfgbtn'),
    'optional settings never add a placeholder beside the primary action');
  act(rowFor('Weather'), 'Show').click();
  await flush(30);
  assert(store.active && store.active.name === 'Weather', 'Show puts the app on the display');
  const racer = rowFor('Racer');
  assert(!switchOf(racer) && !btn(racer, 'Duplicate'), 'an on-demand script has no switch and no rotation actions');
  act(racer, 'Start').click();
  await flush(30);
  assert(store.active.name === 'Racer', 'Start runs it on the device');
  assert(!store.order.order.includes('Racer') && !store.order.disabled.includes('Racer'),
    'and it never lands in the saved order');

  // ---- delete ------------------------------------------------------------------
  assert(!btn(rowFor('Time'), 'Delete'), 'a built-in cannot be deleted - only switched off');
  assert(!btn(rowFor('Weather'), 'Delete'), 'nor a script - that belongs in its editor');
  assert(!!btn(rowFor('Weather'), 'Edit'), 'a script row offers its editor by name');
  const rmco2 = btn(rowFor('co2'), 'Delete');
  rmco2.click();
  await flush(20);
  assert(names('loop').includes('co2'), 'one click only arms it - deleting takes two');
  rmco2.click();
  await flush(60);
  assert(!names('loop').includes('co2'), 'deleting takes the row out of the list');
  assert(!store.order.order.includes('co2') && !store.order.disabled.includes('co2'),
    'and the saved order drops the name');

  // ---- settings a script declares --------------------------------------
  const gearOf = row => row.querySelector('.racts > .cfgbtn');
  assert(!!gearOf(rowFor('Weather')), 'a script with settings offers Settings');
  assert(!gearOf(rowFor('Doorbell')), 'a script without settings does not');
  assert(!gearOf(rowFor('Time')), 'an app without configurable fields has no Settings button');

  assert(!!btn(rowFor('Weather'), 'Install icons'), 'a script that names icons offers to fetch them');
  assert(!btn(rowFor('Doorbell'), 'Install icons'), 'a script that names none does not');

  window.localStorage.awtrixHubToken = 'apps-test-token';
  store.iconBytes = { '2105': 'GIF89a-2105', '2106': 'GIF89a-2106' };
  store.files['/ICONS'].set('2105.gif', 1);
  const uploads = [];
  stubXhr(window, uploads, store);
  btn(rowFor('Weather'), 'Install icons').click();
  await flush(200);
  assert(uploads.map(u => u.files.map(f => f.name).join('')).join(',') === '2106.gif',
    'and fetches only what the clock is missing');

  const panel = rowFor('Weather').querySelector('.appcfg');
  assert(!!panel && panel.hidden, 'the panel starts closed and unfetched');

  gearOf(rowFor('Weather')).click();
  await flush(60);
  assert(!panel.hidden, 'the Settings button opens the panel');

  const ctl = key => {
    const row = [...panel.querySelectorAll('.frow')]
      .find(r => r.querySelector('.key') && r.querySelector('.key').textContent === key);
    return row && row.querySelector('.ctl');
  };
  assert(ctl('city').querySelector('input[type=text]').value === 'Rom',
    'text renders as a text box holding the stored value');
  assert(ctl('metric').querySelector('input[type=checkbox]').checked === false,
    'bool renders as a switch');
  assert(ctl('every').querySelector('input[type=number]').value === '30' &&
         ctl('every').textContent.includes('min'),
    'number renders with its unit');
  assert(ctl('mode').querySelector('select').value === 'today',
    'select renders with the stored option chosen');
  assert(ctl('tint').querySelector('input[type=color]').value === '#00ff00',
    'colour arrives as a number and reaches the picker as hex');
  assert(panel.querySelector('.badge.bad').textContent.includes('line 9'),
    'a warning from the header is shown above the fields');

  const cfgBtn = label => [...panel.querySelectorAll('.cfgbar button')]
    .find(b => b.textContent.trim() === label || b.title === label);
  assert(cfgBtn('Save').disabled, 'Save is off until something changes');

  ctl('city').querySelector('input[type=text]').value = 'Wien';
  ctl('city').querySelector('input[type=text]')
    .dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(20);
  assert(!cfgBtn('Save').disabled, 'editing a field arms Save');

  store.configPatch = null;
  cfgBtn('Save').click();
  await flush(60);
  assert(JSON.stringify(store.configPatch) === JSON.stringify({ city: 'Wien' }),
    'PATCH carries only the field that changed');
  assert(cfgBtn('Save').disabled, 'Save goes quiet again once the values are saved');

  cfgBtn('Reset to defaults').click();
  await flush(20);
  assert(ctl('city').querySelector('input[type=text]').value === 'Berlin' &&
         ctl('tint').querySelector('input[type=color]').value === '#ff8800',
    'the defaults button fills every field from its declared default');
  assert(!cfgBtn('Save').disabled, 'and leaves them unsaved, so it can be undone');

  cfgBtn('Discard').click();
  await flush(20);
  assert(ctl('city').querySelector('input[type=text]').value === 'Wien',
    'Discard returns to the last saved values, not the defaults');

  // The device clamps a number to its range, so the panel has to re-read what
  // was stored - otherwise it keeps showing a value the device never took.
  ctl('every').querySelector('input[type=number]').value = '900';
  ctl('every').querySelector('input[type=number]')
    .dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(20);
  cfgBtn('Save').click();
  await flush(80);
  assert(ctl('every').querySelector('input[type=number]').value === '60',
    'after saving, the panel shows what the device actually stored');
  assert(cfgBtn('Save').disabled, 'and reads as saved, not dirty');

  // Touching another app rebuilds every row. An open panel holding unsaved typing
  // must not be one of the casualties.
  ctl('city').querySelector('input[type=text]').value = 'Graz';
  ctl('city').querySelector('input[type=text]')
    .dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(20);
  btn(rowFor('Time'), 'Duplicate').click();
  await flush(40);
  const after = rowFor('Weather').querySelector('.appcfg');
  assert(after === panel, 'the panel survives a re-render of the rows');
  assert(!after.hidden, 'and stays open');
  assert(ctl('city').querySelector('input[type=text]').value === 'Graz',
    'with the unsaved edit still in it');

  const closeBtn = gearOf(rowFor('Weather'));
  assert(closeBtn.title === 'Close settings', 'the settings button closes an open panel');
  assert(!btn(rowFor('Weather'), 'Settings'), 'settings is absent from the dropdown');
  closeBtn.click();
  await flush(20);
  assert(panel.hidden, 'and closes it again');

  // ---- settings a module owns --------------------------------------------
  assert(!!gearOf(rowFor('location')), 'a module with settings offers Settings too');
  assert(!rowFor('fmt'), 'a module without settings is not on the Apps tab');
  assert(!switchOf(rowFor('location')), 'a module has no switch');

  group('mod').querySelector('summary').click();
  gearOf(rowFor('location')).click();
  await flush(60);
  const modPanel = rowFor('location').querySelector('.appcfg');
  assert(!!modPanel && !modPanel.hidden, 'and it opens the module panel');
  assert([...modPanel.querySelectorAll('.frow .key')].map(e => e.textContent).join(',') === 'city',
    'and it holds what the module declared');
  const modCtl = modPanel.querySelector('.frow .ctl input[type=text]');
  modCtl.value = 'Graz';
  modCtl.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(20);
  const modSave = [...modPanel.querySelectorAll('.cfgbar button')]
    .find(b => b.textContent.trim() === 'Save');
  group('mod').querySelector('summary').click();
  btn(rowFor('Time'), 'Duplicate').click();
  await flush(40);
  assert(!group('mod').open && rowFor('location').querySelector('.appcfg') === modPanel &&
         !modPanel.hidden && modCtl.value === 'Graz' && !modSave.disabled,
    'collapsing a group and re-rendering rows preserves its open dirty config panel');
  group('mod').querySelector('summary').click();
  assert(group('mod').open && rowFor('location').querySelector('.appcfg') === modPanel &&
         modCtl.value === 'Graz' && !modSave.disabled,
    'expanding the group reveals the same unsaved settings');
  modSave.click();
  await flush(60);
  assert(JSON.stringify(store.configPatch) === '{"city":"Graz"}',
    'saving a module setting patches the module by name');

  window.close();
  console.log(failures === 0 ? '\nALL PASS' : `\n${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
}

run().catch(e => { console.error(e); process.exit(2); });
