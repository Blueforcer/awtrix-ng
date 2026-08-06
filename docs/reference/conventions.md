# Conventions

These hold across the **whole API** - HTTP routes, MQTT topics, settings and payloads alike, and
are not repeated per route or per field.

## Reading the examples

Every `curl` example writes the address of your AWTRIX as `<awtrix-ip>`. Substitute the address you
reach it on: its IP address, or its mDNS hostname. See
[Finding AWTRIX](../getting-started/discovery.md).

## Keys and durations

* **Keys are `camelCase`**, everywhere, in every payload.
* **Durations are integer milliseconds** and carry an `...Ms` suffix - `appDurationMs`, `blinkMs`,
  `durationMs`, `fadeMs`, `holdMs`, `lifetimeMs`, `retryInMs`, `textBlinkMs`, `textFadeMs`,
  `transitionDurationMs`. The one value that names a different unit is the read-only
  `uptimeSeconds` in `GET /api/v1/device`.

## Colors

Colors are **written back to you as `"#RRGGBB"`** (uppercase). On the way *in*, any of these
is accepted for any color field:

| Form | Example | Notes |
|---|---|---|
| `"RRGGBB"` | `"FF8800"` | leading `#` optional |
| `"RGB"` | `"F80"` | shorthand, each digit doubled |
| `[r, g, b]` | `[255, 136, 0]` | each channel clamped to 0–255 |
| `["HSV", h, s, v]` | `["HSV", 32, 100, 100]` | `h` wrapped into 0–359, `s`/`v` clamped to 0–100 |
| packed integer | `16746496` | `0xRRGGBB` |

Every channel is an **integer**; a fractional value is rejected.

`null` means **inherit or off** - it clears a nullable color back to its no-color meaning
rather than setting it to black.

Exact ranges, HSV wrapping and which keys are nullable: [Colors](visuals.md#colors).

## `Content-Type` is mandatory

Send `Content-Type: application/json` on every request that carries a JSON body.

What is enforced is narrower than that, and knowing it does not help you: a `PUT` or `PATCH`
declaring any other type is refused with `415 unsupportedMediaType` before the body is read, while
a `POST` is not type-checked at all - its body simply arrives empty and the request fails as
`400 invalidJson` instead. Both failures come from the same mistake, so treat the header as
required everywhere.

`curl -d` sends `application/x-www-form-urlencoded` unless you say otherwise, which is why
`-H "Content-Type: application/json"` appears on every `curl` example in these docs.

A `POST` carrying [`X-HTTP-Method-Override`](http.md#method-override) is checked as the method it
names, so an overridden `PATCH` needs the header just like a real one.

The one exemption is `PUT /api/v1/apps/script/{name}`, which carries Berry source rather than JSON
and accepts any content type. Empty bodies, and the exact status code per route:
[Content-Type: the empty-body trap](errors.md#content-type-the-empty-body-trap).

## Errors

Every failing request on the API carries the same body:

```json
{ "error": { "code": "validationFailed", "message": "invalid value", "field": "brightness" } }
```

`code` is a stable machine-readable identifier - match on it, never on `message`, which is English
prose for humans. `field` is present only when a specific input key caused the failure. The one
route that answers in its own shape is `POST /api/v1/restore`. See [Errors](errors.md).

## Authentication

HTTP Basic auth is **off by default** - the whole API is open on your LAN. It turns on when you set
`authEnabled`, which requires a username and password to be stored with it. From that point on it
is enforced in **every** mode, including access-point (provisioning) mode - there is no first-boot
or AP bypass. See [Authentication](http.md#authentication).
