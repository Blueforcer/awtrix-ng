---
only: [tc002]
---

# Speech

The TC002 reads text aloud through a voice pack: a neural model together with the text frontend of
its language, in one file, run by an engine of its own. The release carries an English pack; the
owner can upload one other pack in its place. This repository holds the request, the frontend
engine, the worker that runs the voice next to the mixer, and the engine. The code is in
[`src/platform/tc002/speech/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/speech),
its tests in [`tests/tc002/runtime/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/tc002/runtime).
How speech is used: [Speech & voices](../../guides/speech.md).

The voice is the uploaded pack, else `share/speech/voice.atts` in the release
([Voice packs](#voice-packs)). Without a pack the runtime accepts it has no voice, so `speaks()` is false: `audio.speech` reports `false`,
`POST /api/v1/audio/play` with `speech` answers `503 no text-to-speech`, a notification shows
without its speech, a list goes on to its next entry, and a script's
`sound.play({'speech': ...})` returns `false`.

## The path of a request

| Step | Where | Thread |
|---|---|---|
| HTTP, MQTT, notifications and scripts parse the sound; `speech` keeps its text as sent | `sound::parse` | render |
| The router asks the sink whether it speaks, then hands the text over | `AudioRouter` | render |
| The current voice reads the text into its plan, once | `Tc002AudioSink::playSpeech`, `readText`, `SpeechVoice::prepare` | render |
| The plan and that voice become the one-shot; a worker starts the voice | `SpeechSource::open` | audio |
| The voice renders into a one-second ring; the mixer pulls from it | `SpeechSource` | worker, audio |

Core carries the text without reading it, so the ESP32 images hold no speech code beyond that
hand-over. `AudioRouter::check` has the text judged when the request arrives
(`IPcmSink::checkSpeech`), only by a sink that speaks. A notification keeps its sound as sent and
parses it again when it shows.

A script's `sound.play()` and `sound.effect()` run the same check at the call, so a mistake raises
at the script's own line. The request then joins the script's other sound calls in the command
queue, so a script's sounds keep their order. A script's speech plays in group app.

## The request

The parser accepts a string of 1 to 512 bytes under `speech`. `readText` reads it and reports the
first problem as `message`; the caller names the field: `speech`, `[1].speech` in a list,
`sound.speech` in a notification.

| `message` | When |
|---|---|
| `must be 1..512 bytes` | the text is empty or longer than `kMaxTextBytes` |
| `no words to speak` | the frontend found nothing to say |

## The text frontend

`Frontend` (`SpeechFrontend.cpp`) turns UTF-8 text into a `Plan` by the tables of the voice pack's
language, so a new language needs no firmware build. `prepare()` runs these steps:

1. **Folding.** ASCII letters become lower case, control characters separate words. Other
   characters go through `[fold]`, longest match first; a code point listed in `[letters]` stays a
   letter, anything else separates words, broken UTF-8 included. When the pack has `[spell]`, a
   word of two to five ASCII capitals in a text that also has lower case is marked as an acronym if
   it has at most three letters or no vowel (`DHL`, `SNCF`, not `NASA`, not a text in capitals).
2. **Words and numbers.** An entry of `[abbreviations]` at the start of a word (`km/h`) reads its
   words. A run of letters is a word; an apostrophe inside it stays. Words joined by hyphens are
   one word when the lexicon knows them joined (`e-mail`, `t-shirt`), the longest known start of
   the chain first; otherwise the hyphen separates them. An acronym reads its lexicon entry written
   in capitals (`UPS`), else it is spelled with the letter names of `[spell]`, so English `US` is
   not the word `us`. Numbers up to six digits are read by `[numbers]`,
   longer ones digit by digit. A decimal sign between digits reads its word and the digits after it
   one by one. A group sign before exactly three digits joins them (`1.000` in German). A time
   `h:mm` up to 23:59 reads one of three templates; a `time word` written after it (`14:30 Uhr`) is
   not read twice. `-` before a number that starts a word is `minus`, and a character in
   `[symbols]` is its words.
3. **Pauses.** `.`, `,`, `?`, `!`, `;`, `:` and a new line end a phrase. Repeated punctuation adds
   no second pause.
4. **Pronunciation.** The lexicon's reading of a word, else the rules'. The lexicon knows whether
   the word opens a sentence, so English "Close the window" reads `close` as the verb. A word of
   more than 64 phones is left out.
5. **Length.** A plan holds `kMaxPhones` phones. When a word no longer fits, the plan ends with the
   word before it.

`prepare()` fails only when no word is left. `Frontend::parse()` checks every table once; a
frontend never changes, so any number of threads read it at the same time.

### The tables

A pack carries its language as two tables: `fe.text` with the phones, letters, numbers, times and
pronunciation rules, and `fe.lexicon`, a word list with pronunciations. `Frontend::parse()` checks
both when the pack loads; a table it cannot use refuses the pack. Their format:
[Language tables](voice-packs.md#language-tables).

### The plan

The plan is the voice's input:

- `phones[i].sound` is an index into the pack's `[phones]`; the names of `PhoneId` are the English
  pack's. A voice is trained on these values: a language's inventory never changes order.
- `phones[i].prosody` holds the vowel's lexical stress in its low two bits (`Primary` 1,
  `Secondary` 2) and `WordEnd` (4) on the last phone of every word.
- Every phrase ends with a `Silence` phone; `boundaries` holds the punctuation that ended it at the
  same index, or 0.

Timing and intonation are the voice's to choose.

## The voice

`SpeechVoice.h` is the whole interface a voice implements:

- `SpeechVoice::rate()` - the sample rate of what it renders, mono.
- `SpeechVoice::prepare(text, plan)` - the text as this voice's plan, by its own frontend; false when
  no word is left. Any thread.
- `SpeechVoice::start(plan)` - an utterance, or `nullptr` when the plan cannot be spoken. It runs
  on the utterance's worker thread.
- `SpeechUtterance::render(out, max)` - up to `max` samples; 0 once the utterance is over,
  finished, failed or cancelled.
- `SpeechUtterance::cancel()` - from any thread, while `render()` runs too, and without waiting:
  `render()` returns soon and 0 from then on.

Every utterance has its own worker, so two utterances can overlap briefly when one replaces the
other: a voice shares only data that never changes between them. A voice that throws has failed;
the worker catches it.

The sink takes the voice as a constructor argument (`Tc002AudioSink(engine, fd, voice)`, handed
through `Tc002Speaker`). With a voice, `speaks()` is true while the speaker helper is available.

## The worker

`SpeechSource` is the one-shot the mixer plays:

- `open()` starts a detached worker and returns at once.
- The worker calls `render()` in blocks of 512 samples outside the lock, passes each block through
  the utterance's `SpeechLimiter` and waits for room in the ring. The ring holds one second. When
  the voice ends, the limiter hands over the 2 ms it still holds.
- `next()` answers `Wait` until 150 ms are in the ring or the voice is done, and again whenever
  the voice falls behind; the mixer then plays the other layers without it. It answers `Error`
  when the voice did not start.
- Destroying the source cancels the utterance and never waits for it: a newer one-shot, a stop,
  Home Assistant Voice taking the speaker and the end of the audio thread all do that. The worker
  ends by itself; `SpeechSource::waitForWorkers()` waits for every worker when the sink shuts
  down.

`SpeechLimiter.h` puts `kSpeechGain` (+5.0 dB) in front of a look-ahead peak limiter with a
ceiling of -1 dBFS. It delays the voice by 2 ms, lowers the gain in a linear ramp over those 2 ms
before a peak that would pass the ceiling, and lets it recover with an 80 ms time constant, so a
loud syllable is turned down instead of clipped. Speech plays at the MP3 volume next to music, and
the gain is set for the voice it plays.

## The model voice

`ModelVoice` (`SpeechModelVoice.h`) is the voice the runtime builds from a model file. The model is
two stages of convolutions, without attention, recurrence or noise:

| Stage | Layers | Rate |
|---|---|---|
| Acoustic model | token embeddings, summed; ConvNeXt encoder; duration and pitch predictors; pitch embedding; length regulator, which repeats each phone's vector for its frames with the frame's position `(j + 0.5) / d` and `log(1 + d) / 4`; ConvNeXt frame decoder; normalised log-mel | phones, then frames of 256 samples |
| Vocoder | k-wide input convolution; ConvNeXt blocks; per frame `n_fft / 2 + 1` log-magnitudes and as many phases | frames |
| Wave (`SpeechWave`) | magnitude `min(100, exp(min(60, x)))`, DC and Nyquist zero; inverse FFT; periodic Hann window, overlap-add divided by the sum of the squared windows, centred; DC blocker `y = x - x' + 0.9973 y'`; int16 | samples |

A ConvNeXt block is a depthwise convolution, a norm, an expanding layer, GELU, a projecting layer
and the input added back: `x + gamma * y`. The sizes come from the file; the voice of the model's
design has 80 encoder, 32 predictor, 128 decoder and 112 vocoder channels, 3 encoder blocks with
dilations 1, 2 and 4, 2 decoder and 5 vocoder blocks, 0.67 million parameters in about 0.77 MB.

The model is the project's own, trained from scratch for this firmware; the file holds no third-party
code, weights or data. `licenses.txt` lists it with AWTRIX NG. Its training audio is about twelve hours
of English that Chatterbox (Resemble AI, MIT) spoke from a female reference voice the author recorded;
the texts are the project's own sentences, LJ Speech transcripts (public domain) and CMU ARCTIC
prompts. At +5.0 dB speech plays at about -17 LUFS, the level of the other sound kinds; the
loudest test sentence reaches the clipper's -1 dBFS ceiling with 0.3 % of its samples in the knee.

### Loading

`SpeechModel::load()` reads the whole file, at most 16 MiB, checks it once and resolves every
tensor; the model never changes afterwards and every utterance reads it at the same time.
`ModelVoice::create()` adds the pack's frontend. A file the runtime refuses has one of these reasons:

| Reason | When |
|---|---|
| `not an ATTS file` | shorter than the header, or another magic |
| `version N not supported` | not version 1 or 2 |
| `size mismatch` | the header's file size is not the file's |
| `checksum mismatch` | the CRC-32 differs |
| `bad header` | no tensors, more than 1024, or header bytes that do not fit the count |
| `sample rate N not supported`, `token contract N not supported`, `flags not supported` | not 24000, not the version, not 0 |
| `bad STFT`, `bad mel count` | `n_fft` no power of two from 16 to 4096, `hop` 0 or above `n_fft`; mels 0 or above 1024 |
| `bad tensor name`, `duplicate tensor X` | a name without its NUL or with other than printable ASCII; a name twice |
| `tensor X: out of bounds`, `empty`, `wrong size`, `unknown kind` | the directory entry |
| `tensor X: padding`, `weight -128`, `not finite` | the contents |
| `missing tensor config`, `config: ...` | the hyper-parameters are missing or out of range |
| `missing tensor X`, `tensor X: wrong shape` | a tensor the configuration or a pack needs |
| `not a voice pack` | a v1 file: a model without a frontend |
| `fe.text line N: ...`, `fe.lexicon: ...` | a table the frontend cannot use |
| `the frontend's phones do not match the model` | `[phones]` and `am.phone` differ in count |
| `pack.info: ...` | not JSON, or `name` or `language` missing or over 64 bytes |

### The file

A pack is one `.atts` file: a header with a CRC-32, the model's tensors, `pack.info` (JSON with
`name`, `language` and an optional `version`) and the two tables. Anything that does not match is
refused with one of the reasons above. The layout: [The file](voice-packs.md#the-file).

### From plan to samples

**Chunks.** `splitPlan()` gives every sentence a chunk of its own: the voice learned from
utterances of 0.5 to 10 seconds. A sentence of more than 127 phones is cut after its last pause
within the limit, a phrase that long after its last whole word. A sentence or piece of fewer than 8
phones joins its neighbour while both fit into one chunk.

**Tokens** (`chunkTokens()`): a chunk starts with BOS, then one token per phone: the
phone's index in the inventory, the stress (`prosody & 3`), whether it ends a word, and on a pause the punctuation that
ended the phrase (1 `,` 2 `.` 3 `?` 4 `!` 5 `;` 6 `:`, 0 none, 7 BOS). A plan with a phone outside
the model's inventory or a stress above 2 is not spoken.

**Acoustic model** (`AcousticStream`). The encoder and both predictors run over the whole chunk.
A phone lasts `clamp(floor(exp(y) - 1 + 0.5), 1, max_duration)` frames. The frame decoder then runs
16 frames per step.

**Vocoder** (`VocoderStream`) takes the mel frames of each step and writes the samples they finish.
A chunk of `T` frames gives `(T - 1) * 256` samples; the DC blocker runs on from chunk to chunk.

Every layer that looks at neighbouring frames reads them from a `FrameWindow`: the rows it still
needs, zero rows before the chunk's first frame and after its last. It computes an output as soon
as the frames it reads are in, so the results equal those of the whole chunk at once, whatever the
step.

### Arithmetic

Dense layers are W8A8: every input vector gets its own scale `s = max|x| / 127` (1 when all are
0), codes `round(x / s)` half away from zero, clamped to +-127, an int32 sum of products, and
`y = (s * scale) * sum + bias`. On ARM the sum is NEON (`vmull.s8`, `vmlal.s8`, `vpadal.s16`, one
weight row against four frames); elsewhere plain C++. Everything else is float32 in the order of
the model's reference runtime: norms with numpy's pairwise sums, a biased
variance and `eps` 1e-6, GELU through `erf`, ReLU. The voice is built with `-O2` also in the `-Os`
release and with `-ffp-contract=off`, so no CPU fuses a multiply and an add.

### Threads and memory

`start()` runs on the utterance's worker: it splits the plan, turns it into tokens and allocates the
utterance's work space once, about 0.5 MB for the voice of the design. `render()` hands out the
samples of a step and runs the next step when they are gone; a step of 16 frames is 4096 samples.
`cancel()` sets a flag that `render()` checks before every step. Nothing else is shared between
utterances than the model.

The design costs 0.65 million multiply-adds per frame (62 million a second of audio) and about
2 million a second at phone rate; per frame further 1,792 GELUs, 10 norms of 128 values, 7
depthwise convolutions and one 1024-point inverse FFT.

### A voice for the release

The release takes its pack from `assets/speech/voice.atts`: `cmake --install --component tc002`
installs it as `share/speech/voice.atts` when the file exists, `build_bundle.sh` names it in the
AWTRIX NG line of the licence texts, and the bundle, browser and source package checks take a
release with it as well as one without. It is the English voice.

## Voice packs

`VoicePacks` keeps the clock's voice. At start it loads the uploaded pack,
`<data>/../speech/voice.atts` (on the TC002 `/data/awtrix-ng/speech/voice.atts`), else the release's
pack that `awtrix-linux --speech-voice FILE` names (the supervisor passes
`<release>/share/speech/voice.atts` with `--tc002-audio-fd` when the release carries it). A pack it
refuses is logged as `speech: <path>: <reason>` and the next one is tried. The routes below exist
when the clock has a speaker.

A loaded pack is read in place: `SpeechModel::load` reads the file once through its descriptor for the
checksum, so a flash error is a refusal, then maps it read-only. The weights stay in the page cache,
which gives them up under memory pressure. An utterance reads them back on its worker thread before
the first sample and locks them in memory until it ends (`SpeechModel::Hold`), so speech under way
never waits for flash, which a file being written can keep busy for longer than the ring holds. The
loop thread runs `prepare()`, which must not wait for flash either, so the frontend keeps its own copy
of the lexicon.

| Request | Does |
|---|---|
| `GET /api/v1/speech/voice` | `name`, `language`, `version`, `bytes`, `uploaded` of the active pack (`name` null without one), and `maxBytes` |
| `POST /api/v1/speech/voice` | a pack as the file of a form or as the raw body, at most 2,000,000 bytes: checked in full, written beside the old one, synced and renamed over it, then active |
| `DELETE /api/v1/speech/voice` | removes the uploaded pack; the release's pack speaks again |

A refused upload answers `400 validationFailed` with the loader's reason, `507 insufficientStorage`
when the pack would leave less than the upload reserve free on `/data`. The sink holds the voice
behind a lock: `playSpeech` reads the text with the voice of that moment and hands both to the
one-shot, so a sentence already speaking keeps its voice and its file, replaced or removed, while
the next one speaks with the new pack. The web UI shows the active voice under **System → Clock voice**, with upload, a way back to
the built-in voice and a test line.

## The English lexicon

The English pack's lexicon holds 33,609 words with 37,659 CMUdict pronunciations, selected with the
ESDB/SCOWL class-35 American English spelling list (no proper names or abbreviations) plus the
words clock announcements need: weekdays, months, holidays, first names, contractions. Notices are
in `LICENSES/CMUdict-SCOWL.txt`. Of each word's pronunciations the first is used, except for
`wind`, `live`, `read`, `lead`, `can`, `perfect` and `excuse`, and for `close` and `use` where
they open a sentence.

## Tests

| CTest | Covers |
|---|---|
| `tc002-speech` | the English pack's frontend: the request, folding, numbers, times, pauses, cutting a long plan, pronunciations; lexicons that lie are refused |
| `tc002-speech-source` | the ring, the clipper, the worker with a fake voice: head start, back-pressure, cancel, a failing voice, and speech over a looping song pulled in real time |
| `tc002-speech-voice` | the model voice against the model's reference runtime: durations, pitch and mel frames equal, samples within one step, steps of any size; the loader's refusals, a pack's info and frontend; tokens and chunks; cancelling; prints the real-time factor of a voice of the design's size |
| `tc002-arm-speech-voice` | the same with the NEON kernel under qemu-arm (`tc002` preset) |
| `tc002-audio-sink` | speech through the real sink: refused requests, stop, a newer one-shot, Home Assistant Voice, no voice |

The voice tests read `tests/tc002/runtime/speech/`: a small voice pack with seeded random weights
and the English rules, and what the model's reference runtime makes of four utterances.
