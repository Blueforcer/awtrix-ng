const { boot, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  ✗ ' + msg); }
}

async function main() {
  const { window } = await boot();
  const links = () => [...window.document.querySelectorAll('#nav a')];
  const before = links();
  assert(before.length > 3, 'the main navigation lists the tabs');
  window.location.hash = '#/editor';
  await flush(40);
  const after = links();
  assert(after.length === before.length && after.every((a, i) => a === before[i]),
    'switching tabs keeps the navigation links, so the bar keeps its scroll position');
  const on = after.filter(a => a.classList.contains('on'));
  assert(on.length === 1 && on[0].getAttribute('href') === '#/editor',
    'only the chosen tab is marked');
  assert(on[0].getAttribute('aria-current') === 'page' &&
    after.filter(a => a.hasAttribute('aria-current')).length === 1,
    'only the chosen tab is the current page for screen readers');
  window.close();
  console.log(`nav: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main();
