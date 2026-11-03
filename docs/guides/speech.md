---
only: [tc002]
---

# Speech & voices

This page lets your clock read text aloud, and shows how to give it another voice, another language
or your own voice.

## What you get

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"speech":"Good morning. It is 7:30 and 21° outside."}'
```

The clock says the sentence through its speaker.

## How it behaves

A clock with a voice reads text aloud, in the language of its voice. The built-in voice speaks
English. A [voice pack](#another-voice-or-language) brings another voice or language. Your clock
has a voice when `speech` is `true` under `audio` in
[`GET /api/v1/capabilities`](../reference/http.md#get-apiv1capabilities).

While [Home Assistant Voice](voice.md) uses the speaker, the clock does not speak.

## Let a notification speak

A notification can speak too. The text on the display and the spoken text are separate:

<!-- panel motion=4 -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Door open","sound":{"speech":"The front door is open."}}'
```

The display shows `Door open`, and the clock says the whole sentence.

On a clock without a voice, that notification shows without sound. To play a sound there instead,
give a list: `"sound":[{"speech":"The front door is open."},"ding"]`.

## How the clock reads text

How the built-in English voice reads the text:

- Up to 512 bytes of text. A plain letter is one byte. A letter with an accent, or a sign like
  `°`, is two or three.
- Letters with accents are read without them: `Café` sounds like `cafe`.
- Numbers are read as words: `23.5` is "twenty three point five", `-5` is "minus five".
- Times are read as times: `7:05` is "seven oh five", `12:00` is "twelve o'clock".
- `%`, `&` and `°` are read as "percent", "and" and "degrees".
- `.`, `,`, `?`, `!`, `;`, `:` and a new line make a short pause.
- Anything else, emoji for example, is skipped.
- A very long text stops after the last word that fits.

A voice in another language reads numbers, times and signs that language's way. A German voice
reads `14:30 Uhr` as "vierzehn Uhr dreißig" and `21,5 °C` as "einundzwanzig Komma fünf Grad
Celsius".

## Another voice or language

A voice pack is one `.atts` file: a voice and the rules of its language. The shop has Lumi (female)
and Milo (male) in several languages, or you can [order your own voice](#have-it-made). After buying
a pack, [install it](#install-a-voice-pack).

[:material-cart: Get a voice pack](https://ko-fi.com/blueforcer/shop){ .md-button .md-button--primary }

## Your own voice

You have two ways to get a voice pack of your own: have it made, or build it yourself.

### Have it made

A pack is made from your own voice, or from another voice you have permission to use.

1. [Order the custom voice](https://ko-fi.com/s/082d288a21) in the shop.
2. Send a Ko-fi message, or write to the email address in your order confirmation. Include your
   order number and:
    - A recording of 10 to 30 seconds of natural speech by one speaker. WAV or FLAC is best, a
      high-bitrate MP3 is fine.
    - The exact words spoken in the recording.
    - The language the pack should speak.
3. You receive the `.atts` file. [Install it](#install-a-voice-pack).

Record in a quiet room with the microphone close by, without music, background noise, echo or
effects. Send only a voice you own, or one whose speaker has explicitly allowed it.

### Build it yourself

You train the voice on your own computer and write the tools for it. This takes programming
experience, a strong graphics card and about 10,000 recorded or generated sentences.
[Making a voice pack](../developers/tc002/voice-packs.md) describes the file, the language tables
and the training.

## Install a voice pack

Your clock keeps one uploaded pack in place of the built-in voice, across restarts and updates.

1. Open **System → Clock voice**.
2. Press **Upload** and choose the `.atts` file. The badge shows the new voice and its language.
3. Type a sentence next to **Test** and press **Speak**.
4. **Use built-in** removes your pack. Press it twice to confirm.

### With the API

```bash
curl -F "voice=@my-voice.atts" http://<awtrix-ip>/api/v1/speech/voice
```

## Good to know

- Write texts in the voice's language. An English sentence read by a German voice sounds German.
- If the clock does not speak, check that the text contains words and fits within 512 bytes. The
  clock also needs speech support and an available speaker.

## Details

- [`/api/v1/speech/voice`](../reference/http.md#get-apiv1speechvoice)
- [Audio playback errors](../reference/errors.md#audio-playback)

## Related

- [Sound](sounds.md)
- [Voice assistant](voice.md)
- [Speech in scripts](scripting/sound.md#speech)
