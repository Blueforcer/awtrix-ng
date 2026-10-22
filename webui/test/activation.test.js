/* The Help AWTRIX question on the TC002: asked once on the home network, never in setup mode
   or on a clock without the activation capability, and the answer goes to the clock.
   Run: node activation.test.js */
const { boot, goto, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  FAIL: ' + msg); }
}

const CAPS = { transitions: [], activation: true };

function clock(consent) {
  const calls = [];
  const answer = (method, body) => {
    calls.push({ method, body });
    if (method === 'PUT') consent = body.consent;
    return [{ consent }, 200];
  };
  return { calls, answer, get consent() { return consent; } };
}

const dialog = window => window.document.querySelector('dialog');
const press = (window, label) => [...dialog(window).querySelectorAll('button')]
  .find(b => b.textContent === label).click();

async function testAskedAndAnswered(label, expected) {
  const c = clock(null);
  const { window } = await boot({ caps: CAPS, activation: c.answer });
  await flush(60);
  const d = dialog(window);
  assert(d && d.open, label + ': an unasked clock asks');
  const text = d?.textContent || '';
  assert(text.includes('Help AWTRIX') && text.includes('planned cooperation with Ulanzi') &&
    text.includes('not to Ulanzi') && text.includes('Ulanzi learns only how many') &&
    text.includes('cannot be taken back'), label + ': the dialog explains it');
  assert(!/anonymous|no personal data|cannot be traced/.test(text), label + ': the dialog promises no anonymity');
  assert(d && d.querySelector('a[href="https://awtrix.de/privacy#tc002-activation"]'), label + ': the dialog links the privacy policy');
  const [yes, no] = d ? [...d.querySelectorAll('button')] : [];
  assert(yes && no && yes.className === no.className, label + ': both answers look the same');
  assert(d && d.querySelector('[autofocus]')?.tagName === 'H2', label + ': no answer has the focus');
  press(window, label);
  await flush(60);
  assert(c.calls.some(x => x.method === 'PUT' && x.body.consent === expected), label + ': the answer goes to the clock');
  assert(!dialog(window), label + ': the dialog is gone after the answer');
}

async function testEscapeKeepsIt() {
  const { window } = await boot({ caps: CAPS, activation: clock(null).answer });
  await flush(60);
  const d = dialog(window);
  d.dispatchEvent(new window.Event('cancel', { cancelable: true }));
  assert(dialog(window) === d && d.open, 'Escape does not dismiss the question');
}

async function testNotAsked(label, opts) {
  const c = clock(opts.consent ?? null);
  const { window } = await boot({ caps: opts.caps || CAPS, activation: c.answer, device: opts.device });
  await flush(60);
  assert(!dialog(window), label + ': no dialog');
  if (opts.noRequest) assert(c.calls.length === 0, label + ': not even a request');
}

const section = window => window.document.querySelector('#sec-activation');

async function testCountAfterNo() {
  const c = clock(false);
  const { window } = await boot({ caps: CAPS, activation: c.answer });
  await goto(window, '#/system');
  await flush(60);
  const sec = section(window);
  assert(sec && sec.textContent.includes('Not counted'), 'after a no, System shows that the clock is not counted');
  const count = [...sec.querySelectorAll('button')].find(b => b.textContent === 'Count my clock');
  assert(count, 'with a button to count it');
  count.click();
  await flush(20);
  assert(dialog(window) && dialog(window).open, 'the button opens the same question');
  press(window, 'No, thanks');
  await flush(60);
  assert(section(window).textContent.includes('Not counted') && c.consent === false, 'no again changes nothing');
  [...section(window).querySelectorAll('button')].find(b => b.textContent === 'Count my clock').click();
  await flush(20);
  press(window, 'Yes, count my clock');
  await flush(60);
  assert(c.consent === true, 'yes goes to the clock');
  assert(section(window).textContent.includes('Counted, thank you') && !section(window).querySelector('button'),
    'the row thanks and the button is gone');
}

async function testNoSection(label, consent, device) {
  const { window } = await boot({ caps: CAPS, activation: clock(consent).answer, device });
  await goto(window, '#/system');
  await flush(60);
  assert(!section(window), label + ': no Help AWTRIX section under System');
}

(async () => {
  await testAskedAndAnswered('Yes, count my clock', true);
  await testAskedAndAnswered('No, thanks', false);
  await testEscapeKeepsIt();
  await testNotAsked('already agreed', { consent: true });
  await testNotAsked('already declined', { consent: false });
  await testNotAsked('setup hotspot', { device: { ipAddress: '0.0.0.0' }, noRequest: true });
  await testNotAsked('without the capability', { caps: { transitions: [] }, noRequest: true });
  await testCountAfterNo();
  await testNoSection('agreed', true);
  await testNoSection('unasked', null);
  console.log(`activation: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
