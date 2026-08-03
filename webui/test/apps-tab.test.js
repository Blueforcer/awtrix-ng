/* Apps tab: three cards, and what Save sends.

   An app has three independent properties - `enabled` (it runs), `inLoop` (it
   is drawn) and `present` (it exists on the device right now). A headless
   script is enabled but never drawn; a pushed app whose sender is quiet is
   enabled and not drawn either, and it must keep its slot rather than look
   switched off. So Save states both halves outright: `order` is what runs, in
   order, and `disabled` is exactly what is switched off. Deriving `disabled` from
   what the page happens to see would switch off every app pushed since it
   loaded.

   Run:  node apps-tab.test.js */
const { boot, goto, flush } = require('./harness');

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

const INVENTORY = [
  { name: 'Time', enabled: true, inLoop: true, slot: 0, present: true, origin: 'builtin' },
  { name: 'co2', enabled: true, inLoop: false, slot: 1, present: false, origin: null },
  { name: 'Weather', enabled: true, inLoop: true, slot: 2, present: true, origin: 'script',
    headless: false, skipped: false, config: true, error: null, meta: {} },
  { name: 'Doorbell', enabled: true, inLoop: false, slot: 3, present: true, origin: 'script',
    headless: true, skipped: false, error: null, meta: {} },
  { name: 'Bridge', enabled: false, inLoop: false, slot: null, present: true, origin: 'script',
    headless: true, skipped: false, error: null, meta: {} },
  { name: 'Date', enabled: false, inLoop: false, slot: null, present: true, origin: 'builtin' },
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

const rowFor2 = (window, name) => [...window.document.querySelectorAll('.approw')]
  .find(r => r.querySelector('.nm') && r.querySelector('.nm').firstChild.textContent === name);
const cards = window => [...window.document.querySelectorAll('.card.wide')];
const cardRows = (window, i) =>
  [...cards(window)[i].querySelectorAll('.approw .nm')].map(n => n.firstChild.textContent);
const visible = card => card.style.display !== 'none';

async function run() {
  const { window, store } = await boot();
  store.apps = INVENTORY;
  store.configs = CONFIG;
  await goto(window, '#/apps');

  const [rotation, background, disabled] = cards(window);

  assert(cardRows(window, 0).join(',') === 'Time,co2,Weather',
    'rotation holds the drawn apps in order, then the enabled ones nothing is sending');
  assert(cardRows(window, 1).join(',') === 'Doorbell', 'background holds the running headless script');
  assert(cardRows(window, 2).sort().join(',') === 'Bridge,Date',
    'disabled holds everything switched off, headless or not');
  assert(visible(background), 'background card shows while a script runs in it');

  const co2Row = [...cards(window)[0].querySelectorAll('.approw')]
    .find(r => r.querySelector('.nm').firstChild.textContent === 'co2');
  assert([...co2Row.querySelectorAll('.chip')].some(c => c.textContent === 'no data'),
    'an enabled app nobody is sending is marked, not silently dropped');

  // A menu starts closed. The `hidden` property alone does not do that: any
  // author `display` rule on the list beats what `hidden` asks the browser for,
  // so this asserts the computed style, not the flag.
  assert(window.getComputedStyle(
    rowFor2(window, 'Time').querySelector('.rowmenu .mlist')).display === 'none',
    'a row menu starts closed');

  const rowsOf = card => [...card.querySelectorAll('.approw')];
  // Every row action lives behind the row's menu now, labelled with words.
  const btn = (row, label) => {
    const m = row.querySelector('.rowmenu .mbtn');
    if (m) m.click();
    return [...row.querySelectorAll('.rowmenu .mlist > button')]
      .find(b => b.textContent.trim() === label);
  };

  assert(!!btn(rowsOf(background)[0], 'Deactivate'),
    'a background row deactivates with the same menu entry as a loop row');
  assert(!btn(rowsOf(background)[0], 'Duplicate'),
    'a background row offers no rotation-only action');

  const moduleCard = cards(window)[3];
  assert(cardRows(window, 3).join(',') === 'location',
    'the module card holds the modules with settings, and leaves out plain library code');
  assert(visible(moduleCard), 'module card shows while such a module is installed');
  const modRow = rowsOf(moduleCard)[0];
  assert(!btn(modRow, 'Activate') && !btn(modRow, 'Deactivate') &&
         !btn(modRow, 'Show now'),
    'a module row offers no rotation actions');
  assert(modRow.textContent.includes('import location'),
    'a module row names what import finds it');

  // + on a disabled row lands the app in the card its own headless flag picks.
  const bridgeRow = rowsOf(disabled).find(r => r.querySelector('.nm').firstChild.textContent === 'Bridge');
  btn(bridgeRow, 'Activate').click();
  await flush(30);
  assert(cardRows(window, 1).join(',') === 'Bridge,Doorbell', 'activating a headless script goes to background');

  const dateRow = rowsOf(cards(window)[2]).find(r => r.querySelector('.nm').firstChild.textContent === 'Date');
  btn(dateRow, 'Activate').click();
  await flush(30);
  assert(cardRows(window, 0).join(',') === 'Time,co2,Weather,Date', 'activating a normal app goes to the rotation');

  const save = async () => {
    store.order = null;
    window.document.querySelector('#savebar button.pri').click();
    await flush(60);
  };

  await save();
  assert(!!store.order, 'Save reaches PUT /api/v1/apps/order');
  assert(JSON.stringify(store.order.order) ===
         JSON.stringify(['Time', 'co2', 'Weather', 'Date', 'Bridge', 'Doorbell']),
    'the body carries the rotation in order, then the running background scripts');
  assert(JSON.stringify(store.order.disabled) === JSON.stringify([]),
    'nothing is switched off, and the body says so instead of leaving it to be guessed');

  // Saving reloads from the device, so the cards are back to INVENTORY here.
  for (const row of rowsOf(cards(window)[1])) { btn(row, 'Deactivate').click(); await flush(20); }
  assert(!visible(cards(window)[1]), 'background card hides once nothing runs in it');

  await save();
  assert(JSON.stringify(store.order.order) === JSON.stringify(['Time', 'co2', 'Weather']),
    'an app nobody is sending keeps its slot in the body');
  assert(store.order.disabled.slice().sort().join(',') === 'Bridge,Date,Doorbell',
    'disabled names every switched-off app, which is what stops a background script');
  assert(!store.order.disabled.includes('co2'),
    'and never an app that is merely absent');

  // Two actions and no third: switch an app off, or delete it. Delete means gone
  // from RAM AND gone from the arrangement - including a name you only mistyped,
  // where there is nothing in RAM to remove in the first place.
  await goto(window, '#/apps');
  const del = row => btn(row, 'Delete');
  assert(!!del(rowFor2(window, 'co2')), 'a name nothing is sending can be deleted');
  assert(!del(rowFor2(window, 'Time')), 'a built-in cannot - it can only be switched off');
  assert(!del(rowFor2(window, 'Weather')), 'nor a script - that belongs in its editor');
  assert(!!btn(rowFor2(window, 'Weather'), 'Edit'), 'a script row offers its editor by name');
  const rmco2 = del(rowFor2(window, 'co2'));
  rmco2.click();
  await flush(20);
  assert(cardRows(window, 0).includes('co2'),
    'one click only arms it - deleting takes two, like every destructive button here');
  rmco2.click();
  await flush(40);
  assert(!cardRows(window, 0).includes('co2'), 'deleting takes the row out of the list');
  await save();
  assert(!store.order.order.includes('co2') && !store.order.disabled.includes('co2'),
    'and Save leaves the name out of both lists, so the device drops it');

  // ---- settings a script declares --------------------------------------
  // Settings is the only entry point, and its absence is information: an app
  // with nothing to set must not offer it.
  await goto(window, '#/apps');
  const rowFor = name => [...window.document.querySelectorAll('.approw')]
    .find(r => r.querySelector('.nm') &&
               r.querySelector('.nm').firstChild.textContent === name);
  const gearOf = row => btn(row, 'Settings');

  assert(!!gearOf(rowFor('Weather')), 'a script with settings offers Settings');
  assert(!gearOf(rowFor('Doorbell')), 'a script without settings does not');
  assert(!gearOf(rowFor('Time')), 'a built-in never does');

  const panel = rowFor('Weather').querySelector('.appcfg');
  assert(!!panel && panel.hidden, 'the panel starts closed and unfetched');

  gearOf(rowFor('Weather')).click();
  await flush(60);
  assert(!panel.hidden, 'the Settings entry opens the panel');

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

  cfgBtn('Use defaults').click();
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

  // While the panel is open the row's menu button IS the close button, so
  // getting back out is one click and not a trip through the menu.
  const closeBtn = rowFor('Weather').querySelector('.rowmenu .mbtn');
  assert(closeBtn.title === 'Close settings', 'the menu button becomes a close button');
  closeBtn.click();
  await flush(20);
  assert(panel.hidden, 'and closes it again');
  assert(rowFor('Weather').querySelector('.rowmenu .mbtn').title === 'Actions',
    'and the menu is back afterwards');

  // ---- settings a module owns --------------------------------------------
  // Several apps share one value by importing the module that holds it, so the
  // module gets the same Settings entry an app does - and one without settings does
  // not appear on this tab at all.
  assert(!!gearOf(rowFor('location')), 'a module with settings offers Settings too');
  assert(!rowFor('fmt'), 'a module without settings is not on the Apps tab');

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
  modSave.click();
  await flush(60);
  assert(JSON.stringify(store.configPatch) === '{"city":"Graz"}',
    'saving a module setting patches the module by name');

  window.close();
  console.log(failures === 0 ? '\nALL PASS' : `\n${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
}

run().catch(e => { console.error(e); process.exit(2); });
