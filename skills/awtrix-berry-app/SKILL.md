---
name: awtrix-berry-app
description: Write, install and debug Berry apps for an AWTRIX NG LED matrix clock (Ulanzi TC001 and other 32x8 panels). Use when the user wants something shown on their AWTRIX or pixel clock, asks for an AWTRIX app, script or .ax file, or reports an ERR: frame or a script error on the panel.
license: PolyForm-Noncommercial-1.0.0
compatibility: Installing and testing needs HTTP access to the device on the local network. Without it, write the script and hand it over for the web UI.
metadata:
  author: Blueforcer
  version: "1.0"
  homepage: https://github.com/Blueforcer/awtrix-ng
---

# AWTRIX NG Berry apps

An app is one Berry class in one file, drawn on a 32x8 LED panel and shown in a
rotation with the other apps. The device is an ESP32 with a shared 96 KB Berry
heap: small scripts are not a style preference, they are the constraint.

## 1. Load the API before writing anything

Read `references/awtrix-api.md` in full first. It is the complete device API -
every call, the lifecycle, the limits, and the mistakes models reliably make on
this panel. A call that is not in it does not exist and the script will not
compile.

Do not fall back on AWTRIX 3 knowledge or on general Berry libraries: this
firmware has its own bindings and a reduced module set.

If that file is missing from the skill directory, fetch it:
`https://raw.githubusercontent.com/Blueforcer/awtrix-ng/main/docs/examples/berry-app-system-prompt.md`

## 2. Reach the device

Ask for the address once, then keep it for the session. Default hostname is
`awtrixng-<last 6 hex of the MAC>.local`; an IP always works.

```bash
curl -s http://$AWTRIX/api/v1/device
```

- `401` - auth is on. Ask for user and password, then use `curl -u user:pass`.
- Nothing answers - do not guess addresses. Write the script, give the user the
  web UI steps from section 6, and stop there.

## 3. Write the app

Follow `references/awtrix-api.md`. Ask at most three questions, all at once, and
only about things you cannot default (which city, which topic, which icon ID).
Never ask the user to paste an API key into chat - leave a marked placeholder
line in the script.

**Anything the user might change later goes in a `# @config` header line**, not
in the source - a city, a name, a colour, an interval, a threshold. That puts it
behind a gear on the web UI's Apps tab, so they never reopen the file. It also
shrinks what you have to ask: give the setting a sensible `default=` and move on
instead of spending a question on it. Read them with `store.get(key)`, and never
repeat the default in the code. Section 5.11b of the reference has the syntax.

If you are writing several apps that want the **same** value, put the `# @config`
on a `# @module` they all import and read it at the module's top level - one
field for all of them. Section 5.11c.

## 4. Install it

The name must match `[A-Za-z0-9_-]{1,32}`. The body is raw Berry source, not
JSON.

```bash
curl -sX PUT "http://$AWTRIX/api/v1/apps/script/weather" \
  -H 'Content-Type: text/plain' --data-binary @weather.ax
```

**A script that does not compile still installs.** The reply is `200` with the
compiler message in `error`, and the panel renders `ERR:<name>` until another
`PUT` replaces it. Read `error` on every install - `null` is the only success:

```json
{"error":{"message":"syntax_error: unexpected token ')'","line":12}}
```

`line` is 1-based in the source you sent; `hook` names the method that raised a
runtime error. Fix the cause and `PUT` again - the app keeps its place in the app
list and its store.

Other statuses: `413` over `scriptMaxBytes` (8192 by default), `507` out of
memory or over the script limit. `507` means shorten the script - fewer, larger
methods - or delete an unused one with
`DELETE /api/v1/apps/{name}`, or reboot to defragment.

## 5. Verify on the panel

Compiling is not drawing. Force the app on screen and read the framebuffer back:

```bash
curl -sX PUT "http://$AWTRIX/api/v1/apps/active" \
  -H 'Content-Type: application/json' -d '{"name":"weather","fast":true}'
curl -s "http://$AWTRIX/api/v1/display/screen"
```

`{"width":32,"height":8,"pixels":[...]}`, row-major, each pixel the packed
`0xRRGGBB` as an unsigned decimal. Check what the user asked for: pixels are not
all `0`, nothing is clipped at the last column, the colours are the intended
ones. Give the network app a moment - data arrives in `loop()`, about once a
second.

**Pin the app again immediately before every read.** The app holds the panel for
its dwell time only; a screenshot taken later is of whichever app the rotation
walked on to, and it looks exactly like a broken app that drew the wrong thing.
A red `ERR:` frame is the app itself reporting the compile error from step 4.

`GET /api/v1/apps` lists every app with `enabled` (it runs), `inLoop` (it is
drawn, and `position` where), its `error`, and `skipped` when the script's own
`should_show()` returned false.

**A headless script has no panel to check.** `# @headless true` means it never
draws, so it is absent from the rotation: pinning it answers `404 app not found`
and its own `rotation.show()` returns false. Verify it in `GET /api/v1/apps` -
`enabled` true, `headless` true, `error` null - and then on what it actually
does: the notification it raises, the `shared` key it fills, the line it logs.

## 6. When there is no device

Hand over the complete file plus these steps, in the user's language:

1. Open the AWTRIX web interface - the device's IP, or
   `http://awtrixng-xxxxxx.local`.
2. **Scripts** tab, create a script, name it with letters, digits, `_` or `-`.
3. Paste, press **Save**.
4. It joins the rotation a moment later.
5. If the panel shows `ERR:` in red, the message stands next to the script in
   that tab - ask them to copy it back.
6. If you gave the app `# @config` settings, tell them where they are: **Apps**
   tab, the gear on that app's row. If you put them on a shared module, the gear
   is on the module's row in the **Modules** card on the same tab.

## 7. Rules

- **Never put a real credential in a script.** A script's HTTPS traffic is
  encrypted but the certificate is not verified, and `GET
  /api/v1/apps/script/{name}` serves the source back to anyone who can reach the
  device unless a login is configured.
- **Touch only the app you were asked about.** Do not delete, replace or reorder
  other scripts, and do not change device settings the user did not ask for.
- **Always deliver the whole file**, never a patch - the user pastes files.
- **Leave settings alone.** `PATCH /api/v1/apps/{name}/config` changes values the
  user chose. Read it to see what an app offers; write it only when asked to.
- A broken script breaks nothing else: the rest of the rotation and the clock
  carry on.
