---
only: [tc002]
---

# Signing in from scripts

This page lets a script show data from a service that needs the user's account, for example
Spotify or Strava. It assumes you know how a script is built. If not, start with
[Your first script](scripting/index.md#your-first-script).

Only the TC002 has the `oauth` module. On other models the import fails, so a script meant for
every model imports it inside `try`. See [Scripts for every model](ble.md#scripts-for-every-model).

## How it behaves

The user signs in once in the app's settings, as described in
[Signing in to services](sign-in.md). The clock keeps the sign-in and adds it to each request the
script sends with `oauth`, so the script never sees it. When the access runs out, the clock renews
it and repeats the request once by itself. Requests go only to the hosts the script names in its
`@oauth` line.

## Name the service

A script names the service in one line at the top:

```berry
# @oauth authorize=https://accounts.spotify.com/authorize token=https://accounts.spotify.com/api/token api=api.spotify.com scope="user-read-currently-playing" pkce
```

| Part | Meaning |
|---|---|
| `authorize=` | the service's sign-in page |
| `token=` | the address where the service hands out access |
| `api=` | the hosts the script may call. Several are separated by commas. Other hosts are refused. |
| `scope=` | what the script asks for, as the service names it |
| `pkce` | for services that support PKCE, such as Spotify. Some of them need no client secret then. |
| `auth=body` | for services that want client ID and secret inside the request, such as Strava |
| `params=` | extra values for the sign-in page, for example `params="access_type=offline prompt=consent"` for Google |

All addresses start with `https://`.

## Make requests

Add `import oauth`. The calls work like [`http`](scripting/network.md).

| Call | Does |
|---|---|
| `oauth.ready()` | `true` once the user has signed in |
| `oauth.get(url, cb, opts?)` | a GET request |
| `oauth.post(url, body, cb, opts?)` | a POST request. A body without a `Content-Type` header is sent as JSON. |
| `oauth.request(method, url, body, cb, opts?)` | GET, POST, PUT, PATCH or DELETE |

A call returns `false` when the clock does not send it: the user is not signed in, the host is not
in `api=`, or too many requests are waiting. Otherwise the callback gets `(body, status)`:

- `status` 0 and `body` `nil`: no connection.
- `status` 401 and `body` `nil`: the user has to sign in again.

`opts` takes `headers`, a map of strings, and `cap`, the most bytes of the answer to keep. `cap` is
8192 by default and at most 262144.

## Example

This app shows the song playing on Spotify:

```berry
# @oauth authorize=https://accounts.spotify.com/authorize token=https://accounts.spotify.com/api/token api=api.spotify.com scope="user-read-currently-playing" pkce
import oauth
import json

class NowPlaying
  var song

  def setup()
    self.song = ""
    self.fetch()
    timer.every(15000, def () self.fetch() end)
  end

  def fetch()
    if !oauth.ready() return end
    oauth.get("https://api.spotify.com/v1/me/player/currently-playing", def (body, status)
      if status == 204 self.song = "" return end
      if status != 200 || body == nil return end
      var answer = json.load(body)
      var item = answer != nil ? answer.find("item") : nil
      if item != nil self.song = item.find("name", "") end
    end, {'cap': 32768})
  end

  def draw()
    clear()
    if !oauth.ready()
      scroll_text("Sign in in the app settings", 0xFFFFFF)
    elif self.song == ""
      text(1, 6, "--", 0x1DB954)
    else
      scroll_text(self.song, 0x1DB954)
    end
  end
end

return NowPlaying()
```

## Good to know

- **Never ask for passwords or tokens in
  [`@config`](scripting/storage.md#settings-the-user-can-change) fields.** The `@oauth` line keeps
  them out of the script.
- **Changing the `@oauth` line signs the user out.** The user then signs in again in the app's
  settings.

## Related

- [Signing in to services](sign-in.md): how the user signs in
- [Network](scripting/network.md): `http`, which the `oauth` calls work like
- [Scripting guide](scripting/index.md): the full script API
