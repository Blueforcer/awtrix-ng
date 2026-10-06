# Writing the docs

These rules keep the user documentation easy to follow. They apply to every page outside
`developers/`. `python tools/docs/build_site.py` checks what a machine can check.

## Who reads it

End customers: people who just unpacked a clock, and tinkerers who write their own apps. Both
need the model first and the details after it.

## What goes where

| Page | Holds |
|---|---|
| [How the display works](../guides/display.md) | the model: positions, what moves by itself, boxes, layers |
| Guides | tasks: how to get something onto the display or the clock |
| Reference | every key, value, limit and edge case |
| Tutorials | one app growing step by step |
| `developers/` | internals, building and testing |

Each fact lives in one place. Other pages link to it.

## A guide page

1. Title, then one sentence: what this page lets you do.
2. **What you get**: a picture and the smallest complete example to copy.
3. **How it behaves**: three to six sentences of model, with a link to How the display works.
4. **Tasks**: one section per thing a reader wants to do. Each has a goal sentence, a complete
   example and a picture.
5. **Good to know**: at most five traps, each one sentence and its fix. Write the right form as
   code, and the wrong one only in words: quick readers copy any command they see.
6. **Details**: a link to the reference. Guides do not list edge cases.
7. **Related**.

Reference pages stay tables. Tutorials keep their steps.

## Words

| Thing | Word | Not |
|---|---|---|
| the lit surface | display | panel, matrix, screen |
| one LED module of a DIY build | panel | – |
| apps taking turns | rotation | loop, app loop |
| anything in the rotation | app: built-in app, pushed app, script | page |
| one-time message on top | notification | – |
| one picture | frame | – |
| the line letters stand on | baseline | – |
| rectangle `[x, y, w, h]` of a layout | box | – |
| the columns of `scroll_text(x, y, w, …)` | columns | box |

Write "shown", not "on screen". Labels of the web UI and names of Home Assistant entities stay as
the UI shows them, for example **System → Panel** or the **Matrix** light.

The build fails on the replaced words in prose outside code: lowercase "panel" and "matrix", "on
screen", "the screen", "the loop" and "e.g.". DIY pages may say panel. Deliberate exceptions go
into `tools/docs/lint-allow.txt` as `*/<page path>/: <exact phrase>` (or `<clock>/…` for one clock).

## Style

- Short sentences, one fact each.
- "for example", never "e.g.". No dash asides, no semicolon chains.
- Web UI steps first and numbered. API calls in their own subsection.
- Say what the reader sees ("the text moves"), never how the firmware does it.
- Describe what the firmware does now. Never "no longer", "previously" or "now returns".
- No internals outside `developers/`.
- Explain a technical word the first time it appears, or link to its explanation.
- A claim about behaviour is checked on the firmware before it is written.

## One text per clock

Each clock's documentation reads as if it were written for that clock alone.

- A page for some clocks only: front matter `only: [tc002]`.
- A passage: `<!-- only tc002 -->` … `<!-- /only -->`, inline or on lines of their own.
- A link into another clock's build: `site:<path>`.

The rules live in `tools/docs/only.py`.

## Pictures

Put a marker on the line directly above a code block, and the built page shows what the example
puts on the display:

```markdown
<!-- panel style=diagram mark=row:6 -->
```

| Option | Meaning | Default |
|---|---|---|
| `style` | `led` for "what you get", `diagram` (grid with row and column numbers) for positions | `led` |
| `mark` | `row:N` or `col:N`: tint one row or column | – |
| `cols` | `A-B`: tint columns A to B; `B` may be `W`, the last column | – |
| `boxes` | outline the boxes of the example's layout | off |
| `at` | milliseconds after the app is shown, for a still picture | `600` |
| `motion` | seconds to record; the picture moves | – |
| `alt` | the alternative text | what the kind of example shows |

- The example must be complete: a whole script, a JSON payload, or one `curl` to
  `/api/v1/apps/pushed/<name>` or `/api/v1/notifications`.
- Icons: only those in `tools/docs/panel-icons/` exist while rendering. `sun` is one.
- Render on Linux or in WSL with a host build (`cmake --build --preset host`):
  `python3 tools/docs/panels.py --binary build/host/awtrix-linux`. It renders what is missing and
  deletes pictures no marker uses. The build fails until every marked example has its picture.
- Before a release: `python3 tools/docs/panels.py --check`. A renderer change does not change an
  example, so only this run finds pictures that no longer match.

## Reader test

It shows whether someone who reads only the docs can build what users ask for, and how much
reading that takes.

1. `python tools/docs/reader_test.py bundle --clock esp32 --out <dir>` (and `tc002`). The bundle
   holds that clock's pages without the developer pages, the AI prompt and the release notes.
2. Give a fresh agent per clock only that folder: Read, Grep and Glob inside it, no web, no
   knowledge from elsewhere. It solves `TASKS.md` and writes `answers.md`. Run two readers: a
   thorough one (Sonnet) and a quick one (Haiku). The quick one fails first where the docs are
   unclear.
3. `python3 tools/docs/reader_test.py check --clock esp32 --answers <dir>/answers.md --binary <path>`
   runs every answer on awtrix-linux.
4. Note the score, the reader's tool calls and tokens, and the gaps it reports. Fewer reads for
   the same score means clearer docs.

The tasks are in `tools/docs/reader_tasks.json`; `tools/docs/reader_reference.md` proves that each
one can pass. The test reads text only: a picture never replaces a sentence.

## Before you finish

- `python -m unittest discover -s tools/docs -p "test_*.py"`
- `python tools/docs/build_site.py`
