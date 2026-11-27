---
only: [esp32, esp32-s3]
---

# Script host

The script host runs your clock's scripts on a computer in your home network, and the clock shows
them.

## What you get

- Your scripts run on the computer and get much more memory there.
- The clock shows them in its rotation like any other app, and keeps more memory free for the rest.
- You keep installing and changing scripts in the clock's web UI and on the [AWTRIX Hub](hub.md).
- One script host serves all your clocks. Each clock keeps its own scripts and settings.

It helps when your scripts do not fit into the clock, for example when saving a script says
**not enough memory**. Without a script host, the clock runs its scripts itself.

## How it behaves

The script host is a program on a computer in the same network as the clock. It runs the scripts
and sends each picture to the clock. The clock still decides which app is shown, and its buttons,
sensors, sound and settings stay with the clock. While the clock is paired, no script runs on the
clock itself.

When the computer sleeps, is switched off or cannot be reached, the clock leaves out its scripts
within three seconds and shows its other apps. When the computer is back, the scripts come back on
their own.

The clock shows each picture up to 0.15 seconds late, so that animations run smoothly over Wi-Fi.
Buttons in games react that much later.

---

## Choose a computer

Pick a computer that is on whenever the clock should show your scripts. It must be in the same
network as the clock.

| Computer | Needs | Runs your scripts |
|---|---|---|
| Home Assistant | Home Assistant OS on a 64-bit computer | whenever it is on |
| Raspberry Pi | the 64-bit Raspberry Pi OS | whenever it is on |
| Linux computer | 64-bit Linux with systemd, on Intel, AMD or ARM | whenever it is on |
| NAS | Docker, on a 64-bit Intel, AMD or ARM processor | whenever it is on |
| Windows PC | Windows 10 or 11, 64-bit | while you are signed in and the PC is awake |
| Mac | macOS 11 or newer, Apple silicon or Intel | while you are logged in and the Mac is awake |

## Set up the script host

Choose your system. At the end you see the 6-digit pairing code. You need it to
[pair the clock](#pair-the-clock).

=== "Windows"

    1. Download
       [`AWTRIX-Script-Host-windows-x64.exe`](https://github.com/Blueforcer/awtrix-ng/releases/latest/download/AWTRIX-Script-Host-windows-x64.exe).
       If your browser warns about the file, choose **Keep**.
    2. Open the file. If Windows shows **Windows protected your PC**, press **More info**, then
       **Run anyway**. Answer the next question with **Yes**.
    3. Press **Next**, **I Agree** and **Install**.
    4. Leave **Start AWTRIX Script Host** ticked and press **Finish**.
    5. Click the small pixel clock in the notification area, at the right end of the taskbar. If
       you do not see it, press the arrow **^** next to the other icons first.

    You now see the pairing code in the menu, for example **Pairing code 482915**. Click it to copy
    it.

=== "macOS"

    1. Download
       [`AWTRIX-Script-Host-macos.pkg`](https://github.com/Blueforcer/awtrix-ng/releases/latest/download/AWTRIX-Script-Host-macos.pkg)
       and open it.
    2. macOS says that it cannot check the package. Press **Done** or **OK**.
    3. Open **System Settings → Privacy & Security**, scroll down and press **Open Anyway** next to
       the package. Confirm with your password. On macOS 11 and 12, the button is under
       **System Preferences → Security & Privacy → General**.
    4. In the installer, press **Continue** and **Install**, and enter your password. Then press
       **Close**.
    5. If macOS asks whether **AWTRIX Script Host** may find devices on your local network, press
       **Allow**. Without it, the script host cannot reach the clocks.
    6. Click the small pixel clock in the menu bar at the top.

    You now see the pairing code in the menu, for example **Pairing code 482915**. Click it to copy
    it.

=== "Linux / Raspberry Pi"

    1. Open a terminal on the computer, or sign in to it with SSH.
    2. Run this line:

        ```bash
        curl -fsSL https://raw.githubusercontent.com/Blueforcer/awtrix-ng/main/packaging/host/linux/install.sh | sudo sh
        ```

    3. If the installer says that ufw or firewalld is active, also run the line it shows below.
       It lets the clocks through the firewall.

    You now see the pairing code in the last lines, for example `Pairing code: 482915`.

    The script host runs in the background and starts with the computer, also when nobody signs
    in.

=== "Home Assistant"

    1. Go to **Settings → Apps → Install app**.
    2. Open the menu **⋮** at the top right and choose **Repositories**. Add
       `https://github.com/Blueforcer/awtrix-ng` and close the window.
    3. Under **AWTRIX NG**, open **AWTRIX Script Host** and press **Install**.
    4. Press **Start**, then **Open Web UI**.

    You now see the pairing code on the page.

    Instead of steps 1 and 2, you can
    [add the repository to your Home Assistant](https://my.home-assistant.io/redirect/supervisor_add_addon_repository/?repository_url=https%3A%2F%2Fgithub.com%2FBlueforcer%2Fawtrix-ng)
    in one click. **Show in sidebar** keeps the page one click away.

    If **Settings** has no **Apps**, your Home Assistant cannot install apps. Use the
    **Linux / Raspberry Pi** or the **Docker** tab instead.

=== "Docker"

    For a NAS or a Linux computer with Docker.

    1. Save this file as `docker-compose.yml` in an empty folder:

        ```yaml
        services:
          awtrix-host:
            image: ghcr.io/blueforcer/awtrix-script-host
            container_name: awtrix-host
            network_mode: host
            restart: unless-stopped
            volumes:
              - awtrix-host:/data

        volumes:
          awtrix-host:
            name: awtrix-host
        ```

    2. Start the script host in that folder:

        ```bash
        docker compose up -d
        ```

    3. Read the code:

        ```bash
        docker logs awtrix-host | grep "pairing code" | tail -n 1
        ```

    You now see the pairing code, for example `pairing code 482915 (valid 10 min)`.

    On a NAS, you can paste the file as a new project in its container app instead. The log of the
    container shows the code there.

    - Keep `network_mode: host`. Without it, the clocks cannot reach the script host.
    - Keep the volume as in the file. A folder of your own in its place must belong to the user
      with the ID 999.
    - Docker Desktop on Windows or on a Mac cannot reach the clocks. Use the **Windows** or
      **macOS** tab there.

## Pair the clock

Do this once for each clock. It works the same with every system.

1. Open the web UI of the clock and go to **System → Script host**.
2. Check **Address**. When the clock has found the script host, its address is there already.
   With several script hosts, press the name of the computer under **Found**. If nothing was found,
   type the address of the computer, for example `192.168.1.5` or `raspberrypi.local`.
3. Type the pairing code into **Code** and press **Pair**.

The clock copies its scripts to the script host, with their settings and saved data. Then it
restarts. **System → Script host** now shows **Connected**, and your scripts are back in the
rotation.<!-- only esp32-s3 --> Their sounds stay on the clock, in the **Audio** tab.<!-- /only -->

A script host shows up under **Found** within half a minute after it started. If you opened the
page before that, reload it.

### A new code

A code works for one clock and for ten minutes. Then the script host makes a new one. You find the
current code here:

- **Windows and macOS:** in the menu of the pixel clock icon.
- **Linux / Raspberry Pi:** open `http://localhost:8300` in a browser on that computer, or run:

    ```bash
    sudo journalctl -u awtrix-script-host | grep "pairing code" | tail -n 1
    ```

- **Home Assistant:** **Open Web UI** on the page of the app.
- **Docker:** run the `docker logs` line from the setup again, or look at the log of the container.

---

## Everyday use

Install and change your scripts as without a script host: in the **Scripts** and **Apps** tabs of
the clock, and with **Send to AWTRIX** on the AWTRIX Hub. The clock passes them on to the script
host. Changes to app settings from Home Assistant or over MQTT reach it too.

### Start and stop

- **Windows and macOS:** the script host starts when you sign in. **Quit** in its menu stops it,
  and the clock shows its other apps. To start it again, open **AWTRIX Script Host** in the Start
  menu or in **Applications**. To keep it from starting by itself, untick **Start at login** in
  the menu.
- **Linux / Raspberry Pi:** it always runs. `sudo systemctl stop awtrix-script-host` stops it until
  the computer starts again. `sudo systemctl start awtrix-script-host` starts it.
- **Home Assistant:** **Stop** and **Start** on the page of the app.
- **Docker:** `docker compose stop` and `docker compose start` in the folder of the file.

### Update

Your scripts and pairings stay.

- **Windows:** download the Setup from [Set up the script host](#set-up-the-script-host) again and
  open it. Setup quits the running script host. Leave **Start AWTRIX Script Host** ticked at the
  end.
- **macOS:** download the package again and open it. The installer quits the running script host
  and starts the new one.
- **Linux / Raspberry Pi:** run the install line again.
- **Home Assistant:** press **Update** when Home Assistant offers it.
- **Docker:** in the folder of `docker-compose.yml`, run `docker compose pull`, then
  `docker compose up -d`.

## Move to another computer

Set up the script host on the new computer first, and leave the old one running.

1. Go to **System → Script host**. Press the name of the new computer under **Found**, or type its
   address into **Address**.
2. Type the pairing code of the new script host into **Code**.
3. Leave **Take my scripts along** on and press **Move**.

The clock copies its scripts to the new script host, with their settings and saved data. Then it
restarts, and the old script host deletes its copies. What you entered under **API keys** and
**Sign-in** in the settings of an app stays behind. Enter it again.

With **Take my scripts along** off, the new script host starts without scripts. The old one keeps
them and stops running them.

If the old computer does not answer, the web UI asks whether to move without your scripts. Press
**Move without them** to go on. The scripts then stay on the old computer.

## Several clocks

Pair each clock the same way, each with its own code. The script host keeps the scripts, settings
and saved data of each clock apart. To show the same picture on several clocks, use
[Mirroring](mirroring.md).

## Unpair

1. Go to **System → Script host** and press **Unpair**. Press it again to confirm.
2. Choose **Bring scripts back** to copy the scripts to the clock, or **Leave them on the host**.

The clock restarts and runs its scripts itself again. Once all scripts are back, the script host
deletes its copies. A script that does not fit into the clock's memory stays on the script host.
The web UI names each one with the reason.

Scripts left on the script host stop there. They run again when you pair the clock with that
script host again.

## Remove the script host

First [unpair](#unpair) each clock and choose **Bring scripts back**. Otherwise the clock keeps
waiting for the script host and shows no scripts.

- **Windows:** open **Settings → Apps**, find **AWTRIX Script Host** and choose **Uninstall**. At
  the end, it asks whether to delete the paired clocks and their scripts too.
- **macOS:** download
  [`AWTRIX-Script-Host-Uninstall-macos.pkg`](https://github.com/Blueforcer/awtrix-ng/releases/latest/download/AWTRIX-Script-Host-Uninstall-macos.pkg)
  and open it like the installer, with **Open Anyway**. Press **Continue** and **Install**. This
  removes the program. Your scripts and pairings stay on the Mac. To delete them too, choose
  **Go → Go to Folder** in the Finder, type `~/Library/Application Support` and move the folder
  **AWTRIX Script Host** to the Trash.
- **Linux / Raspberry Pi:** run `sudo /usr/local/lib/awtrix-script-host/uninstall.sh`. Your scripts
  and pairings stay. To delete them too, run
  `sudo /usr/local/lib/awtrix-script-host/uninstall.sh --purge` instead.
- **Home Assistant:** press **Uninstall** on the page of the app. This deletes its scripts and
  pairings too.
- **Docker:** run `docker compose down` in the folder of the file. Your scripts and pairings stay
  in the volume. To delete them too, run `docker compose down -v` instead.

## If something does not work

| What you see | What to do |
|---|---|
| The script host is not under **Found** | Wait half a minute and reload the page. Some networks block the search: type the address of the computer into **Address**. |
| **wrong code** | The code changes after ten minutes and after each pairing. Type the [current code](#a-new-code). |
| **too many wrong codes** | Wait a minute, then try again. After more wrong codes, the wait grows up to one hour. |
| **No answer** under **System → Script host**, or **The script host does not answer.** | The computer is off or asleep, or the script host was quit. Start it. The device with your browser must be in the same network as the computer too. |
| **The script host does not know this clock.** | The script host lost its pairings, for example because it was installed anew without its data. Type a new code and press **Pair again**. Scripts lost with that data must be installed again. |
| **Stopped: port 8300 is used by another program** in the menu | Another program uses this port, for example the script host of another user signed in to the same computer. Quit it, then choose **Start again**. |
| **Stopped: the script host cannot start** in the menu | Install the script host again. Your scripts and pairings stay. |
| On a Mac, the clocks are not found after **Don't Allow** | Open **System Settings → Privacy & Security → Local Network** and turn on **AWTRIX Script Host**. Then choose **Quit** in its menu and open it again. |
| The status page shows no code | It shows the code only on the computer that runs the script host. Use **Open status page** in the menu, **Open Web UI** in Home Assistant, or the [other ways](#a-new-code). |
| A firewall of your own, for example in an antivirus program | Let the [ports](#ports) through. The Windows and macOS installers already let the clocks through the built-in firewall. |

---

## Details

### The menu

On Windows and macOS, a click on the pixel clock icon opens this menu:

| Entry | What it does |
|---|---|
| **AWTRIX Script Host 1.3.0** | shows the version |
| **Running**, **Starting…**, **Restarting in 10 s…** or **Stopped: …** | shows the state. After an unexpected stop, the script host starts again by itself |
| **Start again** | starts the script host again. Shown only when it stopped because of a problem |
| **Pairing code 482915** | copies the code |
| one line per paired clock, or **No clock paired yet** | the name, address and [state](#states) of the clock. A click opens the web UI of the clock |
| **Open status page** | opens the status page in your browser |
| **Open log** | opens the log |
| **Start at login** | starts the script host when you sign in. Ticked after the installation |
| **Quit** | stops the script host and removes the icon |

On Windows, the icon is green while the script host runs and grey while it is stopped. On a Mac,
it is dimmed while stopped. Opening **AWTRIX Script Host** again while it runs opens the status
page.

### The status page

The status page shows the name and version of the script host, the pairing code and the paired
clocks. Open it with **Open status page** in the menu, or at `http://<computer>:8300` in a browser.
It shows the code only on the computer itself, and in Home Assistant under **Open Web UI**.

### States

| State | Meaning |
|---|---|
| **running** | The scripts of this clock run. |
| **waiting**, on the status page **waiting for the clock** | The scripts of this clock are stopped until the clock is heard again. This happens when the clock was silent for ten minutes, for example because it is off, and for a few seconds after the script host starts. |
| **restarting** | The scripts stopped unexpectedly and start again within a few seconds. |

### Where your scripts and the log are

| System | Scripts and pairings | Log |
|---|---|---|
| Windows | `%LOCALAPPDATA%\AWTRIX Script Host` | **Open log** in the menu, a file in the same folder |
| macOS | `~/Library/Application Support/AWTRIX Script Host` | **Open log** in the menu, a file in `~/Library/Logs/AWTRIX Script Host` |
| Linux / Raspberry Pi | `/var/lib/awtrix-script-host` | `sudo journalctl -u awtrix-script-host` |
| Home Assistant | inside the app, part of your Home Assistant backups | **Log** tab of the app |
| Docker | the volume `awtrix-host` | `docker logs awtrix-host` |

On Windows and macOS, each user of the computer has their own script host, scripts and pairings.

### Ports {#ports}

| Port on the computer | For |
|---|---|
| 8300, TCP | the status page and pairing |
| 4211, UDP | finding the clocks |
| 8301 to 8399, TCP and UDP | one port for each paired clock |

### Reference

- [System settings](../reference/system.md): `scriptHost` and `scriptHostKey`
- [HTTP API: script host status](../reference/http.md#get-apiv1scripthost)
- [Errors](../reference/errors.md): `scriptsOnHost`

## Related

- [Scripting guide](scripting/index.md)
- [AWTRIX Hub](hub.md)
- [Mirroring](mirroring.md)
