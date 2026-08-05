**Fixed**
- Scripts that measure text while they load came up broken after a reboot and only recovered when the script was saved again by hand. `text_width()` and `text_ink_width()` now answer correctly everywhere, including in `init()` and `setup()` at boot.
- `width()` and `height()` returned 0 outside `draw()`, so an app sizing itself in `init()`, `on_show()` or `duration()` got the wrong answer. They now report the panel size in every hook.
- Sensor readings were unavailable while a script loaded at boot.
- `on_show()` fired a full transition late — around a second after the app had already started drawing, so anything reset there (a scroll position, an animation step) spent the first second showing the previous appearance's state. It now runs before the app's first frame.
