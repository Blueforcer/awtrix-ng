<!-- Thanks for the pull request. Keep this short; it is a checklist, not an essay. -->

## What this changes

<!-- And why. If it fixes an issue: "Fixes #123". -->

## How it was verified

<!--
Say what you actually ran. "Simulator only" is a useful answer, not an
apology. If you tested on hardware, name the board.
-->

- [ ] `pio test -e native` passes
- [ ] `pio run -e awtrix`, `pio run -e awtrix_s3_octal` and `pio run -e awtrix_s3_quad` build
- [ ] Tested in the simulator (`pio run -e native_sim`)
- [ ] Tested on hardware — board:

## Checklist

- [ ] New behaviour has a host test in `test/`, or it genuinely cannot run off-device
- [ ] `src/core/` stayed portable (no Arduino, FastLED or board headers)
- [ ] Documentation in `docs/` updated for any new field, endpoint or setting
- [ ] Regenerated any checked-in generated file this change invalidates
      (see the table in [CONTRIBUTING.md](../CONTRIBUTING.md))
