# Conventions

Rules that hold across the **whole API** - HTTP routes, MQTT topics, settings and payloads. They
are not repeated on each route or field.

## Reading the examples

Every `curl` example writes the address of your AWTRIX as `<awtrix-ip>`. Replace it with the
address you reach AWTRIX on: its IP address, or its mDNS name (mDNS lets you use a name such as
`awtrix.local` instead of an IP). See [Find your clock](../getting-started/discovery.md).

## Keys and durations

* **Keys are `camelCase`** in every payload.
* **Durations are whole milliseconds** and their keys end in `Ms`: `appDurationMs`, `blinkMs`,
  `durationMs`, `fadeMs`, `holdMs`, `lifetimeMs`, `retryInMs`, `textBlinkMs`, `textFadeMs`,
  `transitionDurationMs`. The only value in another unit is `uptimeSeconds` in
  `GET /api/v1/device`, which you can only read.

## Colors

AWTRIX **always answers with colors as `"#RRGGBB"`** (uppercase). When you send a color, any of
these forms works for any color field:

| Form | Example | Notes |
|---|---|---|
| `"RRGGBB"` | `"FF8800"` | the leading `#` is optional |
| `"RGB"` | `"F80"` | short form, each digit is doubled |
| `[r, g, b]` | `[255, 136, 0]` | each channel is clamped to 0–255 |
| `["HSV", h, s, v]` | `["HSV", 32, 100, 100]` | `h` wraps around into 0–359, `s` and `v` are clamped to 0–100 |
| packed integer | `16746496` | `0xRRGGBB` |

Every channel must be a **whole number**. A value with a fraction is rejected.

`null` means **inherit or off**. On a color that allows `null`, it removes the color instead of
setting it to black.

Exact ranges, HSV wrapping and which keys allow `null`: [Colors](visuals.md#colors).

## Send `Content-Type: application/json` {#content-type-is-mandatory}

Send `Content-Type: application/json` with every request that has a JSON body. This is what
AWTRIX checks:

* On `PUT` and `PATCH`, a `Content-Type` other than `application/json` is rejected with
  `415 unsupportedMediaType`, and nothing is applied.
* A request **without** a `Content-Type` header is accepted; the body is read as JSON.
* `POST` is not checked. A JSON body sent as `application/x-www-form-urlencoded` is read as JSON
  like any other.

So the header is not strictly required, but a wrong one breaks the request. Sending the right one
makes every request behave the same way.

`curl -d` sends `application/x-www-form-urlencoded` unless you say otherwise. That is why every
`curl` example in these docs has `-H "Content-Type: application/json"`.

A `POST` with [`X-HTTP-Method-Override`](http.md#method-override) is checked as the method it
names. An overridden `PATCH` needs the header just like a real one.

Exceptions:

* `PUT /api/v1/apps/script/{name}` takes the Berry source itself and accepts any content type.
* `PUT /api/v1/apps/script-update/{name}` accepts any content type, but the body must be JSON.
* File uploads use their own documented format.

What an empty body does on each route, and the exact status codes:
[Content-Type: the empty-body trap](errors.md#content-type-the-empty-body-trap).

## Errors

Every failed request gets the same body:

```json
{ "error": { "code": "validationFailed", "message": "invalid value", "field": "brightness" } }
```

* `code` never changes - check this in your code.
* `message` is an English text for people and may change - do not check it in your code.
* `field` is only there when one specific key caused the error.

The only route with a different answer is `POST /api/v1/restore`. See [Errors](errors.md).

## Authentication

Login is **off by default** - anyone on your network can use the API. It turns on when you set
`authEnabled`, which needs a username and password saved with it. From then on, AWTRIX asks for
them (HTTP Basic auth) in **every** mode, also in access-point (setup) mode. See
[Authentication](http.md#authentication).

## Related

- [HTTP API](http.md)
- [MQTT API](mqtt.md)
- [Errors](errors.md)
- [Limits](limits.md)
