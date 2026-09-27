// Capability policy, not chip names, controls optional features.
const assert = require('node:assert/strict');
const { boot, goto, flush } = require('./harness');
(async () => {
  for (const enabled of [false, true]) {
    const { dom, window, store, netlog } = await boot({ caps: {
      scripting: enabled, transitions: [],
      audio: { buzzer: true, track: false, mp3: enabled, radio: enabled }
    }});
    try {

      const nav = () => window.document.querySelector('#nav a[href="#/scripts"]');
      assert.equal(!!nav(), enabled, 'script navigation follows capability');
      await goto(window, '#/system');
      const view = () => window.document.querySelector('#view');
      assert.equal(view().textContent.includes('Run scripts'), enabled, 'script toggle follows capability');
      if (!enabled) assert.match(view().textContent, /not available on this board/i);
      await goto(window, '#/scripts');
      if (!enabled) {
        assert.match(view().textContent, /not available on this board/i);
        assert.equal(view().querySelector('textarea'), null, 'direct route cannot expose editor');
        assert(!netlog.some(s => s.includes('/api/v1/scripts/shared')), 'no script polling');
      } else assert(view().querySelector('textarea'), 'enabled editor remains available');
      await goto(window, '#/audio');
      assert.equal(!!view().querySelector('#sec-mp3'), enabled);
      assert.equal(!!view().querySelector('#sec-radio'), enabled);
      assert(view().querySelector('#sec-melodies'), 'RTTTL remains available');
      if (!enabled) assert.match(view().textContent, /not available on this board/i);
      console.log(`PASS capabilities scripting/mp3/radio=${enabled}`);
    } finally { dom.window.close(); }
  }
  // A page opened straight on a gated route is drawn before /device and /capabilities answer.
  // Each gated view waits for them itself, so a Pico never ends up showing what it lacks.
  const pico = { scripting: false, transitions: [],
    audio: { buzzer: true, track: false, mp3: false, radio: false } };
  for (const [route, device, capabilities] of [['#/audio', 300, 600], ['#/audio', 600, 300],
                                               ['#/system', 300, 600], ['#/system', 600, 300]]) {
    const { dom, window } = await boot({ caps: pico, url: 'http://localhost/' + route,
      device: { soc: 'rp2040', updateImage: 'firmware-galactic-unicorn.uf2' },
      delays: { '/api/v1/device': device, '/api/v1/capabilities': capabilities } });
    try {
      await flush(1000);
      const view = window.document.querySelector('#view');
      assert.equal(view.querySelector('#sec-mp3, #sec-radio'), null, route + ': no MP3 or radio');
      assert.equal(view.textContent.includes('Run scripts'), false, route + ': no script toggle');
      assert.equal(view.querySelector('input[type=file][accept=".bin"]'), null,
        route + ': no .bin upload');
      if (route === '#/audio') assert(view.querySelector('#sec-melodies'), 'RTTTL remains available');
    } finally { dom.window.close(); }
  }
  console.log('PASS capabilities on a route opened before the board answers');
  // A board with fixed wiring shows its pins rather than offering them, and drops the settings of
  // hardware it lacks. The same board reported as configurable keeps the full page.
  const unicorn = { soc: 'rp2040', label: 'Galactic Unicorn (Pico W / Pico 2 W)', fixed: true,
    max: 29, missing: [], inputOnly: [], adc1: [[26, 28]], strapping: [], rtc: [], matrix: [],
    reserved: [{ lo: 13, hi: 20, why: 'the fixed panel interface' },
               { lo: 23, hi: 25, why: 'the Pico wireless interface' },
               { lo: 29, hi: 29, why: 'the Pico wireless interface' }],
    defaults: { pinMatrix: -1, pinBtnLeft: 0, pinBtnSelect: 1, pinBtnRight: 3, pinBattery: -1,
      pinLdr: 28, pinBuzzer: -1, pinI2cSda: -1, pinI2cScl: -1, pinDfRx: -1, pinDfTx: -1,
      pinI2sBclk: 10, pinI2sLrclk: 11, pinI2sDout: 9, pinI2sMclk: -1, pinAmpEnable: 22 },
    panel: { width: 53, heights: [8, 11] } };
  const system = { ...unicorn.defaults, panelWidth: 53, panelHeight: 11, panels: 1,
    panelStart: 'topLeft', panelWiring: 'rows', panelColorOrder: 'GRB', panelSerpentine: true,
    panelChainReverse: false, panelChainSerpentine: false, ldrOnGround: false,
    batteryDividerRatio: 1.79, lowBatteryThreshold: 0, tempOffset: -9, humOffset: 0,
    tempDecimals: 0, dfplayer: false };
  const settings = { useCelsius: true, temperatureColor: 0, humidityColor: 0, batteryColor: 0 };
  for (const fixed of [true, false]) {
    const gpio = fixed ? unicorn : { ...unicorn, fixed: false, panel: undefined };
    const { dom, window } = await boot({ caps: { ...pico, gpio }, system, settings,
      device: { soc: 'rp2040', updateImage: 'firmware-galactic-unicorn.uf2' } });
    try {
      const view = () => window.document.querySelector('#view');
      const text = () => view().textContent;
      await goto(window, '#/system');
      await flush(200);
      const gpioSec = view().querySelector('#sec-gpio');
      assert.equal(gpioSec.querySelectorAll('select').length === 0, fixed, 'pins offered: ' + !fixed);
      assert(gpioSec.textContent.includes('GPIO 28'), 'the light sensor pin is shown');
      for (const absent of ['Buzzer', 'I2C SDA', 'DFPlayer RX', 'Battery'])
        assert.equal(gpioSec.textContent.includes(absent), !fixed, absent + ' row: ' + !fixed);
      assert.equal(gpioSec.textContent.includes('GPIO 23–25, 29: the Pico wireless interface'), fixed);
      assert.equal(text().includes('Cannot wake AWTRIX'), !fixed, 'wake note');
      for (const absent of ['LDR on GND', 'Battery divider', 'Temperature offset', 'Wiring direction'])
        assert.equal(text().includes(absent), !fixed, absent + ': ' + !fixed);
      assert.equal(view().querySelector('#sec-sndhw').textContent.includes('DFPlayer'), !fixed);
      assert.equal(text().includes('Melody volume'), fixed);
      const heights = [...view().querySelectorAll('#sec-panel select option')].map(o => o.value);
      if (fixed) assert.deepEqual(heights, ['8', '11'], 'the panel heights this board runs at');
      assert.equal(window.document.querySelector('#savebar').style.display, 'none', 'nothing dirty');
      await goto(window, '#/display');
      await flush(200);
      assert(text().includes('Celsius'), 'scripts read useCelsius, so it stays');
      for (const absent of ['Temperature colour', 'Battery colour'])
        assert.equal(text().includes(absent), !fixed, absent + ': ' + !fixed);
      console.log(`PASS capabilities fixed wiring=${fixed}`);
    } finally { dom.window.close(); }
  }
})().then(() => process.exit(0)).catch(e => { console.error(e); process.exit(1); });
