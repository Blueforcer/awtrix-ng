# Linux USB access

The DEB and RPM graphical installer packages install the TC002 USB rule for
you. After installing the package, unplug and reconnect the clock. An active
local desktop session receives access automatically through systemd-logind.

For this standalone terminal download, install the included rule once:

```sh
sudo install -D -m 0644 70-awtrix-tc002.rules /etc/udev/rules.d/70-awtrix-tc002.rules
sudo udevadm control --reload-rules
```

Then unplug and reconnect the clock. The rule applies only to the TC002's USB
vendor/product pair `18d1:d002`; it does not open access to other USB devices.

For SSH or a machine without a desktop session, give your normal account
access through the `plugdev` group once:

```sh
sudo groupadd --system --force plugdev
sudo usermod -aG plugdev "$(id -un)"
```

Log out completely and log back in so the new group membership takes effect.
If you created the group after installing the rule, run
`sudo udevadm control --reload-rules` again, then reconnect the clock.

Start `./awtrix-tc002-installer-cli` as your normal user. It will ask before
writing the clock and can configure Wi-Fi after installation. The setup access
point remains available if Wi-Fi settings are omitted or the router cannot be
reached. Running the CLI as root is also possible, but is not needed once the
rule and group membership are in place. Run `./awtrix-tc002-installer-cli --help`
for its options.
