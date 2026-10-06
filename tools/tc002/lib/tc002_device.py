"""A TC002 over ADB, shared by tc002_install.py and tc002_wifi.py.

find_adb() takes --adb, $ADB or adb on PATH. Device runs adb for one serial and device shell
commands with their exit status. connect() brings USB ADB up and keeps it: a TC002 that never ran
AWTRIX shows USB ADB for about 3 s after power-on, until the kernel switches the port to host mode,
so it watches `adb devices` while the user switches the clock off and on and starts a USB keeper on
the device the moment the serial shows up. All TC002 units report the serial 0123456789ABCDEF.
"""
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

USB_ROLE = "/sys/bus/platform/devices/soc:usbotg/otg_role"
USB_KEEPER_PID = "/tmp/awtrix-usb-keeper.pid"
LOADER_LOG = "/tmp/awtrix-loader.log"
VENDOR_MARKER = "/tmp/awtrix-loader.vendor"
KEEPER_SECONDS = 1200
KEEPER_HOLD = 10
_install_limits = json.loads((Path(__file__).resolve().parents[1] / "install/constants.json").read_text())
CONNECT_WAIT = _install_limits["usbConnectWaitMs"] / 1000
CONNECT_POLL = _install_limits["usbConnectPollMs"] / 1000
CONNECT_SETTLE = _install_limits["usbConnectSettleMs"] / 1000
CONNECT_STABLE = _install_limits["usbConnectStableMs"] / 1000
USB_SCAN_END = _install_limits["usbScanEndMs"] / 1000
# The longest command line the tools pass to the device shell in one adb call.
SHELL_LIMIT = 3072
SERIAL = re.compile(r"[A-Za-z0-9._:-]{1,64}")


class DeviceError(Exception):
    pass


def say(message):
    print(message, flush=True)


def quote(text):
    if not re.fullmatch(r"[A-Za-z0-9_./:+=,@%-]+", text):
        raise DeviceError(f"refusing to pass {text!r} to the device shell")
    return text


def usb_serial(serial):
    """Whether adb reaches the serial over USB, not over the network or an emulator."""
    return ":" not in serial and "._adb" not in serial and not serial.startswith("emulator-")


def find_adb(explicit=None):
    for candidate in (explicit, os.environ.get("ADB"), shutil.which("adb")):
        if candidate and Path(candidate).exists():
            return candidate
    raise DeviceError("adb not found; pass --adb or set ADB")


def usb_keeper_script(role=USB_ROLE, uptime="/proc/uptime", pid_file=USB_KEEPER_PID,
                      loader_log=LOADER_LOG, vendor=VENDOR_MARKER, lifetime=KEEPER_SECONDS,
                      hold=KEEPER_HOLD):
    """One device shell line that starts the USB keeper in the background and returns at once.

    The keeper looks once per second of uptime: it reads role and writes usb_device only when it
    reads usb_host or usb_null, then not again for hold seconds. Its lifetime, the hold and the
    looks are measured on uptime, so a sleep that returns at once (the stock BusyBox has none)
    makes it spin on uptime but never read role more often or outlive lifetime. It ignores
    SIGHUP, so it survives the end of the adb session. Once the AWTRIX loader runs in this boot
    without the vendor app, the loader and the daemon keep the port: the keeper stops writing at
    once and ends when that has held for two seconds of uptime, since the loader logs its start
    a moment before a knob start creates the vendor marker. A keeper that still runs is reused."""
    loop = (f"read -r s x <{uptime}; s=${{s%%.*}}; end=$((s + {lifetime})); next=0; last=-1; "
            f"seen=-1; while [ $s -lt $end ]; do if [ $s -ne $last ]; then last=$s; "
            f"if [ -f {loader_log} ] && [ ! -f {vendor} ]; then "
            f"[ $seen -lt 0 ] && seen=$s; [ $s -ge $((seen + 2)) ] && break; "
            f"else seen=-1; r=; read -r r <{role}; "
            f"case $r in usb_host|usb_null) if [ $s -ge $next ]; then "
            f"echo -n usb_device >{role}; next=$((s + {hold})); fi;; esac; fi; fi; "
            f"sleep 1; read -r s x <{uptime}; s=${{s%%.*}}; done; rm -f {pid_file}")
    return (f"k=; [ -f {pid_file} ] && read -r k <{pid_file}; case $k in ''|*[!0-9]*) k=0;; esac; "
            f"if [ $k -gt 0 ] && [ -d /proc/$k ]; then echo awtrix-usb-keeper running $k; "
            f"else trap '' HUP; ( {loop} ) </dev/null >/dev/null 2>&1 & "
            f"echo $! >{pid_file}; echo awtrix-usb-keeper started $!; fi")


USB_KEEPER = usb_keeper_script()
KEEPER_REPLY = re.compile(r"awtrix-usb-keeper (?:started|running) \d+")


class Device:
    """adb for one serial: program is the adb executable or a command prefix."""

    def __init__(self, program, serial):
        if not serial or not SERIAL.fullmatch(serial) or serial.startswith("-"):
            raise DeviceError("an explicit --serial is required")
        self.program = [program] if isinstance(program, (str, os.PathLike)) else list(program)
        self.serial = serial

    def _run(self, args, timeout):
        result = subprocess.run([*self.program, *args], stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=timeout)
        return result.returncode, result.stdout, result.stderr

    def before_command(self):
        """Runs before every adb command for this serial."""

    def adb(self, *args, timeout=120, check=True):
        self.before_command()
        code, out, err = self._run(["-s", self.serial, *args], timeout)
        if check and code:
            raise DeviceError(f"adb {args[0]} failed: {(err or out).strip()}")
        return out.replace("\r", "")

    def quick_shell(self, command, timeout=10):
        """Output of a device shell command, or None when adb fails or does not answer."""
        try:
            code, out, _ = self._run(["-s", self.serial, "shell", command], timeout)
        except (subprocess.TimeoutExpired, OSError):
            return None
        return None if code else out.replace("\r", "")

    def state(self):
        """What `adb devices` says about this serial (device, offline, ...), None when absent."""
        listing = self._run(["devices"], 30)[1].replace("\r", "")
        states = [line.split("\t", 1)[1].strip() for line in listing.splitlines()
                  if line.split("\t", 1)[0] == self.serial and "\t" in line]
        if len(states) > 1:
            raise DeviceError(f"{len(states)} devices report the serial {self.serial}; every "
                              "TC002 reports the same serial, so attach only one at a time")
        return states[0] if states else None

    def require_connected(self, usb=False):
        state = self.state()
        if state != "device":
            raise DeviceError(f"{self.serial} is not connected and authorized "
                              f"(adb devices: {state or 'absent'})")
        if usb and not usb_serial(self.serial):
            raise DeviceError("flashing needs the USB transport (and its power); "
                              f"{self.serial} is a network or emulator transport")

    def shell(self, command, timeout=120, check=True):
        """(exit status, output) of a device shell command. The stock adbd reports no exit status,
        so the command echoes it after its output."""
        output = self.adb("shell", command + "; echo __AWTRIX_RC=$?", timeout=timeout)
        body, marker, status = output.rpartition("__AWTRIX_RC=")
        if not marker:
            raise DeviceError(f"no exit status from device command: {command}")
        code = int(status.strip() or "255")
        if check and code:
            raise DeviceError(f"device command failed ({code}): {command}\n{body.strip()}")
        return code, body

    def text(self, command, timeout=120):
        return self.shell(command, timeout=timeout)[1].strip()

    def push(self, local, remote, timeout=600):
        self.adb("push", str(local), remote, timeout=timeout)

    def pull(self, remote, local, timeout=120):
        self.adb("pull", remote, str(local), timeout=timeout)

    def reboot(self):
        self.adb("shell", "sync; reboot", timeout=30, check=False)

    def getprop(self, name):
        return self.text(f"getprop {quote(name)}")

    def exists(self, path, kind="e"):
        return self.shell(f"[ -{kind} {quote(path)} ]", check=False)[0] == 0


def start_usb_keeper(device):
    """Starts the USB keeper with one adb shell command. Returns its reply, or None when USB ADB
    went away first; raises when the device answered without starting it."""
    reply = (device.quick_shell(USB_KEEPER) or "").strip()
    match = KEEPER_REPLY.search(reply)
    if reply and not match:
        raise DeviceError(f"the USB keeper did not start on {device.serial}: {reply[:200]}")
    return match[0] if match else None


def device_uptime(device):
    try:
        return float((device.quick_shell("cat /proc/uptime") or "").split()[0])
    except (ValueError, IndexError):
        return None


def wait_for_stable_usb(device):
    """Returns once adb has listed the serial as a device for CONNECT_STABLE s in a row and the
    kernel's USB scan, which switches the port to host mode, is over (uptime USB_SCAN_END s)."""
    deadline = time.monotonic() + CONNECT_SETTLE
    since = ready = None
    while time.monotonic() < deadline:
        now = time.monotonic()
        if device.state() != "device":
            since = ready = None
        else:
            since = now if since is None else since
            if ready is None and now - since >= CONNECT_STABLE:
                uptime = device_uptime(device)
                if uptime is None:
                    since = None
                else:
                    ready = now + max(0.0, USB_SCAN_END - uptime)
            if ready is not None and now >= ready:
                say(f"{device.serial} stays on USB ADB")
                return
        time.sleep(CONNECT_POLL)
    raise DeviceError(f"USB ADB of {device.serial} did not come back within {CONNECT_SETTLE} s; "
                      "switch the clock off and on again and run connect")


def connect(device, keep_present=True):
    """Brings USB ADB up and keeps it. This polls `adb devices` for the serial every CONNECT_POLL s
    while the user switches the clock off and on, starts the USB keeper the moment the serial is a
    device, then waits until USB ADB is back and stays. keep_present=False returns at once when
    the serial is already a device. A network serial is left to adb."""
    if not usb_serial(device.serial):
        device.require_connected()
        return
    state = device.state()
    if state == "device" and not keep_present:
        return
    if state != "device":
        say(f"{device.serial} is not on USB ADB ({state or 'absent'}): switch the clock off and on "
            f"again, with its USB cable in this PC. Waiting {CONNECT_WAIT // 60} min. Attach only "
            "one TC002: all report the same serial.")
    deadline = time.monotonic() + CONNECT_WAIT
    missed = False
    while True:
        if state == "device":
            reply = start_usb_keeper(device)
            if reply:
                say(f"USB ADB of {device.serial} is up: {reply}")
                break
            if not missed:
                say(f"USB ADB of {device.serial} went away before the USB keeper started: switch "
                    "the clock off and on again")
                missed = True
        if time.monotonic() >= deadline:
            raise DeviceError(f"{device.serial} did not appear on USB ADB within {CONNECT_WAIT} s; "
                              "check that the cable carries data, then run the command again")
        time.sleep(CONNECT_POLL)
        state = device.state()
    wait_for_stable_usb(device)
