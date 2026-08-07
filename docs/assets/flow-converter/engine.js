// The flow converter's engine: pure functions, no DOM, importable from node --
// the tests under tools/flowconv/ and the drift check both run against this
// exact file, ui.js only renders what convert() returns.
//
// The design is island rewriting, not parse-and-re-emit: URLs, MQTT topics and
// JSON payload islands are replaced in place and every byte outside an island
// is left untouched. That is what keeps YAML comments, blueprint structure and
// the user's own formatting intact. The two JSON export formats (Node-RED,
// N8N) are the exception -- they are machine-written, so those are parsed and
// re-emitted whole, but only when something actually changed.

import {
  KEY_MAP, NG_ONLY_KEYS, DRAW_MAP, TEFF_NAMES, SETTINGS_MAP,
  ENDPOINT_MAP, TOPIC_MAP, WARNINGS,
} from "./maps.js";

// Keys that mean "this payload is already NG": every rename target plus the
// NG-only additions. A payload containing one of these and no AWTRIX-3-only
// key is passed through untouched.
const NG_KEY_SET = new Set(NG_ONLY_KEYS);
for (const spec of Object.values(KEY_MAP)) if (spec.to) NG_KEY_SET.add(spec.to);

// Keys that exist only in the AWTRIX 3 dialect -- their presence is what makes
// a payload convertible. Shared names (text, icon, hold, ...) prove nothing.
const A3_ONLY_KEYS = new Set(Object.entries(KEY_MAP)
  .filter(([k, s]) => (s.to && s.to !== k) ||
    ["palette", "scroll", "effectSettings", "dead"].includes(s.kind))
  .map(([k]) => k));

// Notification-only keys decide the app-vs-notification guess when a payload
// has no endpoint context.
const NOTIFY_KEYS = Object.entries(KEY_MAP)
  .filter(([, s]) => s.notificationOnly).map(([k]) => k);

// ---------------------------------------------------------------- context ---

function newCtx() {
  return {
    changes: [], warnings: [], warningKeys: new Set(),
    sentinelIds: new Set(), wrapMs: new Set(),
    a3MarkerSeen: false, pendingTemplatedTopics: [],
  };
}

function change(ctx, kind, before, after, note) {
  const c = { kind, before, after };
  if (note) c.note = note;
  ctx.changes.push(c);
}

function warn(ctx, code, subs = {}) {
  const spec = WARNINGS[code];
  let message = spec.message;
  for (const [k, v] of Object.entries(subs)) {
    message = message.split("{" + k + "}").join(String(v));
  }
  message = message.replace(" {note}", subs.note ? " " + subs.note : "");
  // One warning per (code, subject) -- a key repeated in five payloads is one
  // problem, not five list entries.
  const dedup = code + "\u0000" + (subs.key ?? subs.where ?? "");
  if (ctx.warningKeys.has(dedup)) return;
  ctx.warningKeys.add(dedup);
  const w = { code, message, page: spec.page };
  if (subs.anchor || spec.anchor) w.anchor = subs.anchor || spec.anchor;
  if (subs.where) w.where = subs.where;
  ctx.warnings.push(w);
}

// ------------------------------------------------------- template masking ---

// Home Assistant payloads are JSON with Jinja spliced in -- `"duration": {{ x }}`
// is not parseable as it stands. Each {{...}} / {%...%} / {#...#} span outside
// a JSON string is replaced by a unique number literal, the result is parsed
// and transformed, and the numbers are swapped back at the end. Templates
// inside JSON strings are opaque string content and stay where they are.
export function maskTemplates(text) {
  let base = 880000001;
  while (text.includes(String(base))) base += 7919;
  const tokens = [];
  let out = "", i = 0, inStr = false;
  while (i < text.length) {
    const c = text[i];
    if (inStr) {
      out += c;
      if (c === "\\" && i + 1 < text.length) { out += text[i + 1]; i += 2; continue; }
      if (c === '"') inStr = false;
      i++;
      continue;
    }
    if (c === '"') { inStr = true; out += c; i++; continue; }
    const two = text.slice(i, i + 2);
    if (two === "{{" || two === "{%" || two === "{#") {
      const end = findTemplateEnd(text, i + 2, two);
      if (end < 0) return null;
      const id = base + tokens.length;
      tokens.push({ id, raw: text.slice(i, end), kind: two });
      out += String(id);
      i = end;
      continue;
    }
    out += c;
    i++;
  }
  return { masked: out, tokens };
}

// Finds the end (exclusive) of a template that started with `open` at
// `from - 2`. Tracks quotes and brace depth so `{{ "}}" }}` and
// `{{ {'a': 1} }}` close where Jinja would close them.
function findTemplateEnd(text, from, open) {
  const close = open === "{{" ? "}" : open === "{%" ? "%" : "#";
  let depth = 0, quote = null;
  for (let i = from; i < text.length; i++) {
    const c = text[i];
    if (quote) { if (c === quote) quote = null; continue; }
    if (c === "'" || c === '"') { quote = c; continue; }
    if (open === "{{") {
      if (c === "{") depth++;
      else if (c === "}") {
        if (depth > 0) depth--;
        else if (text[i + 1] === "}") return i + 2;
        else return -1;
      }
    } else if (c === close && text[i + 1] === "}") {
      return i + 2;
    }
  }
  return -1;
}

function restoreTemplates(json, tokens, ctx) {
  for (const t of tokens) {
    let repl = t.raw;
    if (ctx.wrapMs.has(t.id) && t.kind === "{{") {
      const inner = t.raw.slice(2, -2).replace(/^-|-$/g, "").trim();
      repl = "{{ ( " + inner + " ) * 1000 }}";
    }
    json = json.split(String(t.id)).join(repl);
  }
  return json;
}

// ------------------------------------------------------ body transformers ---

function isSentinel(ctx, v) {
  return typeof v === "number" && ctx.sentinelIds.has(v);
}

function toMs(ctx, key, to, v, out) {
  if (isSentinel(ctx, v)) {
    ctx.wrapMs.add(v);
    out[to] = v;
    change(ctx, "unit", key, to, "template value multiplied by 1000");
    return;
  }
  const n = typeof v === "number" ? v : (typeof v === "string" && v.trim() !== "" ? Number(v) : NaN);
  if (Number.isNaN(n)) {
    out[key] = v;
    warn(ctx, "valueNotNumeric", { key });
    return;
  }
  out[to] = Math.round(n * 1000);
  change(ctx, "unit", key + ": " + JSON.stringify(v), to + ": " + out[to]);
}

function convertDraw(v, ctx) {
  if (!Array.isArray(v)) return v;
  let converted = false;
  const out = v.map((item) => {
    if (Array.isArray(item)) return item;
    if (item && typeof item === "object") {
      const keys = Object.keys(item);
      if (keys.length === 1 && DRAW_MAP[keys[0]] && Array.isArray(item[keys[0]])) {
        converted = true;
        return [DRAW_MAP[keys[0]], ...item[keys[0]]];
      }
      warn(ctx, "unknownDrawCode", { key: keys[0] ?? "?" });
    }
    return item;
  });
  if (converted) change(ctx, "draw", "command objects", "command arrays");
  return out;
}

function convertTextFragments(v, ctx) {
  if (!Array.isArray(v)) return v;
  let converted = false;
  const out = v.map((item) => {
    if (item && typeof item === "object" && "t" in item) {
      converted = true;
      const frag = { text: item.t };
      if ("c" in item) frag.color = item.c;
      return frag;
    }
    return item;
  });
  if (converted) change(ctx, "key", 'fragments {"t", "c"}', '{"text", "color"}');
  return out;
}

// The pushed-app / notification payload -- the heart of the key map. Original
// key order is preserved; converted keys stay in their place.
export function transformAppPayload(obj, kind, ctx) {
  const out = {};
  let wantsPalette = false;
  let scrollObj = null;
  for (const [k, v] of Object.entries(obj)) {
    const spec = KEY_MAP[k];
    if (!spec) {
      out[k] = v;
      if (!NG_KEY_SET.has(k)) warn(ctx, "unmappedKey", { key: k });
      continue;
    }
    if (spec.notificationOnly && kind === "app") {
      warn(ctx, "notificationOnlyKey", { key: spec.to || k });
    }
    if (kind === "notification" && (k === "lifetime" || k === "lifetimeMode")) {
      change(ctx, "strip", k, "", "never read on a notification");
      warn(ctx, "lifetimeIgnoredOnNotify", { key: k });
      continue;
    }
    switch (spec.kind) {
      case "keep":
        out[k] = v;
        if (k === "effect") warn(ctx, "effectNames", {});
        break;
      case "rename":
        out[spec.to] = v;
        change(ctx, "key", k, spec.to);
        break;
      case "seconds":
        toMs(ctx, k, spec.to, v, out);
        break;
      case "enum":
        if (Number.isInteger(v) && v >= 0 && v < spec.values.length) {
          out[spec.to] = spec.values[v];
          change(ctx, "value", k + ": " + v, spec.to + ': "' + spec.values[v] + '"');
        } else if (typeof v === "string" && spec.values.includes(v)) {
          out[spec.to] = v;                          // already the NG word
          if (spec.to !== k) change(ctx, "key", k, spec.to);
        } else {
          out[k] = v;
          warn(ctx, "enumOutOfRange", { key: k, value: JSON.stringify(v) });
        }
        break;
      case "palette":
        if (k === "gradient") {
          if (Array.isArray(v)) {
            out.palette = v;
            wantsPalette = true;
            change(ctx, "key", "gradient", 'palette + textColor: "palette"');
          } else {
            change(ctx, "strip", "gradient: " + JSON.stringify(v), "", "off is the default");
          }
        } else if (v) {                               // rainbow: true
          out.palette = "Rainbow";
          wantsPalette = true;
          change(ctx, "key", "rainbow", 'palette: "Rainbow" + textColor: "palette"');
        } else {
          change(ctx, "strip", "rainbow: false", "", "off is the default");
        }
        break;
      case "scroll":
        scrollObj = scrollObj || {};
        if (k === "noScroll") {
          if (v) scrollObj.mode = "static";
          change(ctx, "key", "noScroll", 'scroll: {"mode": "static"}');
        } else {
          scrollObj.speed = v;
          change(ctx, "key", "scrollSpeed", 'scroll: {"speed": ...}');
        }
        break;
      case "effectSettings": {
        const es = v && typeof v === "object" ? v : {};
        for (const [ek, ev] of Object.entries(es)) {
          if (ek === "speed") { out.effectSpeed = ev; change(ctx, "key", "effectSettings.speed", "effectSpeed", "now a 0.1-10 multiplier of the normal pace"); }
          else if (ek === "palette") { out.palette = ev; change(ctx, "key", "effectSettings.palette", "palette"); }
          else if (ek === "blend") { out.paletteBlend = ev; change(ctx, "key", "effectSettings.blend", "paletteBlend"); }
          else warn(ctx, "unmappedKey", { key: "effectSettings." + ek });
        }
        break;
      }
      case "draw":
        out.draw = convertDraw(v, ctx);
        break;
      case "text":
        out.text = convertTextFragments(v, ctx);
        break;
      case "special":
        if (k === "repeat") {
          out.repeat = v === -1 ? 0 : v;
          if (v === -1) change(ctx, "value", "repeat: -1", "repeat: 0", "0 turns repeat off in NG");
        } else if (k === "overlay") {
          out.overlay = v === "clear" ? "" : v;
          if (v === "clear") change(ctx, "value", 'overlay: "clear"', 'overlay: ""');
        } else {
          out[k] = v;
        }
        break;
      case "dead":
        out[k] = v;
        warn(ctx, "deadKey", { key: k, note: spec.note, anchor: spec.anchor });
        break;
    }
  }
  if (scrollObj) {
    out.scroll = Object.assign(
      {}, typeof out.scroll === "object" && out.scroll ? out.scroll : {}, scrollObj);
  }
  if (wantsPalette) out.textColor = "palette";
  if (out.hold === true && ("durationMs" in out || "duration" in obj)) {
    warn(ctx, "holdIgnoresDuration", {});
  }
  return out;
}

// TFORMAT was an strftime-ish string; NG spells the same choices as words.
// Returns null when the string holds anything the words cannot express.
export function parseTformat(s) {
  if (typeof s !== "string") return null;
  const hour = s.match(/%[HkIl]/);
  if (!hour || !s.includes("%M")) return null;
  const sepMatch = s.match(/%[HkIl](.*?)%M/);
  const sep = sepMatch ? sepMatch[1] : null;
  if (sep !== ":" && sep !== " ") return null;
  const rest = s.replace(/%[HkIlMSp]/g, "").replace(/[: ]/g, "");
  if (rest !== "") return null;
  return {
    time24h: hour[0] === "%H" || hour[0] === "%k",
    timeLeadingZero: hour[0] === "%H" || hour[0] === "%I",
    timeShowSeconds: s.includes("%S"),
    timeShowAmPm: s.includes("%p"),
    timeSeparatorMode: sep === ":" ? "steady" : "blink",
  };
}

export function parseDformat(s) {
  if (typeof s !== "string") return null;
  const seq = s.match(/%[dmyY]/g);
  if (!seq || !seq.includes("%d") || !seq.includes("%m")) return null;
  const rest = s.replace(/%[dmyY]/g, "");
  const seps = new Set(rest.split(""));
  if (seps.size > 1) return null;
  const sepChar = rest[0] ?? ".";
  const sepWord = { ".": "dot", "/": "slash", "-": "dash" }[sepChar];
  if (!sepWord) return null;
  const first = seq[0];
  return {
    dateOrder: first === "%d" ? "dayMonthYear" : first === "%m" ? "monthDayYear" : "yearMonthDay",
    dateSeparator: sepWord,
    dateYearMode: seq.includes("%Y") ? "fourDigit" : seq.includes("%y") ? "twoDigit" : "none",
  };
}

function setNested(out, path, v) {
  const [parent, field] = path.split(".");
  if (typeof out[parent] !== "object" || out[parent] === null) out[parent] = {};
  out[parent][field] = v;
}

export function transformSettingsPayload(obj, ctx) {
  const out = {};
  for (const [k, v] of Object.entries(obj)) {
    const spec = SETTINGS_MAP[k];
    if (!spec) {
      out[k] = v;
      warn(ctx, "settingsNoEquivalent", { key: k });
      continue;
    }
    switch (spec.kind) {
      case "rename":
        out[spec.to] = v;
        change(ctx, "key", k, spec.to);
        break;
      case "seconds":
        toMs(ctx, k, spec.to, v, out);
        break;
      case "teff":
        if (Number.isInteger(v) && v >= 0 && v < TEFF_NAMES.length) {
          out[spec.to] = TEFF_NAMES[v];
          change(ctx, "value", k + ": " + v, spec.to + ': "' + TEFF_NAMES[v] + '"');
        } else {
          out[k] = v;
          warn(ctx, "teffOutOfRange", { value: JSON.stringify(v) });
        }
        break;
      case "colorOrNull":
        out[spec.to] = v === 0 ? null : v;
        change(ctx, "key", k, spec.to, v === 0 ? "0 meant the global color; NG spells that null" : undefined);
        break;
      case "nested":
        setNested(out, spec.to, v);
        change(ctx, "key", k, spec.to);
        break;
      case "tformat": {
        const t = parseTformat(v);
        if (t) { Object.assign(out, t); change(ctx, "key", "TFORMAT: " + JSON.stringify(v), "time24h/timeLeadingZero/timeShowSeconds/timeShowAmPm/timeSeparatorMode"); }
        else { out[k] = v; warn(ctx, "settingsNoEquivalent", { key: k }); }
        break;
      }
      case "dformat": {
        const d = parseDformat(v);
        if (d) { Object.assign(out, d); change(ctx, "key", "DFORMAT: " + JSON.stringify(v), "dateOrder/dateSeparator/dateYearMode"); }
        else { out[k] = v; warn(ctx, "settingsNoEquivalent", { key: k }); }
        break;
      }
      case "warn":
        out[k] = v;
        warn(ctx, spec.warning, { key: k, note: spec.note, anchor: spec.anchor });
        break;
    }
  }
  return out;
}

// The remaining bodies are small. Each returns the new body, or
// {delete: true} where the AWTRIX 3 payload meant "turn it off" and NG wants
// a DELETE (HTTP) or an empty publish (MQTT) instead.
export function transformBody(bodyKind, v, ctx) {
  switch (bodyKind) {
    case "app": return transformAppPayload(v, "app", ctx);
    case "notification": return transformAppPayload(v, "notification", ctx);
    case "settings": return transformSettingsPayload(v, ctx);
    case "indicator": {
      if (!v || typeof v !== "object" || Object.keys(v).length === 0 || v.color === 0) {
        change(ctx, "structure", 'indicator off ({"color": 0} or empty)', "delete call");
        return { delete: true };
      }
      const out = {};
      for (const [k, val] of Object.entries(v)) {
        if (k === "blink") { out.blinkMs = val; change(ctx, "key", "blink", "blinkMs"); }
        else if (k === "fade") { out.fadeMs = val; change(ctx, "key", "fade", "fadeMs"); }
        else { out[k] = val; if (!["color", "blinkMs", "fadeMs"].includes(k)) warn(ctx, "unmappedKey", { key: k }); }
      }
      return out;
    }
    case "reorder": {
      if (!Array.isArray(v)) return v;
      const order = [], disabled = [];
      for (const item of v) {
        if (item && typeof item === "object" && item.name) {
          (item.show === false ? disabled : order).push(item.name);
        }
      }
      change(ctx, "structure", '[{"name", "show"}, ...]', '{"order": [...], "disabled": [...]}',
        '"disabled" is required, an empty list is fine');
      return { order, disabled };
    }
    case "sleep": {
      const out = {};
      if ("sleep" in v) toMs(ctx, "sleep", "durationMs", v.sleep, out);
      for (const [k, val] of Object.entries(v)) if (k !== "sleep") { out[k] = val; warn(ctx, "unmappedKey", { key: k }); }
      return out;
    }
    case "sound": {
      const out = {};
      for (const [k, val] of Object.entries(v)) {
        if (k === "sound") { out.name = val; change(ctx, "key", "sound", "name"); }
        else { out[k] = val; warn(ctx, "unmappedKey", { key: k }); }
      }
      return out;
    }
    case "r2d2":
      change(ctx, "structure", "/api/r2d2", '{"builtin": "r2d2"}');
      return { builtin: "r2d2" };
    case "power":
    case "moodlight": {
      if (bodyKind === "moodlight" && (!v || Object.keys(v).length === 0)) {
        change(ctx, "structure", "moodlight off (empty payload)", "delete call");
        return { delete: true };
      }
      return v;
    }
    case "switch":
      return v;
    default:
      return v;
  }
}

// ------------------------------------------------ JSON island conversion ---

// Takes the raw text of one payload island and returns the converted text, or
// null when the island should stay byte-identical (already NG, not JSON, or
// templates the converter must not touch). {delete: true} bubbles up from the
// off-payloads.
export function convertJsonIslandText(text, bodyKind, ctx, where) {
  const trimmed = text.trim();
  if (trimmed === "" || trimmed === "{}") return null;
  if (bodyKind === "rtttlRaw") {
    change(ctx, "structure", "raw RTTTL body", '{"rtttl": "..."}');
    return { text: JSON.stringify({ rtttl: trimmed }) };
  }
  const masked = maskTemplates(text);
  if (masked === null) {
    ctx.a3MarkerSeen = true;
    warn(ctx, "jinjaPayload", { where });
    return null;
  }
  if (/"(duration|lifetime|pushIcon|scrollSpeed|noScroll|gradient|rainbow|topText|dp|dl|dt|dr|df|dc|dfc|db)"/.test(text)) {
    ctx.a3MarkerSeen = true;
  }
  let parsed;
  try {
    parsed = JSON.parse(masked.masked);
  } catch {
    if (masked.tokens.length > 0) warn(ctx, "jinjaPayload", { where });
    return null;
  }
  if (parsed === null || typeof parsed !== "object") {
    if (masked.tokens.length > 0) warn(ctx, "jinjaPayload", { where });
    return null;
  }
  if (bodyKind == null) {
    // No endpoint tells us what this is -- only convert what is provably an
    // AWTRIX 3 payload: at least one AWTRIX-3-only key and nothing NG-only.
    const keys = Object.keys(parsed);
    if (!keys.some((k) => A3_ONLY_KEYS.has(k))) return null;
    if (keys.some((k) => NG_KEY_SET.has(k) && !KEY_MAP[k])) return null;
    bodyKind = keys.some((k) => NOTIFY_KEYS.includes(k)) ? "notification" : "app";
  }
  ctx.sentinelIds = new Set(masked.tokens.map((t) => t.id));
  const before = JSON.stringify(parsed);
  const result = transformBody(bodyKind, parsed, ctx);
  if (result && result.delete) return { delete: true };
  if (JSON.stringify(result) === before) return null;
  const multiline = /\n/.test(trimmed);
  let json = multiline ? JSON.stringify(result, null, 2) : JSON.stringify(result);
  json = restoreTemplates(json, masked.tokens, ctx);
  if (new TextEncoder().encode(json).length > 8192) warn(ctx, "payloadTooLarge", {});
  return { text: json };
}

// ------------------------------------------------------- topic rewriting ---

const TOPIC_MATCHERS = TOPIC_MAP.map((entry) => {
  const pat = entry.a3.replace(/[/]/g, "\\/").replace("\\/{name}", "\\/([A-Za-z0-9_-]+)");
  return { entry, re: new RegExp("^(?:(.*)\\/)?" + pat + "$") };
});

// Rewrites one MQTT topic string. The prefix -- whatever the user configured,
// template or literal -- is preserved verbatim; only the AWTRIX 3 suffix is
// renamed. Returns null when the topic is not an AWTRIX 3 topic.
export function rewriteTopicValue(topic, ctx) {
  if (typeof topic !== "string" || topic.includes("/cmd/") || topic.startsWith("cmd/") ||
      topic.includes("/state/")) return null;
  for (const { entry, re } of TOPIC_MATCHERS) {
    const m = topic.match(re);
    if (!m) continue;
    if (!entry.ng) {
      warn(ctx, entry.warning, { where: "topic `" + topic + "`" });
      return null;
    }
    const prefix = m[1] !== undefined ? m[1] + "/" : "";
    const ng = prefix + entry.ng.replace("{name}", m[2] ?? "");
    change(ctx, "topic", topic, ng);
    if (entry.warning) warn(ctx, entry.warning, { where: "topic `" + topic + "`" });
    return { topic: ng, bodyKind: entry.body ?? null };
  }
  return null;
}

// ---------------------------------------------------------- URL rewriting ---

const URL_RE = /^(https?:\/\/)?([^/\s"'#?]+)\/api\/([A-Za-z0-9_/]+)(\?[^\s"'#]*)?$/;

// Rewrites one AWTRIX 3 URL. `method` is what the flow used (may be "");
// `emptyBody` says the request body was empty (the old delete convention).
// Returns null for URLs that are not AWTRIX 3 API calls.
export function rewriteUrl(url, method, emptyBody, ctx) {
  const m = url.match(URL_RE);
  if (!m) return null;
  const a3path = m[3].replace(/\/$/, "");
  if (a3path.startsWith("v1")) return null;
  const candidates = ENDPOINT_MAP.filter((e) => e.a3 === a3path);
  if (candidates.length === 0) return null;
  const upper = (method || "").toUpperCase();
  const entry = candidates.find((e) => e.method === upper) ??
    candidates.find((e) => !e.method) ?? candidates[0];
  if (!entry.ng) {
    warn(ctx, entry.warning, { where: "`" + url + "`" });
    return null;
  }
  const query = new Map();
  if (m[4]) {
    for (const pair of m[4].slice(1).split("&")) {
      const eq = pair.indexOf("=");
      if (eq > 0) query.set(decodeURIComponent(pair.slice(0, eq)), decodeURIComponent(pair.slice(eq + 1)));
    }
  }
  let name = "";
  if (entry.nameQuery) {
    name = query.get(entry.nameQuery) ?? "";
    if (!name) return null;                 // /api/custom without a name -- leave it
  }
  const useDelete = emptyBody && entry.emptyBody;
  const target = useDelete ? entry.emptyBody : entry.ng;
  const origin = (m[1] ?? "") + m[2];
  const ngUrl = origin + target.path.replace("{name}", name);
  change(ctx, "endpoint",
    (upper || (emptyBody ? "GET" : "POST")) + " " + url, target.method + " " + ngUrl);
  if (entry.warning) warn(ctx, entry.warning, { where: "`" + url + "`" });
  return {
    url: ngUrl, method: target.method,
    bodyKind: useDelete ? null : entry.body ?? null,
    dropBody: !!useDelete,
    wantsJsonBody: !useDelete && !!entry.body,
  };
}

// ------------------------------------------------------------ curl driver ---

const VALUE_FLAGS = new Set([
  "-X", "--request", "-H", "--header", "-d", "--data", "--data-raw",
  "--data-binary", "--data-urlencode", "--json", "-u", "--user", "-o",
  "--output", "-m", "--max-time", "-F", "--form", "-T", "--upload-file",
  "-A", "--user-agent", "-b", "--cookie", "-e", "--referer",
]);

// Lexes shell text into tokens with their raw spans, honoring '...', "..."
// and backslash-newline continuations. cmd ^ and PowerShell ` continuations
// are tolerated but flagged -- the re-quoted output is POSIX.
export function lexShell(text, ctx) {
  const tokens = [];
  let i = 0;
  while (i < text.length) {
    const c = text[i];
    if (c === " " || c === "\t") { i++; continue; }
    if (c === "\\" && text[i + 1] === "\n") { i += 2; continue; }
    if (c === "\\" && text[i + 1] === "\r" && text[i + 2] === "\n") { i += 3; continue; }
    if ((c === "^" || c === "`") && (text[i + 1] === "\n" || (text[i + 1] === "\r" && text[i + 2] === "\n"))) {
      if (ctx) warn(ctx, "nonPosixQuoting", {});
      i += text[i + 1] === "\r" ? 3 : 2;
      continue;
    }
    if (c === "\n" || c === "\r") { tokens.push({ value: "\n", start: i, end: i + 1, quote: "none" }); i++; continue; }
    const start = i;
    let value = "", quote = "none";
    while (i < text.length && !" \t\n\r".includes(text[i])) {
      const q = text[i];
      if (q === "\\" && text[i + 1] === "\n") break;
      if (q === "'") {
        quote = quote === "none" ? "single" : quote;
        i++;
        while (i < text.length && text[i] !== "'") { value += text[i]; i++; }
        i++;
      } else if (q === '"') {
        quote = quote === "none" ? "double" : quote;
        i++;
        while (i < text.length && text[i] !== '"') {
          if (text[i] === "\\" && i + 1 < text.length) { value += text[i + 1]; i += 2; }
          else { value += text[i]; i++; }
        }
        i++;
      } else if (q === "$" && text[i + 1] === "'") {
        if (ctx) warn(ctx, "nonPosixQuoting", {});
        quote = "single";
        i += 2;
        while (i < text.length && text[i] !== "'") {
          if (text[i] === "\\" && i + 1 < text.length) { value += text[i + 1]; i += 2; }
          else { value += text[i]; i++; }
        }
        i++;
      } else if (q === "\\" && i + 1 < text.length) {
        value += text[i + 1];
        i += 2;
      } else {
        value += q;
        i++;
      }
    }
    tokens.push({ value, start, end: i, quote });
  }
  return tokens;
}

function quoteShell(value, quote) {
  if (quote === "double") return '"' + value.replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
  if (quote === "none" && !/[\s'"\\$&|;<>(){}]/.test(value)) return value;
  return "'" + value.replace(/'/g, "'\\''") + "'";
}

export function convertCurl(input, ctx) {
  const tokens = lexShell(input, ctx);
  const edits = [];
  // Split the token stream into commands at bare newlines.
  const commands = [];
  let current = [];
  for (const t of tokens) {
    if (t.value === "\n") { if (current.length) commands.push(current); current = []; }
    else current.push(t);
  }
  if (current.length) commands.push(current);

  for (const cmd of commands) {
    if (cmd.length === 0 || cmd[0].value !== "curl") continue;
    let urlTok = null, methodTok = null, dataTok = null;
    let hasCT = false;
    for (let i = 1; i < cmd.length; i++) {
      const t = cmd[i];
      if (t.value === "-X" || t.value === "--request") { methodTok = cmd[i + 1]; i++; continue; }
      if (t.value === "-H" || t.value === "--header") {
        if (/^content-type\s*:/i.test(cmd[i + 1]?.value ?? "")) hasCT = true;
        i++;
        continue;
      }
      if (["-d", "--data", "--data-raw", "--data-binary", "--json"].includes(t.value)) {
        dataTok = cmd[i + 1];
        if (t.value === "--json") hasCT = true;
        i++;
        continue;
      }
      if (VALUE_FLAGS.has(t.value)) { i++; continue; }
      if (t.value.startsWith("-")) continue;
      if (!urlTok && /\/api\//.test(t.value)) urlTok = t;
    }
    if (!urlTok) continue;
    const method = methodTok ? methodTok.value : (dataTok ? "POST" : "GET");
    const bodyText = dataTok ? dataTok.value : "";
    // Whether this call is the old delete-by-empty-body convention is decided
    // up front, because it changes which NG call the URL becomes.
    const entry = peekEndpoint(urlTok.value, method);
    const emptyBody = bodyText.trim() === "" || bodyText.trim() === "{}";
    let deleteSignal = emptyBody;
    if (!deleteSignal && entry?.body === "indicator") {
      try { deleteSignal = JSON.parse(bodyText).color === 0; } catch { /* keep false */ }
    }
    const res = rewriteUrl(urlTok.value, method, deleteSignal, ctx);
    if (!res) continue;

    edits.push({ start: urlTok.start, end: urlTok.end, text: quoteShell(res.url, urlTok.quote) });
    if (methodTok) {
      if (methodTok.value.toUpperCase() !== res.method) {
        edits.push({ start: methodTok.start, end: methodTok.end, text: quoteShell(res.method, methodTok.quote) });
        change(ctx, "method", methodTok.value, res.method);
      }
    } else {
      const implicit = dataTok && !res.dropBody ? "POST" : "GET";
      if (res.method !== implicit) {
        edits.push({ start: cmd[0].end, end: cmd[0].end, text: " -X " + res.method });
        change(ctx, "method", "(implicit " + implicit + ")", "-X " + res.method);
      }
    }
    if (dataTok && res.dropBody) {
      // The old delete conventions (empty body, or an indicator's
      // {"color": 0}): the NG call is a bodyless DELETE, so the whole -d pair
      // goes away, together with the blank that separated it.
      const flag = cmd[cmd.indexOf(dataTok) - 1];
      let ws = flag.start;
      while (ws > 0 && (input[ws - 1] === " " || input[ws - 1] === "\t")) ws--;
      edits.push({ start: ws, end: dataTok.end, text: "" });
      change(ctx, "structure", "body meaning delete", "DELETE without a body");
    } else if (dataTok) {
      const converted = res.bodyKind
        ? convertJsonIslandText(dataTok.value, res.bodyKind, ctx, "the request body")
        : null;
      if (converted && !converted.delete) {
        edits.push({ start: dataTok.start, end: dataTok.end, text: quoteShell(converted.text, dataTok.quote === "none" ? "single" : dataTok.quote) });
      }
      if (res.wantsJsonBody && !hasCT) {
        const flag = cmd[cmd.indexOf(dataTok) - 1];
        edits.push({ start: flag.start, end: flag.start, text: '-H "Content-Type: application/json" ' });
        change(ctx, "header", "(none)", "Content-Type: application/json", "required by NG on JSON bodies");
      }
    }
  }
  return applyEdits(input, edits);
}

// Endpoint lookup without side effects -- what rewriteUrl would match, used to
// decide the delete question before any change is recorded.
function peekEndpoint(url, method) {
  const m = url.match(URL_RE);
  if (!m) return null;
  const a3path = m[3].replace(/\/$/, "");
  if (a3path.startsWith("v1")) return null;
  const candidates = ENDPOINT_MAP.filter((e) => e.a3 === a3path);
  if (candidates.length === 0) return null;
  const upper = (method || "").toUpperCase();
  return candidates.find((e) => e.method === upper) ??
    candidates.find((e) => !e.method) ?? candidates[0];
}

function applyEdits(input, edits) {
  const sorted = [...edits].sort((a, b) => b.start - a.start || b.end - a.end);
  let out = input;
  let lastStart = Infinity;
  for (const e of sorted) {
    if (e.end > lastStart) continue;       // overlapping edit -- first one wins
    out = out.slice(0, e.start) + e.text + out.slice(e.end);
    lastStart = e.start;
  }
  return out;
}

// -------------------------------------------------------- HA YAML driver ---

const YAML_KEY_RE = /^(\s*(?:-\s+)?)([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*?)\s*$/;

function stripYamlQuotes(v) {
  if (v.length >= 2 && v[0] === '"' && v.endsWith('"')) {
    return { value: v.slice(1, -1).replace(/\\(.)/g, "$1"), codec: "double" };
  }
  if (v.length >= 2 && v[0] === "'" && v.endsWith("'")) {
    return { value: v.slice(1, -1).replace(/''/g, "'"), codec: "single" };
  }
  return { value: v, codec: "plain" };
}

function encodeYamlValue(v, codec) {
  if (codec === "double") return '"' + v.replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
  if (codec === "single") return "'" + v.replace(/'/g, "''") + "'";
  return v;
}

export function convertHaYaml(input, ctx) {
  const lines = input.split("\n");
  const offsets = [];
  let off = 0;
  for (const l of lines) { offsets.push(off); off += l.length + 1; }
  const edits = [];
  // line index -> body kind, for binding payloads to the topic/url next to them
  const contexts = new Map();

  for (let li = 0; li < lines.length; li++) {
    const m = lines[li].match(YAML_KEY_RE);
    if (!m) continue;
    const [, lead, key, rawValue] = m;
    const colonAt = lines[li].indexOf(":", lead.length + key.length - 1);
    const vs = offsets[li] + lines[li].indexOf(rawValue, colonAt + 1);
    if (key === "topic") {
      const { value, codec } = stripYamlQuotes(rawValue);
      const res = rewriteTopicValue(value, ctx);
      if (res) {
        contexts.set(li, res.bodyKind);
        edits.push({ start: vs, end: vs + rawValue.length, text: encodeYamlValue(res.topic, codec) });
      } else if ((value.includes("{{") || value.includes("!input")) && rawValue !== "") {
        ctx.pendingTemplatedTopics.push("line " + (li + 1));
        contexts.set(li, "templated");
      }
    } else if (key === "url" || key === "resource") {
      const { value, codec } = stripYamlQuotes(rawValue);
      const res = rewriteUrl(value, findYamlMethod(lines, li), false, ctx);
      if (res) {
        contexts.set(li, res.bodyKind);
        edits.push({ start: vs, end: vs + rawValue.length, text: encodeYamlValue(res.url, codec) });
        // Rewrite the sibling method: line, or warn about the header if this
        // is a rest_command without content_type.
        const mi = findYamlMethodLine(lines, li);
        if (mi >= 0) {
          const mm = lines[mi].match(YAML_KEY_RE);
          const mvs = offsets[mi] + lines[mi].indexOf(mm[3], lines[mi].indexOf(":") + 1);
          const lower = mm[3] === mm[3].toLowerCase();
          const newMethod = lower ? res.method.toLowerCase() : res.method;
          if (mm[3] !== newMethod) {
            edits.push({ start: mvs, end: mvs + mm[3].length, text: newMethod });
            change(ctx, "method", mm[3], newMethod);
          }
        }
        if (res.wantsJsonBody && !blockHasKey(lines, li, "content_type")) {
          warn(ctx, "restCommandContentType", {});
        }
      }
    }
  }

  // Payload islands, bound to the nearest converted topic/url around them.
  for (let li = 0; li < lines.length; li++) {
    const m = lines[li].match(YAML_KEY_RE);
    if (!m || !["payload", "payload_template"].includes(m[2])) continue;
    const rawValue = m[3];
    let bodyKind = nearestContext(contexts, li);
    // A fully templated topic says "this is AWTRIX" but not which call --
    // fall back to guessing the payload kind from its keys.
    if (bodyKind === "templated") bodyKind = null;
    const where = "the payload on line " + (li + 1);
    if (rawValue === "" || rawValue === "|" || rawValue === ">" || /^[|>][+-]?$/.test(rawValue)) {
      if (rawValue === "") {
        // `payload:` with an indented block under it -- either a native YAML
        // mapping (unsupported) or nothing.
        if (lines[li + 1]?.match(YAML_KEY_RE) && indentOf(lines[li + 1]) > indentOf(lines[li])) {
          if (bodyKind) warn(ctx, "yamlMapPayload", { where });
        }
        continue;
      }
      // Block scalar: collect the indented lines, convert, re-indent.
      const baseIndent = indentOf(lines[li]);
      let endLi = li + 1;
      while (endLi < lines.length && (lines[endLi].trim() === "" || indentOf(lines[endLi]) > baseIndent)) endLi++;
      const blockLines = lines.slice(li + 1, endLi);
      const contentLines = blockLines.filter((l) => l.trim() !== "");
      if (contentLines.length === 0) continue;
      const blockIndent = Math.min(...contentLines.map(indentOf));
      const text = blockLines.map((l) => l.slice(blockIndent)).join("\n");
      const res = convertJsonIslandText(text, bodyKind, ctx, where);
      if (res && !res.delete) {
        const indented = res.text.split("\n").map((l) => " ".repeat(blockIndent) + l).join("\n");
        const start = offsets[li + 1];
        const end = offsets[endLi - 1] + lines[endLi - 1].length;
        edits.push({ start, end, text: indented });
      }
      continue;
    }
    const { value, codec } = stripYamlQuotes(rawValue);
    if (!value.startsWith("{") && !value.startsWith("[")) continue;
    const res = convertJsonIslandText(value, bodyKind, ctx, where);
    if (res) {
      const vs = offsets[li] + lines[li].indexOf(rawValue, lines[li].indexOf(":") + 1);
      // On MQTT an off-payload becomes an empty publish, which YAML spells "".
      const text = res.delete ? '""' : encodeYamlValue(res.text, codec);
      edits.push({ start: vs, end: vs + rawValue.length, text });
    }
  }

  // A fully templated topic is only worth a warning when the document is
  // recognisably an AWTRIX flow at all.
  if (ctx.pendingTemplatedTopics.length && (ctx.a3MarkerSeen || ctx.changes.length)) {
    for (const where of ctx.pendingTemplatedTopics) warn(ctx, "templatedTopic", { where: "the topic on " + where });
  }
  return applyEdits(input, edits);
}

function indentOf(line) { return line.length - line.trimStart().length; }

function nearestContext(contexts, li) {
  let best = null, bestDist = Infinity;
  for (const [ci, kind] of contexts) {
    const d = Math.abs(ci - li);
    if (d < bestDist && d <= 20) { bestDist = d; best = kind; }
  }
  return best;
}

function findYamlMethodLine(lines, li) {
  const indent = indentOf(lines[li]);
  for (let d = 1; d <= 8; d++) {
    for (const i of [li - d, li + d]) {
      if (i < 0 || i >= lines.length) continue;
      const m = lines[i].match(YAML_KEY_RE);
      if (m && m[2] === "method" && indentOf(lines[i]) === indent) return i;
    }
  }
  return -1;
}

function findYamlMethod(lines, li) {
  const i = findYamlMethodLine(lines, li);
  return i >= 0 ? lines[i].match(YAML_KEY_RE)[3].replace(/["']/g, "") : "";
}

function blockHasKey(lines, li, key) {
  const indent = indentOf(lines[li]);
  for (let d = 1; d <= 8; d++) {
    for (const i of [li - d, li + d]) {
      if (i < 0 || i >= lines.length) continue;
      const m = lines[i].match(YAML_KEY_RE);
      if (m && m[2] === key && indentOf(lines[i]) === indent) return true;
    }
  }
  return false;
}

// ------------------------------------------- Node-RED and N8N (structural) ---

const JS_PAYLOAD_RE = /\b(duration|lifetime|pushIcon|lifetimeMode|scrollSpeed|noScroll|blinkText|fadeText|gradient|rainbow|topText|barBC|progressC|progressBC|loopSound|textOffset)\b/;

export function convertNodeRed(input, ctx) {
  let nodes;
  try { nodes = JSON.parse(input); } catch { return input; }
  if (!Array.isArray(nodes)) return input;
  let modified = false;

  for (const node of nodes) {
    if (!node || typeof node !== "object") continue;
    if ((node.type === "mqtt out" || node.type === "mqtt in") && typeof node.topic === "string") {
      const res = rewriteTopicValue(node.topic, ctx);
      if (res) { node.topic = res.topic; modified = true; }
    }
    if (node.type === "http request" && typeof node.url === "string") {
      const res = rewriteUrl(node.url, node.method, false, ctx);
      if (res) {
        node.url = res.url;
        if (node.method && node.method.toUpperCase() !== res.method) {
          change(ctx, "method", node.method, res.method);
          node.method = node.method === node.method.toLowerCase() ? res.method.toLowerCase() : res.method;
        }
        modified = true;
      }
    }
    if (node.type === "function" && typeof node.func === "string") {
      const rewritten = rewriteJsStringLiterals(node.func, ctx);
      if (rewritten !== node.func) { node.func = rewritten; modified = true; }
      if (JS_PAYLOAD_RE.test(node.func) && /msg\.payload\s*=/.test(node.func)) {
        warn(ctx, "jsCode", { where: 'function node "' + (node.name || node.id) + '"' });
      }
    }
    if (node.type === "template" && typeof node.template === "string") {
      const res = convertJsonIslandText(node.template, null, ctx, 'template node "' + (node.name || node.id) + '"');
      if (res && !res.delete) { node.template = res.text; modified = true; }
    }
    if (node.type === "inject" && node.payloadType === "json" && typeof node.payload === "string") {
      const res = convertJsonIslandText(node.payload, null, ctx, 'inject node "' + (node.name || node.id) + '"');
      if (res && !res.delete) { node.payload = res.text; modified = true; }
    }
  }
  if (!modified) return input;
  return JSON.stringify(nodes, null, 4);
}

// Rewrites AWTRIX 3 topics and URLs that sit in plain string literals inside
// function-node JavaScript. Everything else in the code stays untouched.
function rewriteJsStringLiterals(code, ctx) {
  return code.replace(/(['"])((?:\\.|(?!\1).)*)\1/g, (whole, q, inner) => {
    const topicRes = rewriteTopicValue(inner, ctx);
    if (topicRes) return q + topicRes.topic + q;
    const urlRes = inner.includes("/api/") ? rewriteUrl(inner, "", false, ctx) : null;
    if (urlRes) return q + urlRes.url + q;
    return whole;
  });
}

function n8nParamList(params, ...paths) {
  for (const path of paths) {
    let v = params;
    for (const p of path.split(".")) v = v?.[p];
    if (Array.isArray(v)) return v;
  }
  return null;
}

export function convertN8n(input, ctx) {
  let doc;
  try { doc = JSON.parse(input); } catch { return input; }
  if (!doc || !Array.isArray(doc.nodes)) return input;
  let modified = false;

  for (const node of doc.nodes) {
    const p = node.parameters;
    if (!p) continue;
    if (typeof p.topic === "string") {
      const res = rewriteTopicValue(p.topic, ctx);
      if (res) { p.topic = res.topic; modified = true; }
    }
    if (typeof p.url !== "string" || !p.url.includes("/api/")) continue;
    const method = p.requestMethod || p.method || "GET";
    // The app name may live in a structured query parameter instead of the URL.
    const queryList = n8nParamList(p, "queryParametersUi.parameter", "queryParameters.parameters");
    let url = p.url;
    const nameEntry = queryList?.find((e) => e.name === "name");
    if (nameEntry && !/\?/.test(url)) url += "?name=" + nameEntry.value;
    const bodyList = n8nParamList(p, "bodyParametersUi.parameter", "bodyParameters.parameters");
    const emptyBody = !bodyList?.length && !p.jsonBody && !p.body;
    const res = rewriteUrl(url, method, emptyBody, ctx);
    if (!res) continue;
    modified = true;
    p.url = res.url;
    if (nameEntry) queryList.splice(queryList.indexOf(nameEntry), 1);
    if (p.requestMethod) p.requestMethod = res.method;
    else if (p.method) p.method = res.method;
    else p.method = res.method;
    if (bodyList && res.bodyKind) {
      for (const entry of bodyList) {
        const spec = KEY_MAP[entry.name];
        if (!spec) {
          if (!NG_KEY_SET.has(entry.name)) warn(ctx, "unmappedKey", { key: entry.name });
          continue;
        }
        const isExpr = typeof entry.value === "string" && entry.value.startsWith("=");
        if (spec.kind === "rename") {
          change(ctx, "key", entry.name, spec.to);
          entry.name = spec.to;
        } else if (spec.kind === "seconds") {
          if (isExpr) { warn(ctx, "jinjaPayload", { where: 'body parameter "' + entry.name + '" of node "' + (node.name || "") + '"' }); continue; }
          const n = Number(entry.value);
          if (Number.isNaN(n)) { warn(ctx, "valueNotNumeric", { key: entry.name }); continue; }
          change(ctx, "unit", entry.name + ": " + entry.value, spec.to + ": " + Math.round(n * 1000));
          entry.name = spec.to;
          entry.value = typeof entry.value === "string" ? String(Math.round(n * 1000)) : Math.round(n * 1000);
        } else if (spec.kind === "enum") {
          const n = Number(entry.value);
          if (Number.isInteger(n) && n >= 0 && n < spec.values.length && !isExpr) {
            change(ctx, "value", entry.name + ": " + entry.value, spec.to + ": " + spec.values[n]);
            entry.name = spec.to;
            entry.value = spec.values[n];
          } else {
            warn(ctx, "enumOutOfRange", { key: entry.name, value: JSON.stringify(entry.value) });
          }
        } else if (spec.kind === "dead") {
          warn(ctx, "deadKey", { key: entry.name, note: spec.note, anchor: spec.anchor });
        }
        // The structural kinds (palette, scroll, draw, ...) cannot be expressed
        // as flat name/value pairs; presence of one falls back to a warning.
        else if (["palette", "scroll", "effectSettings", "draw"].includes(spec.kind)) {
          warn(ctx, "jinjaPayload", { where: 'body parameter "' + entry.name + '" of node "' + (node.name || "") + '"' });
        }
      }
    }
    if (typeof p.jsonBody === "string" && res.bodyKind) {
      const conv = convertJsonIslandText(p.jsonBody, res.bodyKind, ctx, 'the JSON body of node "' + (node.name || "") + '"');
      if (conv && !conv.delete) p.jsonBody = conv.text;
    }
  }
  if (!modified) return input;
  return JSON.stringify(doc, null, 2);
}

// -------------------------------------------------------------- detection ---

export function detectInputType(input) {
  const t = input.trim();
  if (/^curl\s/.test(t) || /\ncurl\s/.test(t)) return "curl";
  try {
    const parsed = JSON.parse(t);
    if (Array.isArray(parsed) && parsed.some((n) => n && typeof n === "object" && "type" in n && ("wires" in n || "z" in n))) {
      return "node-red";
    }
    if (parsed && typeof parsed === "object" && Array.isArray(parsed.nodes) && "connections" in parsed) {
      return "n8n";
    }
    if (parsed && typeof parsed === "object") return "json-payload";
  } catch { /* not JSON */ }
  if (/(^|\n)\s*(service|action)\s*:\s*mqtt\.publish|(^|\n)\s*blueprint\s*:|(^|\n)\s*rest_command\s*:|(^|\n)\s*(topic|payload|resource|url)\s*:/.test(t)) {
    return "ha-yaml";
  }
  return "unknown";
}

function convertRawPayload(input, ctx) {
  const trimmed = input.trim();
  // A bare reorder list ([{"name", "show"}...]) is the one array payload.
  try {
    const parsed = JSON.parse(trimmed);
    if (Array.isArray(parsed) && parsed.length && parsed.every((i) => i && typeof i === "object" && "name" in i)) {
      const res = transformBody("reorder", parsed, ctx);
      change(ctx, "endpoint", "POST /api/apps", "PUT /api/v1/apps/order");
      return JSON.stringify(res, null, /\n/.test(trimmed) ? 2 : undefined) ?? input;
    }
  } catch { /* fall through to the island path */ }
  const res = convertJsonIslandText(input, null, ctx, "the payload");
  if (res && !res.delete) return res.text;
  return input;
}

// ----------------------------------------------------------------- convert ---

const DRIVERS = {
  "curl": convertCurl,
  "ha-yaml": convertHaYaml,
  "node-red": convertNodeRed,
  "n8n": convertN8n,
  "json-payload": convertRawPayload,
  "unknown": convertHaYaml,   // the line scanners still catch loose topics/URLs
};

export function convert(input) {
  const ctx = newCtx();
  const inputType = detectInputType(input);
  let output;
  try {
    output = DRIVERS[inputType](input, ctx);
  } catch (e) {
    const fresh = newCtx();
    warn(fresh, "internalError", {});
    return { output: input, inputType, changes: [], warnings: fresh.warnings, alreadyNg: false };
  }
  const alreadyNg = ctx.changes.length === 0 && ctx.warnings.length === 0 && output === input &&
    (/\/api\/v1\//.test(input) || /(^|[/"])cmd\//m.test(input) ||
     inputType === "json-payload");
  return { output, inputType, changes: ctx.changes, warnings: ctx.warnings, alreadyNg };
}
