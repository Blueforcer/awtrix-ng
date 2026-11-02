---
only: [tc002]
---

# Making a voice pack

This page describes everything a voice pack holds and how the voices of the project were trained,
so you can build your own: another voice, another language, or both.

It is a description, not a kit. The repository holds no training code, no language tables, no
datasets and no checkpoints. You write the tools yourself, for example in Python with PyTorch.

You need:

- Programming experience and some knowledge of neural speech models.
- A GPU. On an RTX 5090 the training itself takes some hours.
- Training audio of one speaker, about 10,000 sentences. See [Training data](#training-data).
- For a new language: its pronunciation, as tables. See [Language tables](#language-tables).

If you only want a voice, [ready-made and custom packs](../../guides/speech.md#another-voice-or-language)
save all of this.

## The steps

| Step | Result |
|---|---|
| 1. [Write the language tables](#language-tables) | `fe.text`, `fe.lexicon` |
| 2. [Prepare training data](#training-data) | WAV files and their sentences |
| 3. [Turn sentences into tokens](#tokens) | the labels, from the firmware's own frontend |
| 4. [Train](#training) vocoder, aligner and acoustic model | float checkpoints |
| 5. [Export](#export) to int8 and [write the file](#the-file) | `voice.atts` |
| 6. [Upload and listen](#upload-and-check) | a speaking clock |

Step 3 depends on step 1: a voice is trained on the phones and tokens of its tables. A table change
that changes what a word reads means new labels, and usually new training.

## Language tables

`fe.text` is UTF-8 text. One TAB separates the fields of a line. A space inside a key such as
`hundred 1` stays a space. Lines that are empty or start with `#` are skipped. A line `[name]`
starts a section. This shortened example shows the form of each kind of line, and it is not a
complete table:

```text
[phones]
_	silence
a	vowel
t	consonant

[letters]
ä ö ü ß

[fold]
Ä	ä

[numbers]
0	null
hundred 1	einhundert
decimal	,	komma

[stress]
first	3
prefix	be
onset	aeiou	Q

[variants]
lesen	1
lesen	0	opening

[rules]
ch	[aou]	-	X
e	-	#	AX
```

| Section | Fields |
|---|---|
| `[phones]` | name, then `silence` (first, once), `vowel` or `consonant`. 2 to 64 phones. A name has up to 8 printable ASCII characters and no digit. The order is the model's: the first line is phone 0 |
| `[letters]` | letters besides `a`-`z`, separated by spaces. Non-ASCII only |
| `[fold]` | from, to: replaces a text that starts with a non-ASCII character, longest first. A third field `alone`: only when no ASCII letter follows. ASCII capitals become lower case by themselves, other capitals need a line here |
| `[symbols]` | one ASCII character, its words |
| `[numbers]` | see below |
| `[spell]` | a letter `a`-`z`, its name. Used for acronyms |
| `[abbreviations]` | written form (lower case, no space), its words. Matched at a word's start, longest first |
| `[stress]` | `first`, minimum letters of a word that gets automatic stress. `prefix`, an unstressed prefix, repeatable. `onset`, letters, up to 4 phones: a word that starts with one of the letters begins with these phones |
| `[variants]` | word, variant number 0 to 15: the lexicon variant to read. A third field `opening`: the variant to read when the word opens a sentence. Otherwise the first variant is read |
| `[rules]` | letters, left context, right context, phones. At most 4096 |

All sections are optional for the parser except `[phones]` and the required part of `[numbers]`. Put
`[phones]` first: `onset` and `[rules]` name phones, and only names from lines above count. Neither
can use phone 0, so a rule cannot say a pause.

The words in `[numbers]`, `[symbols]`, `[spell]` and `[abbreviations]` are read as written, without
folding. Write them as folded text: `{h} uhr`, not `{h} Uhr`. A capital that matches no lexicon entry
and no rule is skipped without an error.

**Numbers.** Required: `0` to `99`, `hundred 1` to `hundred 9`, `thousand`, `minus`, `time exact`,
`time early` (minutes 1 to 9) and `time other`. The time lines use `{h}` and `{m}`.

| Line | Meaning | Example |
|---|---|---|
| `one thousand` | the thousand of 1000 to 1999, in place of the count and `thousand` | German `eintausend` |
| `thousand join`, `rest join` | `space` (the default) or `none` between the parts | German `none` |
| `decimal` sign, word | a decimal sign between digits; its word, then digit by digit | German `,` `komma` |
| `group` sign | before exactly three digits it joins them | German `1.000` |
| `time` | the characters that separate hours and minutes. Without this line, times are read as numbers | `:` |
| `hour one` | hour 1 of a time | German `ein` |
| `hour N` | hour N (0 to 23) of a time | Polish `siódma` |
| `time word` | a folded word after a time that is not read twice | German `uhr` |
| `hundred N exact` | N·100 with nothing after it | Spanish `cien` (`hundred 1` is `ciento`) |
| `thousand few` | the thousand word after a count ending in 2 to 4 but not 12 to 14 | Polish `tysiące` |
| `ten thousand` | numbers from 10 000 group by four digits | Mandarin 万 |
| `zero` | said once where a number skips a place | Mandarin 零: 105, 1005, 100 500 |
| `ten prefix` | said before 10 to 19 after a higher place | Mandarin 一: 110 is 一百一十 |
| `two count` | 2 counting thousands and ten thousands | Mandarin 两 |
| `percent first` | `n%` reads this before the number, the sign not again | Mandarin 百分之 |
| `year word` | a four-digit number before this folded word reads digit by digit | Mandarin 年 |
| `one before word` ending, new ending | a number before a word or a `[symbols]` sign ends this way, longest ending first | Spanish `uno`, `un` |

**Rules.** At each letter the first rule in file order whose letters and contexts match wins, and
the word goes on after its letters. A letter no rule takes is skipped. `letters` holds 1 to 8
letters. A context holds up to 8 positions, each one letter except `#`: `#` the word's edge, `?` any
letter, `[abc]` one of, `[^abc]` none of, any other character itself. Both contexts are written in
reading order, so a left context ends with the letter right before `letters`. `-` means no context.
Phones are names from
`[phones]` separated by spaces, `-` for none. A digit after a phone sets its stress (`II1` primary,
`II2` secondary). A word whose rules set no stress gets primary stress on its first vowel after an
unstressed prefix, from `first` letters on.

**Reading order.** A word is read by the lexicon first, by the rules otherwise. Words in the lexicon
are folded text. An acronym's entry is written in capitals (`UPS`), so that `US` stays apart from
`us`. The clock looks for it only when the pack has a `[spell]` section and the text also holds lower
case; otherwise `UPS` is read as the word `ups`. An acronym is a whole word of 2 or 3 capitals, or of
4 or 5 capitals without A, E, I, O, U or Y.

### The lexicon

`fe.lexicon` is optional and binary, little-endian. It counts toward the pack's
[size limit](#the-file).

| Part | Layout |
|---|---|
| Header, 16 bytes | `LXR1`, `uint16` block stride (32), `uint8` phone count (of `[phones]`), `uint8` flags 0, `uint32` words, `uint32` blocks = words / stride, rounded up |
| Index | one `uint32` per block: the offset of the block's first word, counted from the end of the index |
| Word | one byte: shared-prefix length (high nibble) and suffix length (low nibble). A length of 15 or more sets its nibble to 15, and an exact `uint8` follows, prefix first. Then the suffix's UTF-8 bytes, a `uint8` variant count from 1, and the variants |
| Variant | `uint8` phone count, 1 to 64, then a bit stream, least significant bit first: 6 bits phone number and, after a vowel only, 2 bits stress. It ends on a byte boundary |

Words are sorted by their UTF-8 bytes and stay under 64 bytes. The first word of a block shares
nothing with the word before it, so its prefix length is 0.

Good sources of pronunciations are CMUdict for English and Wiktionary data for other languages.
Check their licences before you share a pack.

## Training data

The project's voices were trained on about 10,000 sentences, about twelve hours of speech by one
speaker.

- Sentences that cover the language: all phones in many neighbours, numbers and times, the words
  your clock will say.
- 24 kHz, mono, 16 bit.
- One sentence per clip, 0.5 to 10 seconds.
- Edges trimmed to 60 ms of silence, the same level everywhere: about -20 dBFS while speaking, peaks
  under -1 dBFS.
- A check per clip that it says its text. A speech recogniser such as Whisper finds wrong clips, and
  a speaking rate of 8 to 21 phones per second finds broken ones.

You can record a speaker, or generate the clips with a voice-cloning model from a short recording.
Recording that much is a lot of work, so the project's voices use generated clips. Check two things
first:

- The speaker's permission for the voice.
- The licence of the generating model. It has to cover its weights and its output. Some licences
  forbid commercial use or training on the output. The project uses Chatterbox (Resemble AI, MIT).

Keep 1 % of the clips out of training, to judge the result.

## Tokens

Compile the firmware's frontend, `SpeechFrontend.cpp` and `SpeechTokens.cpp` in
`src/platform/tc002/speech/`, and feed it each sentence with your tables. `Frontend::prepare()`
returns a `Plan`, and `chunkTokens()` turns it into tokens. Labels from anywhere else do not match what the clock says later.

A clip is one chunk: BOS, then one token per phone. A token has four numbers, one embedding table each:

| Number | Values |
|---|---|
| phone | index in `[phones]` |
| stress | 0 none, 1 primary, 2 secondary |
| word end | 1 on the last phone of a word |
| punctuation | on a pause: 1 `,` 2 `.` 3 `?` 4 `!` 5 `;` 6 `:`. 0 otherwise. BOS is phone 0, stress 0, word end 0, punctuation 7 |

The clock's chunks hold at most 127 phones, and every sentence is a chunk of its own. A sentence or
piece under 8 phones joins its neighbour.

## The model

The firmware runs one fixed architecture, and only its sizes come from the file. Train exactly these
layers.

**Audio.** 24 kHz, `n_fft` 1024, `hop` 256, periodic Hann window, centred with reflection, 100 mels on
the HTK scale from 0 to 12 kHz without norm, magnitude rather than power. Log with a floor of 1e-5.
Then normalise each mel bin to mean 0 and deviation 1 over your dataset. Vocoder and acoustic model
need the same means and deviations: the clock passes `am.mel_out` to `voc.in` unchanged, so two sets
distort the voice without an error. A clip of `S` samples has `S / 256 + 1` frames.

**ConvNeXt block.** Input `x`, one row per frame or phone. Depthwise convolution with zero padding,
layer norm (eps 1e-6), dense layer to `width * ratio`, GELU, dense layer to `width`. Output
`x + gamma * y`, with one trainable `gamma` per channel that starts at 1 divided by the number of
blocks.

**Acoustic model.**

1. The four embeddings of a token, summed.
2. Encoder: ConvNeXt blocks with their dilations, then a layer norm.
3. Two predictors on the encoder's output, duration and pitch. Each: convolution with kernel 3 to
   `d_pred`, ReLU, layer norm, the same again, dense layer to 1.
4. Pitch embedding: convolution with kernel 3 from the pitch to `d_enc`, added to the encoder's output.
5. Length regulator: a phone with duration `d` repeats for `d` frames. Each frame gets two more
   values: `(j + 0.5) / d` for frame `j` of the phone, and `log(1 + d) / 4`.
6. Dense layer to `d_dec`.
7. Decoder: ConvNeXt blocks, dilation 1, then a layer norm.
8. Dense layer to the 100 normalised log-mels.

The duration predictor outputs `y = log(1 + frames)`. The clock plays
`clamp(floor(exp(y) - 1 + 0.5), 1, max_duration)` frames.

**Vocoder.** Convolution with kernel `voc_in_kernel` from the mels to `voc_dim`, layer norm,
ConvNeXt blocks with dilation 1, layer norm, dense layer to `n_fft + 2`. The first half of a frame's
output is the log-magnitudes, the second half the phases.

**Wave.** Magnitude `min(100, exp(min(60, x)))`, with DC and Nyquist set to zero. Inverse FFT of
magnitude and phase. Overlap-add, divided by the sum of the squared windows. The firmware also runs
a DC blocker, `y = x - x' + 0.9973 y'`, that you can leave out of training.

**A convolution** of `k` taps is a dense layer over the gathered input, with zeros outside the chunk.
Its weight index `c * k + j` means channel `c` of frame `t + (j - k / 2) * dilation`. This holds for
the kernels of 3 and the vocoder's input. A depthwise convolution reads the same frames: its weight
`[c][j]` multiplies channel `c` of that frame into output channel `c`.

**Sizes.** These have run in real time on the TC002:

| Key | Value | Key | Value |
|---|---|---|---|
| `d_enc` | 80 | `dec_blocks` | 2 |
| `enc_kernel` | 5 | `dec_ratio` | 2 |
| `enc_blocks` | 3 | `voc_dim` | 112 |
| encoder dilations | 1, 2, 4 | `voc_in_kernel` | 3 |
| `enc_ratio` | 2 | `voc_kernel` | 7 |
| `d_pred` | 32 | `voc_blocks` | 5 |
| `d_dec` | 128 | `voc_ratio` | 2 |
| `dec_kernel` | 7 | `max_duration` | 255 |

With 41 phones that is about 0.77 MB and 62 million multiply-adds per second of audio. A bigger
model can fall behind on the clock: the speech then stutters. Test bigger sizes on the clock itself.

## Training

The numbers are what the project's voices use. They are a starting point, and your judgement by ear
counts more than the loss.

**1. Vocoder, without labels.** Random crops of 64 frames, batch 16.

- Generator loss: 45 × L1 of log-mels at `n_fft` 512, 1024 and 2048, plus adversarial loss (LSGAN),
  plus 2 × feature matching.
- Discriminators as in HiFi-GAN: multi-period with periods 2, 3, 5, 7, 11, and multi-resolution on
  spectrograms.
- AdamW, betas 0.8 and 0.99, learning rate 4e-4 for the generator and 2e-4 for the discriminators,
  decay 0.999995 per step.
- 30,000 steps in this phase. It can run while the next steps run. Fix the mel statistics when it
  starts, and use them for the acoustic model too.

**2. Alignment.** Nothing tells you yet how long each phone lasts. Train a small aligner as in
"One TTS Alignment to Rule Them All" (Badlani et al. 2021): convolutions on tokens and mels,
attention with a beta-binomial prior, forward-sum loss, then a binarisation loss. Monotonic alignment
search gives each phone its frames. 4,000 steps are enough. BOS takes the silence at the start, the last pause the
silence at the end.

**3. Pitch.** Track F0 for each clip, for example with YIN, on frames centred like the mels. Average
it per phone over voiced frames. Use log-F0, normalised by the speaker's mean and deviation, and 0
for an unvoiced phone.

**4. Acoustic model.** Teacher-forced with real durations and pitch.

- Loss: L1 on the log-mels, plus 0.5 × (1 - SSIM) against over-smoothing, plus MSE of the
  log-durations, plus MSE of the phone pitch.
- AdamW, betas 0.9 and 0.98, learning rate 1e-3, warm-up over 1,000 steps, cosine decay.
- Dropout 0.1 in encoder, decoder and predictors.
- 40,000 steps.

**5. Vocoder again.** Run the acoustic model over the clips with real durations and pitch, to get
predicted mels. Continue the vocoder up to 60,000 steps in total. Each clip in a batch uses its
predicted mels with a probability of 0.5, its real mels otherwise.

The vocoder's loss stops saying much after about 18,000 steps, and later steps can sound worse. Keep
snapshots every 10,000 steps and compare them by ear.

Same speaker in a new language: start the vocoder from the other language's weights and mel
statistics.

## Export

Dense layers and normal convolutions go to int8. Everything else stays float32, depthwise
convolutions included.

**Weights.** Per output row, in float32: `scale = max|w| / 127` (1 for an all-zero row), then
`code = round(w / scale)` half away from zero, clamped to ±127. Never -128.

**Inputs.** The clock quantises every input vector at run time the same way:
`s = max|x| / 127` (1 when all are 0). So `y = (s * scale) * sum(code_w * code_x) + bias`.

Compare float and int8 before you upload. If the waveforms correlate under 0.98 per clip, fine-tune
briefly with both quantisations in the graph (a straight-through estimator).

## The file

Little-endian. At most 2,000,000 bytes for an upload. Every tensor starts on a 16-byte boundary. Fill
gaps with zeros.

**Header, 64 bytes.**

| Offset | Field |
|---|---|
| 0 | `ATTS` |
| 4 | version: 2 |
| 8 | header bytes: header and directory, 64 + 64 per tensor, rounded up to 16 |
| 12 | file bytes: the whole file. It needs no padding after the last tensor |
| 16 | CRC-32 (zlib) of the whole file with this field zero |
| 20 | tensor count, up to 1024 |
| 24, 28, 32, 36 | sample rate 24000, `hop`, `n_fft`, mels |
| 40 | token contract: 2 |
| 44 | flags: 0 |
| 48 | 16 zero bytes |

**Directory.** One 64-byte entry per tensor, from offset 64: name (40 bytes, NUL-terminated,
printable ASCII without spaces), then six `uint32`: kind, rows, cols, stride, offset, bytes. The
offset counts from the start of the file.

| Kind | Holds | Stride | Bytes |
|---|---|---|---|
| 0 | float32 `[rows][cols]`. A vector is `[n][1]` | cols | rows × cols × 4 |
| 1 | a dense layer: `int8 W[rows][stride]`, then `float scale[rows]`, `float bias[rows]` | cols rounded up to 16, zero-padded | rows × stride + rows × 8 |
| 2 | uint32 `[rows][cols]` | cols | rows × cols × 4 |
| 3 | raw bytes, rows 1 | cols | cols |

**Tensors.** `P` is the number of phones, `m` the mels, and the other letters are the sizes in `config`.
`<i>` counts from 0, for example `am.enc.0.dw.w`. Order does not matter. Other tensors are ignored,
for example your `mel_mean`.

| Name | Kind | Rows × cols |
|---|---|---|
| `config` | 2 | 1 × (15 + `enc_blocks`) |
| `pack.info` | 3 | a JSON object of strings: `name`, `language` (a BCP 47 tag), optional `version`, each 1 to 64 bytes. Leave out a `version` you do not have: an empty one is refused |
| `fe.text`, `fe.lexicon` | 3 | the tables. `fe.lexicon` is optional |
| `am.phone` | 0 | `P` × `d_enc` |
| `am.stress`, `am.wordend`, `am.punct` | 0 | 3, 2 and 8 × `d_enc` |
| `am.enc.<i>` | block | width `d_enc`, `enc_kernel`, `enc_ratio` |
| `am.enc_ln` | norm | `d_enc` |
| `am.dur`, `am.pitch` | predictor | see below |
| `am.pitch_emb` | 1 | `d_enc` × 3 |
| `am.frame_in` | 1 | `d_dec` × (`d_enc` + 2) |
| `am.dec.<i>` | block | width `d_dec`, `dec_kernel`, `dec_ratio` |
| `am.dec_ln` | norm | `d_dec` |
| `am.mel_out` | 1 | `m` × `d_dec` |
| `voc.in` | 1 | `voc_dim` × (`m` × `voc_in_kernel`) |
| `voc.in_ln` | norm | `voc_dim` |
| `voc.blk.<i>` | block | width `voc_dim`, `voc_kernel`, `voc_ratio` |
| `voc.out_ln` | norm | `voc_dim` |
| `voc.head` | 1 | (`n_fft` + 2) × `voc_dim` |

`config` holds, in this order: `d_enc`, `enc_kernel`, `enc_blocks`, `enc_ratio`, `d_pred`, `d_dec`,
`dec_kernel`, `dec_blocks`, `dec_ratio`, `voc_dim`, `voc_in_kernel`, `voc_kernel`, `voc_blocks`,
`voc_ratio`, `max_duration`, then one dilation per encoder block. Kernels are odd and up to 15, the
number of blocks up to 16, ratios up to 8, dilations up to 64.

A **norm** `X` is two float32 vectors: `X.g` and `X.b`, gain and bias per channel.

A **block** `X` of width `w`, kernel `k` and ratio `r`:

| Name | Kind | Rows × cols |
|---|---|---|
| `X.dw.w` | 0 | `w` × `k` |
| `X.dw.b`, `X.gamma` | 0 | `w` × 1 |
| `X.ln` | norm | `w` |
| `X.pw1` | 1 | (`w` × `r`) × `w` |
| `X.pw2` | 1 | `w` × (`w` × `r`) |

A **predictor** `X`:

| Name | Kind | Rows × cols |
|---|---|---|
| `X.c1` | 1 | `d_pred` × (`d_enc` × 3) |
| `X.ln1`, `X.ln2` | norm | `d_pred` |
| `X.c2` | 1 | `d_pred` × (`d_pred` × 3) |
| `X.out` | 1 | 1 × `d_pred` |

## Upload and check

Upload the file under **System → Clock voice**, or with
[`POST /api/v1/speech/voice`](../../reference/http.md#post-apiv1speechvoice). A file the clock cannot
use answers `400` with the reason in `message`. [The list of reasons](speech.md#loading) tells you
which part to look at.

Listen for these:

| You hear | Look at |
|---|---|
| Wrong words | the tables, or labels from older tables |
| Words too fast or too slow, gaps | the alignment |
| Metallic or buzzing sound | int8 against float, then the vocoder snapshots by ear |
| Stuttering | the sizes are too big for the clock |
| Clipping or a quiet voice | the level of your clips |

## Related

- [Speech & voices](../../guides/speech.md)
- [Speech internals](speech.md)
