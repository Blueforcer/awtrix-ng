# Build an app with AI

[AWTRIX scripting](scripting.md) assumes you want to write the code. This page
assumes you do not.

Below is a **system prompt**: a single block of text that teaches a chatbot -
ChatGPT, Claude, Gemini, a local model, whichever you have - everything about
the AWTRIX scripting API. Paste it in once, then describe the app you want in
plain words. What comes back is a complete script you paste into the web UI.

You do not need to understand the code. You do need to be able to copy, paste,
and say what you want.

If instead of a chatbot you use a *coding agent* - Claude Code, Codex, Gemini CLI
- take the [agent skill](#as-an-agent-skill) further down. Same knowledge,
installed once, and the agent puts the app on your panel itself.

[Jump to the prompt](#the-prompt){ .md-button }
[Download as `.md`](https://raw.githubusercontent.com/Blueforcer/awtrix-ng/main/docs/examples/berry-app-system-prompt.md){ .md-button }
[Download the skill](../examples/awtrix-berry-app-skill.zip){ .md-button }

---

## How to use it

**1. Start a fresh chat.** A new conversation, with nothing else in it. If your
tool has a place for permanent instructions - a Custom GPT, a Claude Project, a
system prompt field - put the prompt there instead and it applies to every
message.

**2. Paste the prompt as your first message.** The assistant will not answer with
anything interesting yet. That is correct.

**3. Say what you want, in your own words.** Be concrete about what should be on
the panel; you do not need any technical vocabulary.

> Show the current temperature in my city, Hamburg. Green when it's mild, red
> when it's over 28 degrees.

> A countdown to 24 December. Just the number of days, in a warm colour.

> Show the price of Bitcoin, updated every few minutes.

> When I press the middle button, play a sound and flash "COFFEE" on the screen.

It may ask you a question or two - which city, which currency, which icon. Answer
and it will produce the script.

**4. Install it.** Copy the code block it gives you, open the AWTRIX web
interface, go to the **Scripts** tab, create a script, paste, save. The app joins
the rotation a moment later. The assistant repeats these steps for you at the end
of its answer.

The prompt tells the assistant that AWTRIX has very little memory, so
the scripts come back terse: few, long methods, and only the part of a web
response the app actually needs. You do not have to ask for that, and it does not
change what the app does.

---

## As an agent skill

If you work with a coding agent - Claude Code, Codex, Gemini CLI - you do not
have to paste anything. Install the same knowledge once as a **skill**, and the
agent picks it up by itself whenever you ask for an AWTRIX app.

The agent writes the app, puts it on your device, and looks at the
panel before it tells you it is done - so the copying, the pasting and the
error message below are steps you no longer do by hand.

[Download the skill](../examples/awtrix-berry-app-skill.zip){ .md-button }

**1. Unzip it into your agent's skills folder**, so that the `awtrix-berry-app`
folder from the archive sits directly inside it:

| Your agent | Folder |
|---|---|
| Claude Code | `~/.claude/skills/` |
| OpenAI Codex | `~/.agents/skills/` |
| Gemini CLI | `~/.gemini/skills/` |

On Windows that is `%USERPROFILE%` instead of `~`, and the folder may not exist
yet - create it.

**2. Say what you want**, and give it the address of your AWTRIX once - the IP,
or `awtrixng-xxxxxx.local`:

> Show the ICE departures from Hamburg Hbf on my AWTRIX at 192.168.1.42.

Leave the address out and it simply hands you the script instead, with the
same steps as above. For anything secret it leaves a marked line for you to
fill in; it will not ask you for a key.

---

## When it goes wrong

It will sometimes. The fix is nearly always another turn in the same chat.

If the panel shows **`ERR:`** in red, the script hit an error. Open the
**Scripts** tab in the web UI - the message is shown right next to your script,
and the editor marks the line when it can. **Copy that message back into the
chat.** The assistant has the whole API in front of it and usually fixes the
fault in one turn.

If the app installs and runs but looks wrong, say what you see: *"the text is cut
off on the right"*, *"it only shows a dash"*, *"the colour never changes"*. Ask
for a corrected full file rather than a patch - you are pasting whole scripts,
not editing them.

If the web UI refuses to save with a **`507`** - *"not enough free memory to
compile"* or *"heap too fragmented to compile"* - the script never ran; AWTRIX
turned it away for lack of room. Say so in the chat and the assistant will
come back with a shorter script. Rebooting, and deleting a script you no longer
use, help as well - see ["Not enough free memory to
compile"](scripting.md#not-enough-free-memory-to-compile).

A script that crashes or loops forever breaks only itself: AWTRIX marks that
one app broken, and the clock and the other apps carry on until you fix or delete
it (see [one interpreter, many
scripts](scripting.md#one-interpreter-many-scripts)).

---

## What it cannot do for you

**It does not know your AWTRIX.** It cannot see which icons you have installed,
what your MQTT topics are called, or what your Wi-Fi can reach. Tell it, or it
will guess - the prompt instructs it to draw shapes rather than invent icon IDs,
but it cannot know that your broker publishes on `home/kitchen/temp`.

**It cannot test.** Nothing in the chat runs the script. The first real test is
your panel. If you would rather iterate without AWTRIX on the other side of the
room, the [simulator](../advanced/simulator.md) runs scripts too.

**It does not know your API keys, and should not.** For services that need one,
it will leave a clearly marked line for you to fill in. Note that a script's
HTTPS traffic is [encrypted but unverified](scripting.md#a-complete-fetch) - do not put a
credential you care about into a script.

**It knows a fixed set of calls.** The prompt lists the scripting API; anything
that is not in it, the assistant will not use.

---

## The prompt

Everything the assistant needs is in here: the full API, the app structure,
AWTRIX's limits, and the mistakes language models reliably make when writing for
a 32×8 panel. Paste the whole block; do not summarize or shorten it.

Use the copy button in the top-right corner of the block.

`````text
--8<-- "examples/berry-app-system-prompt.md"
`````

---

## Related

- [AWTRIX scripting](scripting.md) - the same API, written for a person rather than a model
- [Icons & assets](icons.md) - installing the icons you want to name
- [Simulator](../advanced/simulator.md) - iterate without hardware
- [Pushed apps](pushed-apps.md) - when something outside AWTRIX can just keep it fed, no script needed
