# AWTRIX Hub

This page shows how to find scripts, flows and icons on the AWTRIX Hub and use them with your
clock.

The [AWTRIX Hub](https://awtrix.de) is the community site for AWTRIX. It has hundreds of
ready-made apps and automations and thousands of icons, shared by other AWTRIX users.

## What you find there

| Section | What it is |
|---|---|
| [**Scripts**](https://awtrix.de/scripts) | Apps that run on the clock itself: games, clock faces, live data, animations |
| [**Flows**](https://awtrix.de/flows) | Ready-made automations for Home Assistant (also as blueprints), Node-RED, n8n, Domoticz and FHEM |
| [**Icons**](https://awtrix.de/icons) | Images and animations for your apps and notifications |
| [**Studio**](https://awtrix.de/editor) | Draw your own icons and animations in the browser |

## How it behaves

The Hub is a website, and it never reaches your clock by itself. When you press **Send to AWTRIX**,
your browser copies the script or icon to the clock over your own network. A script brings the
icons and sounds it needs. A flow does not go to the clock at all: it runs in your smart home
system.

## Show only what runs on your clock

The fastest way is from the web UI of your clock:

- **Scripts** tab → **Scripts for this device**
- **Icons** tab → **Icons for this device**

The Hub then opens with only what fits your clock.

On the Hub itself, choose your clock under **Which AWTRIX do you have?** at the top. Every page
also says whether it runs on your clock, and why not if it doesn't.

## Send a script or icon to your clock

Your computer or phone must be in the same network as the clock.

1. Open the script or icon on the Hub.
2. Click **Sign in** at the top and sign in with your GitHub or Discord account. You only do this
   once.
3. Click **Send to AWTRIX**.
4. Enter the address of your clock, for example `192.168.178.39` or `awtrixng-a1b2c3.local`.
   [Find your clock](../getting-started/discovery.md) shows where to read it. For an icon, also
   choose **AWTRIX NG** as the version.
5. If you set a login for the web UI, enter the username and password. The password is used for
   this upload only and is not saved.
6. Press **Install script** or **Send icon**. Your browser copies the script or icon straight to
   the clock.

The script then shows up in the **Scripts** tab of the web UI, the icon in the **Icons** tab.

## Flows for your smart home

A flow sends its content to the clock over HTTP or MQTT.

- **Home Assistant blueprint:** click **Import to Home Assistant**. Setup continues in Home
  Assistant.
- **Every other flow:** click **Download flow** and follow the setup steps on its page.

See [Home Assistant](home-assistant.md) and [MQTT](mqtt.md) for connecting the clock.

## Keep scripts up to date

The web UI checks the scripts you installed from the Hub for new versions. Nothing changes without
your click. See [Hub script updates](hub-script-updates.md).

## Share your own

- **A script:** see [Sharing a script](scripting/sharing.md).
- **An icon:** draw it in the [Icon editor](icon-editor.md) and click **Publish to Hub**, or use
  the Hub [Studio](https://awtrix.de/editor).
- **A flow:** [Share a flow](https://awtrix.de/new) on the Hub.

## Good to know

- **Safari and every browser on an iPhone or iPad cannot send to the clock.** Use Chrome, Edge or
  Firefox on a computer or an Android phone, or the **Hub** tab of the [AWTRIX NG app](app.md).
- **When your browser asks whether awtrix.de may access devices on your local network, allow it.**
  If you refused, allow it again in the browser's site settings for awtrix.de.
- **When the Hub cannot reach the clock by its name, enter the IP address instead.** The display
  shows it each time the clock starts.
- **When the browser cannot reach the clock at all, press ↓ Download script on the script's Hub
  page.** Then load the file with **Import** in the [Scripts](../getting-started/web-ui.md#scripts)
  tab of the web UI and save it.
