# Hub script updates

This page shows how to update scripts that you installed from the
[AWTRIX Hub](https://awtrix.de), the online collection of AWTRIX scripts and icons.

## How it behaves

The web UI asks the Hub for new versions of the scripts you installed from it, and shows them in
the **Scripts** tab. Nothing is updated without your click. Only scripts installed from the Hub can
be updated. An update keeps your settings, and a script you changed yourself is never overwritten.
There is no version history, so keep your own backup of scripts that are important to you.

## Before you start

- Enter your Hub connection key in the web UI under
  [**AWTRIX Hub**](../getting-started/web-ui.md#awtrix-hub). Downloads from the Hub need it.
- After **Check for updates**, each script from the Hub shows its state, for example
  **Update available** or **Up to date**.

## Update a script

1. Open the **Scripts** tab in the web UI.
2. Click **Check for updates**. Scripts with a newer version on the Hub show
   **Update available**.
3. Open the script.
4. Click **Update script** and confirm.
5. Wait until **Script updated** appears. A line above the editor shows each step.
6. Check the script in the editor and in the log.

If you changed the script, or the editor holds changes you have not saved, step 4 offers
**Install as a new script** instead. See [Your own changes](#local-changes-and-conflicts).

A new version counts as an update when its code changed, and also when only its sounds changed.

## What an update changes

- **Settings:** settings that still exist in the new version keep their values. New settings
  start with their default values.
- **Icons:** the icons the script needs are downloaded first.
<!-- only esp32-s3 tc002 -->
- **Sounds:** new and changed sounds are copied to AWTRIX before the new code, so the script
  starts with all of them. Sounds that are already on AWTRIX are not copied again. Sounds the
  new version does not use are deleted at the end.
<!-- /only -->
<!-- only esp32 -->
- **Sounds:** this clock cannot play MP3 files, so a script's sounds are not copied. The
  confirmation tells you this.
<!-- /only -->
- **If the new version does not start:** when it cannot be compiled or started, AWTRIX restores
  the previous code and settings.

<!-- only esp32-s3 tc002 -->
## Storage<!-- only esp32-s3 --> and clocks without MP3<!-- /only -->

Before anything is written, the web UI checks that the sounds fit into the free storage. If they
do not fit, it stops and shows how much space is needed and how much is free.
<!-- only esp32-s3 -->

A clock that cannot play MP3 files gets the script without its sounds. The confirmation tells
you this.
<!-- /only -->
<!-- /only -->

## Your own changes {#local-changes-and-conflicts}

When you edit the code of a Hub script, the **Scripts** tab shows **Changed on the clock**. Renaming a
script does not count as a change. The script stays linked to the Hub.

A changed script is not updated in place. To keep your changes and still get the new version,
choose **Install as a new script**. This installs the Hub version as a separate script:

- It starts with its own default settings<!-- only esp32-s3 tc002 --> and gets the script's sounds<!-- /only -->.
- Your own script and the draft in the editor are kept.
- If installing the copy fails, the copy and everything installed for it are removed again.
<!-- only esp32-s3 tc002 -->
- If the copy cannot be restarted after its sounds were added, the web UI asks you to restart
  it on AWTRIX.
<!-- /only -->

## Privacy

Your browser talks to the Hub directly. The Hub never contacts your AWTRIX and never receives
your scripts or your device login. Like every website, the Hub can see the address of the web
UI that makes the request.

When a script is removed from the Hub, the copy on your AWTRIX keeps working.

## When it goes wrong

| What you see | What to do |
|---|---|
| A step fails | Click **Try again**. The update continues where it stopped. What is already on AWTRIX stays. |
| An icon with the same name but different content already exists | The update stops, so your icon is not replaced. Fix the icon under **Icons**, then try again. Icons downloaded before the stop may stay installed. |
| **The script changed. Check for updates again.** | The script was changed while the update was prepared. Open the current script, check the change and start again. |
| **The Hub version changed during download. Try again.** | Every download is checked against the Hub version. Start the update again. |
| **Update the firmware to get script updates.** | Update the firmware first. |
<!-- only esp32-s3 tc002 -->
| Not enough storage for the sounds | Delete sounds or scripts you do not use, then try again. |
<!-- /only -->
| **not enough memory to compile** | Not enough free memory for the script. See [Keeping scripts small](../tutorials/going-easy-on-memory.md#install-refused). |

## Good to know

- **A script that shows no state after Check for updates was not installed from the Hub.** Install
  it again from its Hub page to get updates.
- **An error that happens later, for example in a timer or when data arrives, cannot be found
  during the update.** Check the log after a while.
- **An update cannot undo what a script has already done**, for example messages it has sent.
- **The link to the Hub only shows where a script came from.** It does not prove that the script is
  unchanged or safe. Install only scripts you trust.

## Details

- [Limits](../reference/limits.md#scripting): script size and memory limits
- [Script updates in the HTTP API](../reference/http.md#script-updates): for developers of Hub
  tools

## Related

- [The web UI: Scripts](../getting-started/web-ui.md#scripts): the Scripts tab
- [Scripting guide](scripting/index.md): write your own scripts
