// Unit tests for the flow-converter engine's sharp edges: the Jinja masking
// scanner, the shell lexer, the format-string parsers and the per-body
// transformers. The golden tests cover composition; these cover the corners.
import { test } from "node:test";
import assert from "node:assert/strict";

import {
  maskTemplates, lexShell, parseTformat, parseDformat,
  transformAppPayload, transformBody, convertJsonIslandText,
  rewriteTopicValue, rewriteUrl, detectInputType, convert,
} from "../../docs/assets/flow-converter/engine.js";

function ctx() {
  return {
    changes: [], warnings: [], warningKeys: new Set(),
    sentinelIds: new Set(), wrapMs: new Set(),
    a3MarkerSeen: false, pendingTemplatedTopics: [],
  };
}

// ------------------------------------------------------- template masking ---

test("masking replaces templates outside strings and leaves strings alone", () => {
  const m = maskTemplates('{"a": {{ x }}, "b": "{{ y }}"}');
  assert.equal(m.tokens.length, 1);
  assert.ok(m.masked.includes('"{{ y }}"'));
  assert.doesNotThrow(() => JSON.parse(m.masked));
});

test("masking survives braces and quotes inside the template", () => {
  const m = maskTemplates('{"a": {{ {"k": 1}["k"] }}, "b": {{ "}}" }}}');
  assert.equal(m.tokens.length, 2);
  assert.doesNotThrow(() => JSON.parse(m.masked));
});

test("masking handles {% %} and whitespace-control forms", () => {
  const m = maskTemplates('{%- if x -%}{"a": 1}{%- endif -%}');
  assert.equal(m.tokens.length, 2);
});

test("masking picks a sentinel base that does not collide with the input", () => {
  const m = maskTemplates('{"a": 880000001, "b": {{ x }}}');
  assert.ok(!String(880000001).includes(String(m.tokens[0].id)) &&
            m.tokens[0].id !== 880000001);
});

test("unbalanced template returns null", () => {
  assert.equal(maskTemplates('{"a": {{ x }'), null);
});

// ------------------------------------------------------------ shell lexer ---

test("lexer: quotes, escapes and continuations", () => {
  const t = lexShell("curl -d '{\"a\": \"it''s\"}' \\\n  \"http://x/api/notify\"", null);
  const values = t.map((x) => x.value);
  assert.ok(values.includes("curl"));
  assert.ok(values.some((v) => v.startsWith('{"a"')));
  assert.ok(values.includes("http://x/api/notify"));
});

test("lexer: $'...' is tolerated but flagged", () => {
  const c = ctx();
  lexShell("curl -d $'{\"a\":1}' http://x/api/notify", c);
  assert.ok(c.warnings.some((w) => w.code === "nonPosixQuoting"));
});

// -------------------------------------------------------- format parsers ---

test("TFORMAT parser table", () => {
  assert.deepEqual(parseTformat("%H:%M"), {
    time24h: true, timeLeadingZero: true, timeShowSeconds: false,
    timeShowAmPm: false, timeSeparatorMode: "steady",
  });
  assert.equal(parseTformat("%H %M").timeSeparatorMode, "blink");
  assert.deepEqual(parseTformat("%l:%M %p"), {
    time24h: false, timeLeadingZero: false, timeShowSeconds: false,
    timeShowAmPm: true, timeSeparatorMode: "steady",
  });
  assert.equal(parseTformat("%H:%M:%S").timeShowSeconds, true);
  assert.equal(parseTformat("%I:%M").timeLeadingZero, true);
  assert.equal(parseTformat("whatever"), null);
  assert.equal(parseTformat("%H-%M"), null);
});

test("DFORMAT parser table", () => {
  assert.deepEqual(parseDformat("%d.%m.%y"), {
    dateOrder: "dayMonthYear", dateSeparator: "dot", dateYearMode: "twoDigit",
  });
  assert.deepEqual(parseDformat("%m/%d/%Y"), {
    dateOrder: "monthDayYear", dateSeparator: "slash", dateYearMode: "fourDigit",
  });
  assert.equal(parseDformat("%d-%m").dateYearMode, "none");
  assert.equal(parseDformat("%d %m"), null);
  assert.equal(parseDformat("no tokens"), null);
});

// ------------------------------------------------------------ transformers ---

test("float seconds round to whole milliseconds", () => {
  const c = ctx();
  assert.equal(transformAppPayload({ duration: 1.5 }, "app", c).durationMs, 1500);
});

test("enum out of range keeps the key and warns", () => {
  const c = ctx();
  const out = transformAppPayload({ pushIcon: 7 }, "app", c);
  assert.equal(out.pushIcon, 7);
  assert.ok(c.warnings.some((w) => w.code === "enumOutOfRange"));
});

test("notification loses its lifetime keys with a warning", () => {
  const c = ctx();
  const out = transformAppPayload({ text: "x", lifetime: 60, lifetimeMode: 1 }, "notification", c);
  assert.ok(!("lifetimeMs" in out) && !("lifetime" in out) && !("lifetimeExpiry" in out));
  assert.ok(c.warnings.some((w) => w.code === "lifetimeIgnoredOnNotify"));
});

test("hold true with a duration draws the note", () => {
  const c = ctx();
  transformAppPayload({ hold: true, duration: 5 }, "notification", c);
  assert.ok(c.warnings.some((w) => w.code === "holdIgnoresDuration"));
});

test("notification-only keys on an app warn but still convert", () => {
  const c = ctx();
  const out = transformAppPayload({ rtttl: "x:d=4:c" }, "app", c);
  assert.equal(out.soundRtttl, "x:d=4:c");
  assert.ok(c.warnings.some((w) => w.code === "notificationOnlyKey"));
});

test("unknown draw code stays an object and warns", () => {
  const c = ctx();
  const out = transformAppPayload({ draw: [{ dz: [1, 2] }] }, "app", c);
  assert.deepEqual(out.draw, [{ dz: [1, 2] }]);
  assert.ok(c.warnings.some((w) => w.code === "unknownDrawCode"));
});

test("indicator off becomes a delete, otherwise blink/fade get the Ms suffix", () => {
  assert.deepEqual(transformBody("indicator", { color: 0 }, ctx()), { delete: true });
  assert.deepEqual(transformBody("indicator", {}, ctx()), { delete: true });
  const out = transformBody("indicator", { color: "#fff", blink: 500, fade: 200 }, ctx());
  assert.deepEqual(out, { color: "#fff", blinkMs: 500, fadeMs: 200 });
});

test("reorder always carries the required disabled list", () => {
  const out = transformBody("reorder", [{ name: "a" }], ctx());
  assert.deepEqual(out, { order: ["a"], disabled: [] });
});

test("sleep seconds become durationMs", () => {
  assert.deepEqual(transformBody("sleep", { sleep: 90 }, ctx()), { durationMs: 90000 });
});

// -------------------------------------------------------- island decisions ---

test("an already-NG payload island is left untouched", () => {
  assert.equal(convertJsonIslandText('{"text":"x","durationMs":5000}', null, ctx(), "w"), null);
});

test("a shared-keys-only payload without context is left untouched", () => {
  assert.equal(convertJsonIslandText('{"text":"x","icon":"1"}', null, ctx(), "w"), null);
});

test("a context-free payload with an AWTRIX 3 key converts, guessing the kind", () => {
  const r = convertJsonIslandText('{"text":"x","duration":5,"sound":"a"}', null, ctx(), "w");
  assert.ok(r.text.includes('"durationMs":5000'));
});

test("a template in key position leaves the island alone with a warning", () => {
  const c = ctx();
  assert.equal(convertJsonIslandText('{{{ key }}: 1, "duration": 5}', null, c, "w"), null);
  assert.ok(c.warnings.some((w) => w.code === "jinjaPayload"));
});

// ------------------------------------------------------- topics and URLs ---

test("topic suffixes match at the end, whatever the prefix holds", () => {
  assert.equal(rewriteTopicValue("home/floor2/custom/x", ctx()).topic,
    "home/floor2/cmd/apps/pushed/x");
  assert.equal(rewriteTopicValue("p/with/custom/inside/custom/x", ctx()).topic,
    "p/with/custom/inside/cmd/apps/pushed/x");
  assert.equal(rewriteTopicValue("custom/x", ctx()).topic, "cmd/apps/pushed/x");
  assert.equal(rewriteTopicValue("p/notify/dismiss", ctx()).topic, "cmd/notify/dismiss".replace(/^/, "p/"));
  assert.equal(rewriteTopicValue("awtrixNG/cmd/apps/pushed/x", ctx()), null);
  assert.equal(rewriteTopicValue("zigbee2mqtt/kitchen/light", ctx()), null);
});

test("erase has no MQTT topic and only warns", () => {
  const c = ctx();
  assert.equal(rewriteTopicValue("awtrix/erase", c), null);
  assert.ok(c.warnings.some((w) => w.code === "mqttNoTopic"));
});

test("URLs: name query moves into the path, NG and foreign URLs pass", () => {
  const r = rewriteUrl("http://x/api/custom?name=weather", "POST", false, ctx());
  assert.equal(r.url, "http://x/api/v1/apps/pushed/weather");
  assert.equal(r.method, "PUT");
  assert.equal(rewriteUrl("http://x/api/custom", "POST", false, ctx()), null);
  assert.equal(rewriteUrl("http://x/api/v1/apps", "GET", false, ctx()), null);
  assert.equal(rewriteUrl("http://example.com/api/other", "GET", false, ctx()), null);
});

test("apps is reorder on POST and a read on GET", () => {
  assert.equal(rewriteUrl("http://x/api/apps", "POST", false, ctx()).method, "PUT");
  assert.equal(rewriteUrl("http://x/api/apps", "GET", false, ctx()).url, "http://x/api/v1/apps");
});

// --------------------------------------------------------------- detection ---

test("input detection", () => {
  assert.equal(detectInputType("curl http://x/api/notify"), "curl");
  assert.equal(detectInputType('[{"type":"inject","wires":[]}]'), "node-red");
  assert.equal(detectInputType('{"nodes":[],"connections":{}}'), "n8n");
  assert.equal(detectInputType('{"text":"x"}'), "json-payload");
  assert.equal(detectInputType("service: mqtt.publish\ndata:\n  topic: a/b"), "ha-yaml");
  assert.equal(detectInputType("hello world"), "unknown");
});

test("convert never throws and reports garbage as unchanged", () => {
  const r = convert(" ￿{{{");
  assert.equal(typeof r.output, "string");
});
