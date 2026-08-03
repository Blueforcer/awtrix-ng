#!/usr/bin/env python3
"""Checks that the solidified Berry prelude matches its sources.

src/core/script/PreludeSolidified.h is generated: it is the prelude compiled to
C const data so the device runs it from flash instead of parsing ~17 KB of Berry
into heap protos at every boot. Nothing in the normal build regenerates it, so
without this check a stale header fails in exactly the ways that are hardest to
notice:

1. An edit to core/script/Prelude.h simply does not reach the device. The
   firmware keeps running the previously solidified prelude, so a fixed bug
   stays broken and a new helper is missing -- while the source that was
   "obviously" changed sits right there in the tree.

2. A binding added to or renamed in ScriptBindings.cpp desynchronises from the
   names the bytecode references. The solidified code binds host functions by
   name, so a rename turns into a runtime "undeclared" failure inside the
   prelude rather than a build error.

3. A spliced constant changes without Prelude.h changing at all -- the prelude
   text embeds AWTRIX_MAX_MQTT_SUBS from ScriptServices.h through a C macro.
   The fingerprint below is taken over the EXPANDED prelude for that reason.

Fix a failure with:

    python scripts/gen_prelude_solidified.py

and commit the regenerated header together with lib/berry/generate, whose const
string table is produced in the same run.

Run: python tools/check_prelude_solidified.py
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import gen_prelude_solidified as gen

REGEN = "run `python scripts/gen_prelude_solidified.py` and commit the result"


def fail(message):
    print("FAIL: " + message)
    return 1


def main():
    header = gen.OUT_HEADER
    rel = os.path.relpath(header, ROOT)

    if not os.path.isfile(header):
        return fail("%s is missing -- %s" % (rel, REGEN))

    with open(header, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    if "be_local_closure(awtrix_prelude," not in text:
        return fail("%s does not define awtrix_prelude -- %s" % (rel, REGEN))

    recorded = gen.stamp_of(header)
    if not recorded:
        return fail("%s carries no %s stamp -- %s"
                    % (rel, gen.STAMP_PREFIX.strip(), REGEN))

    os.makedirs(gen.BUILD, exist_ok=True)
    try:
        current = gen.input_fingerprint()
    except SystemExit as exc:
        return fail("cannot compute the prelude fingerprint (%s)" % exc)

    if recorded != current:
        return fail("%s is stale: it was generated from prelude+bindings %s, "
                    "the tree now hashes to %s -- %s"
                    % (rel, recorded[:12], current[:12], REGEN))

    print("prelude solidified: %s current (%s)" % (rel, current[:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
