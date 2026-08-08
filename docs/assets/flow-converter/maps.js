// The conversion tables of the flow converter on guides/migrating-from-awtrix3.md.
//
// Every exported literal is strict JSON -- double-quoted keys, no comments and no
// trailing commas inside the braces -- because tools/check_flow_converter.py
// extracts each one with a balanced-brace scan and json.loads, then compares it
// against the tables the migration page teaches by hand. Comments live between
// the exports, never inside them.

// Payload keys of pushed apps and notifications. "kind" says how the engine
// treats the key:
//   rename           -- new name, value unchanged
//   seconds          -- new name, value multiplied by 1000
//   enum             -- new name, numeric value replaced by values[n]
//   keep             -- same name and value in both dialects
//   palette / scroll / effectSettings / draw / text / special
//                    -- structural conversions the engine implements
//   dead             -- no NG equivalent; left in place, warned about
// "notificationOnly" marks keys NG rejects on a pushed app.
export const KEY_MAP = {
  "text": {"kind": "text"},
  "textCase": {"kind": "enum", "to": "textCase", "values": ["inherit", "upper", "asTyped"]},
  "topText": {"kind": "dead", "note": "place text freely with a draw text command instead"},
  "textOffset": {"kind": "rename", "to": "textOffsetX"},
  "center": {"kind": "rename", "to": "textCenter"},
  "color": {"kind": "rename", "to": "textColor"},
  "gradient": {"kind": "palette"},
  "blinkText": {"kind": "rename", "to": "textBlinkMs"},
  "fadeText": {"kind": "rename", "to": "textFadeMs"},
  "rainbow": {"kind": "palette"},
  "background": {"kind": "rename", "to": "backgroundColor"},
  "noScroll": {"kind": "scroll"},
  "scrollSpeed": {"kind": "scroll"},
  "icon": {"kind": "keep"},
  "pushIcon": {"kind": "enum", "to": "iconMode", "values": ["fixed", "pushOnce", "push"]},
  "duration": {"kind": "seconds", "to": "durationMs"},
  "lifetime": {"kind": "seconds", "to": "lifetimeMs"},
  "lifetimeMode": {"kind": "enum", "to": "lifetimeExpiry", "values": ["remove", "mark"]},
  "repeat": {"kind": "special"},
  "pos": {"kind": "dead", "note": "set the rotation once with PUT /api/v1/apps/order", "anchor": "pos-became-the-order-call"},
  "bar": {"kind": "rename", "to": "barChart"},
  "line": {"kind": "rename", "to": "lineChart"},
  "autoscale": {"kind": "rename", "to": "chartAutoscale"},
  "barBC": {"kind": "dead", "note": "unfilled chart cells show the app background"},
  "progress": {"kind": "keep"},
  "progressC": {"kind": "rename", "to": "progressColor"},
  "progressBC": {"kind": "rename", "to": "progressTrackColor"},
  "effect": {"kind": "keep"},
  "effectSettings": {"kind": "effectSettings"},
  "overlay": {"kind": "special"},
  "draw": {"kind": "draw"},
  "hold": {"kind": "keep", "notificationOnly": true},
  "stack": {"kind": "keep", "notificationOnly": true},
  "wakeup": {"kind": "keep", "notificationOnly": true},
  "sound": {"kind": "keep", "notificationOnly": true},
  "rtttl": {"kind": "rename", "to": "soundRtttl", "notificationOnly": true},
  "loopSound": {"kind": "rename", "to": "soundLoop", "notificationOnly": true},
  "clients": {"kind": "dead", "note": "send to each device yourself"},
  "save": {"kind": "dead", "note": "pushed apps are RAM-only; move the app to a script", "anchor": "save-is-gone-scripts-took-its-place"}
};

// NG payload keys that have no AWTRIX 3 spelling of their own. Together with
// the KEY_MAP targets they form the set the engine recognises as "already
// converted" -- anything outside both sets draws an unknown-key warning.
export const NG_ONLY_KEYS = ["font", "iconOffsetX", "textInFront", "palette", "paletteBlend", "paletteSpan", "paletteSpeed", "chartColor", "scroll", "name", "durationMs", "lifetimeMs", "lifetimeExpiry", "textColor", "backgroundColor", "iconMode", "textOffsetX", "textCenter", "textBlinkMs", "textFadeMs", "barChart", "lineChart", "chartAutoscale", "progressColor", "progressTrackColor", "soundRtttl", "soundLoop", "effectSpeed"];

// AWTRIX 3 draw command codes and the NG command names, one to one.
export const DRAW_MAP = {"dp": "pixel", "dl": "line", "dr": "rect", "df": "rectFill", "dc": "circle", "dfc": "circleFill", "dt": "text", "db": "bitmap"};

// AWTRIX 3 picked the transition with TEFF 0..10; NG names them. The first
// eleven rows of the transitions table in reference/visuals.md are exactly the
// AWTRIX 3 set in AWTRIX 3's numeric order, which is what makes this index map
// valid -- the drift check pins that ordering.
export const TEFF_NAMES = ["Random", "Slide", "Dim", "Zoom", "Rotate", "Pixelate", "Curtain", "Ripple", "Blink", "Reload", "Fade"];

// AWTRIX 3 settings keys (POST /api/settings) and their NG spellings
// (PATCH /api/v1/settings). No such map exists anywhere else -- the AWTRIX 3
// side was curated once against Blueforcer/awtrix3 docs/api.md. Kinds:
//   rename / seconds  -- as in KEY_MAP
//   teff              -- number replaced by TEFF_NAMES[n]
//   colorOrNull       -- AWTRIX 3 used 0 for "use the global color"; NG uses null
//   nested            -- value moves into an object field ("weekdayBar.show")
//   tformat / dformat -- strftime-ish string split into NG's word settings
//   warn              -- no settings equivalent; left in place, warned about
export const SETTINGS_MAP = {
  "ATIME": {"kind": "seconds", "to": "appDurationMs"},
  "TEFF": {"kind": "teff", "to": "transitionEffect"},
  "TSPEED": {"kind": "rename", "to": "transitionDurationMs"},
  "TCOL": {"kind": "rename", "to": "textColor"},
  "TMODE": {"kind": "rename", "to": "timeMode"},
  "CHCOL": {"kind": "rename", "to": "calendarHeaderColor"},
  "CBCOL": {"kind": "rename", "to": "calendarBodyColor"},
  "CTCOL": {"kind": "rename", "to": "calendarTextColor"},
  "WD": {"kind": "nested", "to": "weekdayBar.show"},
  "WDCA": {"kind": "nested", "to": "weekdayBar.activeColor"},
  "WDCI": {"kind": "nested", "to": "weekdayBar.inactiveColor"},
  "SOM": {"kind": "nested", "to": "weekdayBar.startOnMonday"},
  "BRI": {"kind": "rename", "to": "brightness"},
  "ABRI": {"kind": "rename", "to": "autoBrightness"},
  "ATRANS": {"kind": "rename", "to": "autoTransition"},
  "CCORRECTION": {"kind": "rename", "to": "colorCorrection"},
  "CTEMP": {"kind": "rename", "to": "colorTint"},
  "TFORMAT": {"kind": "tformat"},
  "DFORMAT": {"kind": "dformat"},
  "CEL": {"kind": "rename", "to": "useCelsius"},
  "BLOCKN": {"kind": "rename", "to": "blockNavigation"},
  "UPPERCASE": {"kind": "rename", "to": "uppercase"},
  "TIME_COL": {"kind": "colorOrNull", "to": "timeColor"},
  "DATE_COL": {"kind": "colorOrNull", "to": "dateColor"},
  "TEMP_COL": {"kind": "colorOrNull", "to": "temperatureColor"},
  "HUM_COL": {"kind": "colorOrNull", "to": "humidityColor"},
  "BAT_COL": {"kind": "colorOrNull", "to": "batteryColor"},
  "SSPEED": {"kind": "nested", "to": "scroll.speed"},
  "VOL": {"kind": "rename", "to": "volume"},
  "GAMMA": {"kind": "rename", "to": "gamma"},
  "TIM": {"kind": "warn", "warning": "crossEndpoint", "note": "switching built-in apps off is the disabled list of PUT /api/v1/apps/order", "anchor": "pos-became-the-order-call"},
  "DAT": {"kind": "warn", "warning": "crossEndpoint", "note": "switching built-in apps off is the disabled list of PUT /api/v1/apps/order", "anchor": "pos-became-the-order-call"},
  "HUM": {"kind": "warn", "warning": "crossEndpoint", "note": "switching built-in apps off is the disabled list of PUT /api/v1/apps/order", "anchor": "pos-became-the-order-call"},
  "TEMP": {"kind": "warn", "warning": "crossEndpoint", "note": "switching built-in apps off is the disabled list of PUT /api/v1/apps/order", "anchor": "pos-became-the-order-call"},
  "BAT": {"kind": "warn", "warning": "crossEndpoint", "note": "switching built-in apps off is the disabled list of PUT /api/v1/apps/order", "anchor": "pos-became-the-order-call"},
  "MATP": {"kind": "warn", "warning": "crossEndpoint", "note": "panel power moved to PATCH /api/v1/display {\"power\": ...}"},
  "OVERLAY": {"kind": "warn", "warning": "crossEndpoint", "note": "the global overlay moved to PATCH /api/v1/display {\"overlay\": ...}; \"clear\" became null"}
};

// AWTRIX 3 HTTP endpoints (path after /api/) and where they went. "method"
// narrows the match where one AWTRIX 3 path did two jobs; "body" names the
// payload transformer; "emptyBody" is the NG call that replaced the old
// delete-by-empty-body convention (an empty PUT body is a 422 in NG);
// "nameQuery" moves a query parameter into the NG path.
export const ENDPOINT_MAP = [
  {"a3": "custom", "ng": {"method": "PUT", "path": "/api/v1/apps/pushed/{name}"}, "nameQuery": "name", "emptyBody": {"method": "DELETE", "path": "/api/v1/apps/{name}"}, "body": "app"},
  {"a3": "notify", "ng": {"method": "POST", "path": "/api/v1/notifications"}, "body": "notification"},
  {"a3": "notify/dismiss", "ng": {"method": "DELETE", "path": "/api/v1/notifications/active"}},
  {"a3": "indicator1", "ng": {"method": "PUT", "path": "/api/v1/indicators/1"}, "emptyBody": {"method": "DELETE", "path": "/api/v1/indicators/1"}, "body": "indicator"},
  {"a3": "indicator2", "ng": {"method": "PUT", "path": "/api/v1/indicators/2"}, "emptyBody": {"method": "DELETE", "path": "/api/v1/indicators/2"}, "body": "indicator"},
  {"a3": "indicator3", "ng": {"method": "PUT", "path": "/api/v1/indicators/3"}, "emptyBody": {"method": "DELETE", "path": "/api/v1/indicators/3"}, "body": "indicator"},
  {"a3": "moodlight", "ng": {"method": "PUT", "path": "/api/v1/display/moodlight"}, "emptyBody": {"method": "DELETE", "path": "/api/v1/display/moodlight"}, "body": "moodlight"},
  {"a3": "power", "ng": {"method": "PATCH", "path": "/api/v1/display"}, "body": "power"},
  {"a3": "sleep", "ng": {"method": "POST", "path": "/api/v1/device/sleep"}, "body": "sleep"},
  {"a3": "sound", "ng": {"method": "POST", "path": "/api/v1/audio/play"}, "body": "sound"},
  {"a3": "rtttl", "ng": {"method": "POST", "path": "/api/v1/audio/play"}, "body": "rtttlRaw"},
  {"a3": "r2d2", "ng": {"method": "POST", "path": "/api/v1/audio/play"}, "body": "r2d2"},
  {"a3": "settings", "method": "POST", "ng": {"method": "PATCH", "path": "/api/v1/settings"}, "body": "settings"},
  {"a3": "settings", "method": "GET", "ng": {"method": "GET", "path": "/api/v1/settings"}, "warning": "responseShape"},
  {"a3": "resetSettings", "ng": {"method": "POST", "path": "/api/v1/settings/reset"}},
  {"a3": "reboot", "ng": {"method": "POST", "path": "/api/v1/device/reboot"}},
  {"a3": "erase", "ng": {"method": "POST", "path": "/api/v1/device/factory-reset"}},
  {"a3": "apps", "method": "POST", "ng": {"method": "PUT", "path": "/api/v1/apps/order"}, "body": "reorder"},
  {"a3": "apps", "method": "GET", "ng": {"method": "GET", "path": "/api/v1/apps"}, "warning": "responseShape"},
  {"a3": "loop", "method": "GET", "ng": {"method": "GET", "path": "/api/v1/apps"}, "warning": "responseShape"},
  {"a3": "switch", "ng": {"method": "PUT", "path": "/api/v1/apps/active"}, "body": "switch"},
  {"a3": "nextapp", "ng": {"method": "POST", "path": "/api/v1/apps/next"}},
  {"a3": "previousapp", "ng": {"method": "POST", "path": "/api/v1/apps/previous"}},
  {"a3": "stats", "method": "GET", "ng": {"method": "GET", "path": "/api/v1/device"}, "warning": "responseShape"},
  {"a3": "screen", "method": "GET", "ng": {"method": "GET", "path": "/api/v1/display/screen"}, "warning": "responseShape"},
  {"a3": "effects", "method": "GET", "ng": {"method": "GET", "path": "/api/v1/capabilities"}, "warning": "responseShape"},
  {"a3": "transitions", "method": "GET", "ng": {"method": "GET", "path": "/api/v1/capabilities"}, "warning": "responseShape"},
  {"a3": "doupdate", "warning": "noUpdateApi"}
];

// AWTRIX 3 MQTT topic suffixes and the NG cmd topics. The prefix in front of
// the suffix is whatever the user configured and is preserved verbatim;
// {name} matches [A-Za-z0-9_-]+. Entries without "ng" have no MQTT equivalent
// in NG and only warn.
export const TOPIC_MAP = [
  {"a3": "custom/{name}", "ng": "cmd/apps/pushed/{name}", "body": "app"},
  {"a3": "notify/dismiss", "ng": "cmd/notify/dismiss"},
  {"a3": "notify", "ng": "cmd/notify", "body": "notification"},
  {"a3": "indicator1", "ng": "cmd/indicators/1", "body": "indicator"},
  {"a3": "indicator2", "ng": "cmd/indicators/2", "body": "indicator"},
  {"a3": "indicator3", "ng": "cmd/indicators/3", "body": "indicator"},
  {"a3": "moodlight", "ng": "cmd/display/moodlight", "body": "moodlight"},
  {"a3": "power", "ng": "cmd/display", "body": "power"},
  {"a3": "sleep", "ng": "cmd/device/sleep", "body": "sleep"},
  {"a3": "sound", "ng": "cmd/audio/play", "body": "sound"},
  {"a3": "rtttl", "ng": "cmd/audio/play", "body": "rtttlRaw"},
  {"a3": "r2d2", "ng": "cmd/audio/play", "body": "r2d2"},
  {"a3": "settings", "ng": "cmd/settings", "body": "settings"},
  {"a3": "resetSettings", "ng": "cmd/settings/reset"},
  {"a3": "reboot", "ng": "cmd/device/reboot"},
  {"a3": "apps", "ng": "cmd/apps/order", "body": "reorder"},
  {"a3": "switch", "ng": "cmd/apps/switch", "body": "switch"},
  {"a3": "nextapp", "ng": "cmd/apps/next"},
  {"a3": "previousapp", "ng": "cmd/apps/previous"},
  {"a3": "stats", "ng": "state/device", "warning": "responseShape"},
  {"a3": "screen", "ng": "state/screen", "warning": "responseShape"},
  {"a3": "erase", "warning": "mqttNoTopic"},
  {"a3": "doupdate", "warning": "noUpdateApi"}
];

// Every warning the engine can emit. "page" + "anchor" become the link the UI
// renders; the drift check verifies each anchor against the target page's
// headings. {thing} placeholders are filled by the engine.
export const WARNINGS = {
  "deadKey": {"message": "`{key}` has no NG equivalent -- the payload is rejected with 422 until it is removed. {note}", "page": "guides/migrating-from-awtrix3.md", "anchor": "the-key-map"},
  "unmappedKey": {"message": "`{key}` is not an AWTRIX 3 or NG payload key -- NG rejects unknown keys with 422.", "page": "reference/payload.md", "anchor": "errors"},
  "notificationOnlyKey": {"message": "`{key}` only works on a notification; on a pushed app NG rejects it with 422.", "page": "reference/payload.md", "anchor": "notification-only-keys"},
  "lifetimeIgnoredOnNotify": {"message": "`{key}` is never read on a notification -- removed from the payload.", "page": "reference/payload.md", "anchor": "lifetimems-and-lifetimeexpiry"},
  "holdIgnoresDuration": {"message": "`hold: true` makes `durationMs` irrelevant -- the notification stays until dismissed.", "page": "reference/payload.md", "anchor": "notification-only-keys"},
  "enumOutOfRange": {"message": "`{key}: {value}` is outside the AWTRIX 3 range -- left unchanged, NG will reject it.", "page": "guides/migrating-from-awtrix3.md", "anchor": "the-key-map"},
  "valueNotNumeric": {"message": "`{key}` needs a numeric value to become milliseconds -- left unchanged.", "page": "guides/migrating-from-awtrix3.md", "anchor": "timing-and-lifetime"},
  "teffOutOfRange": {"message": "`TEFF: {value}` is not an AWTRIX 3 transition number -- left unchanged.", "page": "reference/visuals.md", "anchor": "transitions"},
  "unknownDrawCode": {"message": "draw command `{key}` is not an AWTRIX 3 code -- left as it is.", "page": "guides/migrating-from-awtrix3.md", "anchor": "draw-commands-arrays-instead-of-objects"},
  "effectNames": {"message": "`effect` names differ between AWTRIX 3 and NG -- check the name against GET /api/v1/capabilities.", "page": "reference/visuals.md", "anchor": "background-effects"},
  "jsCode": {"message": "{where} builds its payload in JavaScript -- convert that code by hand with the key map.", "page": "guides/migrating-from-awtrix3.md", "anchor": "the-key-map"},
  "jinjaPayload": {"message": "{where} is built from templates the converter cannot parse -- convert it by hand with the key map.", "page": "guides/migrating-from-awtrix3.md", "anchor": "the-key-map"},
  "templatedTopic": {"message": "{where} is a template -- the topic was left unchanged; rename its suffix per the topic table.", "page": "guides/migrating-from-awtrix3.md", "anchor": "where-to-send"},
  "yamlMapPayload": {"message": "{where} is a native YAML mapping -- the converter only rewrites JSON payloads; rename the keys by hand.", "page": "guides/migrating-from-awtrix3.md", "anchor": "the-key-map"},
  "settingsNoEquivalent": {"message": "settings key `{key}` has no NG equivalent -- left unchanged, NG will reject it.", "page": "reference/settings.md"},
  "crossEndpoint": {"message": "settings key `{key}` moved to a different call: {note}", "page": "reference/settings.md"},
  "payloadTooLarge": {"message": "the converted payload is over 8192 bytes -- HTTP answers 413, MQTT drops it silently.", "page": "reference/limits.md"},
  "restCommandContentType": {"message": "add `content_type: application/json` to this rest_command -- NG rejects the request with 415 without it.", "page": "reference/conventions.md", "anchor": "content-type-is-mandatory"},
  "nonPosixQuoting": {"message": "this curl uses shell quoting the converter only partly understands -- check the result before running it.", "page": "guides/migrating-from-awtrix3.md", "anchor": "where-to-send"},
  "responseShape": {"message": "the NG response has different fields -- rename any `value_json...` references in your automation.", "page": "reference/device.md"},
  "mqttNoTopic": {"message": "{where} has no NG MQTT topic -- this action is HTTP-only in NG.", "page": "reference/mqtt.md"},
  "noUpdateApi": {"message": "there is no self-update call in NG -- updates are a firmware upload to POST /update.", "page": "guides/updating.md"},
  "internalError": {"message": "the converter hit an internal error -- nothing was changed. Please report this flow.", "page": "guides/migrating-from-awtrix3.md", "anchor": "the-key-map"}
};
