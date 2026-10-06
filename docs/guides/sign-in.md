---
only: [tc002]
---

# Signing in to services

This page shows how to let an app on the clock sign in to a service with your account. Some apps
show data that needs your account, for example the song playing on Spotify or your last run on
Strava. You set it up once, and the clock stays signed in.

## Words used here

- **Developer app**: a small entry you create at the service. It tells the service that your clock
  may ask for your data.
- **Client ID** and **client secret**: the name and password of that developer app. The service
  shows them after you create it. Some services give no client secret.
- **Redirect URL**: the address the service sends you back to after you sign in.

## What you need

- An app on the clock that signs in, for example one from the [AWTRIX Hub](https://awtrix.de).
  Its description says which service it uses.
- An account at that service.
- A phone or computer in the same network as the clock.

## Create the developer app

You do this once per service, in the service's developer area. For Spotify that is the Spotify for
Developers dashboard.

1. Create a new app there. Name and description are up to you.
2. Enter this **redirect URL**:

    ```text
    https://awtrix.de/oauth/callback
    ```

3. Save the app and copy its **client ID**. If the service shows a **client secret**, copy it too.

## Sign in on the clock

1. Open the web UI by the clock's IP address and go to **Apps**.
2. Press the gear button of the app. The **Sign-in** block shows the service and **Not signed in**.
3. Paste the **Client ID**, and the **Client secret** if you have one.
4. Press **Sign in**. The service's sign-in page opens.
5. Sign in there and allow access.
6. You are back in the clock's web UI, and the block shows **Signed in**.

**Sign out** in the same block ends the sign-in. Client ID and secret stay saved.

## What the clock keeps

The client secret and the sign-in stay on the clock. The web UI never shows them again. The
**Client secret** field only says whether one is saved.

You sign in again after you

- restore a backup, because backups do not contain sign-ins,
- rename or delete the app and install it again,
- reset the clock to factory settings.

An update of the app can also ask you to sign in again.

## Privacy

After you sign in, the service sends your browser back through `awtrix.de` to the clock.
awtrix.de only passes this on and stores nothing. Client secret and sign-in never leave the clock
and are not part of a backup.

Anyone who can open the clock's web UI can sign out or change the client ID, but cannot read the
secret or the sign-in. They are stored on the clock without encryption, so keep the clock itself
out of other people's hands.

## When it does not work

| What you see | What to do |
|---|---|
| **Sign-in failed** with `invalid_client` | The client ID or secret is wrong. Copy them again from the developer app. |
| **Sign-in failed** with `invalid_grant` | The service ended the access, for example because you removed it in your account. Press **Sign in** again. |
| The service reports a wrong redirect URL | Enter `https://awtrix.de/oauth/callback` in the developer app exactly as shown. |
| "This sign-in link is not valid" | Open the clock's web UI by its IP address and sign in again. |
| The app shows nothing after signing in | Wait for its next update, or open the app on the clock. |

## Good to know

- **Signing in works only when you open the web UI by the clock's IP address or its local name.**
  Use an address such as `http://192.168.1.50`, `http://awtrix-ng-tc002`, or a name ending in
  `.local` or `.fritz.box`.
- **Text in the Client secret field replaces the saved secret.** Leave the field empty to keep it.

## Related

- [Signing in from scripts](oauth.md): write such an app yourself
