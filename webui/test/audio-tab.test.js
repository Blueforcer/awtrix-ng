/* Audio tab and the sound settings, both driven by the capabilities object.

   GET /api/v1/capabilities answers audio:{buzzer,track,mp3,radio} - one flag per
   output. The Audio tab shows a section per flag, and System > Audio shows one
   volume slider per flag, so a panel with only a buzzer sees one slider rather
   than four.

   Run:  node audio-tab.test.js */
const { boot, goto, flush, stubXhr } = require('./harness');

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

const caps = a => ({ transitions: [], audio: a });
const navLabels = window => [...window.document.querySelectorAll('#nav a')].map(a => a.textContent);
const sections = window => [...window.document.querySelectorAll('#view .section')].map(s => s.id);
const mp3Rows = window => [...window.document.querySelectorAll('#sec-mp3 .melorow')];
const labels = window =>
  [...window.document.querySelectorAll('#view label, #view .frow')].map(n => n.textContent);

async function tabRedirects() {
  console.log('audio: one tab replaces Sounds and Radio');
  const { window } = await boot();
  const names = navLabels(window);
  assert(names.includes('Audio'), 'nav has an Audio tab');
  assert(!names.includes('Sounds') && !names.includes('Radio'), 'Sounds and Radio tabs are gone');

  await goto(window, '#/audio');
  assert(sections(window).join(',') === 'sec-mp3,sec-radio,sec-melodies',
    'a section per sink with buzzer+mp3+radio');

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
      { caps: caps({ buzzer: true, track: false, mp3: false, radio: false }) });
    await goto(window, '#/audio');
    assert(navLabels(window).includes('Audio'), 'tab stays visible with only a buzzer');
    assert(sections(window).join(',') === 'sec-melodies', 'only melodies on a buzzer-only panel');
  }
  {
    const { window } = await boot(
      { caps: caps({ buzzer: false, track: false, mp3: true, radio: true }) });
    await goto(window, '#/audio');
    assert(sections(window).join(',') === 'sec-mp3,sec-radio',
      'no melody editor on a board without a buzzer');
  }
  {
    const { window } = await boot(
      { caps: caps({ buzzer: false, track: false, mp3: false, radio: false }) });
    await goto(window, '#/audio');
    assert(!navLabels(window).includes('Audio'), 'no outputs at all hides the tab');
  }
}

async function oneSliderPerSink() {
  console.log('audio: System shows a volume slider per sink');
  {
    // A Ulanzi: buzzer only. One slider, not four.
    const { window } = await boot(
      { caps: caps({ buzzer: true, track: false, mp3: false, radio: false }) });
    await goto(window, '#/system');
    const text = labels(window).join('|');
    assert(text.includes('Buzzer volume'), 'the buzzer slider is shown');
    assert(!text.includes('DFPlayer volume'), 'no DFPlayer slider without a DFPlayer');
    assert(!text.includes('MP3 volume'), 'no MP3 slider without a speaker');
    assert(!text.includes('Radio volume'), 'no radio slider without a speaker');
  }
  {
    // An ESP32-S3 with an I2S amplifier and no buzzer.
    const { window } = await boot(
      { caps: caps({ buzzer: false, track: false, mp3: true, radio: true }) });
    await goto(window, '#/system');
    const text = labels(window).join('|');
    assert(!text.includes('Buzzer volume'), 'no buzzer slider without a buzzer');
    assert(text.includes('MP3 volume') && text.includes('Radio volume'),
      'stored MP3s and the stream get a slider each');
  }
}

async function volumeSaves() {
  console.log('audio: a slider PATCHes its own key');
  const { window, store } = await boot();
  await goto(window, '#/system');
  const slider = [...window.document.querySelectorAll('#view input[type=range]')]
    .find(i => i.closest('.frow') && /Buzzer volume/.test(i.closest('.frow').textContent));
  assert(!!slider, 'the buzzer slider is on the page');
  slider.value = '35';
  slider.dispatchEvent(new window.Event('input', { bubbles: true }));
  slider.dispatchEvent(new window.Event('change', { bubbles: true }));
  await flush(80);
  const save = [...window.document.querySelectorAll('#view button')]
    .find(b => /Save|Speichern/.test(b.textContent));
  if (save) { save.click(); await flush(120); }
  assert(store.settings.buzzerVolume === 35 || store.settingsPatch,
    'the change reaches PATCH /api/v1/settings');
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
  assert(rows.length === 1 && rows[0].dataset.mp3 === 'ding', 'the file is listed by base name');

  rows[0].querySelectorAll('button')[1].click(); // play on AWTRIX
  await flush(40);
  assert(store.played.length === 1 && store.played[0].mp3 === 'ding',
    'play posts {"mp3":"ding"}, the key that never falls back to a melody');

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
  store.radio.mp3 = { playing: true, name: 'ding' };
  await goto(window, '#/audio');
  const row = mp3Rows(window)[0];
  assert(row.classList.contains('on') && row.querySelector('.meta').textContent.includes('▶'),
    'the playing row is marked');
}

async function melodiesIntact() {
  console.log('audio: melody editor still works inside the tab');
  const { window, store, netlog } = await boot();
  store.melodies = [{ name: 'beep', rtttl: 'beep:d=4,o=5,b=120:c,e,g', valid: true }];
  await goto(window, '#/audio');

  const row = window.document.querySelector('#sec-melodies .melorow');
  assert(!!row, 'saved melody renders');
  const rt = row.querySelector('input.rt');
  rt.value = 'd=4,o=5,b=120:c,e,g,c6';
  rt.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(300); // debounce(150)
  const save = row.querySelector('button.pri');
  assert(!save.disabled, 'editing enables save');
  save.click();
  await flush(60);
  assert(netlog.some(l => l.startsWith('PUT /api/v1/audio/melodies/beep')), 'save PUTs the melody');
}

async function radioIntact() {
  console.log('audio: radio section still works inside the tab');
  const { window, store } = await boot();
  store.radio.stations = [{ name: 'test', url: 'http://example.com/stream' }];
  await goto(window, '#/audio');

  const row = window.document.querySelector('#sec-radio .melorow');
  assert(!!row && row.querySelector('.rn').value === 'test', 'station list renders');
  const url = row.querySelector('.ru');
  url.value = 'http://example.com/other';
  url.dispatchEvent(new window.Event('input', { bubbles: true }));
  await flush(300); // debounce(150)
  const save = row.querySelector('button.pri');
  assert(!save.disabled, 'editing enables the row save');
  save.click();
  await flush(60);
  assert(store.stationsPut && store.stationsPut.stations[0].url === 'http://example.com/other',
    'the row save PUTs the whole station list');
}

(async () => {
  await tabRedirects();
  await sectionsFollowTheSinks();
  await oneSliderPerSink();
  await volumeSaves();
  await mp3Upload();
  await mp3ListPlayDelete();
  await playingIndicator();
  await melodiesIntact();
  await radioIntact();
  console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
