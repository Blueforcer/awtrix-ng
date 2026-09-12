const { boot, goto, flush } = require('./harness');

let pass = 0, fail = 0;
function assert(cond, msg) {
  if (cond) pass++;
  else { fail++; console.error('  ✗ ' + msg); }
}

async function openDashboard(autoBrightness) {
  const ctx = await boot();
  ctx.store.settings.autoBrightness = autoBrightness;
  await goto(ctx.window, '#/system');
  await goto(ctx.window, '#/');
  return ctx;
}

const autoSwitch = window => window.document.querySelector('input[aria-label="Auto brightness"]');

async function testShowsStoredAutoBrightness() {
  const { window, store } = await openDashboard(true);
  const control = autoSwitch(window);
  assert(!!control, 'dashboard shows the Auto brightness switch');
  assert(control && control.checked, 'Auto brightness reflects the stored setting');
  const brightness = window.document.querySelector('input[type=range][aria-label="Brightness"]');
  assert(brightness && brightness.disabled,
    'manual brightness is disabled while Auto brightness is on');
  control.checked = false;
  control.dispatchEvent(new window.Event('change', { bubbles: true }));
  await flush(20);
  assert(store.settingsPatch && store.settingsPatch.autoBrightness === false,
    'changing Auto brightness PATCHes the setting');
  assert(!brightness.disabled, 'manual brightness is enabled again in manual mode');
  window.close();
}

async function main() {
  await testShowsStoredAutoBrightness();
  await flush(20);
  console.log(`dashboard: ${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
}
main();
