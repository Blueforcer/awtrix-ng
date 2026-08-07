/* Audio tab: the merge of the old Sounds and Radio tabs plus the MP3 clips section.

   One tab, three sections behind two capability flags: MP3 clips (audio),
   RTTTL melodies (always) and radio stations (radio). Old #/sounds and #/radio
   bookmarks redirect. Clips are plain files under /SOUNDS via the generic file
   API; play goes through POST /api/v1/sounds/play with the base name, and the
   playing indicator is fed from GET /api/v1/radio's clipPlaying/clipName.

   Run:  node audio-tab.test.js */
const { boot, goto, flush, stubXhr } = require('./harness');

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

const navLabels = window => [...window.document.querySelectorAll('#nav a')].map(a => a.textContent);
const sections = window => [...window.document.querySelectorAll('#view .section')].map(s => s.id);
const clipRows = window => [...window.document.querySelectorAll('#sec-clips .melorow')];

async function tabMergeAndRedirects() {
  console.log('audio: one tab replaces Sounds and Radio');
  const { window } = await boot();
  const labels = navLabels(window);
  assert(labels.includes('Audio'), 'nav has an Audio tab');
  assert(!labels.includes('Sounds') && !labels.includes('Radio'), 'Sounds and Radio tabs are gone');

  await goto(window, '#/audio');
  assert(sections(window).join(',') === 'sec-clips,sec-melodies,sec-radio',
    'three sections with radio+audio caps, clips first');

  await goto(window, '#/apps');
  await goto(window, '#/sounds');
  assert(sections(window).includes('sec-melodies'), '#/sounds redirects to the Audio tab');
  await goto(window, '#/apps');
  await goto(window, '#/radio');
  assert(sections(window).includes('sec-radio'), '#/radio redirects to the Audio tab');
}

async function capabilityGating() {
  console.log('audio: sections follow the capability flags');
  {
    const { window } = await boot({ caps: { transitions: [], radio: false, audio: false } });
    await goto(window, '#/audio');
    assert(navLabels(window).includes('Audio'), 'tab stays visible without radio hardware');
    assert(sections(window).join(',') === 'sec-melodies', 'only melodies without any audio caps');
  }
  {
    const { window } = await boot({ caps: { transitions: [], radio: false, audio: true } });
    await goto(window, '#/audio');
    assert(sections(window).join(',') === 'sec-clips,sec-melodies', 'clips + melodies without radio');
  }
  {
    // Older firmware without the audio flag: the radio DAC implies clip playback.
    const { window } = await boot({ caps: { transitions: [], radio: true } });
    await goto(window, '#/audio');
    assert(sections(window).join(',') === 'sec-clips,sec-melodies,sec-radio',
      'audio flag falls back to radio for old firmware');
  }
}

async function clipUpload() {
  console.log('audio: upload drops into /SOUNDS');
  const { window } = await boot();
  const xhrLog = [];
  stubXhr(window, xhrLog);
  await goto(window, '#/audio');

  const zone = window.document.querySelector('#sec-clips .drop');
  assert(!!zone, 'clips section has an upload zone');
  const file = new window.File([new Uint8Array([0x49, 0x44, 0x33, 4, 0])], 'ding.mp3',
    { type: 'audio/mpeg' });
  const drop = new window.Event('drop', { bubbles: true, cancelable: true });
  drop.dataTransfer = { files: [file] };
  zone.dispatchEvent(drop);
  await flush(60);

  assert(xhrLog.length === 1 && xhrLog[0].url === '/api/v1/files?dir=%2FSOUNDS',
    'upload POSTs to the file API with dir=/SOUNDS');
  assert(xhrLog[0].files.length === 1 && xhrLog[0].files[0].name === 'ding.mp3',
    'the mp3 goes up unconverted under its own name');
}

async function clipListPlayDelete() {
  console.log('audio: list, play, delete');
  const { window, store, netlog } = await boot();
  store.files['/SOUNDS'].set('ding.mp3', 4321);
  await goto(window, '#/audio');

  const rows = clipRows(window);
  assert(rows.length === 1 && rows[0].dataset.clip === 'ding', 'uploaded clip is listed by base name');
  assert(rows[0].querySelector('.sz').textContent.length > 0, 'row shows the file size');

  rows[0].querySelectorAll('button')[1].click(); // ▶ play on AWTRIX
  await flush(40);
  assert(store.played.length === 1 && store.played[0].name === 'ding',
    'play posts {"name":"ding"} to /api/v1/sounds/play');

  const del = rows[0].querySelector('button.danger');
  del.click(); del.click(); // armable double-click confirm
  await flush(80);
  assert(netlog.some(l => l.startsWith('DELETE /api/v1/files?path=%2FSOUNDS%2Fding.mp3')),
    'delete goes through the file API');
  assert(clipRows(window).length === 0, 'row disappears after the reload');
}

async function clipIndicator() {
  console.log('audio: playing indicator from the radio poll');
  const { window, store } = await boot();
  store.files['/SOUNDS'].set('ding.mp3', 4321);
  store.radio.clipPlaying = true;
  store.radio.clipName = 'ding';
  await goto(window, '#/audio');
  const row = clipRows(window)[0];
  assert(row.classList.contains('dirty') && row.querySelector('.meta').textContent.includes('▶'),
    'the playing clip row is marked');
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
  assert(netlog.some(l => l.startsWith('PUT /api/v1/sounds/beep')), 'save PUTs the melody');
  assert(store.melodies.some(m => m.name === 'beep' && m.rtttl.includes('c6')),
    'the new RTTTL body arrives');
}

async function radioIntact() {
  console.log('audio: radio section still works inside the tab');
  const { window, store } = await boot();
  store.radio.stations = [{ name: 'test', url: 'http://example.com/stream' }];
  await goto(window, '#/audio');

  const row = window.document.querySelector('#sec-radio .melorow');
  assert(!!row && row.querySelector('.rn').value === 'test', 'station list renders');
  const buttons = [...window.document.querySelectorAll('#sec-radio .row button')];
  const save = buttons.find(b => b.textContent.includes('Save') || b.textContent.includes('speichern'));
  save.click();
  await flush(60);
  assert(store.stationsPut && store.stationsPut.stations.length === 1 &&
    store.stationsPut.stations[0].name === 'test', 'save PUTs the station list');
}

(async () => {
  await tabMergeAndRedirects();
  await capabilityGating();
  await clipUpload();
  await clipListPlayDelete();
  await clipIndicator();
  await melodiesIntact();
  await radioIntact();
  console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
