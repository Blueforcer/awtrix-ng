"""End-to-end tests of tc002_install.py against an in-memory TC002 behind a fake adb."""
import contextlib
import hashlib
import io
import json
import os
import random
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bundle as bundle_format  # noqa: E402
import tc002_install as installer  # noqa: E402
import tc002_device  # noqa: E402

SERIAL = "0123456789ABCDEF"
COUNTER = 1790000000
BUNDLE_FILES = (("bin/awtrix-linux", 200000), ("bin/awtrix-tc002d", 50000),
                ("bin/awtrix-tc002-flash", 20000), ("bin/awtrix-tc002-audio-pcm", 14000),
                ("bin/udhcpc", 900), ("bin/dhcp-callback", 700), ("bin/wpa_supplicant", 1200),
                ("share/index.html", 30000), ("share/index.html.gz", 9000),
                ("share/ca-certificates.crt", 6000), ("share/licenses.txt.gz", 3000),
                ("lib/modules/aic8800_bsp.ko", 500), ("lib/modules/aic8800_fdrv.ko", 800),
                ("lib/modules/awtrix_pcm.ko", 400), ("lib/libawtrix-loader.so", 9000),
                ("lib/modules/loop.ko", 700))
IMAGE_BYTES = 150000
SLOT_MAGIC = b"AWSLOT01"


def squashfs_like(data):
    """data with a squashfs superblock that claims all of it, as a release image or res has."""
    data = bytearray(data)
    data[:4] = b"hsqs"
    data[40:48] = len(data).to_bytes(8, "little")
    return bytes(data)


def add_image(root, size=IMAGE_BYTES, dirty=False):
    """A stand-in release image in the bundle, listed the way bundle.py image lists it."""
    manifest = json.loads((root / "manifest.json").read_text())
    image = squashfs_like(random.Random(manifest["release"]).randbytes(size))
    (root / bundle_format.IMAGE).write_bytes(image)
    manifest["image"] = {"size": len(image), "sha256": sha(image)}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return bundle_format.verify(root)


def write_manifest(root, version, commit, counter=None, image_bytes=IMAGE_BYTES, dirty=False):
    """manifest.json of a bundle and, with a counter, its release image."""
    (root / bundle_format.IMAGE).unlink(missing_ok=True)
    manifest = bundle_format.write_manifest(root, version, commit, dirty, counter=counter)
    return add_image(root, image_bytes) if counter is not None else manifest


PARTITION = 8 << 20
ERASE = 1 << 16
SETTINGS = (b"[main]\nmacAddress=AABBCCDDEEFF\ndeviceSn=SN-TEST-0001\nsecretKey=k-secret-1\n"
            b"authToken=t-secret-2\nauthRefreshToken=r-secret-3\ntokenExpireTime=1790000000\n"
            b"refreshTokenExpireTime=1790600000\nwifiSsid=HomeNet\nwifiPwd=p-secret-4\nvolume=5\n"
            b"toolsOrder=1,2,3")
CLEAN_SETTINGS = (b"[main]\nmacAddress=AABBCCDDEEFF\ndeviceSn=SN-TEST-0001\nvolume=5\n"
                  b"toolsOrder=1,2,3")
TEMPLATE = b"ctrl_interface=/dev/socket\nupdate_config=1\n"
SUPPLICANT = TEMPLATE + b'\nnetwork={\n\tssid="HomeNet"\n\tpsk="p-secret-4"\n}\n'
HOSTAPD = b"interface=wlan1\nssid=U-Clock\nwpa_passphrase=h-secret-5\n"
VALUES = ("AABBCCDDEEFF", "SN-TEST-0001", "k-secret-1", "t-secret-2", "r-secret-3", "HomeNet",
          "p-secret-4", "h-secret-5", "1790000000")


def sha(data):
    return hashlib.sha256(data).hexdigest()


class FakeTc002:
    """Just enough of the stock shell, adbd and awtrix-tc002-flash for the installer."""

    def __init__(self, stock, data_capacity=8 << 20, tmp_capacity=16 << 20):
        self.files, self.modes, self.links, self.owners = {}, {}, {}, {}
        self.dirs = {"/", "/data", "/tmp", "/dev", "/dev/mtd", "/proc"}
        self.props = {"ro.product.model": installer.MODEL, "init.svc.zkswe": "stopped",
                      "init.svc.hciattach": "stopped"}
        self.mtd = bytearray(stock)
        self.capacity = {"/data": data_capacity, "/tmp": tmp_capacity}
        self.pushes, self.pulls, self.commands, self.rebooted = [], [], [], False
        self.copies, self.stage_peak = [], 0
        self.corrupt_copy = None
        self.lost, self.lose_usb_on_push = False, None
        self.fail_write_offset = None
        self.serials = {SERIAL: "device"}
        self.processes = {1: "/bin/init"}
        self.persist_fails = False
        self.writers = []
        self.on_write = None
        self.daemon_ignores_stop = False
        self.update_lock_busy = False
        self.lock_token = None
        self.locks_taken = []
        self.lock_stale = None
        self.lock_lapsed = False
        self.token_age_max = 0.0
        self.clock, self.adb_seconds, self.mtimes = 0.0, 0.0, {}
        self.holds, self.running_during_write = 0, []
        self.mounted, self.rescued, self.slot_writes, self.slot_failure = None, False, 0, None
        self.adbd_restart, self.adbd_down = 0, 0
        self.bundles, self.stale_mount = {}, None
        self.power_on_at, self.keepers, self.polls = None, [], []
        self.addressed, self.second_unit, self.keeper_works = [], False, True
        self.keeper_replies = []

    def usb_up(self):
        """adb lists the unit: always, or after power_on_at like a new TC002 (USB ADB from 2 s to
        4.5 s of uptime, then again from 8.5 s when a keeper started in that window)."""
        if self.power_on_at is None:
            return True
        up = self.clock - self.power_on_at
        kept = self.keeper_works and any(2.0 <= k - self.power_on_at < 4.5 for k in self.keepers)
        return 2.0 <= up < 4.5 or (kept and up >= 8.5)

    def token_fresh(self):
        token = installer.LOCK_TOKEN
        return token in self.files and self.clock - self.mtimes.get(token, 0.0) < \
            installer.TOKEN_STALE_SECONDS

    def observe(self):
        """What the lock holder and a holding loader notice on their own as time passes."""
        if self.lock_token is not None and self.lock_stale is not None:
            age = self.clock - self.mtimes.get(self.lock_token, 0.0)
            if age >= self.lock_stale:
                self.lock_token, self.lock_lapsed = None, True
            else:
                self.token_age_max = max(self.token_age_max, age)
        if self.processes.get(900) == "/bin/zkgui" and not self.token_fresh():
            del self.processes[900]
            if not self.start_zkswe():
                self.processes[900] = "/bin/busybox"

    def run(self, args, timeout):
        self.clock += self.adb_seconds
        self.observe()
        if args == ["devices"]:
            self.polls.append(self.clock)
            lines = "".join(f"{s}\t{state}\n" for s, state in self.serials.items()
                            if s != SERIAL or self.usb_up())
            lines += f"{SERIAL}\tdevice\n" if self.second_unit else ""
            return 0, "List of devices attached\n" + lines + "\n", ""
        assert args[0] == "-s" and args[1] in self.serials, args
        self.addressed.append(args[1])
        if self.lost or (args[1] == SERIAL and not self.usb_up()):
            return 1, "", f"adb.exe: device '{args[1]}' not found\n"
        if args[2:] == ["shell", tc002_device.USB_KEEPER]:
            if self.keeper_replies:
                return self.keeper_replies.pop(0)
            self.keepers.append(self.clock)
            return 0, "awtrix-usb-keeper started 4711\n", ""
        if args[2] == "push":
            data = Path(args[3]).read_bytes()
            remote = args[4]
            assert remote.startswith("/tmp/"), f"adbd must not write to {remote}"
            if self.lose_usb_on_push and Path(args[3]).name == self.lose_usb_on_push:
                self.lost = True
                return 1, "", "adb.exe: device offline\n"
            self.mkdirs(remote.rsplit("/", 1)[0])
            self.files[remote] = bytearray(data)
            self.modes[remote] = 0o644
            self.owners[remote] = (0, 0)
            self.pushes.append(remote)
            staged = sum(len(d) for p, d in self.files.items()
                         if p.startswith(installer.DEVICE_STAGE + "/"))
            self.stage_peak = max(self.stage_peak, staged)
            return 0, "1 file pushed\n", ""
        if args[2] == "pull":
            remote, local = args[3], args[4]
            if remote not in self.files:
                return 1, "", f"adb: error: failed to stat remote object '{remote}'\n"
            Path(local).write_bytes(bytes(self.files[remote]))
            self.pulls.append(remote)
            return 0, "1 file pulled\n", ""
        assert args[2] == "shell"
        if self.adbd_down:
            self.adbd_down -= 1
            return 0, "", ""
        output = self.shell(args[3])
        self.vendor_memory()
        return 0, output, ""

    def vendor_memory(self):
        """A running vendor app or stock wpa_supplicant writes what it holds back to /data."""
        for service, path, data in self.writers:
            if self.props.get(f"init.svc.{service}") == "running" and path in self.files:
                self.files[path] = bytearray(data)

    def stop_releases(self):
        for pid, exe in list(self.processes.items()):
            if exe.startswith(installer.AWTRIX_EXECUTABLES):
                del self.processes[pid]

    def layout(self):
        """release_slot_locate() on the fake res: header block, image offset, capacity."""
        if bytes(self.mtd[:4]) != b"hsqs":
            return None
        used = int.from_bytes(self.mtd[40:48], "little")
        header = (used + ERASE - 1) // ERASE * ERASE
        if header > PARTITION - 2 * ERASE:
            return None
        return header, header + ERASE, PARTITION - header - ERASE

    def slot(self):
        """The header of the release slot, or None. SLOT_MAGIC and a JSON line stand for it."""
        layout = self.layout()
        block = bytes(self.mtd[layout[0]:layout[0] + ERASE]) if layout else b""
        if not block.startswith(SLOT_MAGIC) or b"\n" not in block:
            return None
        try:
            header = json.loads(block[len(SLOT_MAGIC):block.index(b"\n")])
        except ValueError:
            return None
        return header if header["imageBytes"] <= layout[2] else None

    def slot_intact(self, header):
        image = self.layout()[1]
        return sha(bytes(self.mtd[image:image + header["imageBytes"]])) == header["sha256"]

    def mount(self, header):
        """The release's files as the loop mount shows them; stale_mount names a file whose last
        byte a stale block cache got wrong."""
        self.mounted = header["release"]
        bundle = self.bundles.get(header["release"])
        if bundle:
            for entry in json.loads((bundle / "manifest.json").read_text())["files"]:
                data = bytearray((bundle / entry["path"]).read_bytes())
                if entry["path"] == self.stale_mount:
                    data[-1] ^= 1
                self.put(f"{installer.RELEASE_MOUNT}/{entry['path']}", data, int(entry["mode"], 8))
        self.put(installer.RELEASE_LISTING, json.dumps({"release": header["release"]}).encode())
        if not bundle:
            self.put(installer.RELEASE_DAEMON, b"daemon", 0o755)

    def unmount(self):
        self.mounted = None
        self.remove(installer.RELEASE_MOUNT)

    def start_zkswe(self):
        """init starts zkswe; the loader waits while a deploy token is fresh, then mounts the
        release slot and starts its daemon, or enters rescue."""
        self.props["init.svc.zkswe"] = "running"
        if installer.LOADER_IN_RES not in self.files:
            return False
        if self.token_fresh():
            self.holds += 1
            self.processes[900] = "/bin/zkgui"
            return True
        header = self.slot()
        if header and self.slot_intact(header) and installer.LOOP_IN_RES in self.files:
            self.mount(header)
            self.processes[812] = installer.RELEASE_DAEMON
            self.rescued = False
            self.adbd_down, self.adbd_restart = self.adbd_restart, 0
            return True
        self.rescued = True
        return False

    def lock_held(self):
        return self.lock_token is not None

    def used(self, root):
        inodes = {id(d): len(d) for p, d in self.files.items() if p.startswith(root + "/")}
        return sum(inodes.values())

    def mkdirs(self, path):
        parts = path.strip("/").split("/")
        for i in range(1, len(parts) + 1):
            self.dirs.add("/" + "/".join(parts[:i]))

    def exists(self, path):
        return path in self.files or path in self.dirs or path in self.links

    def remove(self, path):
        for table in (self.files, self.links, self.modes, self.owners):
            for key in [k for k in table if k == path or k.startswith(path + "/")]:
                del table[key]
        self.dirs = {d for d in self.dirs if d != path and not d.startswith(path + "/")}
        if self.lock_token and self.lock_token not in self.files:
            self.lock_token = None

    def uptime(self):
        return 3600.0 if self.power_on_at is None else self.clock - self.power_on_at

    def shell(self, line):
        self.commands.append(line)
        self.umask = 0
        self.variables = {}
        output, code = [], 0
        for sequence in line.split("; "):
            if sequence.startswith("echo __AWTRIX_RC="):
                output.append(f"__AWTRIX_RC={code}\n")
                continue
            for command in sequence.split(" && "):
                code, text = self.execute(command)
                output.append(text)
                if code:
                    break
        return "".join(output).replace("\n", "\r\n")

    def put(self, path, data, mode=0o644, owner=(0, 0)):
        self.mkdirs(path.rsplit("/", 1)[0])
        self.files[path], self.modes[path], self.owners[path] = bytearray(data), mode, owner

    def landed(self, path):
        self.copies.append(path)
        data = self.files[path]
        if self.corrupt_copy and path.endswith(self.corrupt_copy) and data:
            data[-1] ^= 1

    def listing(self, path):
        mode = self.modes.get(path, 0o644)
        bits = "".join(c if mode & (1 << (8 - i)) else "-" for i, c in enumerate("rwxrwxrwx"))
        uid, gid = self.owners.get(path, (0, 0))
        return f"-{bits}    1 {uid:<8} {gid:<8} {len(self.files[path]):>8} Sep 25 00:11 {path}\n"

    def execute(self, command):
        if command == "read -r up rest </proc/uptime":
            self.variables["up"] = f"{self.uptime():.2f}"
            return 0, ""
        if command.startswith(("echo '", "echo deploy ")):
            if command.startswith("echo '"):
                text, _, path = command[len("echo '"):].rpartition("' > ")
            else:
                text, _, path = command[len("echo "):].rpartition(" > ")
                text = " ".join(re.sub(r"^\$(\w+)$", lambda m: self.variables.get(m[1], ""), word)
                                for word in text.split())
            if path.rsplit("/", 1)[0] not in self.dirs:
                return 1, f"can't create {path}: nonexistent directory\n"
            self.files[path] = bytearray(text.encode() + b"\n")
            self.modes[path], self.owners[path] = 0o666 & ~self.umask, (0, 0)
            self.mtimes[path] = self.clock
            return 0, ""
        if command.startswith("umask "):
            self.umask = int(command.split()[1], 8)
            return 0, ""
        words = shlex.split(command)
        name, args = words[0], words[1:]
        if name == installer.DEVICE_HELPER:
            if installer.DEVICE_HELPER not in self.files:
                return 127, "not found\n"
            return self.helper(args)
        if name.endswith("/bin/awtrix-tc002d") and args == ["ctl", "stop"]:
            if name not in self.files:
                return 127, "not found\n"
            if not self.daemon_ignores_stop:
                self.stop_releases()
                if self.props.get("init.svc.zkswe") == "running" and not self.start_zkswe():
                    self.processes[900] = "/bin/busybox"
            return 0, '{"ok":true}\n'
        if name == "mkdir":
            for path in args[1:]:
                self.mkdirs(path)
            return 0, ""
        if name == "chmod":
            for path in args[1:]:
                self.modes[path] = int(args[0], 8)
            return 0, ""
        if name == "chown":
            uid, gid = args[0].split(":")
            for path in args[1:]:
                if path not in self.files:
                    return 1, f"chown: {path}: No such file or directory\n"
                self.owners[path] = (int(uid), int(gid))
            return 0, ""
        if name == "cp":
            source, target = args
            if source not in self.files or target.rsplit("/", 1)[0] not in self.dirs:
                return 1, f"cp: can't create '{target}'\n"
            self.files[target] = bytearray(self.files[source])
            self.modes[target], self.owners[target] = self.modes.get(source, 0o644), (0, 0)
            self.landed(target)
            return 0, ""
        if name == "cat" and len(args) == 3 and args[1] in (">", ">>"):
            source, target = args[0], args[2]
            if source not in self.files or target.rsplit("/", 1)[0] not in self.dirs:
                return 1, f"can't create {target}\n"
            if args[1] == ">" or target not in self.files:
                self.files[target] = bytearray()
                self.modes[target], self.owners[target] = 0o644, (0, 0)
            self.files[target].extend(self.files[source])
            self.landed(target)
            return 0, ""
        if name == "ln":
            source, target = args
            if source not in self.files or self.exists(target):
                return 1, "ln failed\n"
            self.files[target] = self.files[source]
            self.modes[target] = self.modes.get(source, 0o644)
            return 0, ""
        if name == "rm":
            for path in args[1:]:
                self.remove(path)
            return 0, ""
        if name == "mv":
            source, target = [a for a in args if a != "-f"]
            if target in self.files and source in self.files:
                self.remove(target)
            if self.exists(target):
                return 1, "exists\n"
            for table in (self.files, self.modes, self.owners):
                for key in [k for k in table if k == source or k.startswith(source + "/")]:
                    table[target + key[len(source):]] = table.pop(key)
            self.dirs = {target + d[len(source):] if d == source or d.startswith(source + "/")
                         else d for d in self.dirs}
            return 0, ""
        if name == "[":
            flag, path = args[0], args[1]
            ok = {"-e": self.exists(path), "-d": path in self.dirs, "-f": path in self.files,
                  "-L": path in self.links}[flag]
            return (0 if ok else 1), ""
        if name == "sync":
            return 0, ""
        if name == "getprop":
            return 0, self.props.get(args[0], "") + "\n"
        if name == "setprop":
            if args[0] == "ctl.stop":
                self.props[f"init.svc.{args[1]}"] = "stopped"
                if args[1] == "zkswe":
                    self.stop_releases()
                    self.processes.pop(900, None)
            elif args[0] == "ctl.start" and args[1] == "zkswe":
                self.start_zkswe()
            else:
                self.props[args[0]] = args[1]
                store = installer.PERSIST_DIR
                if args[0].startswith("persist.") and store in self.dirs and not self.persist_fails:
                    self.put(f"{store}/{args[0]}", args[1].encode(), 0o600)
            return 0, ""
        if name == "cat":
            if args[0] == "/proc/mtd":
                return 0, ('dev:    size   erasesize  name\n'
                           f'mtd3: {PARTITION:08x} {ERASE:08x} "res"\n')
            if args[0] == "/proc/sys/kernel/random/boot_id":
                return 0, "boot\n"
            if args[0] == "/proc/uptime":
                return 0, f"{self.uptime():.2f} 1.00\n"
            return (0, self.files[args[0]].decode()) if args[0] in self.files else (1, "")
        if name == "ls":
            path = args[-1]
            if path == "/proc/[0-9]*/exe":
                lines = [f"lrwxrwxrwx 1 0 0 0 Jan 1 /proc/{pid}/exe -> {exe}\n"
                         for pid, exe in sorted(self.processes.items())]
                return 1, "".join(lines) + "ls: /proc/2/exe: No such file or directory\n"
            if args[0] in ("-l", "-ln") and path in self.files:
                return 0, self.listing(path)
            if args[0] in ("-l", "-ln"):
                return 0, f"lrwxrwxrwx 1 0 0 20 Jan 1 {path} -> {self.links[path]}\n"
            children = sorted({p[len(path) + 1:].split("/")[0] for p in
                               list(self.dirs) + list(self.files) if p.startswith(path + "/")})
            return 0, "  ".join(children) + "\n"
        if name == "reboot":
            self.rebooted = True
            return 0, ""
        raise AssertionError(f"unexpected device command {command!r}")

    def helper(self, args):
        def line(value):
            return json.dumps(value) + "\n"
        command = args[0]
        if command == "sha256":
            out, code = [], 0
            for path in args[1:]:
                if path in self.files:
                    data = bytes(self.files[path])
                    uid, gid = self.owners.get(path, (0, 0))
                    mode = f"{self.modes.get(path, 0o644):04o}"
                    out.append(line({"command": "sha256", "result": "ok", "path": path,
                                     "size": len(data), "mode": mode, "uid": uid, "gid": gid,
                                     "sha256": sha(data)}))
                else:
                    out.append(line({"command": "sha256", "result": "error", "path": path,
                                     "errno": 2}))
                    code = 1
            return code, "".join(out)
        if command == "statfs":
            root = args[1]
            return 0, line({"command": "statfs", "result": "ok", "path": root,
                            "free_bytes": self.capacity[root] - self.used(root)})
        if command == "symlink":
            self.links[args[2]] = args[1]
            return 0, line({"command": "symlink", "result": "ok", "link": args[2],
                            "target": args[1]})
        if command == "lock":
            directory, token, seconds, stale = args[1:]
            assert directory == installer.UPDATE_WORK and int(seconds) > 0 and int(stale) > 0
            if token not in self.files:
                return 1, line({"command": "lock", "result": "error", "stage": "token",
                                "errno": 2, "modified": False, "message": "missing"})
            if self.update_lock_busy or self.lock_held():
                return 1, line({"command": "lock", "result": "error", "stage": "busy",
                                "errno": 11, "modified": False,
                                "message": "an update holds the lock"})
            self.lock_token = token
            self.lock_stale = int(stale)
            self.locks_taken.append(token)
            self.mkdirs(directory)
            return 0, line({"command": "lock", "result": "ok", "path": directory, "pid": 4242,
                            "seconds": int(seconds), "stale": self.lock_stale})
        if command == "hash":
            path = args[1]
            data = bytes(self.mtd) if path == installer.RES_DEVICE else bytes(self.files[path])
            length = int(args[2]) if len(args) > 2 else len(data)
            return 0, line({"command": "hash", "result": "ok", "path": path, "name": "res",
                            "size": len(data), "erase_size": ERASE, "length": length,
                            "sha256": sha(data[:length])})
        if command == "slot-info":
            layout = self.layout()
            if not layout:
                return 1, line({"command": "slot-info", "result": "error", "stage": "layout",
                                "errno": 22, "modified": False,
                                "message": "res does not start with a squashfs"})
            info = {"command": "slot-info", "result": "ok", "header": layout[0],
                    "image": layout[1], "capacity": layout[2], "slot": "empty"}
            header = self.slot()
            if header:
                info["slot"] = ("verified" if self.slot_intact(header) else "corrupt") \
                    if "--verify" in args else "valid"
                info.update(header)
            return 0, line(info)
        if command == "write-slot":
            def failed(stage, modified, message):
                return 2, line({"command": "write-slot", "result": "error", "stage": stage,
                                "errno": 5, "modified": modified, "message": message})
            options = dict(zip(args[2::2], args[3::2]))
            assert options["--mount"] == installer.RELEASE_MOUNT and len(args) == 12, args
            length, digest = int(options["--length"]), options["--sha256"]
            data = bytes(self.files.get(args[1], b""))
            if len(data) < length or sha(data[:length]) != digest:
                return failed("source", False, f"{args[1]} is not the image named")
            layout = self.layout()
            if length > layout[2]:
                return failed("capacity", False, "the image does not fit the slot")
            if self.mounted:
                if any(e.startswith(installer.RELEASE_MOUNT + "/") for e in self.processes.values()):
                    return failed("unmount", False, "cannot unmount the installed release: busy")
                self.unmount()
            if self.slot_failure and not self.slot_failure[1]:
                return failed(self.slot_failure[0], False, f"{self.slot_failure[0]} failed")
            self.running_during_write += [e for e in self.processes.values()
                                          if e.endswith("/awtrix-tc002d")]
            self.mtd[layout[0]:layout[0] + ERASE] = b"\xff" * ERASE
            if self.slot_failure:
                self.mtd[layout[1]:layout[1] + length] = bytes(length)
                return failed(self.slot_failure[0], True, f"{self.slot_failure[0]} failed")
            span = (length + ERASE - 1) // ERASE * ERASE
            self.mtd[layout[1]:layout[1] + span] = data[:length].ljust(span, b"\xff")
            header = {"release": options["--release"], "counter": int(options["--counter"]),
                      "imageBytes": length, "sha256": digest}
            block = SLOT_MAGIC + json.dumps(header).encode() + b"\n"
            self.mtd[layout[0]:layout[0] + ERASE] = block.ljust(ERASE, b"\xff")
            self.slot_writes += 1
            return 0, line({"command": "write-slot", "result": "ok", "image": layout[1],
                            "blocks": span // ERASE, "skipped": 0, "written": span // ERASE,
                            "blockCache": "dropped", **header})
        if command == "write":
            if self.on_write:
                self.on_write()
            device, image, _, offset, _, name = args[1:7]
            data, offset = bytes(self.files[image]), int(offset)
            length, digest = len(data), sha(data)
            assert device == installer.RES_DEVICE and name == "res" and offset % ERASE == 0
            span = (len(data) + ERASE - 1) // ERASE * ERASE
            data = data.ljust(span, b"\xff")
            written = skipped = 0
            for at in range(0, span, ERASE):
                if self.fail_write_offset is not None and offset + at >= self.fail_write_offset:
                    return 2, line({"command": "write", "result": "error", "stage": "verify",
                                    "errno": 5, "modified": True, "message": "stuck"})
                block = data[at:at + ERASE]
                if self.mtd[offset + at:offset + at + ERASE] == block:
                    skipped += 1
                else:
                    self.mtd[offset + at:offset + at + ERASE] = block
                    written += 1
            return 0, line({"command": "write", "result": "ok", "name": name, "offset": offset,
                            "length": length, "written": written, "skipped": skipped,
                            "modified": written > 0, "sha256": digest})
        raise AssertionError(f"unexpected helper command {args}")


class InstallerTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="awtrix-install-test-"))
        self.addCleanup(shutil.rmtree, self.work, ignore_errors=True)
        noise = random.Random(3)
        self.stock = squashfs_like(noise.randbytes(PARTITION // 2)) + b"\xff" * (PARTITION // 2)
        self.image = squashfs_like(noise.randbytes(300 * 1024 + 123))
        self.res_dir = self.work / "res"
        self.res_dir.mkdir()
        (self.res_dir / "stock-res.img").write_bytes(self.stock)
        (self.res_dir / "awtrix-res.img").write_bytes(self.image)
        (self.res_dir / "res-images.json").write_text(json.dumps({
            "schema_version": 1,
            "partition": {"index": 3, "name": "res", "size": PARTITION, "erase_size": ERASE},
            "stock": {"file": "stock-res.img", "bytes": len(self.stock), "sha256": sha(self.stock)},
            "awtrix": {"file": "awtrix-res.img", "bytes": len(self.image),
                       "sha256": sha(self.image)}}))
        self.bundles = {}
        self.bundle = self.make_bundle("one")
        self.fake = FakeTc002(self.stock)
        self.fake.bundles = self.bundles
        self.device = installer.Device("adb", SERIAL)
        self.device._run = self.fake.run
        self.give_loader()
        self.sleep, installer.time.sleep = installer.time.sleep, lambda seconds: None
        self.private = self.work / "private"
        self.private_root, installer.PRIVATE_ROOT = installer.PRIVATE_ROOT, self.private
        self.archive = self.work / "archive"
        self.archive_root, installer.ARCHIVE_ROOT = installer.ARCHIVE_ROOT, self.archive

    def tearDown(self):
        installer.time.sleep = self.sleep
        installer.PRIVATE_ROOT = self.private_root
        installer.ARCHIVE_ROOT = self.archive_root

    def make_bundle(self, flavour, counter=COUNTER, files=None, image_bytes=IMAGE_BYTES):
        root = self.work / f"bundle-{flavour}"
        noise = random.Random(flavour)
        for path, size in files or BUNDLE_FILES:
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_bytes(noise.randbytes(size))
        write_manifest(root, "1.1.2-gabc1234", "abc1234", counter, image_bytes)
        self.bundles[self.release_of(root)] = root
        return root

    def manifest_of(self, bundle):
        return json.loads((bundle / "manifest.json").read_text())

    def release_of(self, bundle):
        return self.manifest_of(bundle)["release"]

    def deploy(self, bundle):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            installer.deploy(self.device, bundle)
        return output.getvalue()

    def give_loader(self):
        self.fake.put(installer.LOADER_IN_RES, b"loader", 0o755)
        self.fake.put(installer.LOOP_IN_RES, b"loop", 0o644)

    def assert_slot_holds(self, bundle):
        manifest = self.manifest_of(bundle)
        header = self.fake.slot()
        self.assertEqual(header, {"release": manifest["release"], "counter": manifest["counter"],
                                  "imageBytes": manifest["image"]["size"],
                                  "sha256": manifest["image"]["sha256"]})
        image = self.fake.layout()[1]
        self.assertEqual(bytes(self.fake.mtd[image:image + header["imageBytes"]]),
                         (bundle / bundle_format.IMAGE).read_bytes())

    def assert_running(self, bundle):
        self.assertEqual(self.fake.mounted, self.release_of(bundle))
        self.assertEqual(installer.running_release(self.device), self.release_of(bundle))
        self.assertIn(installer.RELEASE_DAEMON, self.fake.processes.values())

    def assert_update_state(self, bundle):
        manifest = self.manifest_of(bundle)
        state = json.loads(bytes(self.fake.files[installer.UPDATE_STATE]))
        self.assertEqual(state, {
            "schema": 2, "state": "confirmed", "acceptedCounter": manifest["counter"],
            "current": {"counter": manifest["counter"], "payloadSha256": manifest["image"]["sha256"],
                        "release": manifest["release"], "target": "awtrix-ng:tc002"},
            "candidate": None, "lease": None, "failure": ""})
        self.assertEqual(self.fake.modes[installer.UPDATE_STATE], 0o600)
        self.assertEqual(self.fake.owners[installer.UPDATE_STATE], (0, 0))
        self.assertNotIn(installer.UPDATE_STATE_STAGED, self.fake.files)

    def test_deploy_fresh_device(self):
        for counter, value in zip(installer.COUNTERS, (b"3\n", b"2\n")):
            self.fake.files[counter] = bytearray(value)
        self.fake.files[installer.VENDOR_MARKER] = bytearray(b"x")
        self.fake.files[installer.RESCUE_MARKER] = bytearray(
            b"no release header; no AWTRIX release in res: rescue\n")
        output = self.deploy(self.bundle)
        release = self.release_of(self.bundle)
        self.assert_slot_holds(self.bundle)
        self.assert_update_state(self.bundle)
        self.assert_running(self.bundle)
        for path in installer.COUNTERS + (installer.VENDOR_MARKER, installer.RESCUE_MARKER):
            self.assertNotIn(path, self.fake.files)
        self.assertEqual(self.fake.locks_taken, [installer.LOCK_TOKEN])
        self.assertFalse(self.fake.lock_held())
        self.assertIn(f"holding the web update lock {installer.UPDATE_WORK}/lock", output)
        self.assertIn(f"release slot: empty, {self.fake.layout()[2]} bytes", output)
        self.assertIn(f"awtrix-tc002d runs {release}", output)
        self.assertEqual(self.fake.pushes, [installer.DEVICE_HELPER, installer.DEVICE_IMAGE])
        self.assertEqual(installer.archived_bundle(release), self.archive / release)
        self.assertTrue((self.archive / release / bundle_format.IMAGE).is_file())
        self.assertFalse(self.installer_leftovers())
        self.assertEqual(self.fake.modes.get(installer.DATA_DIR), 0o755)
        self.assertEqual(self.fake.modes.get(installer.STATE_DIR), 0o700)

    def test_redeploy_writes_nothing(self):
        self.deploy(self.bundle)
        pushes, before = len(self.fake.pushes), len(self.fake.commands)
        output = self.deploy(self.bundle)
        self.assertEqual(self.fake.pushes[pushes:], [installer.DEVICE_HELPER])
        self.assertEqual(self.fake.slot_writes, 1)
        self.assertIn("the release slot already holds this release", output)
        self.assertIn(f"awtrix-tc002d already runs {self.release_of(self.bundle)}", output)
        self.assertFalse([c for c in self.fake.commands[before:] if "ctl.stop" in c or "ctl stop" in c])
        self.assert_running(self.bundle)

    def test_redeploy_rewrites_a_damaged_slot(self):
        self.deploy(self.bundle)
        self.fake.mtd[self.fake.layout()[1] + 5] ^= 1
        output = self.deploy(self.bundle)
        self.assertIn("release slot: " + self.release_of(self.bundle), output)
        self.assertIn("corrupt", output)
        self.assertEqual(self.fake.slot_writes, 2)
        self.assert_slot_holds(self.bundle)
        self.assert_running(self.bundle)

    def test_update_stops_the_daemon_then_writes_the_slot(self):
        self.deploy(self.bundle)
        update = self.make_bundle("two", counter=COUNTER + 60)
        holds, before = self.fake.holds, len(self.fake.commands)
        self.fake.running_during_write.clear()
        output = self.deploy(update)
        self.assert_slot_holds(update)
        self.assert_running(update)
        self.assert_update_state(update)
        self.assertEqual(self.fake.holds, holds + 1)
        self.assertEqual(self.fake.running_during_write, [])
        commands = self.fake.commands[before:]
        stopped = commands.index(f"{installer.RELEASE_DAEMON} ctl stop; echo __AWTRIX_RC=$?")
        written = next(i for i, c in enumerate(commands) if f"{installer.DEVICE_HELPER} write-slot " in c)
        removed = commands.index(f"rm -f {installer.DEVICE_IMAGE}; echo __AWTRIX_RC=$?")
        released = commands.index(f"rm -f {installer.LOCK_TOKEN}; echo __AWTRIX_RC=$?")
        started = commands.index("setprop ctl.start zkswe; echo __AWTRIX_RC=$?")
        self.assertLess(stopped, written)
        self.assertLess(written, removed)
        self.assertLess(removed, released)
        self.assertLess(released, started)
        self.assertIn(f"awtrix-tc002d runs {self.release_of(update)}", output)
        self.assertFalse(self.fake.lock_held())

    def test_archive_keeps_the_newest_bundles_and_the_one_deployed(self):
        foreign = self.archive / "webupdate-A"
        (foreign / "bundle").mkdir(parents=True)
        (foreign / "awtrix-ng-tc002.awup").write_bytes(b"package")
        bundles = [self.make_bundle(f"n{index}") for index in range(installer.ARCHIVE_KEEP + 1)]
        for bundle in bundles:
            self.deploy(bundle)
        kept = sorted(p.name for p in self.archive.iterdir() if p.is_dir() and p != foreign)
        self.assertEqual(kept, sorted(self.release_of(b) for b in bundles[1:]))
        self.assertTrue((foreign / "awtrix-ng-tc002.awup").is_file(),
                        "directories the installer did not archive stay untouched")
        oldest_kept = self.archive / self.release_of(bundles[1])
        self.deploy(oldest_kept)
        self.assert_slot_holds(bundles[1])
        self.deploy(self.make_bundle("newest"))
        self.assertTrue(installer.archived_bundle(self.release_of(bundles[1])))
        self.assertFalse(installer.archived_bundle(self.release_of(bundles[2])))

    def test_deploy_keeps_everything_when_the_daemon_does_not_stop(self):
        self.deploy(self.bundle)
        self.fake.daemon_ignores_stop = True
        self.fake.props["init.svc.zkswe"] = "stopped"
        with self.assertRaisesRegex(installer.InstallError,
                                    r"AWTRIX still runs \(.*\); nothing was changed"):
            self.deploy(self.make_bundle("two"))
        self.assert_slot_holds(self.bundle)
        self.assert_running(self.bundle)
        self.assertFalse(self.installer_leftovers())

    def test_deploy_refuses_an_image_the_slot_cannot_hold(self):
        capacity = self.fake.layout()[2]
        with self.assertRaisesRegex(installer.InstallError,
                                    f"the release image takes {capacity + 1} bytes, the release slot "
                                    f"holds {capacity}; nothing was changed"):
            self.deploy(self.make_bundle("big", image_bytes=capacity + 1))
        self.assertIsNone(self.fake.slot())
        self.assertFalse(self.installer_leftovers())

    def test_deploy_without_room_in_tmp_starts_the_release_it_holds_again(self):
        self.deploy(self.bundle)
        self.fake.capacity["/tmp"] = installer.TMP_RESERVE + IMAGE_BYTES
        with self.assertRaisesRegex(installer.InstallError,
                                    r"/tmp has \d+ bytes free, the release image needs \d+ with the "
                                    r"clock's reserve; the release slot was not touched, so the clock "
                                    r"starts the release it holds again"):
            self.deploy(self.make_bundle("two"))
        self.assert_slot_holds(self.bundle)
        self.assert_running(self.bundle)
        self.assertFalse(self.fake.lock_held())
        self.assertFalse(self.installer_leftovers())

    def test_a_failed_slot_write_shows_usb_recovery_until_deploy_runs_again(self):
        self.deploy(self.bundle)
        update = self.make_bundle("two")
        self.fake.slot_failure = ("verify", True)
        with self.assertRaisesRegex(installer.InstallError,
                                    "writing the release slot failed: verify failed. The slot holds "
                                    "no complete release: the clock shows USB RECOVERY until deploy "
                                    "runs again"):
            self.deploy(update)
        self.assertIsNone(self.fake.slot())
        self.assertTrue(self.fake.rescued)
        self.assertNotIn(installer.RELEASE_DAEMON, self.fake.processes.values())
        self.assertFalse(self.fake.lock_held())
        self.assertFalse(self.installer_leftovers())
        self.fake.slot_failure = None
        self.deploy(update)
        self.assert_slot_holds(update)
        self.assert_running(update)

    def test_a_slot_write_refused_before_erasing_keeps_the_release(self):
        self.deploy(self.bundle)
        self.fake.slot_failure = ("open", False)
        with self.assertRaisesRegex(installer.InstallError,
                                    "open failed; the release slot was not touched"):
            self.deploy(self.make_bundle("two"))
        self.assert_slot_holds(self.bundle)
        self.assert_running(self.bundle)

    def test_deploy_checks_the_mounted_release_against_the_bundle(self):
        output = self.deploy(self.bundle)
        manifest = self.manifest_of(self.bundle)
        self.assertIn(f"all {len(manifest['files'])} files of the mounted release match the bundle", output)
        update = self.make_bundle("two")
        self.fake.stale_mount = "bin/awtrix-linux"
        with self.assertRaisesRegex(installer.InstallError,
                                    "the mounted release differs from the bundle in bin/awtrix-linux: "
                                    ".*switch it off and on"):
            self.deploy(update)
        self.assert_slot_holds(update)
        self.assertFalse(self.installer_leftovers())

    def test_deploy_rides_out_the_adbd_restart_of_the_daemons_first_start(self):
        self.fake.adbd_restart = 4
        output = self.deploy(self.bundle)
        self.assertIn(f"awtrix-tc002d runs {self.release_of(self.bundle)}", output)
        self.assertEqual(self.fake.adbd_down, 0)
        self.assert_running(self.bundle)
        self.assertFalse(self.installer_leftovers())

    def test_deploy_removes_the_layout_before_the_release_slot(self):
        old = f"{installer.LEGACY_RELEASES}/1.1.0-gold-000000000000"
        self.fake.put(f"{old}/bin/awtrix-tc002d", b"old daemon", 0o755)
        self.fake.put(f"{old}/bin/awtrix-linux", b"old runtime", 0o755)
        self.fake.links[installer.DATA_DIR + "/current"] = "releases/1.1.0-gold-000000000000"
        self.fake.put(installer.STATE_DIR + "/factory-attempts", b"1\n")
        self.fake.put("/tmp/awtrix-loader.factory-attempts", b"1\n")
        self.fake.props["init.svc.zkswe"] = "running"
        self.fake.processes.update({812: f"{old}/bin/awtrix-tc002d", 840: f"{old}/bin/awtrix-linux"})
        self.deploy(self.bundle)
        self.assertIn(f"{old}/bin/awtrix-tc002d ctl stop; echo __AWTRIX_RC=$?", self.fake.commands)
        for path in installer.LEGACY:
            self.assertFalse(self.fake.exists(path), path)
        self.assert_slot_holds(self.bundle)
        self.assert_running(self.bundle)
        self.assertNotIn(f"{old}/bin/awtrix-linux", self.fake.processes.values())

    def run_main(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(installer.Device, "_run",
                               lambda _device, args, timeout: self.fake.run(args, timeout)), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = installer.main(["--serial", SERIAL, "--adb", sys.executable, *argv])
        return code, out.getvalue(), err.getvalue()

    def installer_leftovers(self):
        return [p for p in list(self.fake.files) + list(self.fake.dirs)
                if p.startswith((installer.DEVICE_WORK, installer.DEVICE_STAGE))]

    def test_lost_usb_while_pushing_the_image_then_deploy_again_recovers(self):
        self.deploy(self.bundle)
        update = self.make_bundle("two")
        self.fake.lose_usb_on_push = bundle_format.IMAGE
        code, _, err = self.run_main("deploy", "--bundle", str(update))
        self.assertEqual(code, 1)
        lines = err.splitlines()
        self.assertEqual(lines[0], "error: adb push failed: adb.exe: device offline; the release slot "
                                   "was not touched, so the clock starts the release it holds again")
        lost = f"adb shell failed: adb.exe: device '{SERIAL}' not found"
        self.assertEqual(lines[1:4], [
            f"  removing {installer.DEVICE_IMAGE} failed as well: {lost}",
            f"  restarting zkswe failed as well: {lost}",
            f"  removing {installer.DEVICE_WORK} failed as well: {lost}"])
        self.assertRegex(lines[4], r"^USB ADB stopped answering; the device itself keeps running\. "
                                   r"Unplug and replug the USB cable and run deploy again\. .*USB "
                                   r"RECOVERY: it keeps USB ADB for that\.$")
        self.assertEqual(len(lines), 5)
        self.assert_slot_holds(self.bundle)

        self.fake.lost, self.fake.lose_usb_on_push = False, None
        code, out, err = self.run_main("deploy", "--bundle", str(update))
        self.assertEqual((code, err), (0, ""))
        self.assertIn(f"deployed {self.release_of(update)}", out)
        self.assert_slot_holds(update)
        self.assert_running(update)
        self.assertFalse(self.installer_leftovers())

    def test_lost_usb_while_flashing_says_not_to_reboot(self):
        self.fake.lose_usb_on_push = "transfer.bin"
        code, _, err = self.run_main("flash-loader", "--bundle", str(self.bundle), "--res-dir",
                                     str(self.res_dir), "--yes")
        self.assertEqual(code, 1)
        lines = err.splitlines()
        self.assertEqual(lines[0], "error: adb push failed: adb.exe: device offline")
        self.assertTrue(all(" failed as well: " in line for line in lines[1:3]), err)
        self.assertIn("Do not reboot it", lines[3])
        self.assertFalse(self.fake.rebooted)
        self.assertTrue(list(self.res_dir.glob("flash-journal-*")))

    def test_flash_keeps_large_release_counters_exact_across_the_js_host(self):
        bundle = self.make_bundle("large-counter", counter=(1 << 53) + 1)
        installer.flash(self.device, bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.assert_slot_holds(bundle)
        writes = self.fake.slot_writes
        installer.flash(self.device, bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.assertEqual(self.fake.slot_writes, writes)

    def test_engine_exit_during_flash_keeps_recovery_journal_and_prohibits_reboot(self):
        def interrupted(operation, options, dispatch):
            self.assertEqual(operation, "flash")
            dispatch("journal", {"active": True, "target": options["record"]["awtrix"]["sha256"]})
            raise installer.js_engine.EngineError("installation engine connection closed")
        with mock.patch.object(installer.js_engine, "run", side_effect=interrupted):
            code, _, err = self.run_main("flash-loader", "--bundle", str(self.bundle), "--res-dir",
                                         str(self.res_dir), "--yes")
        self.assertEqual(code, 1)
        self.assertIn("installation engine connection closed", err)
        self.assertIn("Do not reboot", err)
        self.assertFalse(self.fake.rebooted)
        self.assertTrue(list(self.res_dir.glob("flash-journal-*")))

    def test_errors_without_usb_loss_get_no_hint(self):
        big = self.make_bundle("big", image_bytes=self.fake.layout()[2] + 1)
        code, _, err = self.run_main("deploy", "--bundle", str(big))
        self.assertEqual(code, 1)
        self.assertEqual(len(err.splitlines()), 1, err)
        self.assertIn("nothing was changed", err)

    def test_adb_writes_only_to_tmp(self):
        self.give_vendor_state()
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.assert_vendor_secured()
        self.deploy(self.make_bundle("two"))
        self.assertIn(installer.DEVICE_IMAGE, self.fake.pushes)
        self.assertEqual({p.rsplit("/", 1)[0] for p in self.fake.pushes},
                         {installer.DEVICE_WORK, installer.DEVICE_STAGE})
        self.assertIn("/data/.setting.ini.awtrix-new", self.fake.copies)
        self.assertFalse(self.installer_leftovers())

    def test_deploy_waits_for_a_web_update_and_changes_nothing(self):
        self.fake.update_lock_busy = True
        with self.assertRaisesRegex(installer.InstallError,
                                    "web update is being uploaded or installed"):
            self.deploy(self.bundle)
        self.assertIsNone(self.fake.slot())
        self.assertFalse(self.installer_leftovers())
        self.assertEqual(len([c for c in self.fake.commands if " lock " in c]),
                         installer.LOCK_TRIES)

    def use_fake_clock(self, per_command=0.0):
        """The installer's clock and sleep follow the fake device's clock; every adb command
        takes per_command seconds."""
        self.fake.adb_seconds = per_command

        def advance(seconds):
            self.fake.clock += seconds
        installer.time.sleep = advance
        patcher = mock.patch.object(installer.time, "monotonic", lambda: self.fake.clock)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_deploy_asks_for_a_lock_that_ends_with_a_stale_token(self):
        self.deploy(self.bundle)
        self.assertEqual(self.fake.lock_stale, installer.TOKEN_STALE_SECONDS)
        self.assertEqual([c for c in self.fake.commands if f"{installer.DEVICE_HELPER} lock" in c],
                         [f"{installer.DEVICE_HELPER} lock {installer.UPDATE_WORK} "
                          f"{installer.LOCK_TOKEN} {installer.LOCK_SECONDS} "
                          f"{installer.TOKEN_STALE_SECONDS}; echo __AWTRIX_RC=$?"])

    def test_deploy_keeps_its_token_fresh_on_a_slow_link(self):
        self.deploy(self.bundle)
        self.use_fake_clock(per_command=4.0)
        update = self.make_bundle("two")
        started = self.fake.clock
        output = self.deploy(update)
        self.assertIn(f"awtrix-tc002d runs {self.release_of(update)}", output)
        self.assertGreater(self.fake.clock - started, installer.TOKEN_STALE_SECONDS)
        self.assertFalse(self.fake.lock_lapsed)
        self.assertLess(self.fake.token_age_max, 2 * installer.TOKEN_REFRESH_SECONDS)
        refresh = f"[ -f {installer.LOCK_TOKEN} ] && {installer.token_write(installer.LOCK_TOKEN)}"
        self.assertGreater(self.fake.commands.count(refresh), 5)
        self.assertFalse(self.fake.lock_held())
        self.assertNotIn(installer.LOCK_TOKEN, self.fake.files)

    def test_an_aborted_deploy_frees_the_lock_and_starts_the_release_by_itself(self):
        self.deploy(self.bundle)
        self.use_fake_clock()
        update = self.make_bundle("two")
        self.fake.lose_usb_on_push = bundle_format.IMAGE
        code, _, err = self.run_main("deploy", "--bundle", str(update))
        self.assertEqual(code, 1, err)
        self.assertTrue(self.fake.lock_held())
        self.assertEqual(self.fake.processes.get(900), "/bin/zkgui")
        self.assertEqual(self.fake.files[installer.LOCK_TOKEN].decode(),
                         f"deploy {self.fake.uptime():.2f}\n")
        self.fake.clock = self.fake.mtimes[installer.LOCK_TOKEN] + installer.TOKEN_STALE_SECONDS - 1
        self.fake.observe()
        self.assertTrue(self.fake.lock_held())
        self.assertEqual(self.fake.processes.get(900), "/bin/zkgui")
        self.fake.clock += 2
        self.fake.observe()
        self.assertFalse(self.fake.lock_held())
        self.assertNotIn(900, self.fake.processes)
        self.assertEqual(self.fake.mounted, self.release_of(self.bundle))
        self.assertIn(installer.RELEASE_DAEMON, self.fake.processes.values())

        self.fake.lost, self.fake.lose_usb_on_push = False, None
        code, out, err = self.run_main("deploy", "--bundle", str(update))
        self.assertEqual((code, err), (0, ""))
        self.assert_running(update)
        self.assertFalse(self.installer_leftovers())

    def test_token_rule_matches_the_device_code(self):
        header = (installer.REPO / "src/platform/tc002/contract/deploy_token.h").read_text()
        self.assertIn(f"#define AWTRIX_DEPLOY_TOKEN_STALE_SECONDS "
                      f"{installer.TOKEN_STALE_SECONDS}\n", header)
        self.assertIn('static const char prefix[] = "deploy ";', header)
        self.assertIn("echo deploy $up > ", installer.token_write(installer.LOCK_TOKEN))
        self.assertNotIn('"', installer.token_write(installer.LOCK_TOKEN))
        paths = (installer.REPO / "src/platform/tc002/loader/loader_paths.h").read_text()
        self.assertIn("#define AWTRIX_LOADER_RUNTIME_DIR TC002_VOLATILE_DIR\n", paths)
        self.assertIn("#define LOADER_DEPLOY_TOKEN AWTRIX_LOADER_RUNTIME_DIR TC002_DEPLOY_TOKEN\n", paths)
        self.assertIn("#define LOADER_USB_DEVICE_ROLE TC002_USB_DEVICE_ROLE\n", paths)
        self.assertIn("#define AWTRIX_LOADER_USB_ROLE TC002_USB_ROLE\n", paths)

    def test_connect_starts_the_keeper_inside_the_usb_window_of_a_new_unit(self):
        self.use_fake_clock(per_command=0.03)
        self.fake.serials["OTHER0001"] = "device"
        self.fake.power_on_at = 20.0
        code, out, err = self.run_main("connect")
        self.assertEqual((code, err), (0, ""))
        self.assertIn(f"{SERIAL} is not on USB ADB (absent): switch the clock off and on again",
                      out)
        self.assertIn("Attach only one TC002", out)
        self.assertEqual(len(self.fake.keepers), 1)
        up = self.fake.keepers[0] - self.fake.power_on_at
        self.assertTrue(2.0 <= up < 2.0 + tc002_device.CONNECT_POLL + 0.1, up)
        gaps = [b - a for a, b in zip(self.fake.polls, self.fake.polls[1:])
                if b <= self.fake.keepers[0]]
        self.assertGreater(len(gaps), 100)
        self.assertLessEqual(max(gaps), tc002_device.CONNECT_POLL + 0.07)
        self.assertIn(f"USB ADB of {SERIAL} is up: awtrix-usb-keeper started 4711", out)
        self.assertIn(f"{SERIAL} stays on USB ADB", out)
        self.assertGreaterEqual(self.fake.clock - self.fake.power_on_at, tc002_device.USB_SCAN_END)
        self.assertTrue(self.fake.usb_up())
        self.assertEqual(set(self.fake.addressed), {SERIAL})

    def test_device_commands_wait_for_usb_when_the_serial_is_absent(self):
        self.use_fake_clock(per_command=0.03)
        self.fake.power_on_at = 5.0
        code, out, err = self.run_main("status")
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(len(self.fake.keepers), 1)
        self.assertLess(out.index("switch the clock off and on again"),
                        out.index("stays on USB ADB"))
        self.assertLess(out.index("stays on USB ADB"), out.index(f"model: {installer.MODEL}"))

    def test_device_commands_with_the_serial_present_go_straight_on(self):
        code, out, err = self.run_main("status")
        self.assertEqual((code, err), (0, ""))
        self.assertNotIn("switch the clock", out)
        self.assertEqual(self.fake.keepers, [])
        self.assertEqual(self.fake.polls, [0.0, 0.0])

    def test_connect_on_a_connected_unit_starts_or_reuses_the_keeper(self):
        self.use_fake_clock(per_command=0.03)
        code, out, err = self.run_main("connect")
        self.assertEqual((code, err), (0, ""))
        self.assertNotIn("switch the clock", out)
        self.assertEqual(len(self.fake.keepers), 1)
        self.assertIn(f"{SERIAL} stays on USB ADB", out)
        self.assertLess(self.fake.clock, tc002_device.CONNECT_STABLE + 1)

    def test_connect_gives_up_after_two_minutes(self):
        self.use_fake_clock(per_command=0.03)
        self.fake.power_on_at = 1e9
        code, out, err = self.run_main("connect")
        self.assertEqual(code, 1)
        self.assertIn(f"{SERIAL} did not appear on USB ADB within {tc002_device.CONNECT_WAIT} s", err)
        self.assertAlmostEqual(self.fake.clock, tc002_device.CONNECT_WAIT, delta=1)
        self.assertEqual(self.fake.keepers, [])
        self.assertEqual(self.fake.addressed, [])

    def test_connect_reports_when_usb_does_not_come_back(self):
        self.use_fake_clock(per_command=0.03)
        self.fake.power_on_at = 5.0
        self.fake.keeper_works = False
        code, out, err = self.run_main("connect")
        self.assertEqual(code, 1)
        self.assertEqual(len(self.fake.keepers), 1)
        self.assertIn(f"USB ADB of {SERIAL} did not come back within {tc002_device.CONNECT_SETTLE} s",
                      err)

    def test_connect_tries_again_when_usb_goes_away_before_the_keeper_starts(self):
        self.use_fake_clock(per_command=0.03)
        self.fake.power_on_at = 1.0
        self.fake.keeper_replies = [(1, "", "adb.exe: device offline\n")]
        code, out, err = self.run_main("connect")
        self.assertEqual((code, err), (0, ""))
        self.assertIn("went away before the USB keeper started: switch the clock off and on again",
                      out)
        self.assertEqual(len(self.fake.keepers), 1)
        self.assertIn(f"{SERIAL} stays on USB ADB", out)

    def test_connect_stops_when_the_device_cannot_start_the_keeper(self):
        self.use_fake_clock(per_command=0.03)
        self.fake.keeper_replies = [(0, "sh: syntax error: unexpected '('\n", "")]
        code, out, err = self.run_main("connect")
        self.assertEqual(code, 1)
        self.assertIn(f"the USB keeper did not start on {SERIAL}: sh: syntax error", err)
        self.assertLess(self.fake.clock, 1)

    def test_connect_refuses_two_units_with_one_serial(self):
        self.fake.second_unit = True
        code, out, err = self.run_main("connect")
        self.assertEqual(code, 1)
        self.assertIn(f"2 devices report the serial {SERIAL}", err)
        self.assertIn("attach only one at a time", err)
        self.assertEqual(self.fake.addressed, [])

    def test_connect_leaves_network_transports_to_adb(self):
        self.fake.serials["192.168.1.9:5555"] = "device"
        network = installer.Device("adb", "192.168.1.9:5555")
        network._run = self.fake.run
        tc002_device.connect(network)
        self.assertEqual(self.fake.keepers, [])
        missing = installer.Device("adb", "192.168.1.10:5555")
        missing._run = self.fake.run
        with self.assertRaisesRegex(tc002_device.DeviceError, "not connected"):
            tc002_device.connect(missing)

    def test_deploy_refuses_a_bundle_without_counter_before_touching_the_device(self):
        with self.assertRaisesRegex(installer.InstallError, "no update counter"):
            self.deploy(self.make_bundle("old", counter=None))
        self.assertFalse(self.fake.commands)

    def test_deploy_refuses_a_bundle_without_release_image_before_touching_the_device(self):
        manifest = self.manifest_of(self.bundle)
        del manifest["image"]
        (self.bundle / "manifest.json").write_text(json.dumps(manifest, indent=2))
        (self.bundle / bundle_format.IMAGE).unlink()
        with self.assertRaisesRegex(installer.InstallError, "no release image"):
            self.deploy(self.bundle)
        self.assertFalse(self.fake.commands)

    def test_deploy_needs_the_loader_and_loop_ko(self):
        for missing in (installer.LOADER_IN_RES, installer.LOOP_IN_RES):
            self.give_loader()
            self.fake.remove(missing)
            with self.assertRaisesRegex(installer.InstallError, "run flash-loader first"):
                self.deploy(self.bundle)
            self.assertIsNone(self.fake.slot())
            self.assertFalse(self.installer_leftovers())

    def test_deploy_refuses_an_unusable_counter_before_touching_the_device(self):
        manifest = self.manifest_of(self.bundle)
        manifest["counter"] = 0
        (self.bundle / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaises((installer.InstallError, bundle_format.BundleError)):
            self.deploy(self.bundle)
        self.assertFalse(self.fake.commands)

    def test_status_shows_the_release_the_update_state_and_every_counter(self):
        self.deploy(self.bundle)
        release = self.release_of(self.bundle)
        self.fake.put(f"{installer.LEGACY_RELEASES}/1.0.0-gold-000000000000/bin/x", b"x")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            installer.status(self.device, None)
        text = output.getvalue()
        self.assertIn(f"mounted release: {release}; AWTRIX processes: {installer.RELEASE_DAEMON}", text)
        self.assertIn(f"{installer.UPDATE_STATE}: state confirmed, accepted counter {COUNTER}, "
                      f"release {release}", text)
        self.assertIn(f"{installer.LEGACY_RELEASES}: 1.0.0-gold-000000000000 (the layout before the "
                      "release slot; deploy removes it)", text)
        self.assertIn(f"{installer.TMP_BOOT_COUNTER}: 0", text)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            installer.status(self.device, self.bundle)
        self.assertIn(f"release slot: {release}, {IMAGE_BYTES} of {self.fake.layout()[2]} bytes, "
                      "verified", output.getvalue())
        self.assertFalse(self.installer_leftovers())

    def test_flash_loader_needs_yes_then_writes_res_and_the_release_and_reboots(self):
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=False, reboot=True)
        self.assertEqual(bytes(self.fake.mtd), self.stock)
        self.assertFalse(self.fake.rebooted)
        self.fake.props["init.svc.zkswe"] = "running"
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assertEqual(bytes(self.fake.mtd[:len(self.image)]), self.image)
        span = (len(self.image) + ERASE - 1) // ERASE * ERASE
        self.assertEqual(bytes(self.fake.mtd[len(self.image):span]),
                         b"\xff" * (span - len(self.image)))
        self.assertEqual(self.fake.layout()[0], span, "the slot header follows the res image")
        self.assert_slot_holds(self.bundle)
        self.assert_update_state(self.bundle)
        self.assertEqual(self.fake.props["init.svc.zkswe"], "stopped")
        self.assertTrue(self.fake.rebooted)
        record = json.loads(bytes(self.fake.files[installer.RES_RECORD]))
        self.assertEqual(record, {"sha256": sha(self.image), "bytes": len(self.image)})
        self.assertEqual(self.fake.modes[installer.RES_RECORD], 0o600)
        self.assertEqual(self.fake.owners[installer.RES_RECORD], (0, 0))
        self.assertFalse(list(self.res_dir.glob("flash-journal-*")))
        self.fake.rebooted = False
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assertIn("res already holds the target image and the release slot this release; "
                      "nothing to flash", output.getvalue())
        self.assertFalse(self.fake.rebooted)
        self.assertEqual(self.fake.slot_writes, 1)

    def test_flash_loader_writes_only_the_slot_when_res_holds_the_image(self):
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.fake.mtd[self.fake.layout()[1]] ^= 1
        writes = len([c for c in self.fake.commands if f"{installer.DEVICE_HELPER} write " in c])
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assertEqual(len([c for c in self.fake.commands if f"{installer.DEVICE_HELPER} write " in c]),
                         writes)
        self.assertEqual(self.fake.slot_writes, 2)
        self.assert_slot_holds(self.bundle)
        self.assertTrue(self.fake.rebooted)

    def test_flash_loader_removes_the_layout_before_the_release_slot(self):
        old = f"{installer.LEGACY_RELEASES}/1.1.0-gold-000000000000"
        self.fake.put(f"{old}/bin/awtrix-tc002d", b"old daemon", 0o755)
        self.fake.links[installer.DATA_DIR + "/current"] = "releases/1.1.0-gold-000000000000"
        self.fake.put(f"{installer.LEGACY_FACTORY}/bin/awtrix-tc002d", b"factory", 0o755)
        self.fake.props["init.svc.zkswe"] = "running"
        self.fake.processes[812] = f"{installer.LEGACY_FACTORY}/bin/awtrix-tc002d"
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        for path in installer.LEGACY:
            self.assertFalse(self.fake.exists(path), path)
        self.assert_slot_holds(self.bundle)
        self.assertFalse(installer.parse_awtrix_executables(
            "".join(f" -> {exe}\n" for exe in self.fake.processes.values())))
        self.assertTrue(self.fake.rebooted)

    def test_flash_loader_reboots_after_stopping_the_knobs_vendor_app(self):
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.fake.put(installer.VENDOR_MARKER, b"x")
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assertFalse(self.fake.rebooted)
        self.fake.props["init.svc.zkswe"] = "running"
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.assertEqual(self.fake.props["init.svc.zkswe"], "stopped")
        self.assertFalse(self.fake.rebooted)
        self.assertIn("vendor app stays stopped until a reboot", output.getvalue())
        self.fake.props["init.svc.zkswe"] = "running"
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assertEqual(bytes(self.fake.mtd[:len(self.image)]), self.image)
        self.assertEqual(self.fake.props["init.svc.zkswe"], "stopped")
        self.assertTrue(self.fake.rebooted)

    def test_restore_stock_in_chunks_erases_the_slot_and_removes_data(self):
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.assertIsNotNone(self.fake.slot())
        self.fake.capacity["/tmp"] = 1536 * 1024
        before = len(self.fake.pushes)
        installer.flash(self.device, self.bundle, self.res_dir, "stock", yes=True,
                        reboot=True, remove_data=True)
        pieces = [p for p in self.fake.pushes[before:] if p.endswith("engine.bin")]
        self.assertGreater(len(pieces), 1)
        self.assertEqual(bytes(self.fake.mtd), self.stock)
        self.assertIsNone(self.fake.slot())
        self.assertFalse(any(p.startswith(installer.DATA_DIR) for p in self.fake.files))
        self.assertTrue(self.fake.rebooted)

    def test_refuses_unknown_res_unless_resuming_or_accepted(self):
        self.fake.mtd = bytearray(random.Random(9).randbytes(PARTITION))
        with self.assertRaisesRegex(installer.InstallError, "refusing to write"):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True,
                            reboot=False)
        with self.assertRaisesRegex(installer.InstallError, "refusing to write"):
            installer.flash(self.device, self.bundle, self.res_dir, "stock", yes=True,
                            reboot=False)
        installer.flash(self.device, self.bundle, self.res_dir, "stock", yes=True, reboot=False,
                        accept_unknown=True)
        self.assertEqual(bytes(self.fake.mtd), self.stock)

    def test_interrupted_flash_keeps_journal_and_can_resume(self):
        self.fake.fail_write_offset = 2 * ERASE
        with self.assertRaisesRegex(installer.InstallError, "do NOT reboot"):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True,
                            reboot=True)
        self.assertFalse(self.fake.rebooted)
        self.assertTrue(list(self.res_dir.glob("flash-journal-*")))
        self.fake.fail_write_offset = None
        self.fake.mtd[0:ERASE] = b"\0" * ERASE
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assertEqual(bytes(self.fake.mtd[:len(self.image)]), self.image)
        self.assert_slot_holds(self.bundle)
        self.assertFalse(list(self.res_dir.glob("flash-journal-*")))

    def test_flash_needs_usb_model_and_serial(self):
        self.fake.serials["192.168.1.9:5555"] = "device"
        network = installer.Device("adb", "192.168.1.9:5555")
        network._run = self.fake.run
        with self.assertRaisesRegex(tc002_device.DeviceError, "USB"):
            installer.flash(network, self.bundle, self.res_dir, "awtrix", yes=True, reboot=False)
        self.fake.props["ro.product.model"] = "other"
        with self.assertRaisesRegex(installer.InstallError, "model"):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True,
                            reboot=False)
        with self.assertRaisesRegex(tc002_device.DeviceError, "explicit --serial"):
            installer.Device("adb", None)
        with self.assertRaisesRegex(tc002_device.DeviceError, "explicit --serial"):
            installer.Device("adb", "-d")
        missing = installer.Device("adb", "OTHER")
        missing._run = self.fake.run
        with self.assertRaisesRegex(tc002_device.DeviceError, "not connected"):
            missing.require_connected()

    def test_corrupted_res_images_are_rejected(self):
        (self.res_dir / "awtrix-res.img").write_bytes(self.image[:-1] + b"\0")
        with self.assertRaisesRegex(installer.InstallError, "does not match"):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True,
                            reboot=False)

    def give_vendor_state(self, settings=SETTINGS, supplicant=SUPPLICANT):
        self.fake.put(installer.VENDOR_SETTINGS, settings, 0o666, (1000, 1000))
        self.fake.put(installer.VENDOR_SUPPLICANT, supplicant, 0o666, (1010, 1010))
        self.fake.put(installer.VENDOR_HOSTAPD, HOSTAPD, 0o660, (1010, 1010))
        self.fake.put(installer.SUPPLICANT_TEMPLATE, TEMPLATE, 0o644)
        self.fake.props.update({"persist.wifi.on": "1", "persist.softap.on": "0"})
        self.fake.put(f"{installer.PERSIST_DIR}/persist.wifi.on", b"1", 0o600)
        self.fake.put(f"{installer.PERSIST_DIR}/persist.softap.on", b"0", 0o600)

    def secure(self, **kwargs):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = installer.secure_vendor(self.device, **kwargs)
        return result, output.getvalue()

    def backup_dir(self, suffix=""):
        return self.private / f"{time.strftime('%Y-%m-%d')}-vendor-secrets-backup{suffix}"

    def assert_vendor_secured(self):
        fake = self.fake
        settings, supplicant = installer.VENDOR_SETTINGS, installer.VENDOR_SUPPLICANT
        self.assertEqual(bytes(fake.files[settings]), CLEAN_SETTINGS)
        self.assertEqual((fake.modes[settings], fake.owners[settings]), (0o666, (1000, 1000)))
        self.assertEqual(bytes(fake.files[supplicant]), TEMPLATE)
        self.assertEqual((fake.modes[supplicant], fake.owners[supplicant]), (0o660, (1010, 1010)))
        self.assertEqual(bytes(fake.files[installer.VENDOR_HOSTAPD]), HOSTAPD)
        self.assertEqual((fake.props["persist.wifi.on"], fake.props["persist.softap.on"]),
                         ("0", "0"))
        self.assertEqual(bytes(fake.files[f"{installer.PERSIST_DIR}/persist.wifi.on"]), b"0")
        self.assertFalse([p for p in fake.files if p.endswith(".awtrix-new")])

    def test_secure_vendor_backs_up_then_removes_credentials(self):
        self.give_vendor_state()
        result, output = self.secure()
        backup = self.backup_dir()
        self.assertEqual(result, {"changed": True, "backup": backup})
        self.assertEqual(sorted(p.name for p in self.private.iterdir()), [backup.name])
        self.assertEqual((backup / "setting.ini").read_bytes(), SETTINGS)
        self.assertEqual((backup / "wpa_supplicant.conf").read_bytes(), SUPPLICANT)
        self.assertEqual((backup / "hostapd.conf").read_bytes(), HOSTAPD)
        record = (backup / "manifest.json").read_bytes()
        files = json.loads(record)["files"]
        self.assertEqual(files[installer.VENDOR_SETTINGS],
                         {"file": "setting.ini", "bytes": len(SETTINGS), "sha256": sha(SETTINGS),
                          "mode": "0666", "owner": "1000:1000"})
        self.assertEqual(files[installer.VENDOR_HOSTAPD]["sha256"], sha(HOSTAPD))
        self.assertEqual(json.loads(record)["properties"],
                         {"persist.wifi.on": "1", "persist.softap.on": "0"})
        self.assert_vendor_secured()
        for value in VALUES:
            self.assertNotIn(value, output)
            self.assertNotIn(value.encode(), record)
        for key in ("secretKey", "wifiPwd", "refreshTokenExpireTime", "macAddress", "toolsOrder"):
            self.assertIn(key, output)
        replaced = [c for c in self.fake.commands if re.search(r"mv -f \S+\.awtrix-new ", c)]
        self.assertEqual(len(replaced), 2)
        targets = (installer.VENDOR_SETTINGS, installer.VENDOR_SUPPLICANT)
        for command, target in zip(replaced, targets):
            self.assertRegex(command, r"chown \d+:\d+ \S+ && chmod 0[0-7]{3} \S+ && sync && "
                                      rf"mv -f \S+\.awtrix-new {re.escape(target)} && sync")

    def test_secure_vendor_is_idempotent(self):
        self.give_vendor_state()
        self.secure()
        pushes, commands = len(self.fake.pushes), len(self.fake.commands)
        result, output = self.secure()
        self.assertEqual(result, {"changed": False, "backup": None})
        self.assertEqual(len(self.fake.pushes), pushes)
        self.assertFalse([c for c in self.fake.commands[commands:] if "setprop" in c or "mv" in c])
        self.assertEqual(sorted(p.name for p in self.private.iterdir()), [self.backup_dir().name])
        self.assert_vendor_secured()

    def test_backup_sync_failure_leaves_vendor_files_and_properties_unchanged(self):
        self.give_vendor_state()
        files = {path: bytes(data) for path, data in self.fake.files.items()}
        properties = dict(self.fake.props)
        with mock.patch.object(installer.flash_backup, "sync_directory", side_effect=OSError("sync failed")):
            with self.assertRaisesRegex(OSError, "sync failed"):
                self.secure()
        self.assertEqual(self.fake.files, files)
        self.assertEqual(self.fake.props, properties)
        self.assertFalse(self.fake.pushes)

    def test_secure_vendor_keeps_every_backup(self):
        self.give_vendor_state()
        first = self.secure()[0]["backup"]
        again = SETTINGS.replace(b"k-secret-1", b"k-secret-9")
        self.give_vendor_state(settings=again)
        second = self.secure()[0]["backup"]
        self.assertEqual(second, self.backup_dir("-2"))
        self.assertEqual((first / "setting.ini").read_bytes(), SETTINGS)
        self.assertEqual((second / "setting.ini").read_bytes(), again)
        self.assert_vendor_secured()

    def test_secure_vendor_dry_run_changes_nothing(self):
        self.give_vendor_state()
        result, output = self.secure(apply=False)
        self.assertEqual(result, {"changed": True, "backup": None})
        self.assertIn("dry run", output)
        self.assertEqual(list(self.private.iterdir()), [])
        self.assertEqual(bytes(self.fake.files[installer.VENDOR_SETTINGS]), SETTINGS)
        self.assertEqual(self.fake.props["persist.wifi.on"], "1")
        self.assertFalse(self.fake.pushes)

    def test_secure_vendor_keeps_the_original_when_the_copy_arrives_damaged(self):
        self.give_vendor_state()
        self.fake.corrupt_copy = ".setting.ini.awtrix-new"
        with self.assertRaisesRegex(installer.InstallError, "setting.ini.*arrived damaged"):
            self.secure()
        self.assertEqual(bytes(self.fake.files[installer.VENDOR_SETTINGS]), SETTINGS)
        self.assertFalse([p for p in self.fake.files if p.endswith(".awtrix-new")])
        self.assertEqual((self.backup_dir() / "setting.ini").read_bytes(), SETTINGS)

    def test_secure_vendor_refusals(self):
        self.give_vendor_state()
        with self.assertRaisesRegex(installer.InstallError, "outside the repository"):
            self.secure(backup_root=installer.HERE / "private")
        self.fake.put(installer.SUPPLICANT_TEMPLATE, SUPPLICANT)
        with self.assertRaisesRegex(installer.InstallError, "template .* network"):
            self.secure()
        self.assertEqual(bytes(self.fake.files[installer.VENDOR_SETTINGS]), SETTINGS)
        self.assertEqual(list(self.private.iterdir()), [])
        self.fake.serials["192.168.1.9:5555"] = "device"
        network = installer.Device("adb", "192.168.1.9:5555")
        network._run = self.fake.run
        pulls = len(self.fake.pulls)
        with self.assertRaisesRegex(tc002_device.DeviceError, "USB"):
            installer.secure_vendor(network)
        self.assertEqual(len(self.fake.pulls), pulls)

    def test_secure_vendor_needs_the_property_to_persist(self):
        self.give_vendor_state()
        self.fake.persist_fails = True
        with self.assertRaisesRegex(installer.InstallError, "persist.wifi.on=0 .* reboot"):
            self.secure()
        self.fake.persist_fails = False
        self.assertTrue(self.secure()[0]["changed"])
        self.assert_vendor_secured()

    def test_secure_vendor_without_vendor_files_turns_wifi_off(self):
        self.fake.props["persist.wifi.on"] = "1"
        result, _ = self.secure()
        self.assertTrue(result["changed"])
        self.assertEqual(self.fake.props["persist.wifi.on"], "0")
        self.assertEqual(sorted(p.name for p in result["backup"].iterdir()), ["manifest.json"])

    def test_status_shows_counters_markers_and_vendor_wifi(self):
        self.fake.put(installer.BOOT_COUNTER, b"3\n")
        self.fake.put(installer.RESCUE_MARKER, b"last starts never became healthy: rescue\n")
        self.fake.props["persist.wifi.on"] = "0"
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            installer.status(self.device, None)
        self.assertIn(f"{installer.BOOT_COUNTER}: 3", output.getvalue())
        self.assertIn(f"{installer.TMP_BOOT_COUNTER}: 0", output.getvalue())
        self.assertIn(f"{installer.RESCUE_MARKER}: last starts never became healthy: rescue",
                      output.getvalue())
        self.assertNotIn(installer.VENDOR_MARKER, output.getvalue())
        self.assertIn("persist.wifi.on=0, persist.softap.on=unset", output.getvalue())

    def test_flash_loader_secures_the_vendor_app_first(self):
        self.give_vendor_state()
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=False, reboot=True)
        self.assertEqual(bytes(self.fake.files[installer.VENDOR_SETTINGS]), SETTINGS)
        self.assertEqual(self.fake.props["persist.wifi.on"], "1")
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assert_vendor_secured()
        self.assertEqual(bytes(self.fake.mtd[:len(self.image)]), self.image)
        commands = self.fake.commands
        secured = next(i for i, c in enumerate(commands) if "setprop persist.wifi.on 0" in c)
        written = next(i for i, c in enumerate(commands) if f"{installer.DEVICE_HELPER} write" in c)
        self.assertLess(secured, written)

    def test_flash_loader_stops_the_vendor_writers_before_securing(self):
        self.give_vendor_state()
        self.fake.props.update({"init.svc.zkswe": "running", "init.svc.wpa_supplicant": "running"})
        self.fake.writers = [("zkswe", installer.VENDOR_SETTINGS, SETTINGS),
                             ("wpa_supplicant", installer.VENDOR_SUPPLICANT, SUPPLICANT)]
        installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True, reboot=True)
        self.assert_vendor_secured()
        self.assertTrue(self.fake.rebooted)
        commands = self.fake.commands
        replaced = next(i for i, c in enumerate(commands) if " mv -f " in c)
        for service in ("zkswe", "wpa_supplicant"):
            stopped = commands.index(f"setprop ctl.stop {service}; echo __AWTRIX_RC=$?")
            self.assertLess(stopped, replaced)

    def test_flash_loader_checks_the_vendor_state_again_after_writing(self):
        self.give_vendor_state()
        self.fake.on_write = lambda: self.fake.put(installer.VENDOR_SUPPLICANT, SUPPLICANT, 0o660,
                                                   (1010, 1010))
        with self.assertRaisesRegex(installer.InstallError, "came back"):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True,
                            reboot=True)
        self.assertFalse(self.fake.rebooted)

    def test_flash_loader_stops_when_the_vendor_app_cannot_be_secured(self):
        self.give_vendor_state()
        self.fake.put(installer.SUPPLICANT_TEMPLATE, SUPPLICANT)
        with self.assertRaisesRegex(installer.InstallError, "template"):
            installer.flash(self.device, self.bundle, self.res_dir, "awtrix", yes=True,
                            reboot=True)
        self.assertEqual(bytes(self.fake.mtd), self.stock)
        self.assertFalse(self.fake.rebooted)


class HelperFunctionTest(unittest.TestCase):
    def test_cleanup_keeps_the_first_error(self):
        def lost():
            raise installer.InstallError("adb shell failed: adb: device offline")
        with self.assertRaises(installer.InstallError) as caught:
            with installer.cleanup("removing /tmp/x", lost):
                raise installer.InstallError("device command failed (1): cp")
        self.assertEqual(str(caught.exception), "device command failed (1): cp")
        self.assertEqual(installer.cleanup_failures(caught.exception),
                         ["removing /tmp/x failed as well: adb shell failed: adb: device offline"])
        self.assertTrue(installer.usb_lost(caught.exception))
        with self.assertRaisesRegex(installer.InstallError, "device offline"):
            with installer.cleanup("removing /tmp/x", lost):
                pass
        self.assertFalse(installer.usb_lost(installer.InstallError("nothing was changed")))
        for text in ("adb.exe: device '0123456789ABCDEF' not found", "error: device offline",
                     "error: no devices/emulators found", "adb: error: closed"):
            self.assertTrue(installer.usb_lost(installer.InstallError(text)), text)

    def test_parsers(self):
        results = installer.parse_results('noise\n{"command":"hash","result":"ok"}\n{bad\n', "hash")
        self.assertEqual(results, [{"command": "hash", "result": "ok"}])
        listing = ("lrwxrwxrwx 1 0 0 0 Sep 25 10:00 /proc/1/exe -> /bin/init\n"
                   "ls: /proc/2/exe: No such file or directory\n"
                   f"lrwxrwxrwx 1 0 0 0 Sep 25 10:00 /proc/812/exe -> {installer.RELEASE_DAEMON}\n"
                   "lrwxrwxrwx 1 0 0 0 Sep 25 10:00 /proc/840/exe -> "
                   "/data/awtrix-ng/releases/1.1.2-gold/bin/awtrix-linux (deleted)\n"
                   "lrwxrwxrwx 1 0 0 0 Sep 25 10:00 /proc/841/exe -> "
                   "/res/awtrix-ng/factory/bin/awtrix-tc002d\n"
                   "lrwxrwxrwx 1 0 0 0 Sep 25 10:00 /proc/842/exe -> /tmp/awtrix-releases/x\n")
        self.assertEqual(installer.parse_awtrix_executables(listing), {
            installer.RELEASE_DAEMON, "/data/awtrix-ng/releases/1.1.2-gold/bin/awtrix-linux",
            "/res/awtrix-ng/factory/bin/awtrix-tc002d"})

    def test_update_state_document_is_what_update_state_writes(self):
        self.assertEqual(
            installer.update_state_document("1.2.0-gabc-0123456789ab", 1790000123, "ab" * 32),
            '{"schema":2,"state":"confirmed","acceptedCounter":1790000123,"current":'
            '{"counter":1790000123,"payloadSha256":"' + "ab" * 32 +
            '","release":"1.2.0-gabc-0123456789ab","target":"awtrix-ng:tc002"},'
            '"candidate":null,"lease":null,"failure":""}')

    def test_slot_holds_and_describe_slot(self):
        manifest = {"release": "1.2.0-g1", "counter": 7}
        image = {"size": 100, "sha256": "ab" * 32}
        slot = {"slot": "verified", "release": "1.2.0-g1", "counter": 7, "imageBytes": 100,
                "sha256": "ab" * 32, "capacity": 4096}
        self.assertTrue(installer.slot_holds(slot, manifest, image))
        self.assertEqual(installer.describe_slot(slot), "1.2.0-g1, 100 of 4096 bytes, verified")
        for change in ({"slot": "valid"}, {"slot": "corrupt"}, {"release": "1.2.0-g2"}, {"counter": 8},
                       {"imageBytes": 99}, {"sha256": "cd" * 32}):
            self.assertFalse(installer.slot_holds(dict(slot, **change), manifest, image), change)
        self.assertEqual(installer.describe_slot({"slot": "empty", "capacity": 4096}), "empty, 4096 bytes")

    def test_bundle_counter(self):
        with self.assertRaisesRegex(installer.InstallError, "no update counter"):
            installer.bundle_counter({"release": "x"})
        self.assertEqual(installer.bundle_counter({"release": "1.2-g1-abc", "counter": 7}), 7)
        for manifest in ({"release": "x", "counter": 0}, {"release": "x", "counter": True},
                         {"release": "x", "counter": 1 << 63}, {"release": "x", "counter": "7"},
                         {"release": "1.2+local-abc", "counter": 7},
                         {"release": "x" * 65, "counter": 7}):
            with self.assertRaises(installer.InstallError, msg=manifest):
                installer.bundle_counter(manifest)

    @unittest.skipUnless(sys.platform == "win32", "Windows paths")
    def test_windows_to_wsl(self):
        self.assertEqual(installer.windows_to_wsl(r"C:\Users\x\file.bin"), "/mnt/c/Users/x/file.bin")
        self.assertTrue(re.fullmatch(r"/mnt/[a-z]/.*", installer.windows_to_wsl(__file__)))



def parse_container(data):
    header = struct.Struct(">8sHHIQQHH32s")
    (magic, major, minor, manifest_size, payload_size, counter, target_length, release_length,
     payload_sha) = header.unpack_from(data)
    target = data[header.size:header.size + target_length].decode()
    release = data[header.size + target_length:header.size + target_length + release_length]
    return {"magic": magic, "version": (major, minor), "counter": counter, "target": target,
            "release": release.decode(), "manifest_size": manifest_size,
            "extra": data[header.size + target_length + release_length:manifest_size],
            "payload": data[manifest_size + 32:], "payload_size": payload_size,
            "payload_sha": payload_sha.hex(),
            "manifest_sha": data[manifest_size:manifest_size + 32].hex(),
            "manifest": data[:manifest_size]}


@unittest.skipUnless(os.name == "posix", "needs Linux")
class PackageTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="awtrix-package-test-"))
        self.addCleanup(shutil.rmtree, self.work, ignore_errors=True)
        self.bundle = self.work / "bundle"
        noise = random.Random("package")
        for path, size in BUNDLE_FILES:
            (self.bundle / path).parent.mkdir(parents=True, exist_ok=True)
            (self.bundle / path).write_bytes(noise.randbytes(size))
        self.manifest = write_manifest(self.bundle, "1.1.2-gabc1234", "abc1234", COUNTER)

    def package(self, out):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            return installer.package(self.bundle, out)

    def test_package_carries_the_release_image(self):
        out = self.work / "out" / installer.PACKAGE_NAME
        info = self.package(out)
        data = out.read_bytes()
        self.assertEqual(info["bytes"], len(data))
        self.assertEqual(info["sha256"], sha(data))
        container = parse_container(data)
        self.assertEqual(container["magic"], b"AWUPD003")
        self.assertEqual(container["version"], (3, 0))
        self.assertEqual(container["target"], "awtrix-ng:tc002")
        self.assertEqual(container["release"], self.manifest["release"])
        self.assertEqual(container["counter"], COUNTER)
        self.assertEqual(container["extra"], b"")
        self.assertEqual(container["payload"], (self.bundle / bundle_format.IMAGE).read_bytes())
        self.assertEqual(container["payload_size"], len(container["payload"]))
        self.assertEqual(container["payload_sha"], self.manifest["image"]["sha256"])
        self.assertEqual(container["payload_sha"], info["payload_sha256"])
        self.assertEqual(container["manifest_sha"], sha(container["manifest"]))

    @unittest.skipUnless(os.environ.get("AWTRIX_UPDATE_VERIFY"),
                         "set AWTRIX_UPDATE_VERIFY to a build of awtrix-update-verify")
    def test_runtime_verifier_accepts_the_package(self):
        out = self.work / installer.PACKAGE_NAME
        info = self.package(out)
        result = subprocess.run([os.environ["AWTRIX_UPDATE_VERIFY"], "--package", str(out),
                                 "--target", "awtrix-ng:tc002",
                                 "--current-counter", str(COUNTER - 1)],
                                capture_output=True, text=True)
        verdict = json.loads(result.stdout)
        self.assertTrue(verdict["ok"], verdict)
        self.assertEqual((verdict["release"], verdict["counter"], verdict["payload_bytes"]),
                         (info["release"], COUNTER, IMAGE_BYTES))
        result = subprocess.run([os.environ["AWTRIX_UPDATE_VERIFY"], "--package", str(out),
                                 "--target", "awtrix-ng:tc002",
                                 "--current-counter", str(COUNTER)], capture_output=True, text=True)
        self.assertFalse(json.loads(result.stdout)["ok"])

    def test_package_refuses_a_dirty_bundle_unless_allowed(self):
        write_manifest(self.bundle, "1.1.2-gabc1234-dirty", "abc1234", COUNTER, dirty=True)
        out = self.work / "dirty.awup"
        with self.assertRaisesRegex(installer.InstallError, "working tree"):
            self.package(out)
        self.assertFalse(out.exists())
        with contextlib.redirect_stdout(io.StringIO()):
            installer.package(self.bundle, out, allow_dirty=True)
        self.assertTrue(out.is_file())

    def test_package_refuses_existing_output_and_bundles_without_counter(self):
        out = self.work / installer.PACKAGE_NAME
        out.write_bytes(b"x")
        with self.assertRaisesRegex(installer.InstallError, "already exists"):
            self.package(out)
        self.assertEqual(out.read_bytes(), b"x")
        write_manifest(self.bundle, "1.1.2-gabc1234", "abc1234", None)
        with self.assertRaisesRegex(installer.InstallError, "no update counter"):
            self.package(self.work / "other.awup")
        self.assertFalse((self.work / "other.awup").exists())

    def test_main_packages_without_keys_or_openssl(self):
        err = io.StringIO()
        with mock.patch.dict(os.environ, {"PATH": ""}), contextlib.redirect_stderr(err), \
                contextlib.redirect_stdout(io.StringIO()):
            code = installer.main(["package", "--bundle", str(self.bundle),
                                   "--out", str(self.work / "x.awup")])
        self.assertEqual(code, 0, err.getvalue())
        self.assertTrue((self.work / "x.awup").exists())


HOST_FLASHER = os.environ.get("AWTRIX_TC002_HOST_FLASHER")


@unittest.skipUnless(HOST_FLASHER and os.name == "posix",
                     "set AWTRIX_TC002_HOST_FLASHER to a host build of awtrix-tc002-flash")
class HelperLockTest(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="awtrix-lock-test-"))
        self.addCleanup(shutil.rmtree, self.work, ignore_errors=True)
        self.directory = self.work / "awtrix-update"
        self.token = self.work / "token"
        self.token.write_text("deploy\n")

    def tearDown(self):
        self.token.unlink(missing_ok=True)
        time.sleep(0.5)

    def lock(self, seconds=30, stale=3600):
        result = subprocess.run([HOST_FLASHER, "lock", str(self.directory), str(self.token),
                                 str(seconds), str(stale)], capture_output=True, text=True, timeout=10)
        return result.returncode, installer.parse_results(result.stdout, "lock")

    def held_elsewhere(self):
        import fcntl
        with open(self.directory / "lock", "r+") as handle:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return True
            fcntl.flock(handle, fcntl.LOCK_UN)
            return False

    def wait_released(self, limit=3.0):
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            if not self.held_elsewhere():
                return True
            time.sleep(0.1)
        return False

    def test_lock_is_held_until_the_token_goes(self):
        code, results = self.lock()
        self.assertEqual((code, results[-1]["result"]), (0, "ok"))
        self.assertEqual(self.directory.stat().st_mode & 0o777, 0o700)
        self.assertEqual((self.directory / "lock").stat().st_mode & 0o777, 0o600)
        self.assertTrue(self.held_elsewhere())
        code, results = self.lock()
        self.assertEqual((code, results[-1]["stage"]), (1, "busy"))
        self.token.unlink()
        self.assertTrue(self.wait_released())
        self.token.write_text("again\n")
        self.assertEqual(self.lock()[0], 0)

    def test_lock_ends_after_its_time(self):
        self.assertEqual(self.lock(seconds=1)[0], 0)
        self.assertTrue(self.held_elsewhere())
        self.assertTrue(self.wait_released(4.0))

    def test_lock_needs_a_token_and_sane_arguments(self):
        self.token.unlink()
        code, results = self.lock()
        self.assertEqual((code, results[-1]["stage"]), (1, "token"))
        self.token.write_text("x")
        for seconds in ("0", "86401", "-1", "x"):
            result = subprocess.run([HOST_FLASHER, "lock", str(self.directory), str(self.token),
                                     seconds, "30"], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 64, seconds)
        result = subprocess.run([HOST_FLASHER, "lock", str(self.directory), str(self.token), "30"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 64, "the staleness window is required")
        for stale in ("0", "86401", "-1", "x"):
            result = subprocess.run([HOST_FLASHER, "lock", str(self.directory), str(self.token),
                                     "30", stale], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 64, stale)

    def age_token(self, seconds):
        written = time.time() - seconds
        os.utime(self.token, (written, written))

    def test_lock_ends_once_the_token_goes_stale(self):
        started = time.monotonic()
        code, results = self.lock(stale=2)
        self.assertEqual((code, results[-1]["result"], results[-1]["stale"]), (0, "ok", 2))
        self.assertTrue(self.held_elsewhere())
        self.assertTrue(self.wait_released(4.0))
        self.assertGreater(time.monotonic() - started, 1.5)

    def test_lock_stays_while_the_token_is_rewritten(self):
        self.assertEqual(self.lock(stale=2)[0], 0)
        for _ in range(8):
            time.sleep(0.5)
            self.token.write_text("deploy\n")
            self.assertTrue(self.held_elsewhere())
        self.assertTrue(self.wait_released(4.0))

    def test_lock_counts_staleness_from_the_last_rewrite_whatever_the_clock_says(self):
        self.age_token(-3600)
        self.assertEqual(self.lock(stale=2)[0], 0)
        time.sleep(1.2)
        self.assertTrue(self.held_elsewhere())
        self.age_token(-7200)
        time.sleep(1.2)
        self.assertTrue(self.held_elsewhere())
        self.assertTrue(self.wait_released(4.0))

    def test_lock_refuses_a_token_that_is_already_stale(self):
        self.age_token(10)
        code, results = self.lock(stale=5)
        self.assertEqual((code, results[-1]["stage"]), (1, "token"))
        self.assertFalse((self.directory / "lock").exists())

    def test_lock_judges_a_new_token_by_its_uptime_not_by_a_stepped_clock(self):
        subprocess.run(["sh", "-c", installer.token_write(str(self.token))], check=True,
                       timeout=10)
        self.assertRegex(self.token.read_text(), r"^deploy \d+\.\d\d\n$")
        self.age_token(56 * 365 * 86400)
        started = time.monotonic()
        code, results = self.lock(stale=2)
        self.assertEqual((code, results[-1]["result"]), (0, "ok"))
        time.sleep(1.2)
        self.assertTrue(self.held_elsewhere())
        self.assertTrue(self.wait_released(4.0))
        self.assertGreater(time.monotonic() - started, 1.5)

    def test_lock_refuses_a_token_whose_uptime_is_stale_whatever_its_mtime(self):
        up = float(Path("/proc/uptime").read_text().split()[0])
        self.token.write_text(f"deploy {up - 10:.2f}\n")
        self.age_token(-3600)
        code, results = self.lock(stale=5)
        self.assertEqual((code, results[-1]["stage"]), (1, "token"))
        self.assertFalse((self.directory / "lock").exists())



if __name__ == "__main__":
    unittest.main()
