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

- BrickBreaker, Snake and PingPong are mini-games on every panel size. Each replays a
  round from a fixed start: at most 511 constant-cost ticks per frame, and the round
  restarts every 512 effect frames (about 34 s at the steady rate). The bounded replay makes
  seeking, dropped frames and repeated calls deterministic; state is fixed-size.
- BrickBreaker: bricks are 3 LEDs with a 1-LED gap, centred; 2 rows, 3 from 11 rows up,
  one hue per row. The ball moves one LED at a time and stops on a hit, so a tick removes
  exactly one brick and the ball never stands on one; it passes through the gap columns. The
  3-LED paddle tracks the ball with an aim offset: a centre contact returns at 45 degrees,
  an edge contact shallow (2 across per 1 down). An empty wall refills at the paddle.
- Snake: greedy steering towards a food pixel (Manhattan distance, avoiding its body and
  dead ends) on a field of up to 64x32 cells. Food rows walk a per-round permutation of the
  rows, columns come from `noise::hash2`, and a raster probe skips the body. Length 4 grows
  by one per food; a stuck or 24-long snake starts a new game at once.
- PingPong: paddles on columns 0 and w-1 (3 LEDs, 4 from 11 rows up) always cover the ball's
  row; the receiving one steers to an aim point, the other lags. The paddle edge returns
  45 degrees, the middle (or a corner) shallow.
- SwirlIn/SwirlOut use two rotating elliptical spiral packets sized to the canvas.
  Radial phase moves particles inward/outward; particles wrap at the centre/outer edge.
  Contraction deliberately narrows the image during part of the cycle. This is not a
  fixed-width guarantee at every frame.
- These renderers use no per-frame heap allocation and no platform-specific APIs.

`test/test_tall_effects` checks brick geometry (3 lit, 1 dark), one brick per hit, a
ball never on a brick, ball coverage over more than half the rows and both slopes; food
never on the snake, growth by one with moving food, mostly-closing distance, no
overlapping segments and full-height snake coverage; both PingPong paddles covering the
ball at columns 1 and w-2 and both slopes; swirl width above 70% at the
reference frame and opposite mean-radius movement over a short window. Determinism
and framebuffer guard sentinels (including PingPong) cover widths 4/32/53/64/80 and every height 8 through 16.
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
| PingPong | Single point moving in both axes; sparse sampling is not unused height. PingPong has since become a paddle game (see above). |
| Plasma | Full-canvas multicolour field. |
| PlasmaCloud | Full-canvas smooth cloud field. |
| Radar | Centred rotating ray; circular sweep meets rectangular panel edges naturally. |
| Ripple | Centred expanding circular rings; top/bottom cropping is intentional expansion. |
| TheaterChase | Repeated full-height chase bars. |
| TwinklingStars | Sparse distributed twinkles; no requirement to populate every row in a short sample. |

These are simulator observations, not a replacement for human inspection on the Unicorn.
