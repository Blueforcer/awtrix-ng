const { boot, goto, flush } = require('./harness');

let failed = 0;
function assert(cond, msg) { if (!cond) { console.error('FAIL:', msg); failed++; } }

// Some client ICU data sets reject a zone that ships in the device's zone
// table (seen in the wild: America/Coyhaique). The System page must degrade
// gracefully instead of dying before the Maintenance and Backup sections.
async function unknownZoneDoesNotKillSystemPage() {
  console.log('system: an unsupported timezone must not kill the System page');
  const real = Intl.DateTimeFormat;
  let page;
  const dom = await (async () => {
    const { dom, window, store } = await boot();
    // The device is configured for Asia/Shanghai, but the zone table's
    // candidates include America/Coyhaique, which this client ICU rejects.
    store.system = { hostname: 'awtrix-ng', tzName: 'Asia/Shanghai', tz: 'CST-8' };
    // Patch after boot so the boot IIFE and Dash don't hit it.
    window.Intl.DateTimeFormat = new Proxy(real, {
      construct(target, args) {
        const opts = args[1] || {};
        if (opts.timeZone === 'America/Coyhaique')
          throw new RangeError('Invalid time zone specified: America/Coyhaique');
        return Reflect.construct(target, args);
      },
    });
    return { dom, window };
  })();
  page = dom.window.document;
  await goto(page.defaultView, '#/system');
  await flush(120);

  const text = page.querySelector('#view').textContent;
  const tzOk = /Timezone|Zeitzone/.test(text);
  const maintOk = /Firmware|choose|Reboot|Neustart/.test(text);
  const backupOk = /Maintenance|Wartung|Backup/.test(text);
  console.log('  tz picker:', tzOk, '| maintenance:', maintOk, '| backup:', backupOk);
  console.log('  sample:', text.slice(0, 600).replace(/\s+/g, ' '));
  assert(tzOk, 'the timezone picker still renders');
  assert(maintOk, 'the Maintenance section still renders below the broken zone');
  assert(backupOk, 'the Maintenance/Backup area is reachable');
  if (failed) { console.log('  page dump:', page.querySelector('body').textContent.slice(0, 300)); }

  const zones = page.querySelectorAll('#view select option');
  const bad = [...zones].some(o => o.value === 'America/Coyhaique');
  assert(!bad, 'the unsupported zone is filtered out of the dropdown');
}

(async () => {
  await unknownZoneDoesNotKillSystemPage();
  console.log(failed ? failed + ' test(s) failed' : 'all ok');
  process.exit(failed ? 1 : 0);
})();