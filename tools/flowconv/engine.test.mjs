// Golden tests for the flow converter on the migration page: every fixture
// under fixtures/ is one real-world flow shape, expected.txt is the blessed
// output and expected.json the blessed metadata. The idempotence loop is the
// core invariant -- converting a converted flow must change nothing, or the
// widget would mangle payloads that are already NG.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

import { convert } from "../../docs/assets/flow-converter/engine.js";
import { WARNINGS } from "../../docs/assets/flow-converter/maps.js";

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), "fixtures");

for (const name of readdirSync(FIXTURES)) {
  const dir = join(FIXTURES, name);
  const inputFile = readdirSync(dir).find((f) => f.startsWith("input."));
  const input = readFileSync(join(dir, inputFile), "utf8");
  const expected = readFileSync(join(dir, "expected.txt"), "utf8");
  const meta = JSON.parse(readFileSync(join(dir, "expected.json"), "utf8"));

  test(`fixture ${name}: converts to the blessed output`, () => {
    const r = convert(input);
    assert.equal(r.output, expected);
    assert.equal(r.inputType, meta.inputType);
    assert.equal(r.alreadyNg, meta.alreadyNg);
    assert.deepEqual([...new Set(r.changes.map((c) => c.kind))].sort(), meta.changeKinds);
    assert.deepEqual([...new Set(r.warnings.map((w) => w.code))].sort(), meta.warningCodes);
  });

  test(`fixture ${name}: converting the output again changes nothing`, () => {
    const second = convert(convert(input).output);
    assert.deepEqual(second.changes, []);
    assert.equal(second.output, convert(input).output);
  });

  test(`fixture ${name}: every emitted warning code exists in WARNINGS`, () => {
    for (const w of convert(input).warnings) {
      assert.ok(w.code in WARNINGS, `unknown warning code ${w.code}`);
      assert.ok(w.message && !/\{(key|value|where|note)\}/.test(w.message), `unfilled template in: ${w.message}`);
    }
  });
}
