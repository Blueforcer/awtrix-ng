#!/usr/bin/env python3
"""Exercise the configured hardened unit under a real systemd system manager.

Requires root for temporary runtime unit registration and /proc inspection;
the tested daemon must run as a non-root DynamicUser. No /etc unit installation,
Internet connection or hardware is used. Only generated fixture/state paths
are removed. Missing security features are failures, never skipped tests.
"""

from __future__ import annotations

import argparse
import errno
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import ssl
import subprocess
import tempfile
import time
import uuid


REQUIRED = {
    "DynamicUser": "yes", "StateDirectoryMode": "0700", "UMask": "0077",
    "NoNewPrivileges": "yes", "CapabilityBoundingSet": "", "AmbientCapabilities": "",
    "ProtectSystem": "strict", "ProtectHome": "yes", "PrivateTmp": "yes",
    "PrivateDevices": "yes", "ProtectKernelTunables": "yes", "ProtectKernelModules": "yes",
    "ProtectKernelLogs": "yes", "ProtectControlGroups": "yes", "ProtectClock": "yes",
    "ProtectHostname": "yes", "RestrictSUIDSGID": "yes", "RestrictRealtime": "yes",
    "RestrictNamespaces": "yes", "LockPersonality": "yes", "MemoryDenyWriteExecute": "yes",
    "SystemCallArchitectures": "native", "SystemCallFilter": "@system-service",
    "SystemCallErrorNumber": "EPERM", "LimitCORE": "0",
}
NAME = re.compile(r"awtrix-test-[0-9a-f]{16}\Z")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def command(args, *, timeout=30, check=True):
    result = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, timeout=timeout)
    if check and result.returncode:
        # Failure messages contain no credential bytes.
        raise AssertionError(f"{Path(args[0]).name} {args[1] if len(args) > 1 else ''} failed ({result.returncode})")
    return result


def service_fields(text):
    section = None
    values = {}
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("["):
            section = stripped
        elif section == "[Service]" and stripped and not stripped.startswith(("#", ";")):
            require("=" in line and not line.endswith("\\"), "unit must use one-line service directives")
            key, value = line.split("=", 1)
            values.setdefault(key.strip(), []).append(value.strip())
    return values


def unit_quote(value):
    require(not any(ord(c) < 32 for c in value), "control characters in unit path")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%").replace("$", "$$") + '"'


def render_unit(template, name, fixture, binary, webui, asset_bind=None):
    require(NAME.fullmatch(name), "invalid generated service name")
    original = service_fields(template)
    for key, expected in REQUIRED.items():
        require(original.get(key) == [expected], f"required hardening directive missing or changed: {key}")
    for key in ("StateDirectory", "EnvironmentFile", "ExecStart"):
        require(len(original.get(key, [])) == 1, f"expected one {key} directive")
    require(not re.search(r"@[A-Z_]+@", template), "unit is not configured; run CMake first")
    credentials = {"admin.token", "tls.crt", "tls.key"}
    found = set()
    result = []
    for line in template.splitlines():
        if line.startswith("StateDirectory="):
            line = "StateDirectory=" + name
        elif line.startswith("EnvironmentFile="):
            # Generated fixture paths contain no whitespace or specifiers.
            line = "EnvironmentFile=" + str(fixture / "service.env")
        elif line.startswith("LoadCredential="):
            identifier, separator, _ = line.partition("=")[2].partition(":")
            require(separator and identifier in credentials and identifier not in found,
                    "unexpected or duplicate LoadCredential entry")
            found.add(identifier)
            line = "LoadCredential=" + identifier + ":" + str(fixture / identifier)
        elif line.startswith("ExecStart="):
            line = ("ExecStart=" + unit_quote(str(binary)) + " --hardened --data " +
                    unit_quote("/var/lib/" + name) + " --webui " + unit_quote(str(webui)) +
                    " --port ${AWTRIX_PORT} --listen ${AWTRIX_LISTEN} --origin ${AWTRIX_ORIGIN}"
                    " --credentials-file %d/admin.token --tls-cert %d/tls.crt --tls-key %d/tls.key")
        result.append(line)
        if line.startswith("ExecStart=") and asset_bind:
            source, destination = asset_bind
            result.append("BindReadOnlyPaths=" + str(source) + ":" + str(destination))
    require(found == credentials, "unit does not load all three required credentials")
    rendered = "\n".join(result) + "\n"
    changed = service_fields(rendered)
    for key, value in original.items():
        if key == "BindReadOnlyPaths" and asset_bind:
            require(changed.get(key) == value + [str(asset_bind[0]) + ":" + str(asset_bind[1])],
                    "test changed existing read-only asset mounts")
            continue
        if key not in {"StateDirectory", "EnvironmentFile", "LoadCredential", "ExecStart"}:
            require(changed.get(key) == value, f"test changed service directive: {key}")
    return rendered


def require_root_systemd():
    require(os.name == "posix" and os.geteuid() == 0, "run this integration test as root on Linux")
    require(Path("/proc/1/comm").read_text().strip() == "systemd", "PID 1 must be systemd")
    for program in ("systemctl", "openssl"):
        require(shutil.which(program), f"required program missing: {program}")


def remove_state(name):
    """Remove only this invocation's exact generated systemd state paths."""
    require(NAME.fullmatch(name), "refusing cleanup: invalid state name")
    public = Path("/var/lib") / name
    private = Path("/var/lib/private") / name
    require(Path("/var/lib").resolve() == Path("/var/lib"), "unexpected /var/lib alias")
    if public.is_symlink():
        require(public.resolve(strict=False) == private, "refusing cleanup: unexpected state symlink")
        public.unlink()
    elif public.exists():
        require(public.is_dir() and public.resolve() == public, "refusing cleanup: unsafe public state path")
        shutil.rmtree(public)
    if private.exists() or private.is_symlink():
        require(not private.is_symlink() and private.is_dir() and private.resolve() == private,
                "refusing cleanup: unsafe private state path")
        shutil.rmtree(private)


class ServiceFixture:
    def __init__(self, template, binary, webui):
        require_root_systemd()
        self.name = "awtrix-test-" + uuid.uuid4().hex[:16]
        self.unit = self.name + ".service"
        self.state = Path("/var/lib") / self.name
        self.private_state = Path("/var/lib/private") / self.name
        self.runtime_link = Path("/run/systemd/system") / self.unit
        self.asset_mount = Path("/run") / (self.name + "-assets")
        for path in (self.state, self.private_state, self.runtime_link, self.asset_mount):
            require(not path.exists() and not path.is_symlink(), "generated fixture name collision")
        self.root = Path(tempfile.mkdtemp(prefix="awtrix-service-test-", dir="/var/tmp"))
        # PID1 reads credentials; DynamicUser may traverse to read frozen public
        # assets through the read-only bind, but cannot list/write the fixture.
        self.root.chmod(0o711)
        self.unit_file = self.root / self.unit
        self.linked = False
        self.closed = False
        self.token = secrets.token_hex(32)
        try:
            assets = self.root / "app"
            assets.mkdir(mode=0o755)
            for source, filename, mode in ((binary, "awtrix-linux", 0o755), (webui, "index.html", 0o644)):
                before = source.stat()
                frozen = assets / filename
                shutil.copyfile(source, frozen)
                frozen.chmod(mode)
                after = source.stat()
                require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
                        (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns),
                        "input artifact changed while freezing; wait for the build to finish")
                frozen_hash = hashlib.sha256(frozen.read_bytes()).hexdigest()
                require(frozen_hash == hashlib.sha256(source.read_bytes()).hexdigest(),
                        "input artifact changed while freezing")
                if filename == "awtrix-linux":
                    self.binary_sha256 = frozen_hash
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                self.port = sock.getsockname()[1]
            self.origin = f"https://localhost:{self.port}"
            for name, contents in {
                "admin.token": self.token + "\n",
                "service.env": f"AWTRIX_LISTEN=127.0.0.1\nAWTRIX_PORT={self.port}\nAWTRIX_ORIGIN={self.origin}\n",
            }.items():
                path = self.root / name
                path.write_text(contents)
                path.chmod(0o600)
            command(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                     "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
                     "-keyout", str(self.root / "tls.key"), "-out", str(self.root / "tls.crt")])
            (self.root / "tls.key").chmod(0o600)
            (self.root / "tls.crt").chmod(0o600)
            self.context = ssl.create_default_context(cafile=str(self.root / "tls.crt"))
            self.unit_file.write_text(render_unit(template, self.name, self.root,
                self.asset_mount / "awtrix-linux", self.asset_mount / "index.html",
                asset_bind=(assets, self.asset_mount)))
            self.unit_file.chmod(0o600)
        except BaseException:
            self.close()
            raise

    def properties(self):
        names = ("MainPID", "ActiveState", "SubState", "Result", "ExecMainCode", "ExecMainStatus", "NRestarts")
        result = command(["systemctl", "show", self.unit, "--property=" + ",".join(names)], check=False)
        return dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)

    def journal(self):
        result = command(["journalctl", "--unit", self.unit, "--lines", "40", "--no-pager", "--output", "cat"],
                         check=False)
        return result.stdout.replace(self.token, "<token>")

    def start(self):
        if not self.linked:
            command(["systemctl", "link", "--runtime", str(self.unit_file)])
            self.linked = True
            command(["systemctl", "daemon-reload"])
        started = command(["systemctl", "start", self.unit], check=False)
        require(started.returncode == 0, f"service start failed ({self.unit}): " +
                json.dumps(self.properties(), sort_keys=True) + "\n" + self.journal())
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            properties = self.properties()
            if properties.get("ActiveState") == "failed" or int(properties.get("NRestarts", "0")):
                raise AssertionError(f"service failed during startup ({self.unit}): " +
                                     json.dumps(properties, sort_keys=True) + "\n" + self.journal())
            try:
                status, _ = self.request("GET", "/api/v1/version", authenticated=False)
                if status == 401:
                    return
                raise AssertionError(f"unauthenticated service returned {status}, expected 401")
            except (OSError, http.client.HTTPException):
                time.sleep(0.05)
        raise AssertionError(f"service readiness timeout ({self.unit}): " +
                             json.dumps(self.properties(), sort_keys=True) + "\n" + self.journal())

    def stop(self):
        command(["systemctl", "stop", self.unit], timeout=35)
        properties = self.properties()
        require(properties.get("MainPID") == "0", "service retained a main process after stop")
        require(properties.get("Result") == "success" and properties.get("ExecMainStatus") == "0",
                "service did not stop cleanly: " + json.dumps(properties, sort_keys=True))

    def request(self, method, path, body=None, authenticated=True, content_type="application/json"):
        headers = {"Host": f"localhost:{self.port}", "Connection": "close"}
        if authenticated:
            headers["Authorization"] = "Bearer " + self.token
        if body is not None:
            body = json.dumps(body).encode() if isinstance(body, dict) else body.encode()
            headers["Content-Type"] = content_type
        connection = http.client.HTTPSConnection("localhost", self.port, context=self.context, timeout=3)
        try:
            connection.request(method, path, body, headers)
            response = connection.getresponse()
            data = response.read()
            return response.status, data
        finally:
            connection.close()

    def json(self, method, path, body=None, content_type="application/json"):
        status, data = self.request(method, path, body, content_type=content_type)
        require(status == 200, f"{method} {path} returned HTTP {status}")
        # Mutation routes may acknowledge success with an empty/plain body.
        # Read routes used below must still return parseable JSON.
        if method != "GET" and not data:
            return None
        try:
            return json.loads(data)
        except json.JSONDecodeError:
            require(method != "GET", f"{method} {path} did not return JSON")
            return data.decode(errors="replace")

    def inspect_sandbox(self):
        properties = self.properties()
        require(properties.get("ActiveState") == "active", "service is not active")
        pid = int(properties.get("MainPID", "0"))
        require(pid > 1, "invalid service MainPID")
        status = dict(line.split(":", 1) for line in Path(f"/proc/{pid}/status").read_text().splitlines() if ":" in line)
        uids = [int(value) for value in status["Uid"].split()]
        require(len(uids) == 4 and all(value != 0 for value in uids), "daemon is running with a root UID")
        for field in ("CapEff", "CapPrm", "CapInh", "CapAmb", "CapBnd"):
            require(int(status[field].strip(), 16) == 0, f"daemon retained {field}")
        require(status["NoNewPrivs"].strip() == "1", "NoNewPrivileges is not effective")
        require(status["Seccomp"].strip() == "2", "seccomp filtering is not active")
        cmdline = Path(f"/proc/{pid}/cmdline").read_bytes()
        environment = Path(f"/proc/{pid}/environ").read_bytes()
        require(self.token.encode() not in cmdline + environment, "administrator token exposed in process metadata")
        require(b"--hardened\x00" in cmdline, "daemon does not run in hardened mode")
        # Exercise the service's read-only mount through its proc root. A unique
        # probe is immediately removed if a broken sandbox unexpectedly allows it.
        probe_name = self.name + ".readonly-probe"
        etc = os.open(f"/proc/{pid}/root/etc", os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            try:
                fd = os.open(probe_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                             0o600, dir_fd=etc)
            except OSError as error:
                require(error.errno == errno.EROFS,
                        f"read-only mount probe failed with unexpected errno {error.errno}")
            else:
                os.close(fd)
                os.unlink(probe_name, dir_fd=etc)
                raise AssertionError("ProtectSystem=strict did not make /etc read-only")
        finally:
            os.close(etc)
        return {"uid": uids[1], "capabilities": "0", "no_new_privileges": True,
                "seccomp_filter": True, "system_files_readonly": True}

    def close(self):
        if self.closed:
            return
        errors = []
        try:
            if self.linked or self.runtime_link.is_symlink():
                stopped = command(["systemctl", "stop", self.unit], timeout=35, check=False)
                properties = self.properties()
                require(stopped.returncode == 0 and properties.get("MainPID") == "0",
                        "cleanup could not stop the generated service")
                command(["systemctl", "reset-failed", self.unit], check=False)
            if self.runtime_link.exists() or self.runtime_link.is_symlink():
                require(self.runtime_link.is_symlink() and self.runtime_link.resolve() == self.unit_file,
                        "refusing cleanup of unexpected runtime unit link")
                self.runtime_link.unlink()
                command(["systemctl", "daemon-reload"])
            remove_state(self.name)
            if self.asset_mount.exists() or self.asset_mount.is_symlink():
                require(self.asset_mount.parent == Path("/run") and not self.asset_mount.is_symlink() and
                        self.asset_mount.resolve() == self.asset_mount,
                        "refusing cleanup of unexpected asset mount path")
                # systemd may leave an empty mountpoint after the unit stops.
                # Never recurse here: a still-mounted/nonempty directory fails.
                self.asset_mount.rmdir()
        except BaseException as error:
            errors.append(str(error))
        if not errors:
            require(self.root.parent == Path("/var/tmp") and self.root.name.startswith("awtrix-service-test-") and
                    not self.root.is_symlink() and self.root.resolve() == self.root,
                    "refusing cleanup of unexpected fixture directory")
            shutil.rmtree(self.root)
            self.closed = True
        if errors:
            raise AssertionError("fixture cleanup failed: " + "; ".join(errors))


def run(template, binary, webui):
    fixture = ServiceFixture(template, binary, webui)
    try:
        fixture.start()
        security = fixture.inspect_sandbox()
        require(fixture.request("GET", "/api/v1/version", authenticated=False)[0] == 401,
                "API permits unauthenticated access")
        require(fixture.json("GET", "/api/v1/version").get("version"), "authenticated API has no version")
        status, webui_bytes = fixture.request("GET", "/")
        require(status == 200 and b"<html" in webui_bytes.lower(), "authenticated Web UI is unavailable")
        fixture.json("PATCH", "/api/v1/settings", {"brightness": 37, "autoTransition": False})
        source = ("class Saved\n def init() store.set('boots', store.get('boots', 0) + 1) end\n"
                  " def draw()\n  pixel(width()-1,height()-1,0x246810)\n"
                  "  pixel(0,0,store.get('boots',0))\n end\nend\nreturn Saved()\n")
        fixture.json("PUT", "/api/v1/apps/script/saved", source, "text/plain")
        identity = (fixture.state / "identity").read_bytes()
        fixture.stop()
        require((fixture.state / "settings.json").is_file(), "settings were not persisted on stop")
        fixture.start()
        fixture.inspect_sandbox()
        require(fixture.json("GET", "/api/v1/settings")["brightness"] == 37,
                "settings did not survive service restart")
        require((fixture.state / "identity").read_bytes() == identity, "device identity changed after restart")
        script_status, saved_source = fixture.request("GET", "/api/v1/apps/script/saved")
        require(script_status == 200 and saved_source.decode() == source, "script source did not survive restart")
        fixture.json("PUT", "/api/v1/apps/active", {"name": "saved", "fast": True})
        deadline = time.monotonic() + 8
        while True:
            screen = fixture.json("GET", "/api/v1/display/screen")
            if screen["pixels"][-1] == 0x246810 and screen["pixels"][0] == 2:
                break
            require(time.monotonic() < deadline, "script pixels/store did not recover after restart")
            time.sleep(0.05)
        fixture.stop()
        return {"ok": True, "checks": ["real-systemd-dynamic-user", "https-401", "authorized-api-and-webui",
                "settings-and-identity-restart", "script-source-store-pixels-restart", "clean-sigterm", "cleanup"],
                "sandbox": security, "binary_sha256": fixture.binary_sha256}
    finally:
        fixture.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unit", type=Path, required=True, help="CMake-configured awtrix-ng.service")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--webui", type=Path, required=True)
    args = parser.parse_args()
    try:
        require_root_systemd()
        paths = [path.resolve(strict=True) for path in (args.unit, args.binary, args.webui)]
        require(all(path.is_file() for path in paths), "unit, binary and Web UI must be regular files")
        result = run(paths[0].read_text(), paths[1], paths[2])
        print(json.dumps(result, sort_keys=True))
        return 0
    except (OSError, AssertionError, ValueError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"ok": False, "error": str(error)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
