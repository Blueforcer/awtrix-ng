---
only: [tc002]
---

# Trusted certificates

Trust certificates for MQTT, Home Assistant Voice and script HTTPS in one place.

## Trust a device's certificate

1. Configure MQTT or Voice, or run the script's HTTPS request once.
2. Open **System → Trusted certificates**. The refused host appears with its SHA-256 fingerprint.
3. Compare that fingerprint with the certificate shown by your server or device.
4. Press **Trust certificate** when they match. MQTT and Voice reconnect automatically.

A fingerprint identifies one exact server certificate. If the server changes its certificate,
compare and approve the new fingerprint. Saved pins ignore certificate dates and names, which
also works for devices whose certificate contains only an internal identifier.

## Upload your own CA

1. Enter the hostname or IP address used by the connection in **Host**.
2. Press **Upload CA** and select a PEM CA file up to 64 KB.

A certificate authority (CA) issues server certificates. For that host, its uploaded CA replaces
public authorities. The server's certificate must match the host and be within its validity dates.
A separately approved fingerprint also counts. Other hosts keep their own trust settings.

## Remove trust

Use **Remove pin**, **Remove CA**, or **Forget host**. Removing the CA restores public authorities
for that host. Changes reconnect MQTT and Voice and apply to subsequent script requests.

## Good to know

- Host trust is shared across ports and these three clients. Scripts cannot choose their own trust.
- Requests using custom trust do not follow redirects.
- Saved trust survives updates and reboots. Pending fingerprints are discovered again after reboot.
- Configuration backups exclude this store. Set trust up again after restoring a backup or upgrading
  from separate MQTT or Voice trust settings.
- An unusable store refuses connections. **Reset certificate store** clears it for fresh setup.

## Details

See [the API reference](../reference/http.md#get-apiv1tlstrust) for routes, limits and errors.
