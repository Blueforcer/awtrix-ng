const { boot, goto, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  FAIL: ' + msg); }
}
const app = (name, enabled = true, config = true) => ({
  name, origin: 'builtin', enabled, inLoop: enabled, present: true, config,
});
const field = (key, type, value, extra = {}) => ({
  key, type, value, default: value, path: key.split('.'), ...extra,
});
const scriptField = (key, type, value, extra = {}) => ({
  key, type, value, default: value, label: key, ...extra,
});
const rowsFor = (doc, name) => [...doc.querySelectorAll('.approw')]
  .filter(r => r.querySelector('.nmt')?.textContent === name);
const rowFor = (doc, name) => rowsFor(doc, name)[0];
const control = (panel, key) => [...panel.querySelectorAll('.frow')]
  .find(r => r.querySelector('.key')?.textContent === key)?.querySelector('.ctl');
const bar = panel => panel.querySelector('.cfgbar');
const save = panel => bar(panel).querySelector('.pri');
const discard = panel => save(panel).previousElementSibling;
const defaults = panel => bar(panel).querySelector('.defaults');
const lastPatch = netlog => netlog.filter(l => l.startsWith('PATCH ')).pop();
const change = async (window, input, value) => {
  if (input.type === 'checkbox') input.checked = value;
  else input.value = value;
  input.dispatchEvent(new window.Event(input.type === 'checkbox' || input.tagName === 'SELECT'
    ? 'change' : 'input', { bubbles: true }));
  await flush(10);
};
async function open(doc, name) {
  rowFor(doc, name).querySelector('.cfgbtn').click();
  await flush(50);
  return rowFor(doc, name).querySelector('.appcfg');
}

async function testEditing() {
  const { window, store, netlog } = await boot();
  const doc = window.document;
  store.apps = [app('Time'), app('Date', false), app('Temperature'), app('Status', true, false)];
  store.builtinConfigs = {
    Time: { fields: [
      field('timeMode', 'select', 1, { options: [0, 1, 5], group: 'time' }),
      field('timeColor', 'color', null, { nullable: true, group: 'time' }),
      field('calendarTextColor', 'color', 0xFFFFFF, { group: 'calendar' }),
      field('weekdayBar.show', 'bool', true, { group: 'weekday' }),
      field('weekdayBar.weekendDays', 'days', ['saturday', 'sunday'], { group: 'weekday' }),
    ] },
    Date: { fields: [field('dateOrder', 'select', 'dayMonthYear', {
      options: ['dayMonthYear', 'monthDayYear'], group: 'date',
    }), field('weekdayBar.show', 'bool', false, { group: 'weekday' })] },
    Temperature: { fields: [field('useCelsius', 'bool', true),
      field('temperatureColor', 'color', 0, { nullable: true, default: null })] },
  };
  await goto(window, '#/apps');
  assert(!rowFor(doc, 'Status').querySelector('.cfgbtn'), 'an app without settings offers none');
  const panel = await open(doc, 'Time');
  assert(netlog.includes('GET /api/v1/apps/builtin/Time/config'), 'a built-in reads its own settings route');
  const mode = control(panel, 'timeMode').querySelector('select');
  assert(mode.options.length === 3, 'the device decides which choices exist');
  await change(window, mode, '5');
  const inherited = control(panel, 'timeColor');
  assert(inherited.querySelector('input[type=checkbox]').checked, 'a null colour inherits the global text colour');
  await change(window, inherited.querySelector('input[type=checkbox]'), false);
  await change(window, inherited.querySelector('input[type=color]'), '#000000');
  await change(window, control(panel, 'calendarTextColor').querySelector('input[type=color]'), '#112233');
  const weekend = control(panel, 'weekdayBar.weekendDays');
  await change(window, [...weekend.querySelectorAll('label')].find(l => l.textContent === 'Fri')
    .querySelector('input'), true);
  await change(window, control(panel, 'weekdayBar.show').querySelector('input'), false);
  panel.querySelector('.cfgsection[data-group="weekday"]').open = false;
  assert(!save(panel).disabled, 'folding a section keeps its changes for Save');
  save(panel).click();
  await flush(60);
  assert(JSON.stringify(store.configPatch) === JSON.stringify({ timeMode: 5, timeColor: 0,
    calendarTextColor: 0x112233, weekdayBar: { show: false, weekendDays: ['sunday', 'friday', 'saturday'] } }) &&
    lastPatch(netlog) === 'PATCH /api/v1/apps/builtin/Time/config',
    'Save sends numeric choices, black as 0 and only the changed weekday bar members to the built-in route');
  let timePanel = rowFor(doc, 'Time').querySelector('.appcfg');
  assert(save(timePanel).disabled && control(timePanel, 'timeMode').querySelector('select').value === '5',
    'the saved values come back clean');
  await change(window, control(timePanel, 'timeColor').querySelector('input[type=checkbox]'), true);
  save(timePanel).click();await flush(60);
  assert(store.configPatch.timeColor === null, 'inheriting again saves null, not black');

  const datePanel = await open(doc, 'Date');
  await change(window, control(datePanel, 'weekdayBar.show').querySelector('input'), true);
  save(datePanel).click();await flush(60);
  assert(JSON.stringify(store.configPatch) === '{"weekdayBar":{"show":true}}' &&
    lastPatch(netlog) === 'PATCH /api/v1/apps/builtin/Date/config',
    'a switched-off built-in saves its settings too');
  const tempPanel = await open(doc, 'Temperature');
  assert(!control(tempPanel, 'temperatureColor').querySelector('input[type=checkbox]').checked,
    'a stored black stays a colour of its own');
  await change(window, control(tempPanel, 'useCelsius').querySelector('input'), false);
  defaults(tempPanel).click();await flush(10);
  assert(control(tempPanel, 'temperatureColor').querySelector('input[type=checkbox]').checked,
    'defaults turn a nullable colour back to inheriting');
  discard(tempPanel).click();await flush(10);
  assert(!control(tempPanel, 'temperatureColor').querySelector('input[type=checkbox]').checked &&
    control(tempPanel, 'useCelsius').querySelector('input').checked,
    'Discard brings back what the device holds');
  await change(window, control(tempPanel, 'useCelsius').querySelector('input'), false);
  store.configFail = 'out of range';
  save(tempPanel).click();await flush(60);
  assert(!save(tempPanel).disabled && !control(tempPanel, 'useCelsius').querySelector('input').checked,
    'a refused Save keeps the change for another try');
  store.configFail = null;
  save(tempPanel).click();await flush(60);
  assert(JSON.stringify(store.configPatch) === '{"useCelsius":false}' &&
    lastPatch(netlog) === 'PATCH /api/v1/apps/builtin/Temperature/config', 'the retry sends it again');

  timePanel = rowFor(doc, 'Time').querySelector('.appcfg');
  await change(window, control(timePanel, 'timeMode').querySelector('select'), '0');
  const timeRow = rowFor(doc, 'Time');
  timeRow.querySelector('.rowmenu .mbtn').click();
  [...timeRow.querySelectorAll('.rowmenu .mlist > button')].find(b => b.textContent.trim() === 'Duplicate').click();
  await flush(60);
  assert(rowsFor(doc, 'Time').length === 2 && rowsFor(doc, 'Time').filter(r => r.querySelector('.cfgbtn')).length === 1,
    'duplicate Time entries share one settings panel');
  assert(rowFor(doc, 'Time').querySelector('.appcfg') === timePanel && !timePanel.hidden &&
    control(timePanel, 'timeMode').querySelector('select').value === '0', 'unsaved settings survive an order change');
  window.close();
}

async function testScriptsKeepTheirTextAndScriptsOff() {
  const { window, store } = await boot();
  const doc = window.document;
  store.apps = [app('Time'), { name: 'Example', origin: 'script', config: true,
    enabled: true, inLoop: true, present: true }];
  store.builtinConfigs.Time = { fields: [field('time24h', 'bool', true, { group: 'time' })] };
  store.configs.Example = { fields: [scriptField('time24h', 'bool', false,
    { label: 'Custom script label', help: 'Script-owned help' })] };
  await goto(window, '#/apps');
  const scriptPanel = await open(doc, 'Example');
  assert(scriptPanel.textContent.includes('Custom script label') && scriptPanel.textContent.includes('Script-owned help'),
    'a script keeps its own label and help even where its key matches a built-in setting');
  window.close();
  const off = await boot({ device: { scriptingRunning: false } });
  off.store.apps = store.apps;
  off.store.builtinConfigs = store.builtinConfigs;
  await goto(off.window, '#/apps');
  assert(!!rowFor(off.window.document, 'Time').querySelector('.cfgbtn') &&
    !rowFor(off.window.document, 'Example'), 'built-in settings stay available with scripting switched off');
  off.window.close();
}

async function testDisplayOwnership() {
  const { window, store } = await boot();
  Object.assign(store.settings, { time24h: true, clockFace: 'sheet', timeMode: 1, timeColor: null,
    dateColor: null, dateOrder: 'dayMonthYear', useCelsius: true, humidityColor: null,
    temperatureColor: null, batteryColor: null, calendarAnimation: true,
    weekdayBar: { show: true }, dateWeekdayBar: { show: false },
    appDurationMs: 7000, autoTransition: true, transitionDurationMs: 400,
    brightness: 100, textColor: 0xFFFFFF, futureSetting: 'still visible' });
  await goto(window, '#/display');
  const keys = [...window.document.querySelectorAll('.key')].map(n => n.textContent);
  assert(!keys.some(k => /^(time|clock|date|calendar|weekday|useCelsius|humidityColor|temperatureColor|batteryColor)/.test(k)),
    'app settings stay off Display, Advanced included');
  assert(keys.includes('appDurationMs') && keys.includes('autoTransition') && keys.includes('transitionDurationMs'),
    'rotation and timing stay under Display');
  assert(keys.includes('brightness') && keys.includes('textColor') && keys.includes('futureSetting'),
    'global settings and unknown keys stay visible');
  window.close();
}

async function testBuiltinAndModuleOfOneName() {
  const { window, store, netlog } = await boot();
  const doc = window.document;
  store.apps = [app('Time'), { name: 'Time', origin: 'module', config: true, meta: {} }];
  store.builtinConfigs.Time = { fields: [field('time24h', 'bool', true)] };
  store.configs.Time = { fields: [scriptField('city', 'text', 'Berlin')] };
  await goto(window, '#/apps');
  const builtin = () => doc.querySelector('#apps-loop .approw');
  const module = () => doc.querySelector('#apps-mod .approw');
  builtin().querySelector('.cfgbtn').click();await flush(50);
  module().querySelector('.cfgbtn').click();await flush(50);
  const builtinPanel = builtin().querySelector('.appcfg');
  const modulePanel = module().querySelector('.appcfg');
  assert(builtinPanel !== modulePanel && !!control(builtinPanel, 'time24h') && !!control(modulePanel, 'city'),
    'a built-in and a module of the same name each get their own panel');
  await change(window, control(builtinPanel, 'time24h').querySelector('input'), false);
  await change(window, control(modulePanel, 'city').querySelector('input'), 'Wien');
  save(builtinPanel).click();await flush(60);
  assert(lastPatch(netlog) === 'PATCH /api/v1/apps/builtin/Time/config' &&
    JSON.stringify(store.configPatch) === '{"time24h":false}' &&
    control(modulePanel, 'city').querySelector('input').value === 'Wien' && !save(modulePanel).disabled,
    'saving the built-in leaves the module’s unsaved text alone');
  save(modulePanel).click();await flush(60);
  assert(lastPatch(netlog) === 'PATCH /api/v1/apps/Time/config' &&
    JSON.stringify(store.configPatch) === '{"city":"Wien"}', 'the module saves to the script route');
  window.close();
}

async function testDefaultsInFoldedSections() {
  const { window, store } = await boot();
  store.apps = [app('Time')];
  store.builtinConfigs.Time = { fields: [field('time24h', 'bool', true, { group: 'time' }),
    field('calendarTextColor', 'color', 0x112233, { default: 0, group: 'calendar' }),
    field('weekdayBar.show', 'bool', true, { default: false, group: 'weekday' })] };
  await goto(window, '#/apps');
  const panel = await open(window.document, 'Time');
  defaults(panel).click();
  assert(control(panel, 'calendarTextColor').querySelector('input').value === '#000000' &&
    !control(panel, 'weekdayBar.show').querySelector('input').checked && !save(panel).disabled,
    'defaults reach folded sections and wait for Save');
  discard(panel).click();
  assert(control(panel, 'calendarTextColor').querySelector('input').value === '#112233' &&
    control(panel, 'weekdayBar.show').querySelector('input').checked && save(panel).disabled,
    'Discard restores folded sections too');
  assert(!store.configPatch, 'defaults and Discard send nothing');
  window.close();
}

async function testOptionalScriptGroups() {
  const { window, store, netlog } = await boot();
  const doc = window.document;
  const heading = '<img src=x onerror=alert(1)> & "Display"';
  store.apps = [{ name: 'Weather', origin: 'script', config: true,
    enabled: true, inLoop: true, present: true },
    { name: 'Plain', origin: 'module', config: true, meta: {} }];
  store.configs.Weather = { fields: [
    scriptField('lead', 'text', 'First'),
    scriptField('city', 'text', 'Berlin', { group: 'Weather data', default: 'World' }),
    scriptField('time24h', 'bool', true, { group: 'time', label: 'Custom clock' }),
    scriptField('every', 'number', 30, { group: 'Weather data', default: 15, min: 1, max: 60 }),
    scriptField('caption', 'text', 'Hello', { group: heading }),
    scriptField('tail', 'bool', false, { group: '' }),
  ] };
  store.configs.Plain = { fields: [scriptField('name', 'text', 'Flat'), scriptField('flag', 'bool', true)] };
  await goto(window, '#/apps');
  let panel = await open(doc, 'Weather');
  const group = name => [...panel.querySelectorAll('.cfgsection')].find(s => s.dataset.group === name);
  assert([...panel.children].filter(n => n.matches('.cfgsection,.frow'))
    .map(n => n.dataset.group || n.querySelector('.key').textContent).join('|') ===
    ['lead', 'Weather data', 'time', heading, 'tail'].join('|'),
    'groups follow their first setting and settings without a group stay outside');
  assert([...group('Weather data').querySelectorAll('.key')].map(n => n.textContent).join(',') === 'city,every',
    'settings of one group stay together in declaration order');
  assert(group('time').querySelector('.cfggrp').textContent === 'time' &&
    panel.textContent.includes('Custom clock'), 'script groups and labels are the author’s text');
  assert(group(heading).querySelector('.cfggrp').textContent === heading && !group(heading).querySelector('img'),
    'a group name is shown as text, never as markup');
  const plainPanel = await open(doc, 'Plain');
  assert(!plainPanel.querySelector('.cfgsection') && plainPanel.querySelectorAll('.frow').length === 2,
    'settings without groups stay one flat list');

  group('Weather data').open = false;
  group('time').open = false;
  await change(window, control(panel, 'lead').querySelector('input'), 'Changed');
  await change(window, control(panel, 'city').querySelector('input'), 'Wien');
  await change(window, control(panel, 'every').querySelector('input'), 45);
  await change(window, control(panel, 'time24h').querySelector('input'), false);
  assert(!save(panel).disabled, 'changes inside folded groups wait for Save');
  save(panel).click();await flush(60);
  assert(JSON.stringify(store.configPatch) === JSON.stringify({ lead: 'Changed', city: 'Wien', time24h: false, every: 45 }) &&
    lastPatch(netlog) === 'PATCH /api/v1/apps/Weather/config',
    'grouped settings save under their own keys to the script route');
  panel = rowFor(doc, 'Weather').querySelector('.appcfg');
  assert(control(panel, 'city').querySelector('input').value === 'Wien' && save(panel).disabled,
    'the saved values come back clean');
  defaults(panel).click();
  assert(control(panel, 'city').querySelector('input').value === 'World' &&
    control(panel, 'every').querySelector('input').value === '15' &&
    control(panel, 'time24h').querySelector('input').checked && !save(panel).disabled,
    'defaults reach settings inside folded groups');
  discard(panel).click();
  assert(control(panel, 'city').querySelector('input').value === 'Wien' &&
    control(panel, 'every').querySelector('input').value === '45' &&
    !control(panel, 'time24h').querySelector('input').checked && save(panel).disabled,
    'Discard restores settings inside folded groups');
  window.close();
}

async function main() {
  await testEditing();
  await testScriptsKeepTheirTextAndScriptsOff();
  await testDisplayOwnership();
  await testBuiltinAndModuleOfOneName();
  await testDefaultsInFoldedSections();
  await testOptionalScriptGroups();
  console.log(`builtin-config: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main().catch(e => { console.error(e); process.exit(1); });
