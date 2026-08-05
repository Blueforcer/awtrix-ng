/* Timezone picker against a browser whose zone database is older than ours.

   The zone list is generated from the IANA release that was current when
   scripts/tz_data.py last ran and ships inside the firmware; the browser's zone
   database ships with the browser. So the device can offer a zone the browser
   has never heard of - `America/Coyhaique` arrived in tzdata 2025a - and every
   `Intl.DateTimeFormat({timeZone})` for it throws RangeError. The picker builds
   one formatter per zone while rendering, so a single unknown name used to take
   the whole settings page down with it.

   Run:  node timezone.test.js */
const { boot, goto } = require('./harness');

let failures = 0;
function assert(cond, msg) {
  if (cond) console.log('  PASS: ' + msg);
  else { console.log('  FAIL: ' + msg); failures++; }
}

// Coyhaique shares the '<-03>3' rule with the whole Argentine list, so its
// offset is still reachable from a sibling. The other two are alone on their
// rule, which leaves only the POSIX string - and both carry a half/quarter hour
// offset, east and west, where a sign slip would show.
const REJECT = ['America/Coyhaique', 'Asia/Kathmandu', 'Pacific/Marquesas'];

// Make jsdom's Intl behave like that older browser: every other zone still
// works, these throw exactly what Chrome throws.
function oldIcu(window) {
  const Real = window.Intl.DateTimeFormat;
  const Fake = function (loc, opt) {
    if (opt && REJECT.includes(opt.timeZone))
      throw new RangeError('Invalid time zone specified: ' + opt.timeZone);
    return new Real(loc, opt);
  };
  Fake.supportedLocalesOf = (...a) => Real.supportedLocalesOf(...a);
  Object.defineProperty(window.Intl, 'DateTimeFormat',
    { value: Fake, writable: true, configurable: true });
}

// Open the system page and read the timezone picker out of it.
async function picker(beforeParse) {
  const { window, store } = await boot(beforeParse ? { beforeParse } : {});
  // The picker only appears when the device's config carries a timezone.
  store.system = { tzName: 'Europe/Berlin', tz: 'CET-1CEST,M3.5.0,M10.5.0/3' };
  await goto(window, '#system');
  const sel = window.document.querySelector('.tzctl select');
  return { window, sel, labels: sel && new Map([...sel.options].map(o => [o.value, o.textContent])) };
}

async function main() {
  const good = await picker(null);
  assert(!!good.sel, 'baseline: the picker renders on a current browser');
  if (!good.sel) return;

  const old = await picker(oldIcu);
  assert(!!old.sel, 'the page renders on the older browser instead of dying');
  if (!old.sel) return;

  const all = old.window.eval('TZNAMES.length');
  assert(old.labels.size === all, 'every zone is still offered, none dropped (' + old.labels.size + '/' + all + ')');

  // The offsets the old browser cannot compute must come out the same as the
  // ones the current browser measures - that is the whole point of the fallback.
  const wrong = [...good.labels].filter(([zone, text]) => old.labels.get(zone) !== text);
  assert(!wrong.length, 'every label matches the current browser' +
    (wrong.length ? ' (first off: ' + wrong[0][0] + ' ' + wrong[0][1] + ' vs ' + old.labels.get(wrong[0][0]) + ')' : ''));
  REJECT.forEach(z => console.log('    ' + z + ' -> ' + old.labels.get(z)));

  const sel = old.sel;
  sel.value = REJECT[0];
  sel.dispatchEvent(new old.window.Event('input', { bubbles: true }));
  const prev = old.window.document.querySelector('.tzprev');
  assert(prev && /UTC-3\b/.test(prev.textContent), 'the preview survives selecting one: ' + (prev && prev.textContent));

  const save = [...old.window.document.querySelectorAll('button')].some(b => /save|speichern/i.test(b.textContent));
  assert(save, 'the rest of the system page is intact');
}

main().then(() => {
  console.log(failures ? '\nFAILED (' + failures + ')' : '\nAll timezone checks passed');
  process.exit(failures ? 1 : 0);
}).catch(e => { console.error(e); process.exit(1); });
