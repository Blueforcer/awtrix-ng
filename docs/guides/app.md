# AWTRIX NG app

The AWTRIX NG app finds the clocks in your Wi-Fi, shows live what they display and brings the
[AWTRIX Hub](hub.md) to your phone.
<!-- only tc002 -->
On the TC002 your phone also becomes a gamepad and a microphone for announcements.
<!-- /only -->

[:material-apple: App Store](https://apps.apple.com/app/id6819090967){ .md-button .md-button--primary }
[:material-google-play: Google Play](https://play.google.com/store/apps/details?id=de.awtrix.ng){ .md-button .md-button--primary }

A one-time purchase: no subscription, no ads, no account. Buying it supports the development of
AWTRIX NG.

## What the app does

### Find your clocks

The app searches your Wi-Fi and lists every AWTRIX NG clock it finds. Tap **Add** and the clock is
on your phone. A clock that is not found can be added by its address, for example
`192.168.178.39`. If you set a login for the web UI, the app asks for it once and remembers it.

### See the clock live

The **Clocks** tab shows what the selected clock displays right now, pixel for pixel. **Open web
interface** opens the full [web UI](../getting-started/web-ui.md) of the clock inside the app.

### Scripts and icons from the Hub

The **Hub** tab is the AWTRIX Hub, made for your phone. It shows only scripts that run on the
selected clock. Tap **Send to AWTRIX** and the script is on the clock, without typing an address.

Enter your Hub connection key under **Settings** to download icons and use your Hub account. Create
the key in [your Hub account](https://awtrix.de/account/settings).

**Scripts** on the **Clocks** tab starts and stops the scripts installed on the clock.

### Notifications from your phone

=== "Android"

    Choose the apps whose notifications should appear, and on which clocks. For each app you set
    the icon, how long it stays and whether the message text is shown or only the app and title.
    **Send test** shows how it looks.

    **Music** shows the song your phone plays: title, cover and progress.

=== "iPhone"

    iOS does not let apps read the notifications of other apps<!-- only esp32 esp32-s3 -->, so the
    app cannot bring them from an iPhone to this clock<!-- /only -->.
<!-- only tc002 -->
    The TC002 gets them itself: it connects to your iPhone over Bluetooth, even while the app is
    closed. In the app you choose which apps appear on the clock. See [iPhone](iphone.md).
<!-- /only -->

### Send a picture

Share a photo from any app with the AWTRIX NG app. Show it on the whole display, or save it on the clock
as an icon. You can also publish the icon in the Hub.
<!-- only tc002 -->

### Play with your phone

The app turns your phone into a gamepad for the games on the clock: D-pad, A, B, X, Y, L1, R1,
SELECT and START. Two phones can play together. See [Gamepad](gamepad.md).

### Announcements

Hold the button, speak, let go. The clock plays what you said, in your voice or higher or lower.
Send it to one clock or to several at once.
<!-- /only -->

## What you need

- An iPhone or iPad with iOS 15 or later, or an Android phone or tablet with Android 7 or later.
- A clock with AWTRIX NG. The app does not work with AWTRIX 3.
- Phone and clock in the same Wi-Fi.

## Privacy

Your clocks, logins and settings stay on your phone. The app talks to your clocks directly in your
Wi-Fi. It only goes online for the Hub<!-- only tc002 --> and, for iPhone notifications, to fetch
app names and icons from the App Store<!-- /only -->.
