# Native simulator effect survey

Run in the build container from the repository root after building `native_sim`:

```console
python tools/sim/effect_survey.py --size 32x8 --size 53x11 --out .pio/effect-survey
```

The tool starts an isolated simulator per size using a temporary `device.json`, enumerates
`/api/v1/capabilities`, holds an empty-text notification for each effect, and samples
`/api/v1/display/screen`. It writes one scaled PNG strip per effect, plus `summary.md`
and `summary.json`. PNG encoding uses only Python's standard library. The simulator is
terminated and temporary device data removed even on failure.

Defaults: 12 frames, 250 ms interval, 500 ms settling time, 4x nearest-neighbour scaling.
`--frames`, `--interval`, `--settle`, `--scale`, `--port`, and `--program` are configurable.
Rows are zero-based; lit/changing rows are unions over the sampled frames. These are
wall-clock observations, not a whole-cycle guarantee or deterministic golden snapshots.
Sparse effects and periodic sampling can omit rows or repeat frames. Use host tests for
frame-exact coverage and determinism assertions.

## Tall-panel effects

- BrickBreaker replays at most 511 ticks from a fixed launch. Two rows of eight bricks
  are knocked out on contact; the ball bounces off the wall and tracking paddle. An
  empty wall resets at the paddle, and the whole round resets every 512 effect frames.
  The bounded replay makes seeking, dropped frames and repeated calls deterministic.
- Snake follows a vertical serpentine path over every column and row, reversing at the
  far endpoint. Its six-pixel trail crosses the height quickly even on wide panels.
- SwirlIn/SwirlOut use two rotating elliptical spiral packets sized to the canvas.
  Radial phase moves particles inward/outward; particles wrap at the centre/outer edge.
  Contraction deliberately narrows the image during part of the cycle. This is not a
  fixed-width guarantee at every frame.
- These renderers use no per-frame heap allocation and no platform-specific APIs.

`test/test_tall_effects` checks ball coverage over more than half the rows, changing
bricks and round reset, rapid full-height snake coverage, swirl width above 70% at the
reference frame and opposite mean-radius movement over a short window. Determinism
and framebuffer guard sentinels cover widths 32/53 and every height 8 through 16.
Guard sentinels verify no out-of-buffer memory writes; Canvas itself drops out-of-range
coordinates. This does not instrument attempted clipped draws.

## Visual review

Before/after strips and complete survey tables were reviewed; they are not checked in
as generated assets. Reviewed all 19 effects at 53x11 and the four changed
effects at 32x8. BrickBreaker now visibly separates ball from paddle and removes bricks;
Snake traverses the height; swirls contract/expand over the panel width instead of
remaining a small clipped central cluster.

The other 15 effects were left unchanged:

| Effect | Reason |
| --- | --- |
| Checkerboard | Full-canvas alternating tiles; odd edge dimensions naturally crop a tile. |
| ColorWaves | Full-height colour bands, no blank band or misplaced centre. |
| Fade | Uniform whole-canvas fade; near-black frames are intentional. |
| Fireworks | Small expanding bursts at varying locations; edge cropping is natural. |
| LookingEyes | Recognisable centred pair with gaze/blink; explicitly excluded from changes. |
| Matrix | Falling heads/trails across the whole canvas. |
| MovingLine | Full-height line scanning horizontally. |
| Pacifica | Smooth full-canvas water pattern. |
| PingPong | Single point moving in both axes; sparse sampling is not unused height. |
| Plasma | Full-canvas multicolour field. |
| PlasmaCloud | Full-canvas smooth cloud field. |
| Radar | Centred rotating ray; circular sweep meets rectangular panel edges naturally. |
| Ripple | Centred expanding circular rings; top/bottom cropping is intentional expansion. |
| TheaterChase | Repeated full-height chase bars. |
| TwinklingStars | Sparse distributed twinkles; no requirement to populate every row in a short sample. |

These are simulator observations, not a replacement for human inspection on the Unicorn.
