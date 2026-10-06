---
only: [tc002]
---

# Home Assistant Voice

This page lets you talk to Home Assistant through the clock: hold the knob for a moment, let go
and speak. Home Assistant's Assist understands the request, carries it out and answers through the
clock's speaker.

## How it behaves

While Voice is on, the clock keeps a connection to your Home Assistant. Holding the knob starts a
request: the clock records what you say and sends it to your Assist pipeline. Home Assistant
carries out the request and sends back a spoken answer, which the clock plays through its speaker.
After a failure, the clock connects again by itself, at most every ten seconds.

## What you need

- Home Assistant reachable from the clock's network.
- An **Assist pipeline** with speech-to-text and text-to-speech, for example Whisper and Piper, or
  Home Assistant Cloud. Set one up under **Settings → Voice assistants** in Home Assistant.
- A **long-lived access token**: in Home Assistant, open your user profile, go to **Security** and
  create one under *Long-lived access tokens*. Copy it right away. Home Assistant shows it only
  once.

## Set it up

1. Open the web UI and go to **System → Home Assistant Voice**.
2. Enter the Home Assistant **Address**, for example `http://homeassistant.local:8123`. Only the
   address: no path such as `/lovelace`.
3. Paste the access token into **Token**.
4. Choose an Assist **Pipeline**, or keep *Default*. Only pipelines with
   speech-to-text and text-to-speech are listed.
5. Press **Save and connect**. This also switches **Voice** on.

**Connection** at the top of the section shows *Ready* once the clock is connected. If it cannot
connect, it shows why. See [When it does not work](#when-it-does-not-work).

The token is kept on the clock and never shown again. To keep it, leave the field empty when you
change something else. When you change the Home Assistant address, enter the token again.
**Remove** next to the token deletes it and switches Voice off.

## Switch it off

The **Voice** switch takes effect at once. Off, the clock closes its connection to Home Assistant
and holding the knob does nothing. Address, token and pipeline stay saved, so switching it on again
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

## Commands for the clock's room

Assist can take "turn on the lights" to mean the lights in the room the clock stands in. For that,
tell the clock which room it is in:

1. In Home Assistant, open **Settings → Devices & services → Devices** and pick a device in the
   clock's room. This can be the clock itself or anything else in that room. It needs an
   **Area**.
2. Copy the address of that device page from the browser, for example
   `http://homeassistant.local:8123/config/devices/device/0123456789abcdef0123456789abcdef`.
3. In the web UI, paste it into **Room device** under **System → Home Assistant Voice** and press
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

The button works only while **Connection** shows *Ready*. Without discovery, publish an empty
message to [`<prefix>/cmd/voice/start`](../reference/mqtt.md#voicestart).

## When it does not work

| Connection shows | What to do |
|---|---|
| HA unreachable | Check the address and that Home Assistant runs. Address only, no path. |
| Token rejected | Create a new long-lived access token and save it. |
| Pipeline lacks STT/TTS | Pick a pipeline with speech-to-text and text-to-speech. |
| Not understood | Speak once the house opens its mouth, without long pauses. |
| Wi-Fi too slow | Move the clock closer to the router. |
| HA too slow | Use a faster speech-to-text or text-to-speech. |
| Microphone update pending | Keep the clock on USB power. The update installs by itself. |
| Microphone error | The microphone stopped while you spoke. Try again. If it happens again, restart the clock. |
| Playback error | The answer could not be played. The pipeline's text-to-speech has to answer in MP3 or WAV. |
| Settings unreadable | The saved Voice settings cannot be read. Enter the address and the token again and press **Save and connect**. |
| HA error | Home Assistant reported a problem. Its log says which. |

## Privacy

The clock records only from the start of a request until Home Assistant hears you finish, you press
the knob, or a minute has passed. The recording goes straight to your Home Assistant and its
pipeline, and is not stored on the clock. With Voice switched off, the microphone is not
used for Voice at all.

## Related

- [Home Assistant](home-assistant.md): entities and notifications over MQTT
- [Install on TC002](../getting-started/tc002.md)
- [`/api/v1/voice`](../reference/http.md#apiv1voice): the same settings over HTTP
