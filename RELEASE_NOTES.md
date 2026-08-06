**New**
- Gateways that can only send POST, like the FRITZ!Box HTTP action, reach every route with an `X-HTTP-Method-Override` header.
- Scripts can read the firmware version with `version()`.

**Changed**
- Smooth scrolling is gone (Settings → Text). Text moves in whole pixels now, which is sharper on an 8 px panel. Nothing to do, a stored value is ignored.

**Fixed**
- A failed temperature or humidity reading wrote `nan` into the MQTT payload. That is not valid JSON, so Home Assistant threw the whole message away and all entities of the device went missing. A bad reading now keeps the last good value (#9).
- Scrolling text stuttered when a frame ran late.
- Custom apps rotated in alphabetical order instead of the order they arrived.
- After `repeat` had shown a message in full, icon and text flashed up once more before the next app.
- Colours turned black at low brightness.
- PNG and JPG icons uploaded in the web UI came out blurry on a grey background. They are converted in the browser now.
- Scripts got wrong answers from `text_width()`, `width()`, `height()` and the sensors outside `draw()`, and only re-saving the script fixed it after a reboot.
- `on_show()` fired about a second too late.
