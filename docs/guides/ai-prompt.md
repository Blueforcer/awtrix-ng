# Build an app with AI

This page shows how to let an AI chatbot write an AWTRIX app for you. You do not need to write
or understand the code.

Below is a **system prompt**: one block of text that teaches a chatbot the AWTRIX scripting API.
It works with ChatGPT, Claude, Gemini, a local model, whichever you use. Paste it in once, then
describe the app you want in plain words. The chatbot answers with a complete script that you
paste into the web UI.

If you use a *coding agent* instead of a chatbot, for example Claude Code, Codex or Gemini CLI,
use the [agent skill](#as-an-agent-skill) further down. It contains the same knowledge, you
install it once, and the agent puts the app on your display by itself.

To write scripts yourself, read the [Scripting guide](scripting/index.md).

[Jump to the prompt](#the-prompt){ .md-button }
[Download as `.md`](https://raw.githubusercontent.com/Blueforcer/awtrix-ng/main/docs/examples/berry-app-system-prompt.md){ .md-button }
[Download the skill](../examples/awtrix-berry-app-skill.zip){ .md-button }

---

## How to use it

**1. Start a new chat** with nothing else in it. If your tool has a place for permanent
instructions, such as a Custom GPT, a Claude Project or a system prompt field, put the prompt
there instead. Then it applies to every message.

**2. Paste the prompt as your first message.** The assistant does not answer with anything
useful yet. That is correct.

**3. Say what you want, in your own words.** Say exactly what should be on the display. You do not
need any technical words.

> Show the current temperature in my city, Hamburg. Green when it's mild, red
> when it's over 28 degrees.

> A countdown to 24 December. Just the number of days, in a warm color.

> Show the price of Bitcoin, updated every few minutes.

> When I press the middle button, play a sound and flash "COFFEE" on the display.

It first asks which clock you have, unless you said it already: a TC001 or another ESP32 clock,
an ESP32-S3, or a TC002. What runs on a TC002 does not run on the others. Then it may ask a question
or two more: which city, which currency, which icon. Answer, and it writes the script.

**4. Install it.** Copy the code block from the answer. Open the AWTRIX web UI, go to the
**Scripts** tab, create a script, paste the code and save. The app appears in the rotation a
moment later. The assistant repeats these steps at the end of its answer.

The prompt tells the assistant that AWTRIX has little memory. So the scripts it writes are
compact: few, long methods, and only the part of a web answer the app needs. You do not have
to ask for this, and it does not change what the app does.

If you want several views of one device or service, ask for them together, for example:

> Read my energy meter once every ten seconds. Show power and voltage as two
> separate apps, sharing the same readings.

The assistant can then write one background script that reads the data, and small apps that
show it. Then not every app has to contact the device. Shared helper functions can go into a
module. [Choose how your apps work together](scripting/several-apps.md#choose-how-your-apps-work-together)
explains when each way is useful.

---

## As an agent skill

With a coding agent, such as Claude Code, Codex or Gemini CLI, you do not have to paste
anything. Install the same knowledge once as a **skill**. The agent uses it by itself whenever
you ask for an AWTRIX app.

The agent writes the app, installs it on your device, and checks the display before it tells
you it is done. You do not copy, paste or report errors by hand.

[Download the skill](../examples/awtrix-berry-app-skill.zip){ .md-button }

**1. Unzip it into your agent's skills folder**, so that the `awtrix-berry-app`
folder from the archive sits directly inside it:

| Your agent | Folder |
|---|---|
| Claude Code | `~/.claude/skills/` |
| OpenAI Codex | `~/.agents/skills/` |
| Gemini CLI | `~/.gemini/skills/` |

On Windows, use `%USERPROFILE%` instead of `~`. If the folder does not exist yet, create it.

**2. Say what you want**, and give the agent the address of your AWTRIX once: the IP address,
or `awtrixng-xxxxxx.local`.

> Show the ICE departures from Hamburg Hbf on my AWTRIX at 192.168.1.42.

Without the address, it gives you the script and the same install steps as above. For
anything secret, such as an API key, it leaves a marked line for you to fill in. It does not
ask you for the key.

---

## When it goes wrong

Usually the fix is one more message in the same chat.

**The display shows `ERR:` in red.** The script has an error. Open the **Scripts** tab in the
web UI. The message is shown next to your script, and the editor marks the line when it can.
**Copy the message into the chat.** The assistant usually fixes the error in one answer.

**The app runs but looks wrong.** Say what you see: *"the text is cut off on the right"*,
*"it only shows a dash"*, *"the color never changes"*. Ask for the complete corrected
script, not only the changed lines. You paste whole scripts.

**The web UI refuses to save and says there is not enough memory.** The script was not
installed because AWTRIX has too little free memory right now. Tell the assistant, and it
writes a shorter script. Restarting AWTRIX and deleting scripts you do not use also help. See
["Not enough memory to compile"](scripting/troubleshooting.md#not-enough-memory-to-compile).

A script that crashes or never ends breaks only itself. AWTRIX marks that one app as broken,
and the clock and the other apps keep running until you fix or delete it (see
[Scripts don't disturb each other](scripting/troubleshooting.md#one-interpreter-many-scripts)).

---

## What it cannot do for you

**It does not know your AWTRIX.** It cannot see which icons you have installed, what your
MQTT topics are called, or what your network can reach. Tell it, or it guesses. The prompt
tells it to draw shapes instead of inventing icon IDs, but it cannot know that your broker
publishes on `home/kitchen/temp`.

**It cannot test.** Nothing in the chat runs the script. The first real test is your display.

**It does not know your API keys, and should not.** For services that need a key, it leaves a
clearly marked line for you to fill in. A key in a script is
[only as private as AWTRIX](scripting/network.md#keep-tokens-private).
<!-- only esp32 esp32-s3 -->
A script's HTTPS connection is encrypted, but the clock does not check the server. Do not put a
password or key you care about into a script.
<!-- /only -->

**It knows only the calls in the prompt.** The prompt lists the scripting API. The assistant
does not use anything that is not in it.

---

## The prompt

The prompt contains everything the assistant needs: the full API, how an app is built,
AWTRIX's limits, and the mistakes AI models often make when they write for a small LED display.
Paste the whole block. Do not shorten it.

Use the copy button in the top-right corner of the block.

`````text
--8<-- "examples/berry-app-system-prompt.md"
`````

---

## Related

- [Scripting guide](scripting/index.md): the same API, written for people
- [Icons](icons.md): install the icons you want to use
- [Pushed apps](pushed-apps.md): show data sent from outside AWTRIX, without a script
