**New**
- Home-automation gateways that can only send POST — the FRITZ!Box HTTP action, for example — now reach every route by adding an `X-HTTP-Method-Override` header.
- Scripts can read the firmware version with `version()`.

**Changed**
- Smooth scrolling is gone (Settings → Text). Text now moves in whole pixels, which is sharper on an 8 px panel. Nothing to do — a stored value is simply ignored.

**Fixed**
- A failed temperature or humidity reading put `nan` into the MQTT state payload, which is not valid JSON. Home Assistant then dropped the whole message, so every entity of that device went missing at once, every 20 to 40 minutes for some users. A bad reading now keeps the last good value, and the sensor is read once per cycle instead of twice (#9).
- Scrolling text stuttered: a slow frame moved the text further than the frames around it. Motion now runs on a fixed clock, so a late frame reads as a short pause instead of a jump.
- Custom apps rotated in alphabetical order instead of the order they were sent.
- Once `repeat` had shown a message in full, the icon and text briefly appeared a second time before the next app.
- Colours were crushed to black at low brightness — the grey weekday bars under the clock were the giveaway.
- PNG and JPG icons uploaded in the web UI came out smeared with a grey background. They are converted in the browser now: pixel-exact, and about a tenth of the size.
- Scripts could not measure text or read `width()`, `height()` and sensors outside `draw()`, so an app sizing itself in `init()` or `on_show()` got the wrong answer — and after a reboot only re-saving the script fixed it.
- `on_show()` fired about a second late, so anything reset there kept showing the previous state for the first second.
