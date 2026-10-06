/* Audio tab and the sound settings, both driven by the capabilities object.

   GET /api/v1/capabilities answers audio:{mp3,rtttl,track,radio,...}. The Audio tab
   starts with the mixer (master, radio, apps, alerts) and adds a section per kind
   of sound the clock plays. System > Audio carries no volumes.

   Run:  node audio-tab.test.js */
const { boot, goto, flush, stubXhr, useFakeTimers } = require('./harness');
const restoreTimers = useFakeTimers();

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

const caps = a => ({ transitions: [], audio: a });
const navLabels = window => [...window.document.querySelectorAll('#nav a')].map(a => a.textContent);
const sections = window => [...window.document.querySelectorAll('#view .section')].map(s => s.id);
const mp3Rows = window => [...window.document.querySelectorAll('#sec-mp3 .arow')];
const shown = window => mp3Rows(window).filter(r => !r.classList.contains('gone')).map(r => r.dataset.name);
const search = async (window, text) => {
  const q = window.document.querySelector('#sec-mp3 input[type=search]');
  q.value = text;
  q.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(200); // debounce(120)
};
const byTitle = (root, title) => [...root.querySelectorAll('button')].find(b => b.title === title);
const typeInto = async (window, input, text) => {
  input.value = text;
  input.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(250); // debounce(150)
};
const keysIn = (window, sel) => [...window.document.querySelectorAll(sel + ' .frow .key')].map(k => k.textContent);
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const until = async (cond, ms = 5000) => {
  for (const end = Date.now() + ms; !cond() && Date.now() < end;) await flush(50);
  return cond();
};
const VOLUMES = ['volume', 'radioVolume', 'appVolume', 'alertVolume'];

async function tabRedirects() {
  console.log('audio: one tab replaces Sounds and Radio');
  const { window } = await boot();
  const names = navLabels(window);
  assert(names.includes('Audio'), 'nav has an Audio tab');
  assert(!names.includes('Sounds') && !names.includes('Radio'), 'Sounds and Radio tabs are gone');

  await goto(window, '#/audio');
  assert(sections(window).join(',') === 'sec-mixer,sec-mp3,sec-radio,sec-melodies',
    'the mixer, then a section per kind with rtttl+mp3+radio');

  await goto(window, '#/apps');
  await goto(window, '#/sounds');
  assert(sections(window).includes('sec-melodies'), '#/sounds redirects to the Audio tab');
  await goto(window, '#/apps');
  await goto(window, '#/radio');
  assert(sections(window).includes('sec-radio'), '#/radio redirects to the Audio tab');
}

async function sectionsFollowTheSinks() {
  console.log('audio: sections follow the sink flags');
  {
    const { window } = await boot(
      { caps: caps({ rtttl: true, track: false, mp3: false, radio: false }) });
    await goto(window, '#/audio');
    assert(navLabels(window).includes('Audio'), 'tab stays visible with only a buzzer');
    assert(sections(window).join(',') === 'sec-mixer,sec-melodies', 'only the mixer and melodies on a buzzer-only panel');
  }
  {
    const { window } = await boot(
      { caps: caps({ rtttl: false, track: false, mp3: true, radio: true }) });
    await goto(window, '#/audio');
    assert(sections(window).join(',') === 'sec-mixer,sec-mp3,sec-radio',
      'no melody editor on a board that cannot play melodies');
  }
  {
    const { window } = await boot(
      { caps: caps({ rtttl: false, track: false, mp3: false, radio: false }) });
    await goto(window, '#/audio');
    assert(!navLabels(window).includes('Audio'), 'no outputs at all hides the tab');
  }
}

async function mixerSliders() {
  console.log('audio: the mixer carries the master and one slider per group');
  const { window, store } = await boot();
  await goto(window, '#/audio');
  assert(same(keysIn(window, '#sec-mixer'), VOLUMES), 'master, radio, apps and alerts');
  const sliders = [...window.document.querySelectorAll('#sec-mixer input[type=range]')];
  assert(sliders.length === 4, 'four sliders');
  assert(same(sliders.map(s => Number(s.value)), [60, 80, 100, 100]), 'they load the stored values');
  for (const id of ['mp3', 'radio', 'melodies'])
    assert(!window.document.querySelector('#sec-' + id + ' input[type=range]'), 'no slider in ' + id);

  const app = sliders[2];
  app.value = '42';
  app.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(400); // debounce(300)
  assert(same(store.settingsPatch, { appVolume: 42 }), 'moving appVolume PATCHes {appVolume:42}');

  const noRadio = await boot({ caps: caps({ rtttl: true, track: false, mp3: false, radio: false }) });
  await goto(noRadio.window, '#/audio');
  assert(same(keysIn(noRadio.window, '#sec-mixer'), ['volume', 'appVolume', 'alertVolume']),
    'no radio slider without radio');
}

async function mixerFollowsTheClock() {
  console.log('audio: the mixer follows volumes changed on the clock');
  const { window, store } = await boot();
  await goto(window, '#/audio');
  const sliders = () => [...window.document.querySelectorAll('#sec-mixer input[type=range]')];
  const values = () => sliders().map(s => Number(s.value));
  store.settings.volume = 35;
  store.settings.radioVolume = 55;
  assert(await until(() => same(values().slice(0, 2), [35, 55])), 'master and radio follow within a poll');
  assert(window.document.querySelector('#sec-mixer .val').textContent === '35', 'the shown number follows too');

  const fetch = window.fetch;
  const held = [];
  window.fetch = (u, o) => {
    if (String(u).endsWith('/api/v1/settings') && !(o && o.method)) {
      const body = JSON.stringify(store.settings);
      return new Promise(r => held.push(() => r({ ok: true, status: 200, text: async () => body })));
    }
    return fetch(u, o);
  };
  assert(await until(() => held.length > 0), 'a poll is on its way');
  const app = sliders()[2];
  app.value = '42';
  app.dispatchEvent(new window.Event('input', { bubbles: true }));
  assert(await until(() => store.settingsPatch && store.settingsPatch.appVolume === 42), 'the change is sent');
  await flush(50);
  held.splice(0).forEach(answer => answer());
  await flush(50);
  assert(values()[2] === 42, 'a poll asked before the change does not undo it');
  window.fetch = fetch;

  const master = sliders()[0];
  master.dispatchEvent(new window.Event('pointerdown', { bubbles: true }));
  store.settings.volume = 70;
  await flush(2300);
  assert(values()[0] === 35, 'a slider held by the pointer is left alone');
  window.dispatchEvent(new window.Event('pointerup'));
  assert(await until(() => values()[0] === 70), 'and follows again once released');
}

async function systemHasNoVolumes() {
  console.log('audio: System > Audio has no volumes and no sound switch');
  const { window, store } = await boot({ caps: { ...caps({ rtttl: true, track: true, mp3: true, radio: true }), microphone: true } });
  store.settings.musicSource = 'auto';
  await goto(window, '#/system');
  const keys = keysIn(window, '#view');
  assert(keys.includes('musicSource'), 'the System page is rendered');
  assert(!window.document.querySelector('#view input[type=range]'), 'no slider');
  assert(!keys.some(k => VOLUMES.includes(k) || k === 'soundEnabled'), 'no volume key and no sound switch');
}

// A volume the knob changed after the page loaded must survive a save of something else.
async function systemSaveKeepsVolumes() {
  console.log('audio: saving System sends only what changed');
  const { window, store } = await boot({ caps: { ...caps({ rtttl: true, mp3: true, radio: true }), microphone: true } });
  store.settings.musicSource = 'auto';
  await goto(window, '#/system');
  store.settings.volume = 25;
  const select = [...window.document.querySelectorAll('#view select')]
    .find(s => [...s.options].some(o => o.value === 'microphone'));
  select.value = 'playback';
  select.dispatchEvent(new window.Event('change', { bubbles: true }));
  select.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(80);
  [...window.document.querySelectorAll('#savebar button')].find(b => /Save|Speichern/.test(b.textContent)).click();
  await flush(150);
  assert(same(store.settingsPatch, { musicSource: 'playback' }), 'the PATCH carries only the changed field');
  assert(store.settings.volume === 25, 'the knob\'s volume stays');
}

async function musicSource() {
  console.log('audio: the music source follows capture capabilities');
  const find = window => [...window.document.querySelectorAll('#view select')]
    .find(select => [...select.options].some(option => option.value === 'microphone'));
  const unsupported = await boot({ caps: caps({ rtttl: true }) });
  await goto(unsupported.window, '#/system');
  assert(!find(unsupported.window), 'capture selection hidden without microphone capability');
  const { window, store } = await boot({ caps: { ...caps({ mp3: true, radio: true }), microphone: true } });
  store.settings.musicSource = 'auto';
  await goto(window, '#/system');
  const select = find(window);
  assert(!!select && [...select.options].map(o => o.value).join(',') === 'auto,playback,microphone',
    'supported input offers automatic, playback and microphone');
  select.value = 'microphone';
  select.dispatchEvent(new window.Event('change', { bubbles: true }));
  await flush(100);
  const save = [...window.document.querySelectorAll('#view button')].find(b => /Save|Speichern/.test(b.textContent));
  if (save) { save.click(); await flush(150); }
  assert(store.settings.musicSource === 'microphone', 'source persists through the settings API');
}

async function mp3Upload() {
  console.log('audio: upload drops into /MP3');
  const { window } = await boot();
  const xhrLog = [];
  stubXhr(window, xhrLog);
  await goto(window, '#/audio');

  const zone = window.document.querySelector('#sec-mp3 .drop');
  assert(!!zone, 'the MP3 section has an upload zone');
  const file = new window.File([new Uint8Array([0x49, 0x44, 0x33, 4, 0])], 'ding.mp3',
    { type: 'audio/mpeg' });
  const drop = new window.Event('drop', { bubbles: true, cancelable: true });
  drop.dataTransfer = { files: [file] };
  zone.dispatchEvent(drop);
  await flush(60);

  assert(xhrLog.length === 1 && xhrLog[0].url === '/api/v1/audio/mp3',
    'upload POSTs to the mp3 route, which needs no dir parameter');
  assert(xhrLog[0].files.length === 1 && xhrLog[0].files[0].name === 'ding.mp3',
    'the mp3 goes up unconverted under its own name');
}

async function mp3ListPlayDelete() {
  console.log('audio: list, play, delete');
  const { window, store, netlog } = await boot();
  store.files['/MP3'].set('ding.mp3', 4321);
  await goto(window, '#/audio');

  const rows = mp3Rows(window);
  assert(rows.length === 1 && rows[0].dataset.name === 'ding', 'the file is listed by base name');

  rows[0].querySelector('.device-play').click();
  await flush(40);
  assert(store.played.length === 1 && same(store.played[0], { file: 'ding' }),
    'play posts {"file":"ding"}');

  const del = rows[0].querySelector('button.danger');
  del.click(); del.click(); // armable double-click confirm
  await flush(80);
  assert(netlog.some(l => l.startsWith('DELETE /api/v1/audio/mp3/ding')),
    'delete addresses the file by name, not by path');
  assert(mp3Rows(window).length === 0, 'the row disappears after the reload');
}

async function playingIndicator() {
  console.log('audio: playing indicator from the audio poll');
  const { window, store } = await boot();
  store.files['/MP3'].set('ding.mp3', 4321);
  store.files['/MP3'].set('door.mp3', 1000);
  store.radio.alert = { playing: true, name: 'ding', error: '' };
  await goto(window, '#/audio');
  const row = window.document.querySelector('#sec-mp3 .arow[data-name="ding"]');
  assert(row.classList.contains('on'), 'the playing alert row is marked');
  assert(row.querySelector('.device-play').classList.contains('playing'),
    'its play button shows the stop state');
  assert(window.document.querySelector('.nowp b').textContent === 'ding', 'the bar names the sound');

  const marked = async appName => {
    const app = await boot();
    app.store.files['/MP3'].set('ding.mp3', 4321);
    app.store.files['/MP3'].set('door.mp3', 1000);
    app.store.sounds.set('racer', new Map([['door.mp3', { size: 10 }]]));
    app.store.radio.app = { playing: true, name: appName, error: '' };
    app.store.radio.alert = { playing: false, name: 'ding', error: '' };
    await goto(app.window, '#/audio');
    return [...app.window.document.querySelectorAll('#sec-mp3 .arow.on')].map(r => r.dataset.name);
  };
  assert(same(await marked('door'), []),
    'a script\'s plain name may be its own sound, so it marks no row; an alert that ended marks none');
  assert(same(await marked('racer/door'), ['racer/door']), 'a script sound by its folder marks that row only');
}

async function mp3Search() {
  console.log('audio: the MP3 list is searchable and counted');
  const { window, store } = await boot();
  for (const [n, size] of [['pistol.mp3', 3240], ['door.mp3', 1000], ['title.mp3', 900],
                           ['alarm.mp3', 500], ['notes.txt', 10]])
    store.files['/MP3'].set(n, size);
  await goto(window, '#/audio');
  const doc = window.document;

  assert(mp3Rows(window).map(r => r.dataset.name).join() === 'alarm,door,pistol,title',
    'one row per MP3 by name, non-mp3 files left out');
  assert(!doc.querySelector('#sec-mp3 details'), 'no folders: names carry no category');
  assert(/^4 sounds · 5\.5 KB · /.test(doc.querySelector('#sec-mp3 .mp3meter').textContent),
    'the meter counts the sounds and their size');

  await search(window, 'O');
  assert(shown(window).join() === 'door,pistol', 'search narrows the list, case-insensitive');
  await search(window, 'zzz');
  assert(shown(window).length === 0 && !doc.querySelector('#sec-mp3 .nohit').classList.contains('gone'),
    'no hit says so');
  await search(window, '');
  assert(shown(window).length === 4 && doc.querySelector('#sec-mp3 .nohit').classList.contains('gone'),
    'clearing the search shows every sound again');
}

async function nowPlayingBar() {
  console.log('audio: one bar says what is playing');
  const { window, store } = await boot();
  store.radio.radio = { playing: true, station: 'SWR3', title: 'Yellow', error: '' };
  await goto(window, '#/audio');
  const bar = window.document.querySelector('#view .nowp');
  assert(!!bar && bar.classList.contains('on'), 'the bar is on while something plays');
  assert(bar.querySelector('b').textContent === 'SWR3 – Yellow' && bar.querySelector('.src').textContent === 'Radio',
    'it names the station, the title and the source');
  bar.querySelector('button').click();
  await flush(80);
  assert(store.audioStops === 1, 'its stop posts to the stop endpoint');
  assert(!bar.classList.contains('on') && bar.querySelector('b').textContent === 'Nothing playing' &&
    bar.querySelector('button').disabled, 'it goes idle right after the stop');

  const buzzer = await boot({ caps: caps({ rtttl: true, track: false, mp3: false, radio: false }) });
  await goto(buzzer.window, '#/audio');
  assert(!buzzer.window.document.querySelector('.nowp'), 'a buzzer-only panel has nothing to poll, so no bar');
}

function transportState(button, on, name) {
  assert(button.classList.contains('playing') === on,
    name + ' button ' + (on ? 'shows' : 'leaves') + ' the stop state');
  assert(button.textContent === (on ? '■' : '▶'),
    name + ' button uses the matching transport symbol');
  assert(button.getAttribute('aria-pressed') === String(on),
    name + ' button exposes aria-pressed=' + on);
  assert(button.getAttribute('aria-label').startsWith(on ? 'Stop on the clock' : 'Play on the clock'),
    name + ' button has the matching accessible label');
}

async function rowTransportToggles() {
  console.log('audio: each device play button becomes its own stop control');

  {
    const { window, store } = await boot();
    store.melodies = [{ name: 'beep', rtttl: 'beep:d=4,o=5,b=120:c,e,g', valid: true }];
    await goto(window, '#/audio');
    const play = window.document.querySelector('#sec-melodies .device-play');
    play.click(); await flush(40);
    assert(same(store.played, [{ rtttl: 'beep:d=4,o=5,b=120:c,e,g' }]),
      'melody starts with {rtttl}');
    transportState(play, true, 'melody');
    store.radio.radio = { playing: true, station: 'SWR3', title: '', error: '' };
    play.click(); await flush(40);
    assert(store.audioStops === 1 && same(store.stopBodies[0], { group: 'alert' }),
      'second melody click stops the alerts only');
    assert(store.radio.radio.playing, 'and leaves the radio playing');
    transportState(play, false, 'melody');
  }

  {
    const { window, store } = await boot();
    store.files['/MP3'].set('ding.mp3', 4321);
    await goto(window, '#/audio');
    const play = window.document.querySelector('#sec-mp3 .device-play');
    play.click(); await flush(40);
    assert(same(store.played, [{ file: 'ding' }]), 'MP3 starts with {file}');
    transportState(play, true, 'MP3');
    play.click(); await flush(40);
    assert(store.audioStops === 1 && same(store.stopBodies[0], { group: 'alert' }),
      'second MP3 click stops the alerts only');
    transportState(play, false, 'MP3');
  }

  {
    const { window, store } = await boot();
    store.radio.stations = [{ name: 'test', url: 'http://example.com/stream' }];
    await goto(window, '#/audio');
    const play = window.document.querySelector('#sec-radio .device-play');
    play.click(); await flush(40);
    assert(same(store.radioPlay, { station: 'test' }), 'radio starts with {station:name}');
    transportState(play, true, 'radio');
    await flush(400);
    assert(play.closest('.arow').classList.contains('on') &&
      window.document.querySelector('.nowp b').textContent === 'test',
      'the next poll follows right after a start: row marked, bar names the station');
    transportState(play, true, 'radio');
    play.click(); await flush(40);
    assert(store.audioStops === 1 && same(store.stopBodies[0], { group: 'radio' }),
      'second radio click stops the radio only');
    transportState(play, false, 'radio');
  }
}

async function melodyRowsAndEditor() {
  console.log('audio: melodies list as rows and edit in place');
  const { window, store, netlog } = await boot();
  store.melodies = [{ name: 'beep', rtttl: 'beep:d=4,o=5,b=120:c,e,g', valid: true },
                    { name: 'bad', rtttl: 'bad:x', valid: false, error: 'no notes' }];
  await goto(window, '#/audio');
  const sec = window.document.querySelector('#sec-melodies');
  const rowOf = n => sec.querySelector('.arow[data-name="' + n + '"]');

  assert(!sec.querySelector('input:not([type=range])'), 'saved melodies show no input fields');
  assert(/^3 notes · \d+\.\d s$/.test(rowOf('beep').querySelector('.sz').textContent), 'a row names notes and length');
  assert(rowOf('bad').classList.contains('bad') && rowOf('bad').querySelector('.device-play').disabled,
    'a melody that does not parse is marked and cannot play');

  byTitle(rowOf('beep'), 'Edit').click();
  const form = sec.querySelector('.aform');
  assert(!!form && !rowOf('beep'), 'edit swaps the row for its form');
  const [name, rt] = form.querySelectorAll('input');
  assert(name.value === 'beep' && rt.value === 'd=4,o=5,b=120:c,e,g', 'the form holds the saved melody');
  const save = form.querySelector('button.pri');
  assert(save.disabled, 'nothing changed, nothing to save');
  await typeInto(window, rt, 'd=4,o=5,b=120:c,e,g,c6');
  assert(!save.disabled && /4 notes/.test(form.querySelector('small').textContent), 'editing enables save');
  save.click();
  await flush(80);
  assert(netlog.some(l => l.startsWith('PUT /api/v1/audio/melodies/beep')), 'save PUTs the melody');
  assert(!sec.querySelector('.aform') && !!rowOf('beep'), 'the list shows rows again after the save');

  byTitle(rowOf('beep'), 'Edit').click();
  const renameForm = sec.querySelector('.aform');
  await typeInto(window, renameForm.querySelector('input'), 'boop');
  renameForm.querySelector('button.pri').click();
  await flush(100);
  assert(netlog.some(l => l.startsWith('PUT /api/v1/audio/melodies/boop')) &&
    netlog.some(l => l.startsWith('DELETE /api/v1/audio/melodies/beep')), 'a rename saves the new name and drops the old');

  const puts = netlog.length;
  [...sec.querySelectorAll('button')].find(b => /New melody/.test(b.textContent)).click();
  const fresh = sec.querySelector('.aform');
  assert(!!fresh && window.document.activeElement === fresh.querySelector('input'), 'new melody opens a focused form');
  assert(!fresh.classList.contains('bad'), 'an empty form shows no error yet');
  fresh.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
  assert(!sec.querySelector('.aform') && netlog.length === puts, 'Escape closes the form without a request');

  const empty = await boot();
  await goto(empty.window, '#/audio');
  const esec = empty.window.document.querySelector('#sec-melodies');
  assert(/No melodies yet/.test(esec.querySelector('.alib').textContent), 'an empty list says so');
  [...esec.querySelectorAll('button')].find(b => /New melody/.test(b.textContent)).click();
  assert(!esec.querySelector('.nohit'), 'the note gives way to the form');
  [...esec.querySelectorAll('.aform button')].find(b => b.textContent === 'Cancel').click();
  assert(!esec.querySelector('.aform') && !!esec.querySelector('.nohit'), 'cancel brings the note back');
}

async function radioRowsAndEditor() {
  console.log('audio: stations list as rows and edit in place');
  const { window, store } = await boot();
  store.radio.stations = [{ name: 'test', url: 'http://example.com/stream' },
                          { name: 'other', url: 'https://radio.example.org/live.mp3' }];
  store.radio.radio = { playing: true, station: 'other', title: '', error: '' };
  await goto(window, '#/audio');
  const sec = window.document.querySelector('#sec-radio');
  const rowOf = n => sec.querySelector('.arow[data-name="' + n + '"]');

  assert(!sec.querySelector('input:not([type=range])'), 'saved stations show no input fields');
  assert(rowOf('other').querySelector('small').textContent === 'radio.example.org', 'a row shows the stream host');
  assert(rowOf('other').classList.contains('on'), 'the playing station is marked');

  byTitle(rowOf('test'), 'Edit').click();
  const form = sec.querySelector('.aform');
  const [name, url] = form.querySelectorAll('input');
  const save = form.querySelector('button.pri');
  await typeInto(window, url, 'ftp://nope');
  assert(save.disabled && /http/.test(form.querySelector('small').textContent), 'a bad url blocks the save');
  await typeInto(window, name, 'other');
  await typeInto(window, url, 'http://example.com/other');
  assert(save.disabled && /already in use/.test(form.querySelector('small').textContent), 'a taken name blocks the save');
  await typeInto(window, name, 'test');
  form.querySelector('.device-play').click();
  await flush(40);
  assert(same(store.radioPlay, { station: 'http://example.com/other' }), 'the form plays the url before saving');
  save.click();
  await flush(80);
  assert(store.stationsPut && store.stationsPut.stations.map(s => s.url).join() ===
    'http://example.com/other,https://radio.example.org/live.mp3', 'save PUTs the whole list with the change');
  assert(!sec.querySelector('.aform') && rowOf('test').querySelector('.nm').title === 'http://example.com/other',
    'the saved station list is read back and replaces the form');

  [...sec.querySelectorAll('button')].find(b => /Add station/.test(b.textContent)).click();
  const fresh = sec.querySelector('.aform');
  const [n2, u2] = fresh.querySelectorAll('input');
  await typeInto(window, n2, 'new');
  await typeInto(window, u2, 'https://new.example/stream');
  u2.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
  await flush(80);
  assert(store.stationsPut.stations.length === 3 && store.stationsPut.stations[2].name === 'new',
    'Enter saves a new station at the end');

  const del = byTitle(rowOf('test'), 'Delete');
  const beforeDelete = store.stationsPut;
  del.click();
  await flush();
  assert(store.stationsPut === beforeDelete && !!rowOf('test'), 'the first delete click only asks for confirmation');
  del.click();
  await flush(80);
  assert(store.stationsPut.stations.map(s => s.name).join() === 'other,new', 'delete PUTs the list without it');

  const fetch = window.fetch;
  window.fetch = (u, o) => String(u).includes('/audio/stations')
    ? Promise.resolve({ ok: false, status: 422, text: async () => '{"error":{"code":"validation","message":"rejected"}}' })
    : fetch(u, o);
  byTitle(rowOf('other'), 'Edit').click();
  const failing = sec.querySelector('.aform');
  await typeInto(window, failing.querySelectorAll('input')[1], 'https://changed.example/');
  failing.querySelector('button.pri').click();
  await flush(80);
  assert(sec.querySelector('.aform') === failing && !failing.querySelector('button.pri').disabled,
    'a rejected save keeps the form open to try again');
  assert(store.stationsPut.stations.map(s => s.name).join() === 'other,new' &&
    /rejected/.test(window.document.querySelector('#toasts').textContent), 'the list stays and the reason is shown');
}

(async () => {
  await tabRedirects();
  await sectionsFollowTheSinks();
  await mixerSliders();
  await mixerFollowsTheClock();
  await systemHasNoVolumes();
  await systemSaveKeepsVolumes();
  await musicSource();
  await mp3Upload();
  await mp3ListPlayDelete();
  await playingIndicator();
  await mp3Search();
  await nowPlayingBar();
  await rowTransportToggles();
  await melodyRowsAndEditor();
  await radioRowsAndEditor();
  console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
  restoreTimers();
  process.exit(failures ? 1 : 0);
})().catch(e => { restoreTimers(); console.error(e); process.exit(1); });
