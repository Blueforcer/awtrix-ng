---
only: [tc002]
---

# Voice in scripts

This page lets a script record what someone says, send it to a service and play audio back. With
it, your app can answer the knob and "Hey AWTRIX" in place of Home Assistant. It assumes you know
how a script is built. If not, start with
[Your first script](scripting/index.md#your-first-script).

Add `import voice` to the script and `# @needs voice` to its header, so other models show it as not
suitable. Only the TC002 has this module. On other models the import fails.

## What you get

Press the knob and speak. When you stop, the clock plays back what you said.

```berry
# @name    Echo
# @desc    Says back what you said
# @version 1.0
# @needs   voice

import voice

class Echo
  var label

  def init()
    self.label = "PRESS"
  end

  def on_knob(event)
    if event != "press" return false end
    self.label = "TALK"
    voice.record({'from': 'now'}, / clip, status -> self.heard(clip, status))
    return true
  end

  def heard(clip, status)
    if clip == nil
      self.label = status == voice.SILENCE ? "NOTHING" : "ERROR"
      return
    end
    self.label = "BACK"
    if !voice.play(clip, / ok -> self.finished()) self.finished() end
  end

  def finished()
    self.label = "PRESS"
  end

  def draw()
    clear()
    text(1, 6, self.label)
  end
end

return Echo()
```

## How it behaves

- **A call returns at once.** Your [callback](scripting/index.md#callbacks) runs once, a little
  later. The display never waits.
- **A clip is audio the clock holds for your app.** Your script gets a small object with `ms`, the
  length, and `kind`, `wav` or `mp3`. It cannot read the sound itself. It can send the clip or play
  it.
- **A recording ends by itself** when the speaker stops talking, when nobody starts talking, or at
  its longest allowed length.
- **Only the app on the display can record.** Hiding your app stops its recording and its playback
  and deletes its finished clips. A send or fetch that is running goes on.
- **Your app stays on the display** while it records, plays, sends or fetches.
- **While it records or plays, other sounds are refused and music waits.** While it only sends or
  fetches, music waits, and speech and other sounds still play.

## Answer the knob and "Hey AWTRIX" {#answer-the-knob-and-wake-word}

To let your app answer instead of Home Assistant, give it an `on_voice(trigger)` method and start
the recording there:

```berry
  def on_voice(trigger)
    voice.record({}, / clip, status -> self.heard(clip, status))
  end
```

`trigger` is the name of what started the app:

| `trigger` | What happened |
|---|---|
| `"wake"` | someone said "Hey AWTRIX" |
| `"knob"` | someone held the knob |
| `"mqtt"` | the **Assist** button in Home Assistant, or an MQTT message to [`cmd/voice/start`](../reference/mqtt.md#voicestart) |
| `"api"` | a call of [`POST /api/v1/voice/start`](../reference/http.md#post-apiv1voicestart) |

Then:

1. Add `# @ondemand` to the header, so the app stays out of the rotation until someone asks. It must
   not be `@headless` or a `@module`.
2. Save the script and pick it under **System → Voice assistant**, as described in
   [Choose the assistant](voice.md#choose-the-assistant).
3. End the app with `rotation.close()` when the answer is done.

The clock opens the app, and `on_voice()` runs right after its first frame. Draw from state your app
already has, so that first frame shows something. A recording started there begins at the moment of
the trigger, so the words right after "Hey AWTRIX" are not lost.

## Record what someone says

```berry
voice.record({'silence': 1200, 'max': 20}, / clip, status -> self.heard(clip, status))
```

The callback gets `clip` and the status `200`, or `nil` and one of the
[status values](#status-values). The options are optional:

| Option | Meaning | Allowed | Default |
|---|---|---|---|
| `silence` | how long a pause in milliseconds ends the recording | 300–3000 | 800 |
| `wait` | how many seconds to wait for someone to start talking | 1–15 | 5 |
| `max` | the longest recording in seconds. It ends there with what it has | 1–30 | 15 |
| `from` | `'trigger'` starts at the moment of the [trigger](#answer-the-knob-and-wake-word), `'now'` starts now | | `'trigger'` |

A trigger serves one recording. Without one, for example for a follow-up question, the recording
starts now. A recording from now first waits up to a second for the speaker to go quiet.

`voice.stop()` ends the recording early, and the callback gets what it has so far.
`voice.level()` is the loudness of the running recording, 0 to 255, so you can show that the clock
hears something:

```berry
  def draw()
    clear()
    rect_fill(0, 7, width() * voice.level() / 255, 2, 0x00AAFF)
  end
```

## Send a clip to a speech service

```berry
voice.send(clip, "https://api.openai.com/v1/audio/transcriptions",
           {'fields': {'model': 'gpt-4o-mini-transcribe', 'language': 'en'},
            'headers': {'Authorization': 'Bearer {secret:key}'}},
           / body, status -> self.on_text(body, status))
```

The clip goes out as a form, the way a browser uploads a file: in the field `file`, named
`speech.wav` or `speech.mp3`, next to your `fields`. The callback gets `body` and `status` as in
[`http`](scripting/network.md#what-your-callback-gets), and `find`, `keep` and `cap` work the same.

For a service that wants the sound as the whole request, add `'as': 'raw'`. The request then says
`audio/wav` or `audio/mpeg`, unless your headers name another `Content-Type`.

`{secret:key}` is filled in by the clock. See
[Keep an API key out of your script](scripting/network.md#api-keys).

## Ask a service that takes its time {#slow-services}

A chat model often thinks for many seconds before it sends its first byte. `http` gives up after 5
seconds without one, and all apps share its queue, so a slow answer holds up everyone else's
requests. Send such a request with `voice.send` and text instead of a clip:

```berry
voice.send(json.dump(request), url,
           {'cap': 16384, 'timeout': 120, 'headers': {'Authorization': 'Bearer {secret:key}'}},
           / body, status -> self.on_answer(body, status))
```

The text is the body of the request, sent as `application/json` unless your headers name another
`Content-Type`. `as`, `field` and `fields` do not apply. `timeout` is how many seconds the whole
request may take, also while it waits for the first byte.

## Fetch audio and play it

```berry
voice.fetch(url, {'body': json.dump(request),
                  'headers': {'Content-Type': 'application/json',
                              'Authorization': 'Bearer {secret:key}'}},
            / clip, status -> self.on_audio(clip, status))
```

With a `body` the request is a `POST`, without one a `GET`. Only an answer with status 200 that
holds WAV or MP3 becomes a clip. Any other answer gives `nil` and its status.

```berry
  def on_audio(clip, status)
    if clip == nil return end
    voice.play(clip, / heard -> self.finished())
  end
```

`voice.play()` returns `false` when it cannot start. Otherwise your callback runs once the sound has
ended, with `true`, or with `false` when it was stopped or replaced by another `voice.play()`. The
clip plays at the [alert volume](sounds.md#volume).

## A complete assistant

This app does all of it: record, turn speech into text, ask a chat model, fetch the spoken answer and
play it. Any service that speaks the OpenAI format works. Pick the app as described in
[Choose the assistant](voice.md#choose-the-assistant) and enter the key in its settings. The
[Assistant app](voice.md#assistant-app) is a larger version of it.

```berry
# @name    Ask
# @desc    Ask out loud and hear the answer
# @version 1.0
# @ondemand
# @needs   voice secret
# @config  stt_url    text "Speech address" default="https://api.openai.com/v1/audio/transcriptions" help="Turns your question into text"
# @config  stt_model  text "Speech model"   default="gpt-4o-mini-transcribe" help="The model of that service"
# @config  chat_url   text "Chat address"   default="https://api.openai.com/v1/chat/completions" help="Answers your question"
# @config  chat_model text "Chat model"     default="gpt-4o-mini" help="The model of that service"
# @config  tts_url    text "Voice address"  default="https://api.openai.com/v1/audio/speech" help="Reads the answer aloud"
# @config  tts_model  text "Voice model"    default="gpt-4o-mini-tts" help="The model of that service"
# @config  tts_voice  text "Voice"          default="alloy" help="The voice of that service"
# @secret  key label="API key" help="Goes to all three addresses. Leave it empty for a service without a key"

import json
import voice
import secret

class Ask
  var turn, label, tint, until

  def init()
    self.turn = 0
    self.label = "ASK"
    self.tint = 0xFFFFFF
    self.until = now_ms() + 10000
  end

  def on_voice(trigger)
    self.listen('trigger')
  end

  def on_knob(event)
    if event != 'press' return false end
    self.listen('now')
    return true
  end

  def on_hide()
    self.turn += 1
    voice.cancel()
  end

  def loop()
    if self.until > 0 && now_ms() >= self.until rotation.close() end
  end

  def headers(typed)
    var out = typed ? {'Content-Type': 'application/json'} : {}
    if secret.has('key') out['Authorization'] = 'Bearer {secret:key}' end
    return out
  end

  def listen(from)
    self.turn += 1
    var turn = self.turn
    voice.cancel()
    self.show("LISTEN", 0x00AAFF, 0)
    voice.record({'from': from}, / clip, code -> self.heard(turn, clip, code))
  end

  def heard(turn, clip, status)
    if turn != self.turn return end
    if clip == nil return self.fail(status) end
    self.show("...", 0xFFFFFF, 0)
    var opts = {'fields': {'model': store.get('stt_model')}, 'headers': self.headers(false)}
    voice.send(clip, store.get('stt_url'), opts, / body, code -> self.transcribed(turn, body, code))
  end

  def transcribed(turn, body, status)
    if turn != self.turn return end
    if status != 200 return self.fail(status) end
    var data = body != nil ? json.load(body) : nil
    var question = isinstance(data, map) ? data.find('text') : nil
    if question == nil || question == '' return self.fail(voice.SILENCE) end
    var request = {'model': store.get('chat_model'), 'messages': [{'role': 'user', 'content': question}]}
    var opts = {'cap': 16384, 'timeout': 120, 'headers': self.headers(true)}
    voice.send(json.dump(request), store.get('chat_url'), opts, / answer, code -> self.answered(turn, answer, code))
  end

  def answered(turn, answer, status)
    if turn != self.turn return end
    if status != 200 return self.fail(status) end
    var reply = nil
    try
      reply = json.load(answer)['choices'][0]['message']['content']
    except ..
    end
    if reply == nil return self.fail(200) end
    self.show(reply, 0xFFFFFF, 0)
    var request = {'model': store.get('tts_model'), 'voice': store.get('tts_voice'), 'input': reply,
                   'response_format': 'mp3'}
    var opts = {'body': json.dump(request), 'timeout': 60, 'headers': self.headers(true)}
    voice.fetch(store.get('tts_url'), opts, / clip, code -> self.fetched(turn, clip, code))
  end

  def fetched(turn, clip, status)
    if turn != self.turn return end
    if clip == nil return self.fail(status) end
    if !voice.play(clip, / ok -> self.finished(turn)) self.finished(turn) end
  end

  def finished(turn)
    if turn == self.turn self.until = now_ms() + 10000 end
  end

  def fail(status)
    var words = "ERROR " + str(status)
    if status == voice.SILENCE
      words = "NOTHING HEARD"
    elif status == 0
      words = "NO NETWORK"
    elif status == 200
      words = "EMPTY ANSWER"
    elif (status == 401 || status == 403) && !secret.has('key')
      words = "KEY MISSING"
    end
    self.show(words, 0xFF3020, now_ms() + 5000)
  end

  def show(words, tint, until)
    self.label = words
    self.tint = tint
    self.until = until
  end

  def draw()
    clear()
    scroll_text(self.label, self.tint)
    rect_fill(0, height() - 1, width() * voice.level() / 255, 1, 0x00AAFF)
  end
end

return Ask()
```

Ten seconds after the answer the app closes. A press on the knob before that asks the next question.

## Good to know

- **A refused call runs your callback before the call returns.** Set your state first, as `listen()`
  does above, and only then call `voice.record()`, `voice.send()` or `voice.fetch()`.
- **A question can overtake an older one.** `voice.cancel()` still delivers `voice.CANCELLED` to the
  old callback. Count your questions, as `turn` does above, and ignore answers to an older one.
- **A clip does not outlive the hidden app.** Hide it, and `voice.send()` with an old clip answers
  `voice.GONE`. Do not keep clips in a member beyond what you are doing with them.
- **A third `voice.send()` or `voice.fetch()` at the same time answers `voice.BUSY`.** Send the next
  one from the callback of the last.
- **A local service with a name needs `.local` to get a key over `http://`.** See
  [Where a key may go](scripting/network.md#where-a-key-may-go).

## Details

### Calls

| Call | Does |
|---|---|
| `voice.record(opts, cb)` | records. `opts` may be `nil`. `cb(clip, status)` |
| `voice.stop()` | ends the recording now. The callback gets what it has |
| `voice.level()` | the loudness of the running recording, 0 to 255, otherwise 0 |
| `voice.state()` | `'idle'`, `'recording'` or `'playing'` |
| `voice.send(data, url, opts, cb)` | sends a clip or a text. `opts` may be `nil`. `cb(body, status)` |
| `voice.fetch(url, opts, cb)` | downloads WAV or MP3 into a clip. `opts` may be `nil`. `cb(clip, status)` |
| `voice.play(clip, done?)` | plays a clip and returns whether it started. `done(heard)` |
| `voice.cancel()` | ends your app's recording, playback, sends and fetches |

A callback that is not a function, `opts` that is not a map or anything but a clip for `voice.play()`
raises `value_error`. `voice.send()` with anything but a clip or a text answers `voice.BAD`.

Options of `voice.send`:

| Option | Meaning | Default |
|---|---|---|
| `as` | for a clip: `'form'` or `'raw'` | `'form'` |
| `field` | for a form: the name of the clip's field | `'file'` |
| `fields` | for a form: a map of further text fields, at most 16 | none |
| `method` | the HTTP method | `POST` |
| `headers` | a map of names to values. A form takes no `Content-Type` of yours | none |
| `find`, `keep`, `cap` | as in [`http`](scripting/network.md#http-calls) | |
| `timeout` | whole seconds for the whole request, also while it waits for the first byte, 1–120 | 30 |

Options of `voice.fetch`:

| Option | Meaning | Default |
|---|---|---|
| `body` | the text to send | none |
| `method` | the HTTP method | `POST` with a `body`, else `GET` |
| `headers` | a map of names to values | none |
| `timeout` | whole seconds for the whole request, also while it waits for the first byte, 1–120 | 15 |

Only a `GET` without a key follows redirects.

### Status values

`voice.send` and `voice.fetch` give the HTTP status of the answer. `0` means no server answered, or
the clock refused to send a [key](scripting/network.md#api-keys). A recording gives `200`. The
negative values are constants of the module:

| Constant | Value | Means |
|---|---|---|
| `voice.SILENCE` | -1 | nobody spoke within `wait`, or `voice.stop()` came before any sound |
| `voice.BUSY` | -2 | the app is not shown, the assistant is busy, another recording runs or the recording failed. Also a third send or fetch of the app |
| `voice.CANCELLED` | -3 | `voice.cancel()`, or the app was hidden while it recorded |
| `voice.FULL` | -4 | the clip would not fit into the [limits](../reference/limits.md#voice-in-scripts), or the clock is short of memory |
| `voice.GONE` | -5 | `voice.send` with a clip that was released |
| `voice.BAD` | -6 | an option out of range or of the wrong type, a bad address, method or header. Also a `voice.fetch` answer with status 200 that is neither WAV nor MP3 |

Every limit is in [Limits](../reference/limits.md#voice-in-scripts).

## Related

- [Voice assistant](voice.md): choose who answers, and the Assistant app
- [Network](scripting/network.md): `http`, and API keys with `@secret`
- [Sound & music](scripting/sound.md): `sound.play()`, speech and music
- [Sharing a script](scripting/sharing.md#what-your-script-asks-of-the-clock): `@needs`
