#!/usr/bin/env python3
"""Install AWTRIX NG on a Ulanzi TC002 over USB ADB (Windows, Linux or macOS host).

  tc002_install.py --serial SERIAL connect
  tc002_install.py --serial SERIAL status [--bundle DIR]
  tc002_install.py --serial SERIAL deploy --bundle DIR
  tc002_install.py --serial SERIAL secure-vendor [--backup-root DIR] [--dry-run]
  tc002_install.py build-res --backup DIR --bundle DIR --output DIR [--loader FILE]
  tc002_install.py --serial SERIAL flash-loader --bundle DIR --res-dir DIR [--backup-root DIR]
                                               [--yes]
  tc002_install.py --serial SERIAL restore-stock --bundle DIR --res-dir DIR [--remove-data] [--yes]
  tc002_install.py package --bundle DIR --out FILE
  tc002_install.py --serial SERIAL backup --output DIR [--timeout SECONDS]
  tc002_install.py verify-backup DIR

deploy writes the release image of a bundle (build_bundle.sh) into the release slot of res, behind
the res squashfs, on a clock whose res holds the loader and loop.ko (flash-loader). The daemon
stops, the image passes through /tmp and awtrix-tc002-flash writes it: it erases the slot's
header first and writes it last, once the image reads back as its SHA-256, so an interrupted
write leaves no release and the loader shows USB RECOVERY until deploy runs again. deploy then
records the release in state/update-state.json, holding the web update's lock meanwhile, removes
what the layout before the release slot kept in /data/awtrix-ng (releases/, current) and restarts
zkswe, so the loader mounts the new release. Every deployed bundle is kept in ~/TC002-releases
(AWTRIX_TC002_RELEASES); deploying one again is the rollback. package wraps the bundle's release
image as a web update package (Linux/WSL: the tool runs through WSL on Windows).

secure-vendor backs up the vendor app's settings and Wi-Fi files into a private directory
(default ~/TC002-private), then removes its cloud tokens and Wi-Fi credentials and turns its
Wi-Fi off; flash-loader runs it first. flash-loader writes awtrix-res.img (build-res) into the res
partition so /bin/zkgui starts the loader, and the bundle's release into the release slot;
restore-stock writes the verified original back over the whole partition, the slot included.
Both refuse a res partition that holds neither the backed-up stock content nor an image made
here, need a USB connection and do nothing without --yes. build-res runs the shared JavaScript
image engine through build_res_image.py. The device helper (bin/awtrix-tc002-flash) is used from
/tmp/awtrix-install and removed afterwards. adb pushes files only into /tmp/awtrix-stage; the
device copies them on and the directory is removed however the command ends.

backup copies every NOR partition with adb pull into a new private directory outside the checkout
(build-res takes the stock res from it); verify-backup checks one offline. A raw flash backup holds
passwords, keys and identifiers: keep it private and encrypted.

connect, and every device command whose serial adb does not list, asks to switch the clock off
and on, watches `adb devices` for the serial and starts a USB keeper on the device the moment it
shows up: a new TC002 offers USB ADB for about 3 s after power-on only. All TC002 units report
the serial 0123456789ABCDEF, so attach only one at a time.
"""
import argparse
import contextlib
import datetime
import hashlib
import json
import base64
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "lib"))
import backup as flash_backup  # noqa: E402
import bundle as bundle_format  # noqa: E402
import tc002_device  # noqa: E402
import js_engine  # noqa: E402
from constants import PRODUCTION_TARGET  # noqa: E402
from tc002_device import (LOADER_LOG, VENDOR_MARKER, DeviceError,  # noqa: E402
                          connect, find_adb, quote, say)

MODEL = "Zkswe_SSD21X_SPINOR"
RES_DEVICE = "/dev/mtd/mtd3"
DATA_DIR = "/data/awtrix-ng"
STATE_DIR = DATA_DIR + "/state"
RES_RECORD = STATE_DIR + "/res-image.json"
DEVICE_WORK = "/tmp/awtrix-install"
DEVICE_HELPER = DEVICE_WORK + "/awtrix-tc002-flash"
DEVICE_IMAGE = DEVICE_WORK + "/release.img"
DEVICE_STAGE = "/tmp/awtrix-stage"
RELEASE_MOUNT = "/tmp/awtrix-release"
RELEASE_DAEMON = RELEASE_MOUNT + "/bin/awtrix-tc002d"
RELEASE_LISTING = RELEASE_MOUNT + "/manifest.json"
RESCUE_MARKER = "/tmp/awtrix-loader.rescue"
BOOT_COUNTER = STATE_DIR + "/boot-attempts"
TMP_BOOT_COUNTER = "/tmp/awtrix-loader.attempts"
COUNTERS = (BOOT_COUNTER, TMP_BOOT_COUNTER)
# Additional process roots and cleanup paths.
LEGACY_RELEASES = DATA_DIR + "/releases"
LEGACY_FACTORY = "/res/awtrix-ng/factory"
LEGACY = (LEGACY_RELEASES, DATA_DIR + "/current", STATE_DIR + "/factory-attempts",
          "/tmp/awtrix-loader.factory-attempts")
UPDATE_STATE = STATE_DIR + "/update-state.json"
UPDATE_STATE_STAGED = STATE_DIR + "/.update-state.deploy"
UPDATE_WORK = "/tmp/awtrix-update"
LOCK_TOKEN = DEVICE_WORK + "/lock.token"
LOCK_SECONDS = 1800
LOCK_TRIES = 5
TOKEN_STALE_SECONDS = 90
TOKEN_REFRESH_SECONDS = 10
PACKAGE_NAME = "awtrix-ng-tc002.awup"
PACKAGE_LIMIT = 8 << 20
VENDOR_SETTINGS = "/data/setting.ini"
VENDOR_SUPPLICANT = "/data/misc/wifi/wpa_supplicant.conf"
VENDOR_HOSTAPD = "/data/misc/wifi/hostapd.conf"
SUPPLICANT_TEMPLATE = "/etc/wifi/wpa_supplicant.conf"
SUPPLICANT_MODE = 0o660
VENDOR_OFFLINE = {"persist.wifi.on": "0", "persist.softap.on": "0"}
PERSIST_DIR = "/data/property"
PRIVATE_ROOT = Path.home() / "TC002-private"
REPO = HERE.parents[2]
LOADER_IN_RES = "/res/lib/libawtrix-loader.so"
LOOP_IN_RES = "/res/awtrix-ng/loop.ko"
ARCHIVE_ROOT = Path(os.environ.get("AWTRIX_TC002_RELEASES") or Path.home() / "TC002-releases")
ARCHIVE_KEEP = 5
STOP_WAIT = 30
# The daemon's first start in a boot switches ADB over TCP off, and init restarts adbd for that:
# USB ADB drops for about a second right after the release starts.
ADB_RESTART_SECONDS = 10
TMP_RESERVE = 2 << 20
STATE_IDENTIFIER = re.compile(r"[A-Za-z0-9._:-]{1,64}")
EXECUTABLE = re.compile(r" -> (/\S+)")
AWTRIX_EXECUTABLES = (RELEASE_MOUNT + "/", LEGACY_RELEASES + "/", LEGACY_FACTORY + "/")
PACKAGE_TOOL = HERE.parents[1] / "update" / "package.py"
ADB_LOST = re.compile(r"device (?:'[^']*' )?(?:not found|offline)|devices: offline|"
                      r"no devices/emulators found|error: closed|protocol fault", re.I)


class InstallError(DeviceError):
    pass


ADB_ERRORS = (DeviceError, subprocess.TimeoutExpired, OSError)


def note_cleanup_failure(error, what, failure):
    error.cleanup_failures = [*getattr(error, "cleanup_failures", []),
                              f"{what} failed as well: {failure}"]
    return error


def undo(error, what, action):
    """Runs action after error; when action fails too, error stays the one reported."""
    try:
        action()
    except ADB_ERRORS as failure:
        note_cleanup_failure(error, what, failure)


@contextlib.contextmanager
def cleanup(what, action):
    """Runs action however the block ends, without letting its failure hide the block's error."""
    try:
        yield
    except BaseException as error:
        undo(error, what, action)
        raise
    action()


def error_chain(error):
    chain = []
    while error is not None and all(error is not seen for seen in chain):
        chain.append(error)
        error = error.__cause__ or error.__context__
    return chain


def cleanup_failures(error):
    return [note for link in reversed(error_chain(error))
            for note in getattr(link, "cleanup_failures", [])]


def usb_lost(error):
    return any(isinstance(link, subprocess.TimeoutExpired) or ADB_LOST.search(str(link))
               for link in error_chain(error)) or any(map(ADB_LOST.search, cleanup_failures(error)))


def usb_lost_hint(command):
    first = "USB ADB stopped answering; the device itself keeps running."
    if command in ("flash-loader", "restore-stock"):
        return (f"{first} Do not reboot it: res may be partly written. Unplug and replug the USB "
                f"cable and run {command} again (or restore-stock).")
    if command == "deploy":
        return (f"{first} Unplug and replug the USB cable and run deploy again. When the release "
                "slot was being written the clock shows USB RECOVERY: it keeps USB ADB for that.")
    return f"{first} Unplug and replug the USB cable and run {command} again."


def step(message):
    print(f"==> {message}", flush=True)


def sha256_file(path, length=None):
    try:
        return bundle_format.sha256_file(path, length)
    except bundle_format.BundleError as error:
        raise InstallError(str(error)) from error


def bundle_files(manifest):
    """Every file of the bundle: the installed ones and the host-only ones."""
    return manifest["files"] + manifest.get("hostOnly", [])


def bundle_counter(manifest):
    """The bundle's update counter, which the device's update state records."""
    counter = manifest.get("counter")
    if counter is None:
        raise InstallError("the bundle carries no update counter; rebuild it with build_bundle.sh")
    if not isinstance(counter, int) or isinstance(counter, bool) or \
            not 0 < counter <= bundle_format.MAX_COUNTER:
        raise InstallError("the bundle manifest's counter is not a positive 63-bit integer")
    if not STATE_IDENTIFIER.fullmatch(manifest["release"]):
        raise InstallError(f"release name {manifest['release']!r} cannot be recorded as an update "
                           "(at most 64 of A-Z a-z 0-9 . _ : -)")
    return counter


def bundle_image(bundle_dir, manifest):
    """The bundle's release image (release.img): path, size and SHA-256."""
    image = manifest.get("image")
    if not image:
        raise InstallError(f"the bundle has no release image ({bundle_format.IMAGE}); rebuild it "
                           "with build_bundle.sh")
    return {"path": Path(bundle_dir) / bundle_format.IMAGE, "size": image["size"],
            "sha256": image["sha256"]}


def update_state_document(release, counter, image_sha256):
    """state/update-state.json in UpdateState's format: the release is confirmed and its counter
    accepted; payloadSha256 names its release image, the payload a web update carries."""
    return json.dumps({
        "schema": 2, "state": "confirmed", "acceptedCounter": counter,
        "current": {"counter": counter, "payloadSha256": image_sha256,
                    "release": release, "target": PRODUCTION_TARGET},
        "candidate": None, "lease": None, "failure": ""}, separators=(",", ":"))


def slot_holds(slot, manifest, image):
    """Whether slot-info --verify found this bundle's release, intact, in the release slot."""
    return (slot.get("slot") == "verified" and slot.get("release") == manifest["release"] and
            slot.get("counter") == manifest.get("counter") and slot.get("sha256") == image["sha256"] and
            slot.get("imageBytes") == image["size"])


def describe_slot(slot):
    if "release" in slot:
        return f"{slot['release']}, {slot['imageBytes']} of {slot['capacity']} bytes, {slot['slot']}"
    return f"{slot.get('slot') or 'unreadable'}, {slot.get('capacity', 0)} bytes"


def parse_results(text, command):
    results = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                value = json.loads(line)
            except ValueError:
                continue
            if value.get("command") == command:
                results.append(value)
    return results


def parse_awtrix_executables(text):
    """The executables of AWTRIX processes in an `ls -l /proc/[0-9]*/exe` listing: those of the
    mounted release and of releases the layout before the release slot installed."""
    return {m[1] for m in EXECUTABLE.finditer(text) if m[1].startswith(AWTRIX_EXECUTABLES)}


def windows_to_wsl(path):
    resolved = Path(path).resolve()
    drive = resolved.drive
    if not re.fullmatch(r"[A-Za-z]:", drive):
        raise InstallError(f"{path} is not on a local drive WSL can reach")
    rest = PurePosixPath(*resolved.parts[1:])
    return f"/mnt/{drive[0].lower()}/{rest.as_posix()}"


def token_write(path):
    """Device shell command that writes the deploy token: `deploy` and the device's uptime. The
    lock holder and the loader judge the age of a token they first meet by that uptime, since
    the wall clock may still be at 1970 and jump when SNTP sets it."""
    return f"read -r up rest </proc/uptime && echo deploy $up > {path}"


class Device(tc002_device.Device):
    """The clock as the installer drives it: while deploy holds the web update lock every adb
    command first rewrites the deploy token when it is due, and helper() runs the device helper
    from DEVICE_WORK."""

    def __init__(self, program, serial):
        super().__init__(program, serial)
        self.token, self.token_written = None, 0.0

    def keep_token(self, path):
        """From now on every adb command first rewrites path when it is older than
        TOKEN_REFRESH_SECONDS, so it stays fresh while this tool talks to the device."""
        self.token, self.token_written = path, time.monotonic()

    def drop_token(self):
        self.token = None

    def before_command(self):
        if not self.token or time.monotonic() - self.token_written < TOKEN_REFRESH_SECONDS:
            return
        self.token_written = time.monotonic()
        with contextlib.suppress(subprocess.TimeoutExpired, OSError):
            self._run(["-s", self.serial, "shell",
                       f"[ -f {self.token} ] && {token_write(self.token)}"], 30)

    def run(self, description, command, timeout=120):
        step(description)
        return self.shell(command, timeout=timeout)[1]

    def helper(self, *args, timeout=600, check=True):
        command = " ".join([DEVICE_HELPER] + [quote(str(a)) for a in args])
        code, body = self.shell(command, timeout=timeout, check=False)
        results = parse_results(body, args[0])
        if check and (code or not results or any(r.get("result") != "ok" for r in results)):
            raise InstallError(f"awtrix-tc002-flash {args[0]} failed ({code}): {body.strip()}")
        return code, results


def check_model(device):
    model = device.getprop("ro.product.model")
    if model != MODEL:
        raise InstallError(f"device model is {model!r}, not {MODEL!r}")


def load_bundle(path):
    manifest = bundle_format.verify(Path(path))
    if not bundle_format.valid_release_name(manifest["release"]):
        raise InstallError("bundle release name is not usable as a directory name")
    return manifest


def install_helper(device, bundle_dir, manifest):
    entry = next((e for e in bundle_files(manifest) if e["path"] == "bin/awtrix-tc002-flash"),
                 None)
    if not entry:
        raise InstallError("the bundle has no bin/awtrix-tc002-flash")
    device.shell(f"rm -rf {DEVICE_WORK}; mkdir -p {DEVICE_WORK} && chmod 700 {DEVICE_WORK}")
    device.push(Path(bundle_dir) / entry["path"], DEVICE_HELPER)
    device.shell(f"chmod 700 {DEVICE_HELPER}")
    _, results = device.helper("sha256", DEVICE_HELPER)
    if results[0]["sha256"] != entry["sha256"]:
        raise InstallError("the helper arrived corrupted on the device")


def across_adb_restart(action):
    """action(), repeated while USB ADB comes back from the adbd restart of a daemon's first start."""
    for attempt in range(ADB_RESTART_SECONDS):
        try:
            return action()
        except ADB_ERRORS:
            if attempt + 1 == ADB_RESTART_SECONDS:
                raise
            time.sleep(1)


def remove_helper(device):
    device.drop_token()
    across_adb_restart(lambda: device.shell(f"rm -rf {DEVICE_WORK}", check=False))


@contextlib.contextmanager
def device_stage(device):
    """An empty private directory in /tmp for pushed files; removed however the block ends."""
    device.shell(f"rm -rf {DEVICE_STAGE}; mkdir -p {DEVICE_STAGE} && chmod 700 {DEVICE_STAGE}")
    with cleanup(f"removing {DEVICE_STAGE}",
                 lambda: device.shell(f"rm -rf {DEVICE_STAGE}", check=False)):
        yield DEVICE_STAGE


def statfs(device, path):
    return device.helper("statfs", path)[1][0]


def data_free(device):
    return statfs(device, "/data")["free_bytes"]


def running_release(device):
    """The release the loader mounted, named by its manifest.json, or None."""
    if not device.exists(RELEASE_LISTING, "f"):
        return None
    try:
        name = json.loads(device.text(f"cat {RELEASE_LISTING}")).get("release")
    except (DeviceError, ValueError, AttributeError):
        return None
    return name if isinstance(name, str) and bundle_format.valid_release_name(name) else None


def awtrix_executables(device):
    return parse_awtrix_executables(device.shell("ls -l /proc/[0-9]*/exe", check=False)[1])


def file_facts(device, paths):
    """Size, SHA-256, mode and numeric owner of each device file that could be read."""
    if not paths:
        return {}
    code, results = device.helper("sha256", *paths, check=False)
    return {r["path"]: r for r in results if r.get("result") == "ok"}


def archived_bundle(release):
    """This PC's copy of `release` when it is complete and intact."""
    directory = ARCHIVE_ROOT / release
    try:
        return directory if bundle_format.verify(directory)["release"] == release else None
    except (bundle_format.BundleError, OSError, ValueError, KeyError):
        return None


def archive_order(directory):
    try:
        return int((ARCHIVE_ROOT / f"{directory.name}.order").read_text())
    except (OSError, ValueError):
        return 0


def mark_archive_used(release):
    others = [archive_order(d) for d in ARCHIVE_ROOT.iterdir() if d.is_dir()]
    (ARCHIVE_ROOT / f"{release}.order").write_text(f"{max(others, default=0) + 1}\n")


def store_archive(partial, release):
    target = ARCHIVE_ROOT / release
    shutil.rmtree(target, ignore_errors=True)
    partial.rename(target)
    mark_archive_used(release)
    kept = sorted((d for d in ARCHIVE_ROOT.iterdir() if d.is_dir() and d != target and
                   not d.name.endswith(".partial") and archive_order(d) > 0),
                  key=lambda d: (archive_order(d), d.name), reverse=True)
    for old in kept[ARCHIVE_KEEP - 1:]:
        shutil.rmtree(old, ignore_errors=True)
        (ARCHIVE_ROOT / f"{old.name}.order").unlink(missing_ok=True)
    return target


def archive_bundle(bundle_dir, manifest):
    """Keeps a copy of a deployed bundle on this PC; deploying it again is the rollback."""
    release = manifest["release"]
    if archived_bundle(release):
        return ARCHIVE_ROOT / release
    partial = ARCHIVE_ROOT / f"{release}.partial"
    shutil.rmtree(partial, ignore_errors=True)
    for entry in bundle_files(manifest):
        (partial / entry["path"]).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Path(bundle_dir) / entry["path"], partial / entry["path"])
    shutil.copyfile(Path(bundle_dir) / "manifest.json", partial / "manifest.json")
    if "image" in manifest:
        shutil.copyfile(Path(bundle_dir) / bundle_format.IMAGE, partial / bundle_format.IMAGE)
    bundle_format.verify(partial)
    return store_archive(partial, release)


def wait_for(condition, seconds):
    for _ in range(seconds):
        if condition():
            return True
        time.sleep(1)
    return condition()


def stop_daemon(device):
    """Ends every AWTRIX process: awtrix-tc002d over its control socket, so the runtime saves and
    ends cleanly; ctl.stop zkswe (a SIGKILL of the process group) only when that does not end it."""
    running = awtrix_executables(device)
    if not running:
        return
    step("stop the AWTRIX daemon")
    daemon = next((e for e in sorted(running) if e.endswith("/bin/awtrix-tc002d")), None)
    if daemon:
        device.shell(f"{quote(daemon)} ctl stop", check=False)
    if wait_for(lambda: not awtrix_executables(device), STOP_WAIT):
        return
    if device.getprop("init.svc.zkswe") == "running":
        device.run("the daemon did not stop; stop the zkswe service", "setprop ctl.stop zkswe")
        if wait_for(lambda: not awtrix_executables(device), 10):
            return
    raise InstallError(f"AWTRIX still runs ({', '.join(sorted(awtrix_executables(device)))}); "
                       "nothing was changed")


def end_deploy_hold(device):
    """Removes the deploy token: the loader stops waiting and the web update lock is free."""
    device.drop_token()
    device.shell(f"rm -f {LOCK_TOKEN}", check=False)


def restart_zkswe(device):
    """Stops zkswe (a loader holding for deploy with it), ends the hold, starts zkswe again."""
    if device.getprop("init.svc.zkswe") == "running":
        device.shell("setprop ctl.stop zkswe")
        wait_for(lambda: device.getprop("init.svc.zkswe") != "running", 10)
    end_deploy_hold(device)
    device.shell("setprop ctl.start zkswe")


def start_release(device, release):
    """Starts zkswe again, so the loader mounts the release slot and starts the release."""
    step("restart the zkswe service; the loader mounts the release slot and starts it")
    restart_zkswe(device)

    def running():
        try:
            return running_release(device) == release and RELEASE_DAEMON in awtrix_executables(device)
        except ADB_ERRORS:
            return False
    if wait_for(running, STOP_WAIT):
        say(f"awtrix-tc002d runs {release}")
    else:
        say(f"awtrix-tc002d did not start {release} within {STOP_WAIT} s; see status")


def verify_mounted(device, manifest):
    """Reads every file of the release through the mount and compares it with the bundle: a block
    cache that still held the slot's former image would show here."""
    step("check the mounted release against the bundle")
    paths = [f"{RELEASE_MOUNT}/{entry['path']}" for entry in manifest["files"]]
    found = across_adb_restart(lambda: file_facts(device, paths))
    wrong = [entry["path"] for entry in manifest["files"]
             if found.get(f"{RELEASE_MOUNT}/{entry['path']}", {}).get("sha256") != entry["sha256"]]
    if wrong:
        raise InstallError(f"the mounted release differs from the bundle in {', '.join(wrong[:3])}"
                           f"{' and more' if len(wrong) > 3 else ''}: the release slot holds the "
                           "bundle, but the clock read older data; switch it off and on")
    say(f"all {len(paths)} files of the mounted release match the bundle")


def push_image(device, image):
    """Copies the release image to DEVICE_IMAGE and checks it there."""
    free = statfs(device, "/tmp")["free_bytes"]
    if free < image["size"] + TMP_RESERVE:
        raise InstallError(f"/tmp has {free} bytes free, the release image needs "
                           f"{image['size'] + TMP_RESERVE} with the clock's reserve")
    step(f"copy the release image ({image['size']} bytes) to {DEVICE_IMAGE}")
    device.push(image["path"], DEVICE_IMAGE)
    facts = file_facts(device, [DEVICE_IMAGE]).get(DEVICE_IMAGE, {})
    if (facts.get("size"), facts.get("sha256")) != (image["size"], image["sha256"]):
        raise InstallError("the release image arrived damaged on the clock")


class SlotWriteError(InstallError):
    """awtrix-tc002-flash write-slot failed; modified says whether it erased anything (None when
    unknown)."""

    def __init__(self, message, modified):
        super().__init__(message)
        self.modified = modified


def write_slot(device, manifest, image):
    step(f"write {manifest['release']} into the release slot")
    code, results = device.helper("write-slot", DEVICE_IMAGE, "--length", image["size"],
                                  "--sha256", image["sha256"], "--release", manifest["release"],
                                  "--counter", bundle_counter(manifest), "--mount", RELEASE_MOUNT,
                                  check=False)
    result = results[-1] if results else {}
    if code or result.get("result") != "ok":
        raise SlotWriteError(result.get("message") or f"the device helper did not answer ({code})",
                             result.get("modified"))
    say(f"release slot written: {result['written']} blocks, {result['skipped']} unchanged")
    if result.get("blockCache", "dropped") != "dropped":
        say(f"the helper could not drop the block cache of the slot ({result['blockCache']})")


def install_release(device, manifest, image):
    """Writes the bundle's release into the release slot unless the slot holds it already; the
    daemon stops first. A failure before the helper erased anything leaves the release that was
    there; zkswe then starts it again. Returns whether the slot was written."""
    slot = device.helper("slot-info", "--verify")[1][0]
    say(f"release slot: {describe_slot(slot)}")
    if image["size"] > slot["capacity"]:
        raise InstallError(f"the release image takes {image['size']} bytes, the release slot holds "
                           f"{slot['capacity']}; nothing was changed")
    if slot_holds(slot, manifest, image):
        say("the release slot already holds this release; not writing it again")
        return False
    stop_daemon(device)
    try:
        push_image(device, image)
        write_slot(device, manifest, image)
    except ADB_ERRORS as failure:
        if getattr(failure, "modified", False) is False:
            error = InstallError(f"{failure}; the release slot was not touched, so the clock starts "
                                 "the release it holds again")
        else:
            error = InstallError(f"writing the release slot failed: {failure}. The slot holds no "
                                 "complete release: the clock shows USB RECOVERY until deploy runs "
                                 "again")
        error.__cause__ = failure
        undo(error, f"removing {DEVICE_IMAGE}", lambda: device.shell(f"rm -f {DEVICE_IMAGE}", check=False))
        undo(error, "restarting zkswe", lambda: restart_zkswe(device))
        raise error
    device.shell(f"rm -f {DEVICE_IMAGE}")
    return True


def take_update_lock(device):
    """Holds the lock of the web update (/tmp/awtrix-update/lock) while deploy runs, so no web
    update starts or installs meanwhile. The device helper keeps the lock while LOCK_TOKEN exists
    and was written within TOKEN_STALE_SECONDS; deploy rewrites the token before its adb commands,
    so a deploy that loses USB frees the lock within that time. The same token keeps the loader
    from starting a release meanwhile."""
    device.shell(token_write(LOCK_TOKEN))
    device.keep_token(LOCK_TOKEN)
    for attempt in range(LOCK_TRIES):
        _, results = device.helper("lock", UPDATE_WORK, LOCK_TOKEN, LOCK_SECONDS,
                                   TOKEN_STALE_SECONDS, check=False)
        result = results[-1] if results else {}
        if result.get("result") == "ok":
            say(f"holding the web update lock {UPDATE_WORK}/lock")
            return
        if result.get("stage") != "busy":
            raise InstallError("cannot take the web update lock "
                               f"({result.get('message') or 'the device helper did not answer'}); "
                               "nothing was changed")
        if attempt + 1 < LOCK_TRIES:
            time.sleep(1)
    raise InstallError("a web update is being uploaded or installed (it holds "
                       f"{UPDATE_WORK}/lock); wait until the clock has restarted, then deploy "
                       "again; nothing was changed")


def record_update_state(device, release, counter, image_sha256):
    """state/update-state.json names the installed release as confirmed and its counter as
    accepted, so the web update installs only newer packages."""
    document = update_state_document(release, counter, image_sha256)
    device.run(f"record {release} as confirmed, accepted counter {counter}, in {UPDATE_STATE}",
               f"umask 077; echo '{document}' > {UPDATE_STATE_STAGED} && "
               f"chown 0:0 {UPDATE_STATE_STAGED} && chmod 600 {UPDATE_STATE_STAGED} && sync && "
               f"mv -f {UPDATE_STATE_STAGED} {UPDATE_STATE} && sync")
    if device.text(f"cat {UPDATE_STATE}") != document:
        raise InstallError(f"{UPDATE_STATE} does not read back as written")


def finish_install(device, manifest, image, markers):
    """What follows a release in the slot: the boot attempt counter and what the layout before the
    release slot kept in /data go, with markers also this boot's vendor and rescue markers, and
    the update state records the release."""
    removed = COUNTERS + LEGACY + ((VENDOR_MARKER, RESCUE_MARKER) if markers else ())
    device.run("clear the boot attempt counter" +
               (" and this boot's vendor and rescue markers" if markers else "") +
               "; remove the releases the layout before the release slot kept in /data",
               f"mkdir -p {STATE_DIR}; chmod 755 {DATA_DIR}; chmod 700 {STATE_DIR}; "
               f"rm -rf {' '.join(removed)}; sync")
    record_update_state(device, manifest["release"], bundle_counter(manifest), image["sha256"])


def deploy(device, bundle_dir):
    manifest = load_bundle(bundle_dir)
    bundle_counter(manifest)
    image = bundle_image(bundle_dir, manifest)
    device.require_connected()
    check_model(device)
    if not device.exists(LOADER_IN_RES, "f") or not device.exists(LOOP_IN_RES, "f"):
        raise InstallError("res holds no AWTRIX loader with loop.ko: run flash-loader first; "
                           "nothing was changed")
    with cleanup(f"removing {DEVICE_WORK}", lambda: remove_helper(device)):
        install_helper(device, bundle_dir, manifest)
        take_update_lock(device)
        install(device, bundle_dir, manifest, image)


def install(device, bundle_dir, manifest, image):
    name = manifest["release"]
    if archived_bundle(name):
        mark_archive_used(name)
    running = running_release(device)
    say(f"bundle {name} ({manifest['version']}), release image {image['size']} bytes; running: "
        f"{running or 'none'}")
    written = install_release(device, manifest, image)
    finish_install(device, manifest, image, markers=True)
    say(f"deployed {name}; /data free {data_free(device)} bytes")
    try:
        say(f"copy for a later rollback: {archive_bundle(bundle_dir, manifest)}")
    except (OSError, bundle_format.BundleError) as error:
        say(f"no copy of {name} kept on this PC: {error}")
    if written or running != name or RELEASE_DAEMON not in awtrix_executables(device):
        start_release(device, name)
    else:
        say(f"awtrix-tc002d already runs {name}")
    verify_mounted(device, manifest)


def private_directory(path):
    root = Path(path).expanduser().resolve()
    if root == REPO or REPO in root.parents:
        raise InstallError(f"{root} is inside the repository; keep the vendor secrets backup "
                           "outside the repository")
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    return root


def new_backup_directory(root):
    base = f"{time.strftime('%Y-%m-%d')}-vendor-secrets-backup"
    for index in range(1, 1000):
        directory = root / (base if index == 1 else f"{base}-{index}")
        try:
            directory.mkdir(mode=0o700)
            return directory
        except FileExistsError:
            continue
    raise InstallError(f"no free backup directory name left in {root}")


def back_up_vendor(snapshot, root):
    backup = new_backup_directory(root)
    record = {"created": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
              "files": {}, "properties": snapshot["properties"]}
    for entry in snapshot["files"]:
        remote, attributes = entry["file"], entry["attributes"]
        if remote not in (VENDOR_SETTINGS, VENDOR_SUPPLICANT, VENDOR_HOSTAPD):
            raise InstallError("unexpected vendor backup file")
        name = PurePosixPath(remote).name
        data = base64.b64decode(entry["bytes"], validate=True)
        with open(backup / name, "xb") as output:
            os.chmod(backup / name, 0o600)
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        record["files"][remote] = {"file": name, "bytes": len(data),
                                   "sha256": hashlib.sha256(data).hexdigest(),
                                   "mode": attributes["mode"],
                                   "owner": f"{attributes['uid']}:{attributes['gid']}"}
    flash_backup.atomic_json(backup / "manifest.json", record)
    flash_backup.sync_directory(root)
    say(f"backed up {', '.join(record['files']) or 'the properties'} to {backup}")
    return str(backup)


def run_engine(device, operation, options, backup_root=None, handlers=None):
    with tempfile.TemporaryDirectory(prefix="awtrix-engine-") as temporary:
        local = Path(temporary) / "transfer.bin"
        def dispatch(action, args):
            if handlers and action in handlers:
                return handlers[action](args)
            if action == "status":
                say(args["message"])
            elif action == "sleep":
                time.sleep(args["ms"] / 1000)
            elif action == "run":
                return device.shell(args["command"], timeout=args["timeoutMs"] / 1000)[1].strip()
            elif action == "exists":
                return device.exists(args["file"], args["kind"])
            elif action == "readFile":
                device.pull(args["file"], local, timeout=args.get("timeoutMs", 120000) / 1000)
                if local.stat().st_size > args["maxBytes"]:
                    raise InstallError("device file exceeds the installation limit")
                return local.read_bytes()
            elif action == "writeFile":
                data = base64.b64decode(args["bytes"], validate=True)
                local.write_bytes(data)
                remote, mode = quote(args["file"]), int(args["mode"])
                folder = remote.rsplit("/", 1)[0]
                with device_stage(device):
                    staged = DEVICE_STAGE + "/engine.bin"
                    device.push(local, staged, timeout=args.get("timeoutMs", 120000) / 1000)
                    device.shell(f"mkdir -p {folder} && chmod 700 {folder} && "
                                 f"mv -f {staged} {remote} && chmod {mode:04o} {remote}")
            elif action == "backup":
                return back_up_vendor(args, backup_root)
            elif action == "helper":
                result = device.helper(args["command"], *args["args"], timeout=args["timeoutMs"] / 1000)[1][-1]
                if "counter" in result:
                    result = dict(result, counter=str(result["counter"]))
                return result
            else:
                raise InstallError("unsupported installation host operation")
        try:
            return js_engine.run(operation, options, dispatch)
        except js_engine.EngineError as error:
            raise InstallError(str(error)) from error


def secure_vendor(device, backup_root=None, apply=True):
    root = private_directory(backup_root or PRIVATE_ROOT)
    device.require_connected(usb=True)
    check_model(device)
    result = run_engine(device, "secure-vendor", {"apply": apply}, root)
    if result["backup"]:
        result["backup"] = Path(result["backup"])
    return result


def load_res_dir(res_dir):
    res_dir = Path(res_dir)
    record = json.loads((res_dir / "res-images.json").read_text())
    if record.get("schema_version") != 1 or record["partition"]["name"] != "res":
        raise InstallError("unsupported res-images.json")
    for key in ("stock", "awtrix"):
        image = res_dir / record[key]["file"]
        if image.stat().st_size != record[key]["bytes"] or sha256_file(image) != record[key]["sha256"]:
            raise InstallError(f"{image} does not match res-images.json")
    if record["stock"]["bytes"] != record["partition"]["size"]:
        raise InstallError("the stock image must cover the whole partition")
    return record


def journal_path(res_dir, serial):
    return Path(res_dir) / f"flash-journal-{re.sub('[^A-Za-z0-9]', '_', serial)}.json"


def read_device_record(device):
    if not device.exists(RES_RECORD, "f"):
        return None
    try:
        record = json.loads(device.text(f"cat {RES_RECORD}"))
        if re.fullmatch(r"[0-9a-f]{64}", record["sha256"]) and int(record["bytes"]) > 0:
            return {"sha256": record["sha256"], "bytes": int(record["bytes"])}
    except (ValueError, KeyError, TypeError):
        pass
    return None


def flash(device, bundle_dir, res_dir, target, yes, reboot, remove_data=False,
          accept_unknown=False, backup_root=None):
    record = load_res_dir(res_dir)
    manifest = load_bundle(bundle_dir)
    image = bundle_image(bundle_dir, manifest) if target == "awtrix" else None
    if image:
        bundle_counter(manifest)
    device.require_connected(usb=True)
    check_model(device)
    root = private_directory(backup_root or PRIVATE_ROOT) if target == "awtrix" else None
    journal = journal_path(res_dir, device.serial)
    def journal_update(args):
        if args["active"]:
            flash_backup.atomic_json(journal, {"target": args["target"], "started": time.time()})
        else:
            journal.unlink()
            flash_backup.sync_directory(journal.parent)
    def deployment(args):
        if not args["slotReady"]:
            install_release(device, manifest, image)
        finish_install(device, manifest, image, markers=False)
    options = {"record": record, "target": target, "yes": yes, "reboot": reboot,
               "removeData": remove_data, "acceptUnknown": accept_unknown,
               "resume": journal.exists(), "previous": read_device_record(device)}
    if image:
        options["release"] = {"release": manifest["release"], "counter": str(manifest["counter"]),
                              "size": image["size"], "sha256": image["sha256"]}
    handlers = {"journal": journal_update, "deploy": deployment,
                "image": lambda args: (Path(res_dir) / record[args["target"]]["file"]).read_bytes()}
    with cleanup(f"removing {DEVICE_WORK}", lambda: remove_helper(device)):
        install_helper(device, bundle_dir, manifest)
        try:
            result = run_engine(device, "flash", options, root, handlers)
        except Exception as error:
            if journal.exists():
                error.recovery_needed = True
            raise
    if result["reboot"]:
        step("reboot the device")
        device.reboot()


def update_state_summary(text):
    try:
        state = json.loads(text)
        current = state.get("current") or {}
        return (f"state {state.get('state')}, accepted counter {state.get('acceptedCounter')}, "
                f"release {current.get('release') or 'none'}"
                f"{', failure ' + state['failure'] if state.get('failure') else ''}")
    except (ValueError, AttributeError, TypeError):
        return "unreadable"


def status(device, bundle_dir):
    device.require_connected()
    say(f"model: {device.getprop('ro.product.model')}")
    say(f"boot id: {device.text('cat /proc/sys/kernel/random/boot_id')}")
    say(f"zkswe: {device.getprop('init.svc.zkswe') or 'unknown'}, "
        f"sys.zkapp.state: {device.getprop('sys.zkapp.state') or 'unset'}")
    say("vendor Wi-Fi: " + ", ".join(f"{name}={device.getprop(name) or 'unset'}"
                                     for name in VENDOR_OFFLINE))
    say(f"mounted release: {running_release(device) or 'none'}; AWTRIX processes: "
        f"{', '.join(sorted(awtrix_executables(device))) or 'none'}")
    if device.exists(LEGACY_RELEASES, "d"):
        say(f"{LEGACY_RELEASES}: {device.text(f'ls {LEGACY_RELEASES}') or 'empty'} (the layout "
            "before the release slot; deploy removes it)")
    if device.exists(UPDATE_STATE, "f"):
        say(f"{UPDATE_STATE}: {update_state_summary(device.text(f'cat {UPDATE_STATE}'))}")
    for counter in COUNTERS:
        say(f"{counter}: {device.text(f'cat {counter}') if device.exists(counter, 'f') else 0}")
    for marker in (VENDOR_MARKER, RESCUE_MARKER):
        if device.exists(marker, "f"):
            say(f"{marker}: {device.text(f'cat {marker}') or 'present'}")
    for log in (STATE_DIR + "/loader.log", LOADER_LOG):
        if device.exists(log, "f"):
            say(f"--- {log}\n{device.text(f'cat {log}')}")
    if bundle_dir:
        manifest = load_bundle(bundle_dir)
        with cleanup(f"removing {DEVICE_WORK}", lambda: remove_helper(device)):
            install_helper(device, bundle_dir, manifest)
            say(f"/data free: {data_free(device)} bytes; /tmp free: "
                f"{statfs(device, '/tmp')['free_bytes']} bytes")
            say(f"release slot: {describe_slot(device.helper('slot-info', '--verify')[1][0])}")
            full = device.helper("hash", RES_DEVICE)[1][0]
            say(f"res sha256 (full partition): {full['sha256']}")
            record = read_device_record(device)
            if record:
                ours = device.helper("hash", RES_DEVICE, record["bytes"])[1][0]["sha256"]
                say(f"flashed image record {record['sha256'][:12]}: "
                    f"{'matches' if ours == record['sha256'] else 'does NOT match'} the partition")


def back_up(device, output, timeout):
    if not 1 <= timeout <= 3600:
        raise InstallError("--timeout must be 1 to 3600 seconds")
    device.require_connected()
    check_model(device)
    step(f"copy every MTD partition into {output}")
    directory = flash_backup.capture(device, output, timeout)
    say(json.dumps(flash_backup.verify(directory), indent=2))


def wsl_command(script, arguments):
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        raise InstallError(f"{Path(script).name} needs WSL on Windows")
    return [wsl, "-d", os.environ.get("AWTRIX_WSL_DISTRO", "Ubuntu"), "--exec", "python3",
            windows_to_wsl(script), *arguments]


def build_res(backup, bundle, output, replace, loader=None):
    script = HERE / "build_res_image.py"
    paths = {"--backup": backup, "--bundle": bundle, "--output": output}
    if loader:
        paths["--loader"] = loader
    arguments = [part for option, path in paths.items() for part in (option, str(path))]
    if replace:
        arguments.append("--replace")
    command = [sys.executable, str(script), *arguments]
    step("build the res images: " + " ".join(shlex.quote(str(c)) for c in command))
    if subprocess.run(command).returncode:
        raise InstallError("build_res_image.py failed")


def package(bundle_dir, out, allow_dirty=False):
    """Wraps the bundle's release image as a web update package with tools/update/package.py: an
    AWUPD003 container. A bundle built from a working tree that differs from its commit is refused
    unless allow_dirty. Runs on Linux; on Windows the command runs itself through WSL."""
    if platform.system() == "Windows":
        command = wsl_command(Path(__file__), ["package", "--bundle", windows_to_wsl(bundle_dir),
                                               "--out", windows_to_wsl(out)]
                              + (["--allow-dirty"] if allow_dirty else []))
        step("build the package in WSL")
        if subprocess.run(command).returncode:
            raise InstallError("package failed in WSL")
        return None
    manifest = load_bundle(bundle_dir)
    if manifest.get("dirty") and not allow_dirty:
        raise InstallError(f"{manifest['release']} was built from a working tree that differs from its "
                           "commit; build the release from a commit, or pass --allow-dirty")
    counter = bundle_counter(manifest)
    image = bundle_image(bundle_dir, manifest)
    out = Path(out)
    if out.exists() or out.is_symlink():
        raise InstallError(f"{out} already exists; remove it or name another file")
    with tempfile.TemporaryDirectory(prefix="awtrix-package-") as work:
        staging = Path(work) / "package"
        staging.mkdir(mode=0o700)
        os.chmod(staging, 0o700)
        built = staging / PACKAGE_NAME
        step(f"package {manifest['release']} (counter {counter}) with {PACKAGE_TOOL}")
        result = subprocess.run([sys.executable, str(PACKAGE_TOOL), "--image", str(image["path"]),
                                 "--release", manifest["release"], "--counter", str(counter),
                                 "--output", str(built)],
                                capture_output=True, text=True, timeout=900)
        if result.returncode:
            raise InstallError(f"{PACKAGE_TOOL.name} failed: "
                               f"{(result.stderr or result.stdout).strip()}")
        try:
            info = json.loads(result.stdout.strip().splitlines()[-1])
        except (ValueError, IndexError):
            raise InstallError(f"{PACKAGE_TOOL.name} reported no result")
        if (info.get("release"), info.get("counter"), info.get("target")) != (
                manifest["release"], counter, PRODUCTION_TARGET):
            raise InstallError(f"{PACKAGE_TOOL.name} packaged {info.get('release')} counter "
                               f"{info.get('counter')} for {info.get('target')}, not "
                               f"{manifest['release']} counter {counter} for {PRODUCTION_TARGET}")
        size = built.stat().st_size
        if size > PACKAGE_LIMIT:
            raise InstallError(f"the package is {size} bytes; the web update takes at most "
                               f"{PACKAGE_LIMIT}")
        digest = sha256_file(built)
        out.parent.mkdir(parents=True, exist_ok=True)
        partial = out.with_name(f".{out.name}.partial")
        shutil.copyfile(built, partial)
        if sha256_file(partial) != digest:
            partial.unlink()
            raise InstallError(f"{out} was not written intact")
        os.replace(partial, out)
    info.update(package=str(out), bytes=size, sha256=digest)
    say(json.dumps(info, indent=2, sort_keys=True))
    return info


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--serial", help="ADB serial of the TC002 (required for device commands)")
    parser.add_argument("--adb", help="adb executable")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("connect", help="bring USB ADB up and keep it (a new TC002 shows it for "
                                        "about 3 s after power-on)")
    show = commands.add_parser("status")
    show.add_argument("--bundle", type=Path, help="also hash res and report free space")
    put = commands.add_parser("deploy")
    put.add_argument("--bundle", type=Path, required=True)
    backup_help = f"private directory for the vendor files backup (default {PRIVATE_ROOT})"
    secure = commands.add_parser("secure-vendor")
    secure.add_argument("--backup-root", type=Path, help=backup_help)
    secure.add_argument("--dry-run", action="store_true", help="only report what would change")
    res = commands.add_parser("build-res")
    res.add_argument("--backup", type=Path, required=True)
    res.add_argument("--bundle", type=Path, required=True,
                     help="its loader and loop.ko go into res; its release image must fit the slot")
    res.add_argument("--loader", type=Path, help="another libawtrix-loader.so than the bundle's")
    res.add_argument("--output", type=Path, required=True)
    res.add_argument("--replace", action="store_true")
    copy = commands.add_parser("backup", help="copy every NOR partition into a new private "
                                              "directory outside the checkout")
    copy.add_argument("--output", type=Path, required=True)
    copy.add_argument("--timeout", type=int, default=300, help="seconds per partition (1-3600)")
    check = commands.add_parser("verify-backup", help="check a backup offline")
    check.add_argument("directory", type=Path)
    pack = commands.add_parser("package")
    pack.add_argument("--bundle", type=Path, required=True)
    pack.add_argument("--out", type=Path, required=True, help=f"new file, e.g. {PACKAGE_NAME}")
    pack.add_argument("--allow-dirty", action="store_true",
                      help="also package a bundle built from a changed working tree")
    for name in ("flash-loader", "restore-stock"):
        sub = commands.add_parser(name)
        sub.add_argument("--bundle", type=Path, required=True,
                         help="the device helper; flash-loader also writes its release")
        sub.add_argument("--res-dir", type=Path, required=True, help="output of build-res")
        sub.add_argument("--yes", action="store_true", help="really write the res partition")
        sub.add_argument("--no-reboot", action="store_true")
        if name == "flash-loader":
            sub.add_argument("--backup-root", type=Path, help=backup_help)
        if name == "restore-stock":
            sub.add_argument("--remove-data", action="store_true",
                             help=f"also delete {DATA_DIR}")
            sub.add_argument("--accept-unknown-res", action="store_true",
                             help="restore even if res holds content this tool does not know")
    args = parser.parse_args(argv)
    try:
        if args.command == "build-res":
            build_res(args.backup, args.bundle, args.output, args.replace, args.loader)
            return 0
        if args.command == "package":
            package(args.bundle, args.out, args.allow_dirty)
            return 0
        if args.command == "verify-backup":
            say(json.dumps(flash_backup.verify(args.directory), indent=2))
            return 0
        device = Device(find_adb(args.adb), args.serial)
        connect(device, keep_present=args.command == "connect")
        if args.command == "status":
            status(device, args.bundle)
        elif args.command == "backup":
            back_up(device, args.output, args.timeout)
        elif args.command == "deploy":
            deploy(device, args.bundle)
        elif args.command == "secure-vendor":
            secure_vendor(device, args.backup_root, apply=not args.dry_run)
        elif args.command == "flash-loader":
            flash(device, args.bundle, args.res_dir, "awtrix", args.yes, not args.no_reboot,
                  backup_root=args.backup_root)
        elif args.command == "restore-stock":
            flash(device, args.bundle, args.res_dir, "stock", args.yes, not args.no_reboot,
                  args.remove_data, args.accept_unknown_res)
    except (DeviceError, bundle_format.BundleError, flash_backup.BackupError, OSError, ValueError,
            KeyError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        for failure in cleanup_failures(error):
            print(f"  {failure}", file=sys.stderr)
        if args.command not in ("build-res", "package", "verify-backup") and usb_lost(error):
            print(usb_lost_hint(args.command), file=sys.stderr)
        elif getattr(error, "recovery_needed", False) and "do NOT reboot" not in str(error):
            print("Do not reboot the clock; repeat the command or run restore-stock.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
