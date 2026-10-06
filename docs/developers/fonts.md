# Fonts

AWTRIX NG has twelve panel fonts: the legacy `small` and `large`, and ten Matrix-Fonts. All are
monochrome bitmap fonts compiled into the firmware as C++ headers; nothing is loaded at run time.
The sources are BDF files in
[`assets/fonts/`](https://github.com/Blueforcer/awtrix-ng/tree/main/assets/fonts), the generators
are in [`scripts/`](https://github.com/Blueforcer/awtrix-ng/tree/main/scripts), and the generated
headers are in [`src/media/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/media). How
the fonts look and how to choose one is in [Text](../guides/text.md) and the
[visual reference](../reference/visuals.md).

## Sources

| Directory | Content |
|---|---|
| `assets/fonts/matrix-fonts/` | The ten original Matrix-Fonts BDF files and their `LICENSE`, byte-identical to upstream, with `provenance.json` |
| `assets/fonts/awtrix-ng/` | One supplement BDF per font: the glyphs AWTRIX NG adds so every font covers the same characters |
| `assets/fonts/` | `awtrix.bdf` and the `MatrixChunky6.bdf` / `MatrixChunky8.bdf` the legacy fonts are built from |

The originals are unchanged copies of
[Trip5 / Matrix-Fonts](https://github.com/trip5/Matrix-Fonts/tree/894dddc5e1b158e43483ffbf31c7629f69391124),
commit `894dddc5e1b158e43483ffbf31c7629f69391124`. `provenance.json` records a SHA-256 digest for
every upstream file, so no network access is needed to reproduce the generated fonts.

| Font IDs | Original files | Glyphs in the original | Ascent / descent / line height |
|---|---|---|---|
| `matrix-chunky6`, `matrix-chunky6x` | `6-series/MatrixChunky6.bdf`, `MatrixChunky6X.bdf` | 441 | 6 / 0 / 6 |
| `matrix-light6`, `matrix-light6x` | `6-series/MatrixLight6.bdf`, `MatrixLight6X.bdf` | 441 | 6 / 0 / 6 |
| `matrix-chunky8`, `matrix-chunky8x` | `8-series/MatrixChunky8.bdf`, `MatrixChunky8X.bdf` | 731 | 8 / 0 / 8 |
| `matrix-light8`, `matrix-light8x` | `8-series/MatrixLight8.bdf`, `MatrixLight8X.bdf` | 731 | 8 / 0 / 8 |
| `matrix-chunky8x6`, `matrix-light8x6` | `8-series/MatrixChunky8x6.bdf`, `MatrixLight8x6.bdf` | 463 | 8 / 0 / 8 |

The catalog has 6- and 8-pixel fonts only. The suffix `8x6` names upstream's fixed-width variant;
it does not mean scaling. Original glyphs keep their pixels, advance and BDF offsets, including
glyphs wider than eight pixels.

## Supplements

The supplements in `assets/fonts/awtrix-ng/` make every font cover the same character set as the
largest original, 731 characters.

| File | Read by | On top of |
|---|---|---|
| `MatrixChunky6.bdf`, `MatrixChunky6X.bdf`, `MatrixLight6.bdf`, `MatrixLight6X.bdf` | `scripts/gen_matrix_fonts.py` | the 6-series original of the same name |
| `MatrixChunky8x6.bdf`, `MatrixLight8x6.bdf`, `MatrixLight8X.bdf` | `scripts/gen_matrix_fonts.py` | the 8-series original of the same name |
| `small.bdf` | `scripts/gen_font.py` | `awtrix.bdf` filled from `MatrixChunky6.bdf` |
| `large.bdf` | `scripts/gen_font.py` | `MatrixChunky8.bdf` |

What they add:

- Greek, Vietnamese (`Ơ ơ Ư ư`, `U+1EA0`–`U+1EF9`), IPA (`U+0250`–`U+02AF`) and the Chinese and
  Korean date and weekday characters in the 6-series fonts.
- Vietnamese, IPA, Chinese and Korean, currency signs, punctuation, `℃ ℉ □ ￥ �` and the pixel
  spaces `⓪ ①`–`⑩` in the `8x6` fonts.
- Greek, Vietnamese, IPA, Chinese and Korean in `small`.

Each glyph follows the cap height, x-height, stroke and advance of the font it joins. Letters that
look like Latin reuse the font's own Latin glyph. Accented letters are composed from the font's
letters and marks. The rest was drawn from the matching 8-series glyph at the target height. IPA,
Chinese and Korean are shared between the Chunky and Light variants of a size, as upstream does in
the 8-series. `⓪ ①`–`⑩` are blank on purpose: in Matrix-Fonts they are spaces 0 to 10 pixels
wide.

A supplement glyph wins over an original with the same code point. Four cases use this:

- `ă Ă` in the 6-series, which upstream draws exactly like `â Â`;
- the umlauts in `matrix-chunky6` and `matrix-light6` that upstream places one row below their
  base letters; the capital glyphs are compacted to keep the marks inside six rows;
- `⓪` in `MatrixLight8X`, which upstream gives one pixel of advance where every other font gives
  none;
- in `large`, the Vietnamese letters with two marks, which in `MatrixChunky8` would reach above the
  panel once they sit on the baseline.

## Generating the headers

```bash
python scripts/gen_matrix_fonts.py   # src/media/MatrixFonts.h and MatrixFontsCompact.h
python scripts/gen_font.py           # src/media/AwtrixFont.h (small and large)
python tools/check_font_sync.py      # checks all three headers against their sources
```

`gen_matrix_fonts.py` verifies every source digest, reads each font's supplement on top of it and
packs the monochrome pixels without resizing. All ten fonts share one bitmap, so a glyph that
several fonts draw alike (the IPA, Chinese and Korean sets of a size, the letters Chunky and Light
have in common) is stored once. The generated header lists, per font, how many glyphs are
original, how many come from AWTRIX NG and how many originals a supplement replaces.

| Header | Fonts | Used by |
|---|---|---|
| `MatrixFonts.h` | all ten Matrix fonts | Linux and the TC002 |
| `MatrixFontsCompact.h` | `matrix-light6`, `matrix-chunky8x6`, in their own shared bitmap | ESP32 builds (`AWTRIX_COMPACT_FONTS=1`) |
| `AwtrixFont.h` | `small`, `large` | every build |

So the ESP32 carries four fonts and Linux carries twelve. The compact header contains no tables
of the omitted fonts. Fonts are immutable compiled data; there is no run-time buffer per font.
`check_font_sync.py` runs in CI, and the native tests exercise the full and the compact catalog
separately.

To edit a glyph, change the supplement BDF, never the original, then regenerate and run the check.

## License

The twelve combined panel fonts, including the generated bitmap, glyph, metric, index and range
tables, are distributed under the **SIL Open Font License 1.1** (OFL). The full texts and notices
are in
[`LICENSES/MIT-Matrix-Fonts.txt`](https://github.com/Blueforcer/awtrix-ng/blob/main/LICENSES/MIT-Matrix-Fonts.txt).

Why OFL:

- The pinned Matrix-Fonts repository grants MIT (copyright 2026 Trip5) in its
  [LICENSE](https://github.com/trip5/Matrix-Fonts/blob/894dddc5e1b158e43483ffbf31c7629f69391124/LICENSE).
  Every original BDF also contains an unversioned `COMMENT "CC-BY"`. The originals and that comment
  stay unchanged; no CC-BY version is inferred.
- Matrix-Fonts' 8-series includes Chinese glyphs derived from BoutiqueBitmap7x7, which is
  OFL-licensed. The [OFL FAQ, section 3.2](https://openfontlicense.org/ofl-faq/#32-i-have-a-font-that-needs-a-few-extra-glyphs---can-i-take-them-from-an-ofl-licensed-font-and-copy-them-into-mine)
  requires a distributed font that incorporates OFL glyphs to stay under the OFL. This also applies
  to supplements drawn from those glyphs, including the six-row fonts, and it covers the whole
  font, not only the Chinese glyphs.
- Trip5's MIT grant permits merging and sublicensing Trip5's own contributions. The MIT notice is
  kept, but the combined font is not offered under MIT as an alternative.

AWTRIX NG's own font contributions (`assets/fonts/awtrix.bdf`, every supplement and the project's
portions of the generated font data) are released under OFL-1.1 by the
[font data exception](https://github.com/Blueforcer/awtrix-ng/blob/main/LICENSE.md#font-data-exception).
The application, the renderer and the generator programs keep their own licenses; the font
license does not extend to them.

The [upstream README](https://github.com/trip5/Matrix-Fonts/blob/894dddc5e1b158e43483ffbf31c7629f69391124/README.md#inspiration)
names the fonts' earlier sources:

| Source | License evidence | Relevance |
|---|---|---|
| Tom Thumb, Brian Swetland and Robey Pointer | [Author's page](https://robey.lag.net/2010/01/23/tiny-monospace-font.html), offering MIT, CC0 or CC-BY 3.0 | Original base; later drawings may still resemble it. |
| Dalmoori, RanolP and contributors | [Apache-2.0 license](https://github.com/RanolP/dalmoori-font/blob/897f0e71224d9964a84b888f2596b2bfd7f98def/LICENSE) | Inspiration for the Korean characters. |
| BoutiqueBitmap7x7, Cen-cyun Liu / Luke Liu, with MisakiGothic by Num Kadoma | [Attribution and OFL-1.1 text](https://github.com/scott0107000/BoutiqueBitmap7x7/blob/b174bc547dfaad118642316f279b4fec82ca7121/OFL.txt) | Source of copied Chinese characters and the stretched X-series variants. |

BoutiqueBitmap7x7
[reserves](https://github.com/scott0107000/BoutiqueBitmap7x7/blob/b174bc547dfaad118642316f279b4fec82ca7121/README.md#授權)
the names `BoutiqueBitmap`, `精品點陣體` and `精品点阵体`. No AWTRIX NG font uses them, and
AWTRIX NG declares no Reserved Font Names of its own. The same section permits modification and
bundling under OFL without notifying the author.

## Related

- [Text](../guides/text.md) and the [visual reference](../reference/visuals.md)
- [Display foundation](display-foundation.md)
- [Building from source](building.md)
