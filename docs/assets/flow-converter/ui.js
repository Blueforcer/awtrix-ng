// The widget on guides/migrating-from-awtrix3.md. All conversion logic lives
// in engine.js -- this file only wires a textarea to convert() and renders
// what comes back. Everything runs in the page; nothing is uploaded.
import { convert } from "./engine.js";

// The one page this widget lives on sits two directories below the site root,
// so cross-page warning links all start with ../../.
const PAGE = "guides/migrating-from-awtrix3.md";
const ROOT = "../../";

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") node.className = v;
    else node.setAttribute(k, v);
  }
  node.append(...children);
  return node;
}

function warningHref(w) {
  if (!w.page) return null;
  const hash = w.anchor ? "#" + w.anchor : "";
  if (w.page === PAGE) return hash || null;
  return ROOT + w.page.replace(/\.md$/, "/") + hash;
}

// Warning messages carry `code` spans; render those as <code>.
function richText(text) {
  const frag = document.createDocumentFragment();
  const parts = text.split("`");
  parts.forEach((part, i) => {
    frag.append(i % 2 ? el("code", {}, part) : part);
  });
  return frag;
}

const PLACEHOLDER = `Paste an AWTRIX 3 flow, for example:

curl -X POST http://awtrix-ip/api/custom?name=weather -d '{"text":"21.5 C","icon":"2422","duration":8}'`;

const TYPE_LABELS = {
  "curl": "curl command", "ha-yaml": "Home Assistant YAML",
  "node-red": "Node-RED export", "n8n": "N8N workflow",
  "json-payload": "JSON payload", "unknown": "unrecognized",
};

function render(host) {
  const input = el("textarea", {
    class: "flowconv-input", rows: "8", spellcheck: "false",
    placeholder: PLACEHOLDER, "aria-label": "AWTRIX 3 flow to convert",
  });
  const results = el("div", { "aria-live": "polite" });
  host.replaceChildren(input, results);

  let timer = null;
  input.addEventListener("input", () => {
    clearTimeout(timer);
    timer = setTimeout(() => update(input.value, results), 150);
  });
}

function update(text, results) {
  results.replaceChildren();
  if (text.trim() === "") return;
  const r = convert(text);

  const badge = el("span", { class: "flowconv-badge" }, TYPE_LABELS[r.inputType]);
  const status = el("p", { class: "flowconv-status" }, badge);
  if (r.alreadyNg) {
    status.append(" Already AWTRIX NG - nothing to convert.");
    results.append(status);
    return;
  }
  if (r.changes.length === 0 && r.warnings.length === 0) {
    status.append(" Nothing to convert found - paste a flow that talks to AWTRIX 3.");
    results.append(status);
    return;
  }
  status.append(r.changes.length === 0
    ? " Nothing was changed - see the warnings below."
    : ` ${r.changes.length} change${r.changes.length === 1 ? "" : "s"}.`);
  results.append(status);

  if (r.changes.length > 0) {
    const code = el("code", {}, r.output);
    const copy = el("button", { class: "md-button flowconv-copy", type: "button" }, "Copy");
    copy.addEventListener("click", async () => {
      try {
        await navigator.clipboard.writeText(r.output);
      } catch {
        const ta = el("textarea", {}, r.output);
        document.body.append(ta);
        ta.select();
        document.execCommand("copy");
        ta.remove();
      }
      copy.textContent = "Copied";
      setTimeout(() => { copy.textContent = "Copy"; }, 1500);
    });
    results.append(
      el("div", { class: "flowconv-output" }, el("pre", {}, code), copy),
      el("details", { class: "flowconv-changes" },
        el("summary", {}, "What changed"),
        el("ul", {}, ...r.changes.map((c) => el("li", {},
          el("code", {}, c.before),
          " → ",
          c.after === "" ? "removed" : el("code", {}, c.after),
          c.note ? " - " + c.note : "")))),
    );
  }

  if (r.warnings.length > 0) {
    const list = el("ul", {}, ...r.warnings.map((w) => {
      const li = el("li", {}, richText(w.message), " ");
      const href = warningHref(w);
      if (href) li.append(el("a", { href }, "Details"));
      return li;
    }));
    results.append(el("div", { class: "admonition warning" },
      el("p", { class: "admonition-title" }, "Not converted automatically"),
      list));
  }
}

const host = document.getElementById("awtrix-flow-converter");
if (host) render(host);
