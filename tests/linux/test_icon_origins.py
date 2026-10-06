#!/usr/bin/env python3
"""Icon origins over HTTP against a freshly spawned awtrix-linux: atomic replacement,
persistence across a restart, local edits, rename, deletion and ZIP restore."""

from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import sys
import urllib.error
import urllib.request
import zipfile


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime import free_port, stop_process, wait_ready


GIF = base64.b64decode("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--webui", required=True, type=Path)
    args = parser.parse_args()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    process = None
    with tempfile.TemporaryDirectory(prefix="awtrix-icon-origins-") as directory:
        data = Path(directory).resolve()

        def request(method, path, body=None, content_type="application/json", expected=200):
            if isinstance(body, dict):
                body = json.dumps(body).encode()
            req = urllib.request.Request(base + path, data=body, method=method)
            if body is not None:
                req.add_header("Content-Type", content_type)
            try:
                response = urllib.request.urlopen(req, timeout=5)
            except urllib.error.HTTPError as error:
                response = error
            with response:
                payload = response.read()
                assert response.status == expected, (method, path, response.status, payload)
                return json.loads(payload) if payload.startswith(b"{") else payload

        def start():
            proc = subprocess.Popen(
                [str(args.binary.resolve()), "--port", str(port), "--data", str(data),
                 "--webui", str(args.webui.resolve())],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            try:
                wait_ready(proc, lambda: request("GET", "/api/v1/icons/origins") is not None)
            except BaseException:
                stop_process(proc, require_success=False)
                raise
            return proc

        def upload(path, filename, payload):
            boundary = "awtrix-origins-regression-boundary"
            body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
                    f'filename="{filename}"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
            body += payload + f"\r\n--{boundary}--\r\n".encode()
            return request("POST", path, body, f"multipart/form-data; boundary={boundary}")

        origin = {"name": "mail.gif", "hub": "https://custom.example:8443/awtrix/icons/",
                  "slug": "mail", "sha256": hashlib.sha256(GIF).hexdigest()}
        try:
            process = start()
            assert request("GET", "/api/v1/icons/origins") == {"icons": []}
            upload("/api/v1/files?dir=/ICONS", "mail.gif", GIF)
            request("PUT", "/api/v1/icons/origins", origin)
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            assert json.loads((data / "config/icon-origins.json").read_text()) == {"icons": [origin]}
            # The second save exercises replacement of an existing metadata file.
            origin["slug"] = "mail-2"
            request("PUT", "/api/v1/icons/origins", origin)
            upload("/api/v1/files?dir=/ICONS", "mail.gif", GIF + b"locally changed")
            assert request("GET", "/ICONS/mail.gif") != GIF
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            stop_process(process)
            process = start()
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            rename = lambda source, target, expected=200: request(
                "POST", "/api/v1/icons/rename", {"from": source, "to": target}, expected=expected)
            upload("/api/v1/files?dir=/ICONS", "sun.gif", GIF)
            assert rename("mail.gif", "sun.gif", 409)["error"]["code"] == "nameTaken"
            request("GET", "/api/v1/icons/rename", expected=405)
            edited = request("GET", "/ICONS/mail.gif")
            rename("mail.gif", "letter.gif")
            assert request("GET", "/ICONS/letter.gif") == edited
            request("GET", "/ICONS/mail.gif", expected=404)
            assert request("GET", "/api/v1/icons/origins") == {"icons": [{**origin, "name": "letter.gif"}]}
            rename("letter.gif", "mail.gif")
            request("DELETE", "/api/v1/files?path=/ICONS/sun.gif")
            # Unavailable metadata storage must leave the existing image and link intact.
            metadata = data / "config/icon-origins.json"
            saved_metadata = data / "saved-origins.json"
            metadata.rename(saved_metadata)
            metadata.mkdir()
            try:
                request("DELETE", "/api/v1/files?path=/ICONS/mail.gif", expected=500)
                assert (data / "ICONS/mail.gif").exists()
            finally:
                metadata.rmdir()
                saved_metadata.rename(metadata)
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            request("DELETE", "/api/v1/files?path=/ICONS/mail.gif")
            upload("/api/v1/files?dir=/ICONS", "mail.gif", GIF)
            assert request("GET", "/api/v1/icons/origins") == {"icons": []}
            # Metadata precedes image in the ZIP; the restore must defer linking until images exist.
            archive = io.BytesIO()
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as backup:
                backup.writestr("manifest.json", json.dumps({"app": "awtrix-ng", "backupFormat": 1}))
                backup.writestr("config/icon-origins.json", json.dumps({"icons": [origin]}))
                backup.writestr("ICONS/mail.gif", GIF)
            restored = upload("/api/v1/restore", "backup.zip", archive.getvalue())
            assert restored["ok"] and restored["applied"]["iconOrigins"] == 1
            assert request("GET", "/api/v1/icons/origins") == {"icons": [origin]}
            request("DELETE", "/api/v1/icons/origins?name=mail.gif")
            request("DELETE", "/api/v1/icons/origins?name=mail.gif")
            assert request("GET", "/ICONS/mail.gif") == GIF
            print("PASS: icon origins HTTP atomic replacement, reboot persistence, local edits, rename, deletion and ZIP restore")
        finally:
            stop_process(process)


if __name__ == "__main__":
    main()
