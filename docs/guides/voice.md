---
only: [tc002]
---

# Voice assistant

This page lets you talk to the clock: hold the knob for a moment or say "Hey AWTRIX", then speak.
Home Assistant's Assist, or an app such as the [Assistant app](#assistant-app), understands the
request and answers through the clock's speaker.

## How it behaves

While Voice is on, the clock keeps a connection to your Home Assistant. Holding the knob starts a
request, and so does the wake word "Hey AWTRIX" once you switch it on: the clock records what you
say and sends it to your Assist pipeline. Home Assistant
carries out the request and sends back a spoken answer, which the clock plays through its speaker.
After a failure, the clock connects again by itself, at most every ten seconds. An app on the clock
can answer instead of Home Assistant.

## Choose the assistant

The assistant answers the knob, the wake word and the **Assist** button. It is Home Assistant, or an
installed app that can listen and answer.

1. Open the web UI and go to **System → Voice assistant**.
2. Under **Assistant**, pick **Home Assistant** or one of your apps. The list shows the installed
   apps that can answer. The choice is saved at once.

With an app, holding the knob, saying "Hey AWTRIX" or pressing **Assist** opens that app on the
display, and the app answers. The clock then keeps no connection to Home Assistant, and the Home
Assistant fields are hidden. **Wake word** works for both. If the display was off, it comes on for
the app and goes off again when the app is done. A radio station waits until then. An app that is
switched off on the **Apps** tab does not answer.

To talk to an AI service, use the [Assistant app](#assistant-app). To write such an app yourself,
see [Voice in scripts](voice-scripts.md).

## What you need

- Home Assistant reachable from the clock's network.
- An **Assist pipeline** with speech-to-text and text-to-speech, for example Whisper and Piper, or
  Home Assistant Cloud. Set one up under **Settings → Voice assistants** in Home Assistant.
- A **long-lived access token**: in Home Assistant, open your user profile, go to **Security** and
  create one under *Long-lived access tokens*. Copy it right away. Home Assistant shows it only
  once.

## Set it up

1. Open the web UI and go to **System → Voice assistant**.
2. Under **Assistant**, keep **Home Assistant**.
3. Enter the Home Assistant **Address**, for example `http://homeassistant.local:8123`. Only the
   address: no path such as `/lovelace`.
4. Paste the access token into **Token**.
5. Choose an Assist **Pipeline**, or keep *Default*. Only pipelines with
   speech-to-text and text-to-speech are listed.
6. Press **Save and connect**. This also switches **Voice** on.

**Connection** shows *Ready* once the clock is connected. If it cannot connect, it shows why. See
[When it does not work](#when-it-does-not-work).

The token is kept on the clock and never shown again. To keep it, leave the field empty when you
change something else. When you change the Home Assistant address, enter the token again.
**Remove** next to the token deletes it and switches Voice off.

## Switch it off

The **Voice** switch takes effect at once. Off, the clock closes its connection to Home Assistant,
and neither holding the knob nor the wake word starts a request. Address, token and pipeline stay saved, so switching it on again
is enough. **Save and connect** also switches Voice on.

## Talk to it

1. **Hold** the knob for half a second. The Home Assistant house appears on the display. You can let
   go now.
2. Speak once the house opens its mouth wide.
3. Stop talking. Home Assistant hears that you have finished and answers through the speaker.

The answer plays at the alert volume, `alertVolume`, as a share of the master volume. A radio
station that was playing comes back afterwards. See [Volume](sounds.md#volume).

Home Assistant listens for about 15 seconds at most. To finish sooner, press the knob briefly. A
short press before the house opens its mouth cancels the request.

Turning the knob still opens the bars for brightness and volume, and a short press switches
between them while the bars are shown, as described in
[Buttons, knob & clock](device-controls.md#the-knob).

## Start it with "Hey AWTRIX"

Instead of holding the knob, you can say "Hey AWTRIX".

1. In the web UI, go to **System → Voice assistant** and switch on **Wake word**. It takes
   effect at once.
2. Say "Hey AWTRIX". The Home Assistant house appears on the display.
3. Speak once the house opens its mouth wide, as after holding the knob.

The wake word works while **Connection** shows *Ready*. With an app as the assistant, it opens the
app instead and works while the app is installed and switched on. The clock does not react to it
while it answers or handles another request. If nothing appears, say "Hey AWTRIX" again, a little
closer to the clock.

## Commands for the clock's room

Assist can take "turn on the lights" to mean the lights in the room the clock stands in. For that,
tell the clock which room it is in:

1. In Home Assistant, open **Settings → Devices & services → Devices** and pick a device in the
   clock's room. This can be the clock itself or anything else in that room. It needs an
   **Area**.
2. Copy the address of that device page from the browser, for example
   `http://homeassistant.local:8123/config/devices/device/0123456789abcdef0123456789abcdef`.
3. In the web UI, paste it into **Room device** under **System → Voice assistant** and press
   **Save and connect**. The clock keeps only the device ID at the end.

Requests then use that device's area. If you move the device to another area in Home Assistant,
the next request follows. Leave the field empty to switch this off.

## Start it from Home Assistant

You can also start Voice without touching the clock, for example from a dashboard button or an
automation.

1. Connect the clock to Home Assistant over MQTT and switch on discovery, as described in
   [Home Assistant](home-assistant.md).
2. In Home Assistant, open the clock's device page. It has a button called **Assist**.
3. Press it. The clock starts listening, just as if you had held the knob.

The button works only while **Connection** shows *Ready*. With an app as the assistant, it opens
the app instead. Without discovery, publish an empty message to
[`<prefix>/cmd/voice/start`](../reference/mqtt.md#voicestart).

## Ask an AI with the Assistant app {#assistant-app}

The Assistant app answers your question with an AI service and needs no Home Assistant. It sends
what you say to a speech service, the text to a chat service, and reads the reply aloud while the
words run across the display. OpenAI works with the settings as they are. Any service with the same
interface works too, also one on a computer in your own network.

1. Install **Assistant** from the [AWTRIX Hub](hub.md).
2. In the web UI, go to **System → Voice assistant** and pick **Assistant** under **Assistant**.
3. Go to **Apps** and press **⚙** on the app's row. Under **API keys**, paste your key into
   **API key** and press **Save**. See [API keys](../getting-started/web-ui.md#api-keys). A local
   service needs no key.
4. Hold the knob, or say "Hey AWTRIX" with **Wake word** on, and ask.

Moving bars mean the clock listens, bouncing dots that it thinks. After the answer, a bar runs down
for ten seconds. Press the knob during that time to ask a follow-up. The app remembers your last four
questions. A press also ends your question early and interrupts an answer.

### Its settings

The settings are under **Apps → ⚙**.

| Group | Setting | Key | Default | Meaning |
|---|---|---|---|---|
| Speech to text | **Address** | `stt_url` | `https://api.openai.com/v1/audio/transcriptions` | the service that turns your question into text |
| | **Model** | `stt_model` | `gpt-4o-mini-transcribe` | its speech model |
| | **Language** | `language` | `de` | the language you speak, for example `de` or `en`. Empty: the service detects it. Starting with `de`, the clock's messages are in German |
| Chat | **Address** | `chat_url` | `https://api.openai.com/v1/chat/completions` | the service that answers |
| | **Model** | `chat_model` | `gpt-4o-mini` | its chat model |
| | **Instructions** | `system` | answer briefly in plain text | how the assistant should answer |
| Answer | **Answer** | `reply` | `local` | `local`: the clock's own voice. `network`: the voice service below. `text`: the display only |
| | **Address** | `tts_url` | `https://api.openai.com/v1/audio/speech` | with `network`: the service that reads the answer aloud |
| | **Model** | `tts_model` | `gpt-4o-mini-tts` | with `network`: its speech model |
| | **Voice** | `tts_voice` | `alloy` | with `network`: its voice |
| API keys | **API key** | `key` | empty | your key, see below |

### Other services and local ones

Change the addresses and models. Use one service for speech and chat, or a different one for each.

| Service | Speech to text | Chat | Model, for example |
|---|---|---|---|
| Groq | `https://api.groq.com/openai/v1/audio/transcriptions` | `https://api.groq.com/openai/v1/chat/completions` | `whisper-large-v3`, `llama-3.3-70b-versatile` |
| OpenRouter | | `https://openrouter.ai/api/v1/chat/completions` | the name OpenRouter shows |
| Ollama on your computer | | `http://192.168.1.20:11434/v1/chat/completions` | `llama3.2` |
| LM Studio on your computer | | `http://192.168.1.20:1234/v1/chat/completions` | the model you loaded |
| Your own Whisper server | `http://192.168.1.20:8000/v1/audio/transcriptions` | | what the server offers |

Replace `192.168.1.20` with your computer's address. The service has to accept connections from your
network, not only from its own computer.

!!! warning "One key for all three addresses"
    The app has one key. Once it is saved, it goes to all three addresses: speech to text, chat and
    voice. If you mix services, each of them receives the same key, including the key of the other
    service. Use one service for everything that needs a key, or leave the key empty when only local
    services are in use. A local service needs no key.

With a key saved, the clock sends it over `https://`, and over `http://` only to an address in your
own network. See [Where a key may go](scripting/network.md#where-a-key-may-go). Any other `http://`
address then shows **No network**. Without a key, `http://` works everywhere.

### Messages of the app

| The display shows | What to do |
|---|---|
| **Nothing heard** (*Nichts gehört*) | Speak once the bars move, a little closer to the clock. |
| **No network** (*Kein Netz*) | The service did not answer. Check its address, and that it can be reached from the clock. A key does not go to a public `http://` address. |
| **Key missing** (*Schlüssel fehlt*) | The service wants a key. Enter it under **Apps → ⚙**. |
| **Error 401**, **Error 404** (*Fehler 401*) | The service's own answer. 401 usually means a wrong key, 404 a wrong address or model. |
| **Empty answer** (*Antwort leer*) | The service answered, but not in the expected format. Check the address. |

## When it does not work

With Home Assistant as the assistant, **Connection** says why it fails:

| Connection shows | What to do |
|---|---|
| HA unreachable | Check the address and that Home Assistant runs. Address only, no path. |
| Token rejected | Create a new long-lived access token and save it. |
| Pipeline lacks STT/TTS | Pick a pipeline with speech-to-text and text-to-speech. |
| Not understood | Speak once the house opens its mouth, without long pauses. |
| Wi-Fi too slow | Move the clock closer to the router. |
| HA too slow | Use a faster speech-to-text or text-to-speech. |
| Microphone update pending | Keep the clock on USB power. The update installs by itself within a few minutes. If the message stays, restart the clock on USB power. If it still stays after three restarts, see [Microphone controller updates](updating.md#mcu-updates). |
| Microphone error | The microphone stopped while you spoke. Try again. If it happens again, restart the clock. |
| Playback error | The answer could not be played. The pipeline's text-to-speech has to answer in MP3 or WAV. |
| Settings unreadable | The saved Voice settings cannot be read. Enter the address and the token again and press **Save and connect**. |
| HA error | Home Assistant reported a problem. Its log says which. |

## Privacy

The clock records only from the start of a request until Home Assistant hears you finish, you press
the knob, or a minute has passed. The recording goes straight to your Home Assistant and its
pipeline, and is not stored on the clock. With Voice switched off, the microphone is not
used for Voice at all.

With **Wake word** on, the microphone listens all the time while the assistant is ready, and while
an app answers. The clock itself listens for "Hey AWTRIX". While it listens, it keeps at most the
last second of sound in memory and stores nothing. Only the request after the wake word goes to
Home Assistant.

With an app as the assistant, the app records what you say and decides where it goes, for example
to a speech service on the internet. Any app that uses voice can record while it is shown, also
when it is not the assistant. Its recordings stay in the clock's memory only while the app uses
them.

The Assistant app sends each question and the last four questions with their answers to the services
in its settings. What those services do with them is up to them. With local services, nothing leaves
your network.

## Related

- [Home Assistant](home-assistant.md): entities and notifications over MQTT
- [Voice in scripts](voice-scripts.md): write an app that listens and answers
- [Install on TC002](../getting-started/tc002.md)
- [`/api/v1/voice`](../reference/http.md#apiv1voice): the same settings over HTTP, and
  [`POST /api/v1/voice/start`](../reference/http.md#post-apiv1voicestart) to start the assistant
