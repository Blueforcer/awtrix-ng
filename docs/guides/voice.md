---
only: [tc002]
---

# Voice assistant

This page lets you talk to the clock: hold the knob for a moment or say "Hey AWTRIX", then speak.
Home Assistant's Assist, or an app such as the [Assistant app](#assistant-app), understands the
request and answers through the clock's speaker.

## How it behaves

With Home Assistant as the assistant, the clock keeps a connection to it. Holding the knob starts a
request, and so does the wake word "Hey AWTRIX" once you switch it on: the clock records what you
say and sends it to your voice assistant in Home Assistant. Home Assistant
carries out the request and sends back a spoken answer, which the clock plays through its speaker.
After a failure, the clock connects again by itself, at most every ten seconds. An app on the clock
can answer instead of Home Assistant.

## Choose the assistant

The assistant answers the knob, the wake word, the **Assist** button in Home Assistant, and the
start commands over [MQTT](../reference/mqtt.md#voicestart) and
[HTTP](../reference/http.md#post-apiv1voicestart). It is Home Assistant, or an installed app that can
listen and answer. A new clock starts with **Off**.

1. Open the web UI and go to **System → Voice assistant**.
2. Under **Assistant**, pick **Home Assistant** or one of your apps. The list shows the installed
   apps that can answer. The choice is saved at once.

**Status** below it shows at a glance whether the assistant is ready, and what to do if it is not.

With an app, holding the knob, saying "Hey AWTRIX" or pressing **Assist** opens that app on the
display, and the app answers. The clock then keeps no connection to Home Assistant, and the Home
Assistant settings are hidden. The wake word works for both. An app that is switched off on the **Apps** tab does not answer, and
**Status** says so.

If the display was off, it comes on for the app. It stays on while the app is open and while the
clock speaks, and goes off again after that. An app that stays in the rotation counts as open until
ten seconds after its last recording, playback or request. A radio station waits until then. While the
clock speaks, or while the app still records, plays or waits for a service, a new start does nothing.

To talk to an AI service, use the [Assistant app](#assistant-app). To write such an app yourself,
see [Voice in scripts](voice-scripts.md).

## What you need for Home Assistant

- Home Assistant reachable from the clock's network.
- A **voice assistant** in Home Assistant with speech-to-text and text-to-speech, for example Whisper
  and Piper, or Home Assistant Cloud. Set one up under **Settings → Voice assistants** in Home Assistant.
- A **long-lived access token**: in Home Assistant, open your user profile, go to **Security** and
  create one under *Long-lived access tokens*. Copy it right away. Home Assistant shows it only
  once.

## Set it up

1. Open the web UI and go to **System → Voice assistant**.
2. Under **Assistant**, pick **Home Assistant**. **Status** shows *Not set up*.
3. Enter the Home Assistant **Address**, for example `http://homeassistant.local:8123`.
4. Paste the access token into **Token**.
5. Press **Connect**.

**Status** shows *Connecting…*, then *Ready*, and the settings fold away under **Home Assistant
settings**. If it cannot connect, **Status** says why and the settings stay open. See
[When it does not work](#when-it-does-not-work).

The token is kept on the clock and never shown again. To keep it, leave the field empty. When you
change the address, the field asks for the token again. **Remove** next to the token deletes it and
closes the connection.

## Choose a voice assistant of Home Assistant

The clock uses Home Assistant's default voice assistant. To use another one:

1. Open **Home Assistant settings** while **Status** shows *Ready*.
2. Pick it under **HA assistant**. The list shows those with speech-to-text and text-to-speech. The
   choice is saved at once.

## Switch it off

Under **Assistant**, pick **Off**. It takes effect at once. The clock closes its connection to Home
Assistant and does not listen for the wake word. Holding the knob and pressing **Assist** do nothing.
Its settings stay saved, so picking **Home Assistant** again is enough.

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

Instead of holding the knob, you can say "Hey AWTRIX". This is experimental: the clock may not hear
every call, and it can start by mistake now and then.

1. In the web UI, go to **System → Voice assistant** and switch on **Wake word**. It takes effect at
   once. The switch appears once an assistant is chosen and set up.
2. Say "Hey AWTRIX". The Home Assistant house appears on the display.
3. Speak once the house opens its mouth wide, as after holding the knob.

The wake word works while **Status** shows *Ready*. With an app as the assistant, it opens the
app instead and works while the app is installed and switched on. The clock does not react to it
while it answers or handles another request. If nothing appears, say "Hey AWTRIX" again, a little
closer to the clock. When the clock cannot listen right now, **Wake word** says *Waits until the
assistant is ready*, and **Status** says why.

**Sensitivity** appears while the wake word is on and sets how easily the clock hears the word. It is
saved at once. **Low** starts by
mistake less often and needs a clearer word. **High** hears you more easily and starts by mistake more
often. **Normal** suits most rooms.

## Commands for the clock's room

Assist can take "turn on the lights" to mean the lights in the room the clock stands in. For that,
tell the clock which room it is in:

1. In Home Assistant, open **Settings → Devices & services → Devices** and pick a device in the
   clock's room. This can be the clock itself or anything else in that room. It needs an
   **Area**.
2. Copy the address of that device page from the browser, for example
   `http://homeassistant.local:8123/config/devices/device/0123456789abcdef0123456789abcdef`.
3. In the web UI, go to **System → Voice assistant** and open **Home Assistant settings**. Paste the
   address into **Room device** and leave the field. It is saved at once, and the clock keeps only
   the device ID at the end. The field appears once the clock is connected.

Requests then use that device's area. If you move the device to another area in Home Assistant,
the next request follows. Empty the field to switch this off.

## Start it from Home Assistant

You can also start the assistant without touching the clock, for example from a dashboard button or an
automation.

1. Connect the clock to Home Assistant over MQTT and switch on discovery, as described in
   [Home Assistant](home-assistant.md).
2. In Home Assistant, open the clock's device page. It has a button called **Assist**.
3. Press it. The clock starts listening, just as if you had held the knob.

The button works only while **Status** shows *Ready*. With an app as the assistant, it opens
the app instead. Without discovery, publish an empty message to
[`<prefix>/cmd/voice/start`](../reference/mqtt.md#voicestart).

## Ask an AI with the Assistant app {#assistant-app}

The Assistant app answers your question with an AI service and needs no Home Assistant. It sends
what you say to a speech service, the text to a chat service, and reads the reply aloud. OpenAI
works with the settings as they are. Any service with the same
interface works too, also one on a computer in your own network.

1. Install **Assistant** from the [AWTRIX Hub](hub.md).
2. In the web UI, go to **System → Voice assistant**, open the **Assistant** list and pick the app
   you just installed.
3. Go to **Apps** and press **⚙** on the app's row. Under **API keys**, paste the key of your service
   into its field, for example **OpenAI key**, and press **Save**. See
   [API keys](../getting-started/web-ui.md#api-keys). A local service needs no key. **Language** starts
   as `de`, so change it if you speak something else.
4. Hold the knob, or say "Hey AWTRIX" with **Wake word** on, and ask.

The display shows what the app is doing: it listens, thinks and answers. After the answer it listens
again, so you can simply ask a follow-up. If you say nothing for five seconds, it says goodbye and
closes. The app remembers your last four questions. A press on the knob ends your question early or
skips the rest of an answer. Opened from the device menu, the app waits ten seconds for a press.

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
| Answer | **Answer** | `reply` | `local` | `local`: the clock's own voice. `network`: the voice service below. `text`: the words run across the display, without a voice |
| Follow-up | **Follow-up** | `followup` | on | listen again after an answer |
| | **Wait** | `followup_wait` | 5 s | how long it waits for you to speak, 1 to 15 s |
| | **Address** | `tts_url` | `https://api.openai.com/v1/audio/speech` | with `network`: the service that reads the answer aloud |
| | **Model** | `tts_model` | `gpt-4o-mini-tts` | with `network`: its speech model |
| | **Voice** | `tts_voice` | `alloy` | with `network`: its voice |
| API keys | **OpenAI key**, **Groq key**, **OpenRouter key**, **Mistral key** | `openai_key`, `groq_key`, `openrouter_key`, `mistral_key` | empty | one key per service, see below |

### Other services and local ones

Change the addresses and models. Use one service for speech and chat, or a different one for each.

To run everything on your own computer, with no account and no key, use
[awtrix-local-ai](https://github.com/Blueforcer/awtrix-local-ai). It offers speech recognition and a
chat model in your network and sets up the app on the clock.

| Service | Speech to text | Chat | Model, for example |
|---|---|---|---|
| Groq | `https://api.groq.com/openai/v1/audio/transcriptions` | `https://api.groq.com/openai/v1/chat/completions` | `whisper-large-v3`, `llama-3.3-70b-versatile` |
| OpenRouter | | `https://openrouter.ai/api/v1/chat/completions` | the name OpenRouter shows |
| Mistral | | `https://api.mistral.ai/v1/chat/completions` | the name Mistral shows |
| Ollama on your computer | | `http://192.168.1.20:11434/v1/chat/completions` | `llama3.2` |
| LM Studio on your computer | | `http://192.168.1.20:1234/v1/chat/completions` | the model you loaded |
| Your own Whisper server | `http://192.168.1.20:8000/v1/audio/transcriptions` | | what the server offers |

Replace `192.168.1.20` with your computer's address. The service has to accept connections from your
network, not only from its own computer.

### Keys for each service

The app has one key per service. Each key goes only to the host of its service, the server name in the
address:

| Field | Goes only to |
|---|---|
| **OpenAI key** | `api.openai.com` |
| **Groq key** | `api.groq.com` |
| **OpenRouter key** | `openrouter.ai` |
| **Mistral key** | `api.mistral.ai` |

For each address, the app picks the key that may go there, so you can mix services. Fill in only the
keys of the services you use. A local service needs no key.

The app sends no key to a host that is not in this table. To use a key with another host, edit the
app's code: on the **Scripts** tab, open **Assistant**, change `hosts=` in the `@secret` line of that
key to your host and save. The clock erases a key when its `hosts=` changes, so enter the key again.

A key goes over `https://`, and over `http://` only to an address in your own network that its line
lists. See [Where a key may go](scripting/network.md#where-a-key-may-go). The app sends no key to any
other address, and `http://` works there without one.

### Messages of the app

| The display shows | What to do |
|---|---|
| **Nothing heard** (*Nichts gehört*) | Speak once the bars move, a little closer to the clock. |
| **No network** (*Kein Netz*) | The service did not answer. Check its address, and that it can be reached from the clock. |
| **Wrong address** (*Adresse falsch*) | The address in the settings is not valid, or the voice service answered with something other than sound. Check the address. |
| **Microphone busy** (*Mikrofon belegt*) | Something else is using the microphone, or it stopped. Try again in a moment. |
| **Key missing** (*Schlüssel fehlt*) | The service wants a key, and the app has none for this address. Enter it under **Apps → ⚙**, or see [Keys for each service](#keys-for-each-service). |
| **Error 401**, **Error 404** (*Fehler 401*) | The service's own answer. 401 usually means a wrong key, 404 a wrong address or model. |
| **Error -4** (*Fehler -4*) | The clock is short of memory. Try again in a moment. If it happens again, restart the clock. |
| **Empty answer** (*Antwort leer*) | The service answered, but not in the expected format. Check the address. |

If only the voice service fails, the answer stays on the display as text.

## When it does not work

**Status** says what stops the assistant:

| Status shows | What to do |
|---|---|
| Not set up | Enter the address and the token and press **Connect**. |
| No connection | Check the address and that Home Assistant runs. The clock keeps trying. |
| Token refused | Create a new long-lived access token, paste it into **Token** and press **Connect**. |
| Speech not set up | The voice assistant needs speech-to-text and text-to-speech. Add them in Home Assistant, or pick another one under **HA assistant**. |
| Home Assistant error | Home Assistant reported a problem. Its log says which. |
| Microphone update pending | Keep the clock on USB power. The update installs by itself within a few minutes. If the message stays, restart the clock on USB power. If it still stays after three restarts, see [Microphone controller updates](updating.md#mcu-updates). |
| Settings unreadable | The saved settings cannot be read. Choose the assistant and set it up again. |
| App not available | The app is not installed or cannot answer. Install it again or pick another assistant. |
| App switched off | Switch it on in the **Apps** tab. |
| App has an error | Open it in the **Scripts** tab. The editor shows the error. |

When a request fails, the clock stays ready. For five minutes, or until the next request, **Status**
shows the reason behind *Last try*:

| Last try | What to do |
|---|---|
| Not understood | Speak right after the clock starts listening, without long pauses. |
| Wi-Fi too slow | Move the clock closer to the router. |
| Home Assistant too slow | Use a faster speech-to-text or text-to-speech. |
| Microphone error | The microphone stopped while you spoke. Try again. If it happens again, restart the clock. |
| Playback error | The answer could not be played. The text-to-speech has to answer in MP3 or WAV. |
| No connection | The connection dropped during the request. Try again. |
| Home Assistant error | Home Assistant reported a problem. Its log says which. |

## Privacy

The clock records only from the start of a request until Home Assistant hears you finish, you press
the knob, or a minute has passed. The recording goes straight to your Home Assistant, and is not
stored on the clock. With **Assistant** on **Off**, the microphone is not used for the assistant at
all.

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

Without a [web login](../reference/http.md#authentication), any web page opened in a browser on the
same network can change scripts and app settings on the clock, and with that reach API keys and the
microphone. Set a login.

## Related

- [Home Assistant](home-assistant.md): entities and notifications over MQTT
- [Voice in scripts](voice-scripts.md): write an app that listens and answers
- [Install on TC002](../getting-started/tc002.md)
- [`/api/v1/voice`](../reference/http.md#apiv1voice): the same settings over HTTP, and
  [`POST /api/v1/voice/start`](../reference/http.md#post-apiv1voicestart) to start the assistant
